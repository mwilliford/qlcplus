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

#include <rigmath/fixed.hpp>
#include <rigmath/kinematics.hpp>
#include <rigmath/moving_head.hpp>
#include <rigmath/moving_mirror.hpp>
#include <rigmath/pan_only.hpp>

#include "fixture.h"
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

    // Use QLCFixtureMode::channelNumber(group, controlByte) which searches
    // channels directly by group — works for fixtures with no <Head> elements
    // (e.g. scanners and other fixtures defined without explicit heads).
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
    map.panRange  = phy.focusPanMax()  > 0 ? phy.focusPanMax()  : 540.0;
    map.tiltRange = phy.focusTiltMax() > 0 ? phy.focusTiltMax() : 270.0;

    // Pick the concrete rigmath kinematics subclass once, here. Downstream
    // callers (setFocusAim, rebuildBeamCones) dispatch polymorphically —
    // no `if (isMovingMirror)` branches anywhere else.
    const bool isMirror =
        phy.focusType().compare(QStringLiteral("Mirror"), Qt::CaseInsensitive) == 0;

    if (map.hasPan() && map.hasTilt())
    {
        if (isMirror)
        {
            map.kinematics = std::make_shared<rigmath::MovingMirrorKinematics>(
                                 map.panRange, map.tiltRange);
            // Empirically verified against a Chauvet Intimidator Scan 360:
            // both DMX channels run opposite to rigmath's right-hand-rule
            // convention. The rendered cone already matches the click
            // point (kinematics is correct), but the physical fixture
            // moves the opposite way unless we flip the DMX sign on both
            // axes. Both flags are applied symmetrically in encode and
            // decode so the round-trip (and the rendered cone) is
            // unaffected — only the bytes sent to the hardware change.
            //
            // This is fixture-calibration specific. If we ever support a
            // mirror scanner with the opposite convention, these will
            // need to be per-fixture configurable.
            map.invertPan = true;
            map.invertTilt = true;
        }
        else
        {
            map.kinematics = std::make_shared<rigmath::MovingHeadKinematics>(
                                 map.panRange, map.tiltRange);
        }
    }
    else if (map.hasPan())
    {
        map.kinematics = std::make_shared<rigmath::PanOnlyKinematics>(map.panRange);
    }
    else
    {
        map.kinematics = std::make_shared<rigmath::FixedKinematics>();
    }

    return map;
}

// Convert a signed rigmath angle ∈ [-range/2, +range/2] to a QLC unsigned
// angle ∈ [0, range], then encode as DMX.
//
// QLC+ convention: DMX=0 → 0°, DMX=max → range°. Center (home) is at DMX=max/2.
// rigmath convention: home is at 0°, so we shift by +range/2 before encoding.
std::vector<FocusDmxWrite> anglesToDmxWrites(const PanTiltChannelMap &map,
                                              double panDeg, double tiltDeg)
{
    std::vector<FocusDmxWrite> writes;
    if (!map.isMovingHead())
        return writes;

    auto encode = [](double signedDeg, double range,
                     quint32 msbAddr, quint32 lsbAddr,
                     std::vector<FocusDmxWrite> &out)
    {
        double normalized = (signedDeg + range / 2.0) / range;
        normalized = std::clamp(normalized, 0.0, 1.0);

        if (lsbAddr != QLCChannel::invalid())
        {
            const quint16 d16 = static_cast<quint16>(normalized * 65535.0 + 0.5);
            out.push_back({msbAddr, static_cast<uchar>(d16 >> 8)});
            out.push_back({lsbAddr, static_cast<uchar>(d16 & 0xFF)});
        }
        else
        {
            const uchar d8 = static_cast<uchar>(normalized * 255.0 + 0.5);
            out.push_back({msbAddr, d8});
        }
    };

    // Apply per-axis inversion at the DMX boundary. See PanTiltChannelMap docs.
    const double encodePan  = map.invertPan  ? -panDeg  : panDeg;
    const double encodeTilt = map.invertTilt ? -tiltDeg : tiltDeg;

    encode(encodePan,  map.panRange,  map.panMSBAddr,  map.panLSBAddr,  writes);
    encode(encodeTilt, map.tiltRange, map.tiltMSBAddr, map.tiltLSBAddr, writes);
    return writes;
}

// Inverse of encode: DMX byte(s) → normalized → signed rigmath angle.
static double decodeAngle(const QByteArray &snap, quint32 msbAbsAddr,
                          quint32 lsbAbsAddr, double range)
{
    // Convert absolute address back to channel-within-universe [0..511].
    const int msbRel = static_cast<int>(msbAbsAddr & 0x1FF);
    const int lsbRel = lsbAbsAddr != QLCChannel::invalid()
                         ? static_cast<int>(lsbAbsAddr & 0x1FF)
                         : -1;

    if (msbRel < 0 || msbRel >= snap.size())
        return 0.0;

    double normalized;
    if (lsbRel >= 0 && lsbRel < snap.size())
    {
        const quint8 msb = static_cast<quint8>(snap.at(msbRel));
        const quint8 lsb = static_cast<quint8>(snap.at(lsbRel));
        const quint16 d16 = (quint16(msb) << 8) | quint16(lsb);
        normalized = double(d16) / 65535.0;
    }
    else
    {
        const quint8 msb = static_cast<quint8>(snap.at(msbRel));
        normalized = double(msb) / 255.0;
    }

    return normalized * range - range / 2.0;
}

PanTiltAngles dmxSnapshotToAngles(const PanTiltChannelMap &map,
                                   const QByteArray &universeSnapshot)
{
    PanTiltAngles out;
    if (map.hasPan())
    {
        double raw = decodeAngle(universeSnapshot, map.panMSBAddr,
                                 map.panLSBAddr, map.panRange);
        out.panDeg = map.invertPan ? -raw : raw;
        out.hasPan = true;
    }
    if (map.hasTilt())
    {
        double raw = decodeAngle(universeSnapshot, map.tiltMSBAddr,
                                 map.tiltLSBAddr, map.tiltRange);
        out.tiltDeg = map.invertTilt ? -raw : raw;
        out.hasTilt = true;
    }
    return out;
}
