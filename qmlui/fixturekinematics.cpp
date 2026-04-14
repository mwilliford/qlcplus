/*
  Q Light Controller Plus
  fixturekinematics.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "fixturekinematics.h"

#include <algorithm>
#include <cmath>

#include <rigmath/kinematic_chain.hpp>
#include <rigmath/channel_binding.hpp>

#include "fixture.h"
#include "gdtfgeometrydata.h"
#include "gdtfkinematics.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

// ---------------------------------------------------------------------------
// FixtureKinematics
// ---------------------------------------------------------------------------

int FixtureKinematics::dofCount() const
{
    return chain ? chain->dof_count() : 0;
}

int FixtureKinematics::beamCount() const
{
    return chain ? chain->beam_count() : 0;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

static quint32 absAddrOrInvalid(const Fixture *fx, quint32 relCh)
{
    if (relCh == QLCChannel::invalid())
        return QLCChannel::invalid();
    return fx->universeAddress() + relCh;
}

FixtureKinematics buildFixtureKinematics(const Fixture *fx)
{
    FixtureKinematics fk;
    if (fx == nullptr)
        return fk;

    const QLCFixtureMode *mode = fx->fixtureMode();
    if (mode == nullptr)
        return fk;

    fk.universeId = fx->universe();

    QLCPhysical phy = mode->physical();

    // Get GDTF geometry data (real for GDTF fixtures, synthesize for QXF)
    const QLCFixtureDef *def = fx->fixtureDef();
    const GDTFGeometryData *geoData = def ? def->gdtfGeometryData() : nullptr;

    GDTFGeometryData synthesized;
    GDTFDmxModeInfo modeInfo;

    if (geoData && !geoData->dmxModes.isEmpty())
    {
        // GDTF fixture — find the matching mode info by name
        QString modeName = mode->name();
        for (const auto &mi : geoData->dmxModes)
        {
            if (mi.modeName == modeName)
            {
                modeInfo = mi;
                break;
            }
        }
        if (modeInfo.channels.isEmpty() && !geoData->dmxModes.isEmpty())
            modeInfo = geoData->dmxModes.first();
    }
    else
    {
        // QXF fixture — synthesize GDTF data from channel layout + physical
        double panRange  = phy.focusPanMax()  > 0 ? phy.focusPanMax()  : 540.0;
        double tiltRange = phy.focusTiltMax() > 0 ? phy.focusTiltMax() : 270.0;
        bool isMirror = phy.focusType().compare(
            QStringLiteral("Mirror"), Qt::CaseInsensitive) == 0;

        quint32 panMSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::MSB);
        quint32 panLSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::LSB);
        quint32 tiltMSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
        quint32 tiltLSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::LSB);

        synthesizeGDTFFromQXF(
            panMSB != QLCChannel::invalid(),
            tiltMSB != QLCChannel::invalid(),
            panRange, tiltRange, isMirror,
            panMSB != QLCChannel::invalid() ? static_cast<int>(panMSB) : -1,
            panLSB != QLCChannel::invalid() ? static_cast<int>(panLSB) : -1,
            tiltMSB != QLCChannel::invalid() ? static_cast<int>(tiltMSB) : -1,
            tiltLSB != QLCChannel::invalid() ? static_cast<int>(tiltLSB) : -1,
            phy.lensDegreesMin(), phy.lensDegreesMax(),
            synthesized, modeInfo);
        geoData = &synthesized;
    }

    // Build kinematics from GDTF data (single code path)
    GDTFKinematicsResult kinResult = buildGDTFKinematics(*geoData, modeInfo);
    fk.chain = kinResult.chain;
    fk.channelMap = kinResult.channelMap;

    // Build the absolute DMX address list matching the ChannelMap's
    // channel_index layout. Use the QLC mode's channel lookup (not the
    // raw GDTF offset) because GeometryReference expansion may compress
    // the channel footprint, making GDTF offsets and QLC indices diverge.
    for (const auto &ch : modeInfo.channels)
    {
        if (ch.attributeName.startsWith(QStringLiteral("Pan")) ||
            ch.attributeName.startsWith(QStringLiteral("Tilt")))
        {
            QLCChannel::Group grp = ch.attributeName.startsWith(QStringLiteral("Pan"))
                                        ? QLCChannel::Pan : QLCChannel::Tilt;
            quint32 msb = mode->channelNumber(grp, QLCChannel::MSB);
            fk.dmxAddresses.push_back(absAddrOrInvalid(fx, msb));

            quint32 lsb = mode->channelNumber(grp, QLCChannel::LSB);
            if (lsb != QLCChannel::invalid())
                fk.dmxAddresses.push_back(absAddrOrInvalid(fx, lsb));
        }
    }

    return fk;
}

// ---------------------------------------------------------------------------
// DMX conversion
// ---------------------------------------------------------------------------

std::vector<FocusDmxWrite> anglesToDmxWrites(const FixtureKinematics &fk,
                                              const std::vector<double> &dofs)
{
    std::vector<FocusDmxWrite> writes;
    if (!fk.channelMap || dofs.empty())
        return writes;

    std::vector<uint8_t> bytes = fk.channelMap->angles_to_dmx(dofs);

    for (size_t i = 0; i < bytes.size() && i < fk.dmxAddresses.size(); i++)
    {
        if (fk.dmxAddresses[i] != QLCChannel::invalid())
            writes.push_back({fk.dmxAddresses[i], bytes[i]});
    }

    return writes;
}

std::vector<double> dmxSnapshotToDofs(const FixtureKinematics &fk,
                                       const QByteArray &universeSnapshot)
{
    if (!fk.channelMap || fk.dmxAddresses.empty())
        return {};

    // Extract DMX bytes from the snapshot at the known absolute addresses
    std::vector<uint8_t> bytes;
    bytes.reserve(fk.dmxAddresses.size());
    for (quint32 absAddr : fk.dmxAddresses)
    {
        const int rel = static_cast<int>(absAddr & 0x1FF);
        if (rel >= 0 && rel < universeSnapshot.size())
            bytes.push_back(static_cast<uint8_t>(universeSnapshot.at(rel)));
        else
            bytes.push_back(0);
    }

    return fk.channelMap->dmx_to_angles(bytes, fk.dofCount());
}
