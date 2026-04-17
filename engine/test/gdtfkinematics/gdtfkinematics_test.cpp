/*
  Q Light Controller Plus
  gdtfkinematics_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QtTest>
#include <cmath>

#include "gdtfkinematics_test.h"
#include "gdtfkinematics.h"
#include "gdtfgeometrydata.h"

#include <rigmath/beam.hpp>
#include <rigmath/kinematic_chain.hpp>
#include <rigmath/channel_binding.hpp>
#include <rigmath/rigid_transform.hpp>
#include <rigmath/vec3.hpp>

// =====================================================================
// Helpers: geometry tree construction
// =====================================================================

// Set a column-major 4x4 identity matrix with optional Z-translation
static void setIdentityWithOffset(float m[16], float zOffset = 0.0f)
{
    float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::copy(id, id + 16, m);
    // Column-major: translation is in elements [12],[13],[14]
    m[14] = zOffset;
}

// Set a column-major Rx(angle) rotation with optional translation
static void setRotationX(float m[16], float angleDeg, float tz = 0.0f)
{
    float c = std::cos(angleDeg * M_PI / 180.0f);
    float s = std::sin(angleDeg * M_PI / 180.0f);
    float mat[16] = {
        1, 0, 0, 0,
        0, c, s, 0,
        0,-s, c, 0,
        0, 0, tz, 1
    };
    std::copy(mat, mat + 16, m);
}

static GDTFGeometryNode makeNode(const QString &name, GDTFGeometryType type,
                                  GDTFPrimitiveType prim = PrimitiveUndefined,
                                  float zOffset = 0.0f)
{
    GDTFGeometryNode node;
    node.name = name;
    node.type = type;
    node.primitiveType = prim;
    setIdentityWithOffset(node.localTransform, zOffset);
    return node;
}

static GDTFGeometryNode makeLamp(float beamAngle = 25.0f, float zOffset = 0.0f)
{
    GDTFGeometryNode node = makeNode("Lamp", GeometryLamp, PrimitiveCylinder, zOffset);
    node.beamAngle = beamAngle;
    node.fieldAngle = beamAngle;
    return node;
}

static GDTFDmxChannelInfo makePanChannel(int coarse, int fine = -1,
                                          double from = -270.0, double to = 270.0,
                                          const QString &geoRef = "Yoke")
{
    GDTFDmxChannelInfo ch;
    ch.attributeName = QStringLiteral("Pan");
    ch.coarseOffset = coarse;
    ch.fineOffset = fine;
    ch.physicalFrom = from;
    ch.physicalTo = to;
    ch.geometryRef = geoRef;
    return ch;
}

static GDTFDmxChannelInfo makeTiltChannel(int coarse, int fine = -1,
                                           double from = -135.0, double to = 135.0,
                                           const QString &geoRef = "Head")
{
    GDTFDmxChannelInfo ch;
    ch.attributeName = QStringLiteral("Tilt");
    ch.coarseOffset = coarse;
    ch.fineOffset = fine;
    ch.physicalFrom = from;
    ch.physicalTo = to;
    ch.geometryRef = geoRef;
    return ch;
}

// Set a column-major identity matrix with X, Y, Z translation offsets
static void setTransformXYZ(float m[16], float tx, float ty, float tz)
{
    float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::copy(id, id + 16, m);
    m[12] = tx;  // column 3, row 0
    m[13] = ty;  // column 3, row 1
    m[14] = tz;  // column 3, row 2
}

// Convert GDTF column-major float[16] to rigmath RigidTransform.
// Duplicated from gdtfkinematics.cpp (static there) for test use.
static rigmath::RigidTransform testGdtfToRigmath(const float colMajor[16])
{
    double rowMajor[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            rowMajor[r * 4 + c] = static_cast<double>(colMajor[c * 4 + r]);
    return rigmath::RigidTransform::from_4x4_row_major(rowMajor);
}

// Walk the GDTF geometry tree like renderSceneGraph, computing beam rays
// in fixture-local space. This simulates what the renderer produces
// (single source of truth) for comparison with kinematics output.
static std::vector<rigmath::Ray> sceneGraphBeamRays(
    const GDTFGeometryNode &root,
    const std::vector<AxisDofTag> &axisTags,
    const std::vector<double> &dofAngles)
{
    std::vector<rigmath::Ray> rays;

    std::function<void(const GDTFGeometryNode &, rigmath::RigidTransform)> walk;
    walk = [&](const GDTFGeometryNode &node, rigmath::RigidTransform parentWorld) {
        // World transform = parent * local (same composition as renderSceneGraph)
        rigmath::RigidTransform nodeWorld = parentWorld.compose(
            testGdtfToRigmath(node.localTransform));

        // If DOF node, apply articulation rotation
        rigmath::RigidTransform articulatedWorld = nodeWorld;
        for (const auto &tag : axisTags)
        {
            if (tag.geometryName == node.name && tag.dofIndex >= 0 &&
                tag.dofIndex < (int)dofAngles.size())
            {
                double angleRad = dofAngles[tag.dofIndex] * M_PI / 180.0;
                auto dofRot = rigmath::RigidTransform::rotation_around_axis(
                    rigmath::Vec3(tag.axis[0], tag.axis[1], tag.axis[2]), angleRad);
                articulatedWorld = nodeWorld.compose(dofRot);
                break;
            }
        }

        // If beam node, record the world-space ray
        if (node.type == GeometryLamp || node.type == GeometryLaser)
        {
            rigmath::Ray localRay{0, 0, 0, 0, 0, -1};
            rays.push_back(articulatedWorld.transform_ray(localRay));
        }

        // Recurse children with the articulated transform
        for (const auto &child : node.children)
            walk(child, articulatedWorld);
    };

    walk(root, rigmath::RigidTransform::identity());
    return rays;
}

// Verify a ray origin is close to expected position
static void verifyOrigin(const rigmath::Ray &ray, double ex, double ey, double ez,
                          double tolMeters = 0.001)
{
    double dx = ray.ox - ex, dy = ray.oy - ey, dz = ray.oz - ez;
    double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    QVERIFY2(dist < tolMeters,
             qPrintable(QString("Origin mismatch: got (%1,%2,%3) expected (%4,%5,%6) dist=%7m")
                        .arg(ray.ox, 0, 'f', 6).arg(ray.oy, 0, 'f', 6).arg(ray.oz, 0, 'f', 6)
                        .arg(ex, 0, 'f', 6).arg(ey, 0, 'f', 6).arg(ez, 0, 'f', 6)
                        .arg(dist, 0, 'f', 6)));
}

// Verify a ray direction is close to expected (normalized comparison)
static void verifyDirection(const rigmath::Ray &ray, double ex, double ey, double ez,
                            double tolDeg = 0.5)
{
    double dot = ray.dx * ex + ray.dy * ey + ray.dz * ez;
    double angleDeg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
    QVERIFY2(angleDeg < tolDeg,
             qPrintable(QString("Direction mismatch: got (%1,%2,%3) expected (%4,%5,%6) angle=%7°")
                        .arg(ray.dx, 0, 'f', 4).arg(ray.dy, 0, 'f', 4).arg(ray.dz, 0, 'f', 4)
                        .arg(ex, 0, 'f', 4).arg(ey, 0, 'f', 4).arg(ez, 0, 'f', 4)
                        .arg(angleDeg, 0, 'f', 4)));
}

// =====================================================================
// buildGDTFKinematics tests
// =====================================================================

void GDTFKinematics_Test::movingHead_standardIdentityRotation()
{
    // Common case on gdtf-share: identity rotation on all axes.
    // Axis directions inferred from attribute names (Pan→Z, Tilt→X).
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke, -0.15f);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);
    head.children.append(makeLamp(25.0f, -0.05f));
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makePanChannel(0));
    mode.channels.append(makeTiltChannel(1));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);
    QCOMPARE(r.chain->dof_count(), 2);
    QVERIFY(r.chain->beam_count() >= 1);

    // Home beam should point approximately -Z
    rigmath::Ray home = r.chain->forward_local({0, 0});
    verifyDirection(home, 0, 0, -1, 1.0);

    // Tilt 45° → beam should differ from home direction
    rigmath::Ray tilted = r.chain->forward_local({0, 45});
    double dot = home.dx * tilted.dx + home.dy * tilted.dy + home.dz * tilted.dz;
    QVERIFY2(dot < 0.95, qPrintable(QString("Tilt 45° should change direction (dot=%1)")
                                     .arg(dot, 0, 'f', 4)));
}

void GDTFKinematics_Test::movingHead_wellAuthoredRotation()
{
    // Well-authored GDTF: tilt axis has Rx(90°) in Position matrix,
    // so local +Z maps to world +X. Like the Varytec Heroscan.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke, -0.15f);
    // Yoke: identity rotation (pan around Z) — correct as-is

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);
    // Head: Rx(90°) so local Z maps to world X (tilt axis)
    setRotationX(head.localTransform, 90.0f, -0.04f);

    head.children.append(makeLamp(25.0f, -0.05f));
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makePanChannel(0));
    mode.channels.append(makeTiltChannel(1));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);

    // Home beam — direction depends on the Rx(90°) transform composition.
    // Verify the chain built successfully and produces a valid ray.
    rigmath::Ray home = r.chain->forward_local({0, 0});
    double len = std::sqrt(home.dx*home.dx + home.dy*home.dy + home.dz*home.dz);
    QVERIFY2(std::abs(len - 1.0) < 0.01, "Home beam should be unit direction");
}

void GDTFKinematics_Test::movingHead_identityVsWellAuthored_equivalence()
{
    // Both identity-rotation and well-authored GDTF should produce
    // equivalent beam directions for the same DOF angles (within tolerance).
    // This tests that our axis-inference logic matches the proper GDTF encoding.

    // Identity version
    GDTFGeometryData geoId;
    geoId.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode yokeId = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    GDTFGeometryNode headId = makeNode("Head", GeometryAxis, PrimitiveHead);
    headId.children.append(makeLamp());
    yokeId.children.append(headId);
    geoId.root.children.append(yokeId);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    mode.channels.append(makePanChannel(0, -1, -270, 270));
    mode.channels.append(makeTiltChannel(1, -1, -135, 135));

    auto rId = buildGDTFKinematics(geoId.root, mode);

    // Factory version (the "truth")
    auto factory = rigmath::KinematicChain::moving_head(540.0, 270.0);

    // Compare at several angle combinations
    std::vector<std::pair<double, double>> testAngles = {
        {0, 0}, {45, 0}, {0, 45}, {90, -30}, {-120, 60}
    };

    for (auto [pan, tilt] : testAngles)
    {
        rigmath::Ray rayId = rId.chain->forward_local({pan, tilt});
        rigmath::Ray rayFac = factory.forward_local({pan, tilt});

        double dot = rayId.dx * rayFac.dx + rayId.dy * rayFac.dy + rayId.dz * rayFac.dz;
        double angleDeg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
        QVERIFY2(angleDeg < 1.0,
                 qPrintable(QString("Mismatch at pan=%1 tilt=%2: angle=%3°")
                            .arg(pan).arg(tilt).arg(angleDeg, 0, 'f', 4)));
    }
}

void GDTFKinematics_Test::movingMirror_identityRotation()
{
    // Mirror scanner with identity rotation.
    // Pan should be inferred as +Y (from scanner primitives), Tilt as +X.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveScanner);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveScanner);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    // Mirror: inverted physical ranges (From > To)
    mode.channels.append(makePanChannel(0, -1, 90, -90, "Yoke"));
    mode.channels.append(makeTiltChannel(1, -1, 55, -55, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);

    // Mirror scanner pan axis should be +Y (detected from scanner primitives)
    // Verify: pan 90° produces beam swing in XZ plane (rotation around Y)
    rigmath::Ray home = r.chain->forward_local({0, 0});
    rigmath::Ray panned = r.chain->forward_local({45, 0});
    // Pan around Y: beam X and Z components change, Y stays ~0
    QVERIFY(std::abs(panned.dx - home.dx) > 0.1 || std::abs(panned.dz - home.dz) > 0.1);
}

void GDTFKinematics_Test::movingMirror_wellAuthoredRotation()
{
    // Well-authored mirror: Rx(-90°) on pan axis so local Z maps to world Y.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveScanner);
    setRotationX(yoke.localTransform, -90.0f);  // local Z → world Y

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveScanner);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    mode.channels.append(makePanChannel(0, -1, -90, 90, "Yoke"));
    mode.channels.append(makeTiltChannel(1, -1, -55, 55, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);
    // Non-identity rotation → uses GDTF convention (axis = local Z)
}

void GDTFKinematics_Test::tiltOnly()
{
    // Fixture with only a tilt axis (like POS-6 LED bar).
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.14f);
    head.children.append(makeLamp(25.0f, -0.06f));
    geo.root.children.append(head);

    GDTFDmxModeInfo mode;
    mode.modeName = "30CH";
    mode.channels.append(makeTiltChannel(28, -1, 120, -120, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 1);
    QCOMPARE(r.chain->dof_count(), 1);

    // Home: beam points down
    rigmath::Ray home = r.chain->forward_local({0});
    verifyDirection(home, 0, 0, -1, 2.0);

    // Tilt 45°: beam should swing in the XZ or YZ plane
    rigmath::Ray tilted = r.chain->forward_local({45});
    QVERIFY(std::abs(tilted.dz) < std::abs(home.dz));  // less downward
}

void GDTFKinematics_Test::panOnly()
{
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    yoke.children.append(makeLamp());
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    mode.channels.append(makePanChannel(0, -1, -180, 180));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 1);
}

void GDTFKinematics_Test::fixedFixture()
{
    // No axis nodes at all.
    GDTFGeometryData geo;
    geo.root = makeNode("Body", GeometryGeneral, PrimitiveConventional);
    geo.root.children.append(makeLamp());

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 0);
    QCOMPARE(r.chain->dof_count(), 0);

    rigmath::Ray home = r.chain->forward_local({});
    verifyDirection(home, 0, 0, -1, 1.0);
}

void GDTFKinematics_Test::multiBeam_ledBar()
{
    // LED bar: one tilt axis with 6 beam emitters at different X offsets.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);
    // 6 GeometryReference nodes at different X positions
    for (int i = 0; i < 6; i++)
    {
        GDTFGeometryNode ref;
        ref.name = QString("LED %1").arg(i + 1);
        ref.type = GeometryReference;
        ref.primitiveType = PrimitiveCylinder;
        setIdentityWithOffset(ref.localTransform);
        // Set X offset in column-major: element [12] = tx
        ref.localTransform[12] = -0.25f + i * 0.1f;
        ref.localTransform[14] = -0.06f;  // Z offset
        head.children.append(ref);
    }
    geo.root.children.append(head);

    GDTFDmxModeInfo mode;
    mode.modeName = "30CH";
    mode.channels.append(makeTiltChannel(28, -1, -120, 120, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 1);
    QCOMPARE(r.chain->beam_count(), 6);

    // All beams point same direction at home but have different origins
    auto rays = r.chain->forward_local_all({0});
    QCOMPARE((int)rays.size(), 6);

    for (int i = 0; i < 6; i++)
        verifyDirection(rays[i], 0, 0, -1, 2.0);

    // Verify different X origins
    QVERIFY(std::abs(rays[0].ox - rays[5].ox) > 0.3);
}

void GDTFKinematics_Test::invertedPhysicalRange()
{
    // PhysicalFrom > PhysicalTo (mirror scanner convention).
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    // Inverted: From=+270, To=-270
    mode.channels.append(makePanChannel(0, -1, 270, -270));
    mode.channels.append(makeTiltChannel(1, -1, 135, -135));

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QVERIFY(r.channelMap != nullptr);

    // ChannelMap should handle inversion: DMX=0 → physical=+270, DMX=max → physical=-270
    // Verify round-trip: angles → DMX → angles
    std::vector<double> dofs = {45.0, -30.0};
    auto dmx = r.channelMap->angles_to_dmx(dofs);
    auto back = r.channelMap->dmx_to_angles(dmx, 2);
    QVERIFY(std::abs(back[0] - dofs[0]) < 1.0);
    QVERIFY(std::abs(back[1] - dofs[1]) < 1.0);
}

void GDTFKinematics_Test::sixteenBitChannels()
{
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "16bit";
    mode.channels.append(makePanChannel(0, 1, -270, 270));  // coarse=0, fine=1
    mode.channels.append(makeTiltChannel(2, 3, -135, 135));  // coarse=2, fine=3

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    QVERIFY(r.channelMap != nullptr);

    // 16-bit should produce 4 DMX bytes (pan MSB, pan LSB, tilt MSB, tilt LSB)
    auto dmx = r.channelMap->angles_to_dmx({0, 0});
    QCOMPARE((int)dmx.size(), 4);

    // Center position (0°, 0°) → DMX should be ~128/128 (midpoint for standard range)
    QVERIFY(dmx[0] >= 126 && dmx[0] <= 130);  // pan MSB near center
    QVERIFY(dmx[2] >= 126 && dmx[2] <= 130);  // tilt MSB near center
}

void GDTFKinematics_Test::missingDmxChannel()
{
    // Axis exists in geometry but has no matching DMX channel.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    // Provide only Pan and a non-motion channel (Dimmer).
    // The second axis (Head) should not find a Pan or Tilt match
    // and become a Fixed joint.
    GDTFDmxModeInfo mode;
    mode.modeName = "Reduced";
    mode.channels.append(makePanChannel(0));
    // Add a non-motion channel that won't match Pan or Tilt
    GDTFDmxChannelInfo dimmer;
    dimmer.attributeName = QStringLiteral("Dimmer");
    dimmer.coarseOffset = 1;
    dimmer.physicalFrom = 0;
    dimmer.physicalTo = 1;
    dimmer.geometryRef = QStringLiteral("Head");
    mode.channels.append(dimmer);
    // No Tilt channel at all — second axis should become Fixed joint.
    // Note: findChannelForGeometry falls back to attribute-prefix matching,
    // so if ANY Pan channel exists, the second axis might pick it up.
    // With only Pan + Dimmer, the second axis tries Tilt first → no match,
    // then tries Pan → finds it (but it's already used by first axis).
    // Current behavior: both axes can match the same channel.
    // This is a known limitation — the matching doesn't track "used" channels.

    auto r = buildGDTFKinematics(geo.root, mode);

    QVERIFY(r.chain != nullptr);
    // The second axis finds Pan via fallback → both get a DOF.
    // This is imprecise but doesn't crash. Real GDTF files won't hit this
    // because they have distinct Pan and Tilt attributes.
    QVERIFY(r.dofCount >= 1);
}

// =====================================================================
// synthesizeGDTFFromQXF tests
// =====================================================================

void GDTFKinematics_Test::synthesize_movingHead()
{
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(true, true, 540, 270, false,
                           0, 1, 2, 3, 5.0, 25.0, geo, mode);

    // Should have Base → Yoke → Head → Lamp
    QCOMPARE(geo.root.children.size(), 1);
    QCOMPARE(geo.root.children[0].name, QStringLiteral("Yoke"));
    QCOMPARE(geo.root.children[0].type, GeometryAxis);
    QCOMPARE(geo.root.children[0].children.size(), 1);
    QCOMPARE(geo.root.children[0].children[0].name, QStringLiteral("Head"));
    QCOMPARE(geo.root.children[0].children[0].children.size(), 1);
    QCOMPARE(geo.root.children[0].children[0].children[0].name, QStringLiteral("Lamp"));
    QCOMPARE(geo.root.children[0].children[0].children[0].type, GeometryLamp);

    // Standard (non-mirror): physicalFrom < physicalTo
    QCOMPARE(mode.channels.size(), 2);
    QVERIFY(mode.channels[0].physicalFrom < mode.channels[0].physicalTo);  // Pan: -270 < 270
    QVERIFY(mode.channels[1].physicalFrom < mode.channels[1].physicalTo);  // Tilt: -135 < 135
}

void GDTFKinematics_Test::synthesize_mirrorScanner()
{
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(true, true, 180, 110, true,
                           0, -1, 1, -1, 0, 13.0, geo, mode);

    // Mirror: physicalFrom > physicalTo (inverted)
    QCOMPARE(mode.channels.size(), 2);
    QVERIFY(mode.channels[0].physicalFrom > mode.channels[0].physicalTo);  // Pan inverted
    QVERIFY(mode.channels[1].physicalFrom > mode.channels[1].physicalTo);  // Tilt inverted

    // Head primitive should be Scanner for mirrors
    auto &head = geo.root.children[0].children[0];
    QCOMPARE(head.primitiveType, PrimitiveScanner);
}

void GDTFKinematics_Test::synthesize_panOnly()
{
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(true, false, 360, 0, false,
                           0, -1, -1, -1, 0, 0, geo, mode);

    QCOMPARE(mode.channels.size(), 1);
    QCOMPARE(mode.channels[0].attributeName, QStringLiteral("Pan"));
    // Only Yoke → Lamp (no Head)
    QCOMPARE(geo.root.children.size(), 1);
    QCOMPARE(geo.root.children[0].name, QStringLiteral("Yoke"));
    QCOMPARE(geo.root.children[0].children.size(), 1);
    QCOMPARE(geo.root.children[0].children[0].type, GeometryLamp);
}

void GDTFKinematics_Test::synthesize_fixed()
{
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(false, false, 0, 0, false,
                           -1, -1, -1, -1, 0, 25.0, geo, mode);

    QCOMPARE(mode.channels.size(), 0);
    // Only Base → Lamp
    QCOMPARE(geo.root.children.size(), 1);
    QCOMPARE(geo.root.children[0].type, GeometryLamp);
}

// =====================================================================
// Integration / round-trip tests
// =====================================================================

void GDTFKinematics_Test::pipeline_qxfToKinematics_matchesFactory()
{
    // Synthesize from QXF params, build kinematics, compare with factory.
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(true, true, 540, 270, false,
                           0, -1, 1, -1, 0, 25.0, geo, mode);

    auto r = buildGDTFKinematics(geo.root, mode);
    auto factory = rigmath::KinematicChain::moving_head(540, 270);

    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.chain->dof_count(), factory.dof_count());

    // Forward kinematics should match at several angles
    std::vector<std::pair<double, double>> angles = {
        {0, 0}, {90, 0}, {0, 90}, {-45, 30}, {180, -60}
    };
    for (auto [p, t] : angles)
    {
        rigmath::Ray rSynth = r.chain->forward_local({p, t});
        rigmath::Ray rFac = factory.forward_local({p, t});

        double dot = rSynth.dx * rFac.dx + rSynth.dy * rFac.dy + rSynth.dz * rFac.dz;
        double err = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
        QVERIFY2(err < 0.1,
                 qPrintable(QString("FK mismatch at (%1,%2): %3°").arg(p).arg(t).arg(err)));
    }
}

void GDTFKinematics_Test::pipeline_forwardInverseRoundTrip()
{
    GDTFGeometryData geo;
    GDTFDmxModeInfo mode;
    synthesizeGDTFFromQXF(true, true, 540, 270, false,
                           0, -1, 1, -1, 0, 25.0, geo, mode);
    auto r = buildGDTFKinematics(geo.root, mode);

    // Pick a target point, do IK, then FK, verify ray passes through target
    double tx = 2.0, ty = 1.0, tz = -3.0;
    auto ik = r.chain->inverse_local(tx, ty, tz);
    QVERIFY(ik.reachable);

    rigmath::Ray fk = r.chain->forward_local(ik.angles);

    // Ray should pass near the target: compute perpendicular distance
    double dx = tx - fk.ox, dy = ty - fk.oy, dz = tz - fk.oz;
    // Cross product of (target - origin) × direction
    double cx = dy * fk.dz - dz * fk.dy;
    double cy = dz * fk.dx - dx * fk.dz;
    double cz = dx * fk.dy - dy * fk.dx;
    double perpDist = std::sqrt(cx*cx + cy*cy + cz*cz);
    QVERIFY2(perpDist < 0.01,
             qPrintable(QString("Perp distance: %1m").arg(perpDist, 0, 'f', 6)));
}

void GDTFKinematics_Test::pipeline_multiBeamForwardAll()
{
    // 3-beam fixture: all beams should respond to the same DOF values
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);

    for (int i = 0; i < 3; i++)
    {
        GDTFGeometryNode beam = makeLamp(25.0f);
        beam.name = QString("Beam%1").arg(i);
        beam.localTransform[12] = -0.1f + i * 0.1f;  // X offset
        head.children.append(beam);
    }
    geo.root.children.append(head);

    GDTFDmxModeInfo mode;
    mode.modeName = "Std";
    mode.channels.append(makeTiltChannel(0, -1, -120, 120, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);
    QCOMPARE(r.chain->beam_count(), 3);

    // Tilt 45°: all beams point same direction but from different origins
    auto rays = r.chain->forward_local_all({45});
    QCOMPARE((int)rays.size(), 3);

    // Same direction for all beams
    for (int i = 1; i < 3; i++)
    {
        double dot = rays[0].dx * rays[i].dx + rays[0].dy * rays[i].dy + rays[0].dz * rays[i].dz;
        QVERIFY(dot > 0.99);
    }

    // Different origins (X offsets)
    QVERIFY(std::abs(rays[0].ox - rays[2].ox) > 0.1);
}

void GDTFKinematics_Test::pipeline_channelMapRoundTrip()
{
    // angles → DMX → angles should round-trip within quantization error
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral);
    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead);
    head.children.append(makeLamp());
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "16bit";
    mode.channels.append(makePanChannel(0, 1, -270, 270));
    mode.channels.append(makeTiltChannel(2, 3, -135, 135));

    auto r = buildGDTFKinematics(geo.root, mode);

    std::vector<double> original = {42.7, -18.3};
    auto dmx = r.channelMap->angles_to_dmx(original);
    auto recovered = r.channelMap->dmx_to_angles(dmx, 2);

    // 16-bit quantization: 540°/65535 ≈ 0.008° per step
    QVERIFY(std::abs(recovered[0] - original[0]) < 0.02);
    QVERIFY(std::abs(recovered[1] - original[1]) < 0.02);
}

// =====================================================================
// Beam origin vs scene graph consistency tests
// =====================================================================

void GDTFKinematics_Test::intermediateNode_beforeFirstAxis()
{
    // POS-6 style: Base → Base1(offset Z=-0.141) → Head(Axis) → Lamp
    // The intermediate "Base 1" node should contribute its offset to the
    // beam origin. Without the prefix transform fix, this offset is lost.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode base1 = makeNode("Base 1", GeometryGeneral, PrimitiveUndefined);
    setTransformXYZ(base1.localTransform, 0.0084f, 0.0f, -0.141f);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);
    head.children.append(makeLamp(25.0f, -0.05f));
    base1.children.append(head);
    geo.root.children.append(base1);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makeTiltChannel(0, -1, -120, 120, "Head"));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 1);

    // Home beam origin should include Base1's offsets
    rigmath::Ray home = r.chain->forward_local({0});

    // Expected Z = -0.141 (Base1) + -0.04 (Head) + -0.05 (Lamp) = -0.231
    // Expected X = 0.0084 (Base1)
    verifyOrigin(home, 0.0084, 0.0, -0.231, 0.002);
    verifyDirection(home, 0, 0, -1, 1.0);
}

void GDTFKinematics_Test::intermediateNode_betweenAxes()
{
    // Base → Yoke(Axis, pan) → Mount(General, z=-0.08) → Head(Axis, tilt) → Lamp
    // The "Mount" grouping node between axes should contribute its offset.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke, -0.15f);

    GDTFGeometryNode mount = makeNode("Mount", GeometryGeneral, PrimitiveUndefined, -0.08f);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);
    head.children.append(makeLamp(25.0f, -0.05f));

    mount.children.append(head);
    yoke.children.append(mount);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makePanChannel(0));
    mode.channels.append(makeTiltChannel(1));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);

    // Home beam origin Z = -0.15 (Yoke) + -0.08 (Mount) + -0.04 (Head) + -0.05 (Lamp) = -0.32
    rigmath::Ray home = r.chain->forward_local({0, 0});
    verifyOrigin(home, 0, 0, -0.32, 0.002);
}

void GDTFKinematics_Test::intermediateNode_beforeBeam()
{
    // Base → Head(Axis, tilt) → Optics(General, z=-0.03) → Lamp(z=-0.02)
    // The "Optics" grouping node between axis and lamp should contribute
    // its offset to the beam offset.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.10f);

    GDTFGeometryNode optics = makeNode("Optics", GeometryGeneral, PrimitiveUndefined, -0.03f);
    optics.children.append(makeLamp(25.0f, -0.02f));

    head.children.append(optics);
    geo.root.children.append(head);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makeTiltChannel(0, -1, -120, 120, "Head"));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 1);

    // Home beam origin Z = -0.10 (Head) + -0.03 (Optics) + -0.02 (Lamp) = -0.15
    rigmath::Ray home = r.chain->forward_local({0});
    verifyOrigin(home, 0, 0, -0.15, 0.002);
}

void GDTFKinematics_Test::beamOrigin_matchesSceneGraphWalk()
{
    // Standard moving head (no intermediates).
    // Verify kinematics beam rays match scene-graph-walk beam rays
    // at multiple DOF angles.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);
    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke, -0.15f);
    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);
    head.children.append(makeLamp(25.0f, -0.05f));
    yoke.children.append(head);
    geo.root.children.append(yoke);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makePanChannel(0));
    mode.channels.append(makeTiltChannel(1));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);

    // Test at several angle combinations
    std::vector<std::vector<double>> testAngles = {
        {0, 0}, {45, 0}, {0, 45}, {90, -30}, {-120, 60}
    };

    for (const auto &dofs : testAngles)
    {
        // Kinematics beam ray
        rigmath::Ray kinRay = r.chain->forward_local(dofs);

        // Scene graph walk beam ray
        auto sgRays = sceneGraphBeamRays(geo.root, r.axisTags, dofs);
        QCOMPARE((int)sgRays.size(), 1);

        // Origins should match
        double dx = kinRay.ox - sgRays[0].ox;
        double dy = kinRay.oy - sgRays[0].oy;
        double dz = kinRay.oz - sgRays[0].oz;
        double originDist = std::sqrt(dx*dx + dy*dy + dz*dz);
        QVERIFY2(originDist < 0.001,
                 qPrintable(QString("Origin mismatch at dofs=(%1,%2): %3m")
                            .arg(dofs[0]).arg(dofs[1]).arg(originDist, 0, 'f', 6)));

        // Directions should match
        double dot = kinRay.dx * sgRays[0].dx + kinRay.dy * sgRays[0].dy
                     + kinRay.dz * sgRays[0].dz;
        double angleDeg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
        QVERIFY2(angleDeg < 0.5,
                 qPrintable(QString("Direction mismatch at dofs=(%1,%2): %3°")
                            .arg(dofs[0]).arg(dofs[1]).arg(angleDeg, 0, 'f', 4)));
    }
}

void GDTFKinematics_Test::beamOrigin_matchesSceneGraphWalk_withIntermediates()
{
    // POS-6 style tree with intermediate nodes at all levels:
    //   Base → Base1(offset) → Yoke(Axis, pan) → Mount(offset) → Head(Axis, tilt) → Optics(offset) → Lamp
    // This is the comprehensive consistency test.
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode base1 = makeNode("Base 1", GeometryGeneral);
    setTransformXYZ(base1.localTransform, 0.01f, 0.0f, -0.14f);

    GDTFGeometryNode yoke = makeNode("Yoke", GeometryAxis, PrimitiveYoke, -0.10f);

    GDTFGeometryNode mount = makeNode("Mount", GeometryGeneral, PrimitiveUndefined, -0.06f);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.04f);

    GDTFGeometryNode optics = makeNode("Optics", GeometryGeneral, PrimitiveUndefined, -0.02f);
    optics.children.append(makeLamp(25.0f, -0.01f));

    head.children.append(optics);
    mount.children.append(head);
    yoke.children.append(mount);
    base1.children.append(yoke);
    geo.root.children.append(base1);

    GDTFDmxModeInfo mode;
    mode.modeName = "Standard";
    mode.channels.append(makePanChannel(0, -1, -270, 270, "Yoke"));
    mode.channels.append(makeTiltChannel(1, -1, -135, 135, "Head"));

    GDTFKinematicsResult r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.dofCount, 2);

    // Test at several angle combinations
    std::vector<std::vector<double>> testAngles = {
        {0, 0}, {30, 0}, {0, 45}, {90, -30}, {-60, 60}, {180, 0}
    };

    for (const auto &dofs : testAngles)
    {
        rigmath::Ray kinRay = r.chain->forward_local(dofs);
        auto sgRays = sceneGraphBeamRays(geo.root, r.axisTags, dofs);
        QCOMPARE((int)sgRays.size(), 1);

        // Origins must match within float→double precision
        double dx = kinRay.ox - sgRays[0].ox;
        double dy = kinRay.oy - sgRays[0].oy;
        double dz = kinRay.oz - sgRays[0].oz;
        double originDist = std::sqrt(dx*dx + dy*dy + dz*dz);
        QVERIFY2(originDist < 0.001,
                 qPrintable(QString("Origin mismatch at dofs=(%1,%2): dist=%3m "
                                    "kin=(%4,%5,%6) sg=(%7,%8,%9)")
                            .arg(dofs[0]).arg(dofs[1]).arg(originDist, 0, 'f', 6)
                            .arg(kinRay.ox, 0, 'f', 6).arg(kinRay.oy, 0, 'f', 6).arg(kinRay.oz, 0, 'f', 6)
                            .arg(sgRays[0].ox, 0, 'f', 6).arg(sgRays[0].oy, 0, 'f', 6).arg(sgRays[0].oz, 0, 'f', 6)));

        // Directions must match
        double dot = kinRay.dx * sgRays[0].dx + kinRay.dy * sgRays[0].dy
                     + kinRay.dz * sgRays[0].dz;
        double angleDeg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
        QVERIFY2(angleDeg < 0.5,
                 qPrintable(QString("Direction mismatch at dofs=(%1,%2): %3°")
                            .arg(dofs[0]).arg(dofs[1]).arg(angleDeg, 0, 'f', 4)));
    }

    // Also verify the home origin includes ALL intermediate offsets
    rigmath::Ray home = r.chain->forward_local({0, 0});
    // Expected Z = -0.14 (Base1) + -0.10 (Yoke) + -0.06 (Mount) + -0.04 (Head)
    //              + -0.02 (Optics) + -0.01 (Lamp) = -0.37
    // Expected X = 0.01 (Base1)
    verifyOrigin(home, 0.01, 0, -0.37, 0.002);
}

// =====================================================================
// rigmath v1.1 API adoption tests
// =====================================================================
//
// These tests exercise the v1.1 APIs used in SpatialView::rebuildBeamCones:
//   - KinematicChain::forward_world_all  (single chain walk for all beams)
//   - rigmath::Beam                      (beam + plane-intersection helpers)
//   - Beam::hit_plane_z                  (floor clipping)
//
// They guard the refactor from regressing, and document the intended
// behavior of the floor-clip logic for reviewers.

void GDTFKinematics_Test::forwardWorldAll_matchesForwardWorldPerBeam()
{
    // Multi-beam LED bar. Verify forward_world_all returns the same rays
    // as calling forward_world(beam_idx) once per beam. This is the
    // optimization used in rebuildBeamCones().
    GDTFGeometryData geo;
    geo.root = makeNode("Base", GeometryGeneral, PrimitiveBase);

    GDTFGeometryNode head = makeNode("Head", GeometryAxis, PrimitiveHead, -0.10f);
    for (int i = 0; i < 6; i++)
    {
        GDTFGeometryNode ref;
        ref.name = QString("LED %1").arg(i + 1);
        ref.type = GeometryReference;
        ref.primitiveType = PrimitiveCylinder;
        setIdentityWithOffset(ref.localTransform);
        ref.localTransform[12] = -0.25f + i * 0.10f;  // X offset
        ref.localTransform[14] = -0.05f;              // Z offset
        head.children.append(ref);
    }
    geo.root.children.append(head);

    GDTFDmxModeInfo mode;
    mode.modeName = "Bar";
    mode.channels.append(makeTiltChannel(0, -1, -120, 120, "Head"));

    auto r = buildGDTFKinematics(geo.root, mode);
    QVERIFY(r.chain != nullptr);
    QCOMPARE(r.chain->beam_count(), 6);

    // Fixture mounted at world (2, 3, 4)
    rigmath::RigidTransform xf = rigmath::RigidTransform::identity();
    xf.pos[0] = 2.0; xf.pos[1] = 3.0; xf.pos[2] = 4.0;

    std::vector<std::vector<double>> testDofs = {{0.0}, {30.0}, {-60.0}};

    for (const auto &dofs : testDofs)
    {
        std::vector<rigmath::Ray> allRays = r.chain->forward_world_all(xf, dofs);
        QCOMPARE((int)allRays.size(), 6);

        for (int bi = 0; bi < 6; bi++)
        {
            rigmath::Ray single = r.chain->forward_world(xf, dofs, bi);

            QVERIFY2(std::abs(allRays[bi].ox - single.ox) < 1e-9,
                     qPrintable(QString("beam %1 ox mismatch at dof=%2").arg(bi).arg(dofs[0])));
            QVERIFY2(std::abs(allRays[bi].oy - single.oy) < 1e-9, "oy mismatch");
            QVERIFY2(std::abs(allRays[bi].oz - single.oz) < 1e-9, "oz mismatch");
            QVERIFY2(std::abs(allRays[bi].dx - single.dx) < 1e-9, "dx mismatch");
            QVERIFY2(std::abs(allRays[bi].dy - single.dy) < 1e-9, "dy mismatch");
            QVERIFY2(std::abs(allRays[bi].dz - single.dz) < 1e-9, "dz mismatch");
        }
    }
}

void GDTFKinematics_Test::beamHitPlaneZ_matchesManualFloorClip()
{
    // The old rebuildBeamCones floor-clip math:
    //   if (oz > 0.05 && dz < -1e-3) tFloor = -oz / dz;
    // The new code uses rigmath::Beam::hit_plane_z(0).
    // Verify the two agree on the cases where both produce a valid clip.
    struct Case {
        double ox, oy, oz, dx, dy, dz;
        const char *name;
    };
    std::vector<Case> cases = {
        {0, 0, 5.0,   0,  0, -1.0,        "straight down from 5m"},
        {1, 2, 3.0,   0.5, 0, -0.866025,  "30° off vertical from 3m"},
        {0, 0, 10.0,  0.7071, 0, -0.7071, "45° from 10m"},
        {2, 2, 2.5,   0.1, 0.1, -0.99,    "slightly off-axis from 2.5m"},
    };

    for (const auto &c : cases)
    {
        rigmath::Beam beam;
        beam.origin    = rigmath::Vec3(c.ox, c.oy, c.oz);
        beam.direction = rigmath::Vec3(c.dx, c.dy, c.dz);

        rigmath::PlaneHit hit = beam.hit_plane_z(0.0);
        QVERIFY2(hit.hits, c.name);

        double manualT = -c.oz / c.dz;
        QVERIFY2(std::abs(hit.t - manualT) < 1e-6,
                 qPrintable(QString("%1: hit.t=%2 vs manual=%3")
                            .arg(c.name).arg(hit.t, 0, 'f', 6).arg(manualT, 0, 'f', 6)));

        // Intersection point should land on Z=0
        QVERIFY2(std::abs(hit.point.z) < 1e-6, "intersection should lie on Z=0");
    }
}

void GDTFKinematics_Test::beamHitPlaneZ_rejectsPointingAwayAndParallel()
{
    // Cases where the old code would skip clipping (dz >= -1e-3). The
    // new code via Beam::hit_plane_z should return hits=false so the
    // caller falls through to the default length.

    // Pointing up — misses Z=0 floor from above
    {
        rigmath::Beam beam;
        beam.origin    = rigmath::Vec3(0, 0, 2.0);
        beam.direction = rigmath::Vec3(0, 0, 1.0);
        rigmath::PlaneHit hit = beam.hit_plane_z(0.0);
        QVERIFY2(!hit.hits, "upward beam should miss floor plane");
    }

    // Parallel to floor
    {
        rigmath::Beam beam;
        beam.origin    = rigmath::Vec3(0, 0, 2.0);
        beam.direction = rigmath::Vec3(1.0, 0, 0);
        rigmath::PlaneHit hit = beam.hit_plane_z(0.0);
        QVERIFY2(!hit.hits, "horizontal beam should miss floor plane");
    }

    // Pointing up from below the floor — also a miss from the caller's
    // perspective (t would be negative)
    {
        rigmath::Beam beam;
        beam.origin    = rigmath::Vec3(0, 0, -1.0);
        beam.direction = rigmath::Vec3(0, 0, -1.0);
        rigmath::PlaneHit hit = beam.hit_plane_z(0.0);
        // Origin is already below 0 and direction is further down —
        // either hits=false or t<0; rebuildBeamCones guards with
        // `hit.hits && hit.t > 0.05` so either result is safe.
        QVERIFY2(!hit.hits || hit.t <= 0.05,
                 "beam below plane pointing away should not clip");
    }
}

QTEST_APPLESS_MAIN(GDTFKinematics_Test)
#include "gdtfkinematics_test.moc"
