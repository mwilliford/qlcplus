/*
  Q Light Controller Plus
  fixturekinematics.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef FIXTUREKINEMATICS_H
#define FIXTUREKINEMATICS_H

#include <QByteArray>
#include <QtGlobal>
#include <memory>
#include <vector>

#include "qlcchannel.h"

namespace rigmath { class KinematicChain; class ChannelMap; }
class Fixture;

/**
 * @brief Kinematics + DMX binding for a fixture instance.
 *
 * Built once per fixture via buildFixtureKinematics(). Contains the
 * rigmath KinematicChain (N-DOF, N-beam), the ChannelMap (DMX ↔
 * physical angle conversion), and the absolute DMX addresses that
 * bridge QLC+'s universe system to the ChannelMap's byte-indexed API.
 *
 * Fully generic: handles 0-DOF (fixed), 1-DOF (tilt-only), 2-DOF
 * (pan+tilt), N-DOF (robot arms, XY stages), and multi-beam (LED bars).
 */
struct FixtureKinematics
{
    /// rigmath kinematic chain (serial joint chain + beam emitters).
    /// Null if the fixture couldn't be classified.
    std::shared_ptr<rigmath::KinematicChain> chain;

    /// DMX ↔ physical angle conversion. Signed physical_from/physical_to
    /// handles axis inversion generically.
    std::shared_ptr<rigmath::ChannelMap> channelMap;

    /// Absolute DMX addresses for each channel binding, ordered to match
    /// the ChannelMap's channel_index layout. Used by anglesToDmx() and
    /// dmxToDofs() to bridge between QLC+ absolute addresses and rigmath's
    /// byte-indexed ChannelMap.
    std::vector<quint32> dmxAddresses;

    quint32 universeId = 0;

    int dofCount() const;
    int beamCount() const;
};

/** Build the kinematics for a fixture. */
FixtureKinematics buildFixtureKinematics(const Fixture *fx);

/** A single DMX write — absolute channel address + value. */
struct FocusDmxWrite
{
    quint32 absAddr;
    uchar value;
};

/**
 * @brief Convert physical DOF angles to DMX writes.
 *
 * DOF values are in the chain's physical convention (degrees, centered
 * at 0). Returns one write per channel binding (MSB + optional LSB per DOF).
 * Returns empty if the fixture has no DOFs or no channelMap.
 */
std::vector<FocusDmxWrite> anglesToDmxWrites(const FixtureKinematics &fk,
                                              const std::vector<double> &dofs);

/**
 * @brief Read current DMX values from a universe snapshot and convert
 *        to physical DOF angles.
 *
 * @param fk                   Fixture kinematics
 * @param universeSnapshot     Latest QByteArray from universeWritten (0..511)
 * @return DOF values in physical convention. Size = fk.dofCount().
 *         Empty if no channelMap or no DOFs.
 */
std::vector<double> dmxSnapshotToDofs(const FixtureKinematics &fk,
                                       const QByteArray &universeSnapshot);

#endif // FIXTUREKINEMATICS_H
