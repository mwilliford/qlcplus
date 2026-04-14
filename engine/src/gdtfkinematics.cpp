/*
  Q Light Controller Plus
  gdtfkinematics.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "gdtfkinematics.h"
#include "gdtfgeometrydata.h"

#include <rigmath/channel_binding.hpp>
#include <rigmath/joint.hpp>
#include <rigmath/kinematic_chain.hpp>
#include <rigmath/rigid_transform.hpp>
#include <rigmath/vec3.hpp>

#include <cmath>
#include <functional>
#include <vector>

// Convert a GDTF column-major float[16] transform to rigmath RigidTransform.
// GDTF stores 4x4 column-major in meters; rigmath wants row-major doubles.
static rigmath::RigidTransform gdtfTransformToRigmath(const float colMajor[16])
{
    // Transpose column-major → row-major
    double rowMajor[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            rowMajor[r * 4 + c] = static_cast<double>(colMajor[c * 4 + r]);
    return rigmath::RigidTransform::from_4x4_row_major(rowMajor);
}

// Find the first GeometryLamp or GeometryLaser node in the subtree.
static const GDTFGeometryNode *findBeamNode(const GDTFGeometryNode &node)
{
    if (node.type == GeometryLamp || node.type == GeometryLaser)
        return &node;
    for (const auto &child : node.children)
    {
        auto *result = findBeamNode(child);
        if (result)
            return result;
    }
    return nullptr;
}

// Find the GDTFDmxChannelInfo for a given geometry node name (Pan or Tilt attribute).
static const GDTFDmxChannelInfo *findChannelForGeometry(
    const GDTFDmxModeInfo &modeInfo, const QString &geoName, const QString &attrPrefix)
{
    for (const auto &ch : modeInfo.channels)
    {
        if (ch.attributeName.startsWith(attrPrefix) && ch.geometryRef == geoName)
            return &ch;
    }
    // Fallback: match by attribute prefix alone (some GDTF files may not
    // set geometryRef on channels, or geometryRef may reference a parent).
    for (const auto &ch : modeInfo.channels)
    {
        if (ch.attributeName.startsWith(attrPrefix))
            return &ch;
    }
    return nullptr;
}

// Collect axis nodes in root-to-leaf DFS order.
static void collectAxes(const GDTFGeometryNode &node,
                         std::vector<const GDTFGeometryNode *> &axes)
{
    if (node.type == GeometryAxis)
        axes.push_back(&node);
    for (const auto &child : node.children)
        collectAxes(child, axes);
}

GDTFKinematicsResult buildGDTFKinematics(const GDTFGeometryData &geoData,
                                          const GDTFDmxModeInfo &modeInfo)
{
    GDTFKinematicsResult result;

    // Collect axis nodes from the geometry tree
    std::vector<const GDTFGeometryNode *> axes;
    collectAxes(geoData.root, axes);

    if (axes.empty())
    {
        // Fixed fixture — no axes. Build a zero-DOF chain.
        result.chain = std::make_shared<rigmath::KinematicChain>(
            rigmath::KinematicChain::fixed_beam());
        result.channelMap = std::make_shared<rigmath::ChannelMap>();
        result.dofCount = 0;
        return result;
    }

    // Build joints from axis nodes.
    // For real GDTF data: axes rotate around local +Z with parent_to_joint
    // encoding the actual axis direction (e.g., Ry(90°) for tilt around X).
    // For QXF-synthesized data: parent_to_joint is identity and we set
    // the axis vector directly from the attribute (Pan→matching factory axis).
    std::vector<rigmath::Joint> joints;
    int dofIdx = 0;

    static const QString kPan = QStringLiteral("Pan");
    static const QString kTilt = QStringLiteral("Tilt");

    auto channelMap = std::make_shared<rigmath::ChannelMap>();
    int channelIdx = 0;  // running index for ChannelMap byte array

    for (const GDTFGeometryNode *axisNode : axes)
    {
        rigmath::Joint j;
        j.name = axisNode->name.toStdString();
        j.type = rigmath::JointType::Rotational;
        j.parent_to_joint = gdtfTransformToRigmath(axisNode->localTransform);

        // Find the matching DMX channel for this axis.
        const GDTFDmxChannelInfo *chInfo = nullptr;
        QString matchedAttr;
        if (dofIdx == 0)
        {
            chInfo = findChannelForGeometry(modeInfo, axisNode->name, kPan);
            if (chInfo) matchedAttr = kPan;
        }
        if (!chInfo && dofIdx <= 1)
        {
            chInfo = findChannelForGeometry(modeInfo, axisNode->name, kTilt);
            if (chInfo) matchedAttr = kTilt;
        }
        if (!chInfo)
        {
            chInfo = findChannelForGeometry(modeInfo, axisNode->name, kPan);
            if (chInfo) matchedAttr = kPan;
        }

        // Determine the rotation axis.
        //
        // Well-authored GDTF: all axes rotate around local +Z. The
        // parent_to_joint ROTATION bakes in the effective world-frame
        // direction (e.g., Ry(90°) for tilt-around-X). Translation is
        // just the joint offset (yoke arm, head offset) and doesn't
        // affect axis direction.
        //
        // Poorly-authored GDTF (common on gdtf-share): the Position
        // matrix has identity rotation for ALL axes, with only translation
        // offsets. Both pan and tilt end up rotating around Z, which is
        // wrong. We detect this and infer axis from attribute names.
        //
        // QXF synthesis: same as poorly-authored — identity rotation,
        // axis inferred from attribute.
        double ax_r, ay_r, az_r;
        j.parent_to_joint.get_axis_angle(ax_r, ay_r, az_r);
        bool hasIdentityRotation =
            (std::abs(ax_r) + std::abs(ay_r) + std::abs(az_r) < 1e-6);

        if (hasIdentityRotation && chInfo)
        {
            // Identity rotation — infer axis from attribute name to match
            // factory convention. Pan: +Z (or +Y for mirrors). Tilt: +X.
            if (matchedAttr == kPan)
            {
                bool hasScannerPrim = false;
                for (const auto &a : axes)
                    if (a->primitiveType == PrimitiveScanner || a->primitiveType == PrimitiveScanner1_1)
                        hasScannerPrim = true;
                j.axis = hasScannerPrim ? rigmath::Vec3(0, 1, 0) : rigmath::Vec3(0, 0, 1);
            }
            else
            {
                j.axis = rigmath::Vec3(1, 0, 0);
            }
        }
        else
        {
            // Non-identity rotation — well-authored GDTF. Use local +Z
            // (the parent_to_joint rotation encodes the effective direction).
            j.axis = rigmath::Vec3(0, 0, 1);
        }

        if (chInfo)
        {
            j.dof_index = dofIdx;
            j.range_min = std::min(chInfo->physicalFrom, chInfo->physicalTo);
            j.range_max = std::max(chInfo->physicalFrom, chInfo->physicalTo);

            // Build ChannelBinding with the raw signed physical_from/physical_to.
            // Axis inversion is encoded by physicalFrom > physicalTo.
            if (chInfo->fineOffset >= 0)
            {
                channelMap->add_binding({dofIdx, channelIdx,
                                         rigmath::DmxByte::MSB16,
                                         chInfo->physicalFrom, chInfo->physicalTo});
                channelMap->add_binding({dofIdx, channelIdx + 1,
                                         rigmath::DmxByte::LSB16,
                                         chInfo->physicalFrom, chInfo->physicalTo});
                channelIdx += 2;
            }
            else
            {
                channelMap->add_binding({dofIdx, channelIdx,
                                         rigmath::DmxByte::Byte8,
                                         chInfo->physicalFrom, chInfo->physicalTo});
                channelIdx += 1;
            }
            dofIdx++;
        }
        else
        {
            // Axis exists in geometry but no DMX channel drives it — Fixed joint
            j.dof_index = -1;
        }

        // Capture axis DOF tag for the render layer
        AxisDofTag tag;
        tag.geometryName = axisNode->name;
        tag.dofIndex = j.dof_index;
        tag.axis[0] = static_cast<float>(j.axis.x);
        tag.axis[1] = static_cast<float>(j.axis.y);
        tag.axis[2] = static_cast<float>(j.axis.z);
        result.axisTags.push_back(tag);

        joints.push_back(j);
    }

    // Collect ALL beam nodes from the last axis's subtree.
    // Multi-beam fixtures (LED bars, multi-pixel) have one beam per emitter.
    // Each beam node's transform relative to the last axis becomes a beam_offset.
    const GDTFGeometryNode *lastAxis = axes.back();
    std::vector<rigmath::RigidTransform> beamOffsets;

    std::function<void(const GDTFGeometryNode &)> collectBeams;
    collectBeams = [&](const GDTFGeometryNode &node) {
        if (node.type == GeometryLamp || node.type == GeometryLaser)
            beamOffsets.push_back(gdtfTransformToRigmath(node.localTransform));
        // Also check GeometryReference nodes — they often point to shared
        // Beam/Lamp geometries (e.g., LED bar pixels).
        if (node.type == GeometryReference)
            beamOffsets.push_back(gdtfTransformToRigmath(node.localTransform));
        for (const auto &child : node.children)
            collectBeams(child);
    };
    for (const auto &child : lastAxis->children)
        collectBeams(child);

    // If no beam nodes found, use identity (beam at chain tip)
    if (beamOffsets.empty())
        beamOffsets.push_back(rigmath::RigidTransform::identity());

    result.chain = std::make_shared<rigmath::KinematicChain>(
        rigmath::KinematicChain(joints, beamOffsets));
    result.channelMap = channelMap;
    result.dofCount = dofIdx;

    return result;
}

// ---------------------------------------------------------------------------
// QXF → GDTF synthesis
// ---------------------------------------------------------------------------

void synthesizeGDTFFromQXF(bool hasPan, bool hasTilt,
                            double panRange, double tiltRange,
                            bool isMirror,
                            int panCoarse, int panFine,
                            int tiltCoarse, int tiltFine,
                            double beamAngle, double fieldAngle,
                            GDTFGeometryData &outGeoData,
                            GDTFDmxModeInfo &outModeInfo)
{
    outGeoData = GDTFGeometryData();
    outModeInfo = GDTFDmxModeInfo();
    outModeInfo.modeName = QStringLiteral("Synthesized");

    // Build a minimal geometry tree: Base → [Pan Axis] → [Tilt Axis] → Lamp
    // For QXF fixtures, the geometry tree is approximate (no joint offsets).
    // The axes use the same convention as rigmath's factory methods:
    //   Moving head: pan axis = direct (0,0,1), tilt axis = direct (1,0,0)
    //   Mirror:      pan axis = direct (0,1,0), tilt axis = direct (1,0,0)
    // All parent_to_joint transforms are identity (zero offsets).

    GDTFGeometryNode &root = outGeoData.root;
    root.name = QStringLiteral("Base");
    root.type = GeometryGeneral;
    root.primitiveType = PrimitiveBase;

    GDTFGeometryNode *current = &root;

    if (hasPan)
    {
        GDTFGeometryNode panNode;
        panNode.name = QStringLiteral("Yoke");
        panNode.type = GeometryAxis;
        panNode.primitiveType = PrimitiveYoke;
        // parent_to_joint = identity (default), no offset data from QXF

        GDTFDmxChannelInfo panCh;
        panCh.attributeName = QStringLiteral("Pan");
        panCh.coarseOffset = panCoarse;
        panCh.fineOffset = panFine;
        panCh.geometryRef = panNode.name;

        // Standard convention: center at 0, range symmetric.
        // Mirror scanners invert: physicalFrom > physicalTo.
        double panHalf = panRange / 2.0;
        if (isMirror)
        {
            panCh.physicalFrom = panHalf;
            panCh.physicalTo = -panHalf;
        }
        else
        {
            panCh.physicalFrom = -panHalf;
            panCh.physicalTo = panHalf;
        }
        outModeInfo.channels.append(panCh);

        current->children.append(panNode);
        current = &current->children.last();
    }

    if (hasTilt)
    {
        GDTFGeometryNode tiltNode;
        tiltNode.name = QStringLiteral("Head");
        tiltNode.type = GeometryAxis;
        tiltNode.primitiveType = isMirror ? PrimitiveScanner : PrimitiveHead;
        // parent_to_joint = identity (default), no offset data from QXF

        GDTFDmxChannelInfo tiltCh;
        tiltCh.attributeName = QStringLiteral("Tilt");
        tiltCh.coarseOffset = tiltCoarse;
        tiltCh.fineOffset = tiltFine;
        tiltCh.geometryRef = tiltNode.name;

        double tiltHalf = tiltRange / 2.0;
        if (isMirror)
        {
            tiltCh.physicalFrom = tiltHalf;
            tiltCh.physicalTo = -tiltHalf;
        }
        else
        {
            tiltCh.physicalFrom = -tiltHalf;
            tiltCh.physicalTo = tiltHalf;
        }
        outModeInfo.channels.append(tiltCh);

        current->children.append(tiltNode);
        current = &current->children.last();
    }

    // Add lamp node
    GDTFGeometryNode lampNode;
    lampNode.name = QStringLiteral("Lamp");
    lampNode.type = GeometryLamp;
    lampNode.beamAngle = static_cast<float>(beamAngle);
    lampNode.fieldAngle = static_cast<float>(fieldAngle);
    current->children.append(lampNode);

    // Store mode info inside geoData so downstream code can find it
    // without needing the separate outModeInfo parameter.
    outGeoData.dmxModes.append(outModeInfo);
}
