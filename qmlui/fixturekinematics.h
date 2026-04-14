/*
  Q Light Controller Plus
  fixturepantilt.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef FIXTUREPANTILT_H
#define FIXTUREPANTILT_H

#include <QByteArray>
#include <QtGlobal>
#include <memory>
#include <vector>

#include "qlcchannel.h"

namespace rigmath { class KinematicChain; class ChannelMap; }
class Fixture;

/**
 * @brief Pan/tilt channel layout for a fixture, both directions.
 *
 * Used by Focus mode to:
 *   - Flow A: convert IK angle results into DMX writes to send to SimpleDesk.
 *   - Flow B: convert the current live DMX state back into physical angles
 *     that rigmath::KinematicChain::forward_local can consume for beam-cone rendering.
 *
 * Addresses are absolute universe addresses (`(universe << 9) + address`).
 * An LSB address of QLCChannel::invalid() means the fixture uses only an
 * 8-bit pan or tilt channel.
 *
 * `kinematics` holds the rigmath KinematicChain for this fixture, built via
 * factory methods (moving_head, moving_mirror, pan_only, fixed_beam).
 * All downstream code dispatches through this pointer rather than branching
 * on fixture type.
 *
 * `channelMap` holds the rigmath ChannelMap that maps between physical DOF
 * angles and DMX bytes. Signed physical_from/physical_to in ChannelBinding
 * handles axis inversion (e.g. Chauvet Intimidator Scan 360) — no separate
 * invertPan/invertTilt flags needed.
 */
struct PanTiltChannelMap
{
    quint32 panMSBAddr = QLCChannel::invalid();
    quint32 panLSBAddr = QLCChannel::invalid();
    quint32 tiltMSBAddr = QLCChannel::invalid();
    quint32 tiltLSBAddr = QLCChannel::invalid();
    quint32 universeId = 0;
    double panRange = 540.0;
    double tiltRange = 270.0;

    // Concrete kinematics for this fixture. Null if the fixture couldn't
    // be classified (no fixture mode, etc.).
    std::shared_ptr<rigmath::KinematicChain> kinematics;

    // DMX ↔ DOF angle conversion. Handles axis inversion via signed
    // physical_from/physical_to ranges (replaces invertPan/invertTilt).
    std::shared_ptr<rigmath::ChannelMap> channelMap;

    bool hasPan() const { return panMSBAddr != QLCChannel::invalid(); }
    bool hasTilt() const { return tiltMSBAddr != QLCChannel::invalid(); }
    bool isMovingHead() const { return hasPan() && hasTilt(); }
};

/** Build the pan/tilt channel map for a fixture. */
PanTiltChannelMap buildPanTiltChannelMap(const Fixture *fx);

/** A single DMX write — absolute channel address + value. */
struct FocusDmxWrite
{
    quint32 absAddr;
    uchar value;
};

/**
 * @brief Flow A: convert physical pan/tilt angles (degrees) into DMX writes.
 *
 * Angles are in rigmath convention: [-range/2, +range/2] around center.
 * Uses the map's ChannelMap for conversion (handles axis inversion via
 * signed physical_from/physical_to). 16-bit channels emit MSB + LSB.
 * 8-bit channels emit MSB only.
 *
 * Returns an empty vector if the map has no pan or no tilt (e.g. a fixed
 * fixture or a pan-only wash that shouldn't be aimed in v1).
 */
std::vector<FocusDmxWrite> anglesToDmxWrites(const PanTiltChannelMap &map,
                                              double panDeg, double tiltDeg);

/** Physical angles extracted from a DMX snapshot. */
struct PanTiltAngles
{
    double panDeg = 0.0;
    double tiltDeg = 0.0;
    bool hasPan = false;
    bool hasTilt = false;
};

/**
 * @brief Flow B: read current DMX values from a universe snapshot and
 *        convert them back to physical angles.
 *
 * @param map                  Pan/tilt channel layout for the fixture
 * @param universeSnapshot     Latest QByteArray from
 *                             InputOutputMap::universeWritten — indexed 0..511
 *                             within the fixture's universe
 * @return Angles in rigmath convention; hasPan/hasTilt reflect which channels
 *         were actually present in the map *and* available in the snapshot.
 *         Missing angles default to 0 so callers can feed the result straight
 *         into forward_local() for the present axis.
 */
PanTiltAngles dmxSnapshotToAngles(const PanTiltChannelMap &map,
                                   const QByteArray &universeSnapshot);

#endif // FIXTUREPANTILT_H
