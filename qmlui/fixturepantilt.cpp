/*
  Q Light Controller Plus
  fixturepantilt.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "fixturepantilt.h"

#include <algorithm>
#include <cmath>

#include <rigmath/kinematic_chain.hpp>
#include <rigmath/channel_binding.hpp>

#include "fixture.h"
#include "gdtfgeometrydata.h"
#include "gdtfkinematics.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

static quint32 absAddrOrInvalid(const Fixture *fx, quint32 relCh)
{
    if (relCh == QLCChannel::invalid())
        return QLCChannel::invalid();
    return fx->universeAddress() + relCh;
}

PanTiltChannelMap buildPanTiltChannelMap(const Fixture *fx)
{
    PanTiltChannelMap map;
    if (fx == nullptr)
        return map;

    const QLCFixtureMode *mode = fx->fixtureMode();
    if (mode == nullptr)
        return map;

    // Resolve absolute DMX addresses for pan/tilt channels
    const quint32 panMSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::MSB);
    const quint32 panLSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::LSB);
    const quint32 tiltMSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
    const quint32 tiltLSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::LSB);

    map.panMSBAddr  = absAddrOrInvalid(fx, panMSB);
    map.panLSBAddr  = absAddrOrInvalid(fx, panLSB);
    map.tiltMSBAddr = absAddrOrInvalid(fx, tiltMSB);
    map.tiltLSBAddr = absAddrOrInvalid(fx, tiltLSB);
    map.universeId  = fx->universe();

    QLCPhysical phy = mode->physical();

    // Get GDTF geometry data + mode info. For GDTF fixtures this comes from
    // the parsed file. For QXF fixtures we synthesize approximate GDTF data.
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
        // Fallback: use the first mode if name didn't match
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

        synthesizeGDTFFromQXF(
            map.hasPan(), map.hasTilt(),
            panRange, tiltRange, isMirror,
            panMSB != QLCChannel::invalid() ? static_cast<int>(panMSB) : -1,
            panLSB != QLCChannel::invalid() ? static_cast<int>(panLSB) : -1,
            tiltMSB != QLCChannel::invalid() ? static_cast<int>(tiltMSB) : -1,
            tiltLSB != QLCChannel::invalid() ? static_cast<int>(tiltLSB) : -1,
            phy.lensDegreesMin(), phy.lensDegreesMax(),
            synthesized, modeInfo);
        geoData = &synthesized;
    }

    // Build kinematics from GDTF data (single code path for both formats)
    GDTFKinematicsResult kinResult = buildGDTFKinematics(*geoData, modeInfo);
    map.kinematics = kinResult.chain;
    map.channelMap = kinResult.channelMap;

    // Store ranges for UI display
    for (const auto &ch : modeInfo.channels)
    {
        if (ch.attributeName.startsWith(QStringLiteral("Pan")))
            map.panRange = std::abs(ch.physicalTo - ch.physicalFrom);
        if (ch.attributeName.startsWith(QStringLiteral("Tilt")))
            map.tiltRange = std::abs(ch.physicalTo - ch.physicalFrom);
    }

    return map;
}

// Map from ChannelMap channel_index to absolute DMX address.
// The channel_index layout matches how bindings are built in buildGDTFKinematics:
//   pan MSB, [pan LSB], tilt MSB, [tilt LSB]
static std::vector<quint32> buildChannelAddrs(const PanTiltChannelMap &map)
{
    std::vector<quint32> addrs;
    if (map.hasPan())
    {
        addrs.push_back(map.panMSBAddr);
        if (map.panLSBAddr != QLCChannel::invalid())
            addrs.push_back(map.panLSBAddr);
    }
    if (map.hasTilt())
    {
        addrs.push_back(map.tiltMSBAddr);
        if (map.tiltLSBAddr != QLCChannel::invalid())
            addrs.push_back(map.tiltLSBAddr);
    }
    return addrs;
}

std::vector<FocusDmxWrite> anglesToDmxWrites(const PanTiltChannelMap &map,
                                              double panDeg, double tiltDeg)
{
    std::vector<FocusDmxWrite> writes;
    if (!map.isMovingHead() || !map.channelMap)
        return writes;

    std::vector<double> dofs = {panDeg, tiltDeg};
    std::vector<uint8_t> bytes = map.channelMap->angles_to_dmx(dofs);

    std::vector<quint32> addrs = buildChannelAddrs(map);

    for (size_t i = 0; i < bytes.size() && i < addrs.size(); i++)
        writes.push_back({addrs[i], bytes[i]});

    return writes;
}

static std::vector<uint8_t> extractBytes(const PanTiltChannelMap &map,
                                          const QByteArray &snap)
{
    std::vector<quint32> addrs = buildChannelAddrs(map);
    std::vector<uint8_t> bytes;
    bytes.reserve(addrs.size());
    for (quint32 absAddr : addrs)
    {
        const int rel = static_cast<int>(absAddr & 0x1FF);
        if (rel >= 0 && rel < snap.size())
            bytes.push_back(static_cast<uint8_t>(snap.at(rel)));
        else
            bytes.push_back(0);
    }
    return bytes;
}

PanTiltAngles dmxSnapshotToAngles(const PanTiltChannelMap &map,
                                   const QByteArray &universeSnapshot)
{
    PanTiltAngles out;
    if (!map.channelMap)
        return out;

    std::vector<uint8_t> bytes = extractBytes(map, universeSnapshot);
    int numDofs = (map.hasPan() ? 1 : 0) + (map.hasTilt() ? 1 : 0);
    std::vector<double> dofs = map.channelMap->dmx_to_angles(bytes, numDofs);

    if (map.hasPan() && dofs.size() > 0)
    {
        out.panDeg = dofs[0];
        out.hasPan = true;
    }
    if (map.hasTilt())
    {
        int tiltIdx = map.hasPan() ? 1 : 0;
        if (tiltIdx < (int)dofs.size())
        {
            out.tiltDeg = dofs[tiltIdx];
            out.hasTilt = true;
        }
    }
    return out;
}
