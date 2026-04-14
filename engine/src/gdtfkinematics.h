/*
  Q Light Controller Plus
  gdtfkinematics.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef GDTFKINEMATICS_H
#define GDTFKINEMATICS_H

#include <memory>
#include <vector>

#include <QString>

namespace rigmath { class KinematicChain; class ChannelMap; }
struct GDTFGeometryData;
struct GDTFDmxModeInfo;

/**
 * Per-axis DOF metadata extracted alongside kinematics.
 * Used by the render layer to tag SceneNode DOF fields without
 * duplicating axis-inference logic.
 */
struct AxisDofTag
{
    QString geometryName;   ///< axis node name (matches GDTFGeometryNode::name)
    int dofIndex = -1;      ///< -1 if no DMX channel drives this axis
    float axis[3] = {};     ///< rotation axis in node-local space (unit vector)
};

/**
 * @brief Result of building kinematics from GDTF geometry data.
 *
 * Contains the KinematicChain (joint structure + IK) and ChannelMap
 * (DMX ↔ physical angle conversion with signed physical_from/to).
 */
struct GDTFKinematicsResult
{
    std::shared_ptr<rigmath::KinematicChain> chain;
    std::shared_ptr<rigmath::ChannelMap> channelMap;
    int dofCount = 0;       ///< number of active DOFs in the chain
    std::vector<AxisDofTag> axisTags;  ///< one per GeometryAxis node (DFS order)
};

/**
 * @brief Build a KinematicChain + ChannelMap from GDTF geometry tree and DMX mode info.
 *
 * Walks the geometry tree depth-first collecting GeometryAxis nodes as joints.
 * Each axis node's localTransform becomes Joint::parent_to_joint. The GDTF
 * convention is rotation around local +Z, so Joint::axis = (0,0,1). The first
 * GeometryLamp/GeometryLaser after the last axis becomes the beam_offset.
 *
 * The ChannelMap uses the raw PhysicalFrom/PhysicalTo from GDTFDmxChannelInfo,
 * which encodes both range and direction (inverted when From > To). This
 * replaces all invertPan/invertTilt logic.
 *
 * @param geoData   Geometry tree (must not be null)
 * @param modeInfo  DMX mode metadata with physical ranges
 * @return Result with chain + channelMap. chain is null if no axes found.
 */
GDTFKinematicsResult buildGDTFKinematics(const GDTFGeometryData &geoData,
                                          const GDTFDmxModeInfo &modeInfo);

/**
 * @brief Synthesize GDTFGeometryData + GDTFDmxModeInfo from QXF fixture data.
 *
 * Creates a minimal GDTF-shaped representation for QXF fixtures so that
 * downstream code has a single code path. The synthesized geometry tree is:
 *   Base → Yoke (Pan axis) → Head (Tilt axis) → Lamp
 *
 * Pan/Tilt axis orientations follow GDTF convention:
 *   - All axes rotate around local +Z
 *   - The parent_to_joint rotation bakes the axis direction into the chain
 *   - Moving heads: pan effective axis = world +Z, tilt effective axis = world +X
 *   - Mirrors: pan effective axis = world +Y, tilt effective axis = world +X
 *
 * Joint offsets are zero (QXF doesn't carry this data).
 *
 * @param hasPan       true if fixture has pan channel
 * @param hasTilt      true if fixture has tilt channel
 * @param panRange     pan range in degrees (e.g. 540)
 * @param tiltRange    tilt range in degrees (e.g. 270)
 * @param isMirror     true if focusType == "Mirror"
 * @param panCoarse    DMX offset for pan MSB (-1 if none)
 * @param panFine      DMX offset for pan LSB (-1 if 8-bit)
 * @param tiltCoarse   DMX offset for tilt MSB (-1 if none)
 * @param tiltFine     DMX offset for tilt LSB (-1 if 8-bit)
 * @param beamAngle    beam angle from QLCPhysical (degrees)
 * @param fieldAngle   field angle from QLCPhysical (degrees)
 * @param[out] outGeoData  synthesized geometry data
 * @param[out] outModeInfo synthesized DMX mode info
 */
void synthesizeGDTFFromQXF(bool hasPan, bool hasTilt,
                            double panRange, double tiltRange,
                            bool isMirror,
                            int panCoarse, int panFine,
                            int tiltCoarse, int tiltFine,
                            double beamAngle, double fieldAngle,
                            GDTFGeometryData &outGeoData,
                            GDTFDmxModeInfo &outModeInfo);

#endif // GDTFKINEMATICS_H
