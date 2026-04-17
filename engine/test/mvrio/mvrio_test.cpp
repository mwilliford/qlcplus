/*
  Q Light Controller Plus - Unit test
  mvrio_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QtTest>
#include <QTemporaryDir>
#include <cmath>

#include "Include/VectorworksMVR.h"

#include "mvrio_test.h"
#include "mvrio.h"
#include "doc.h"
#include "qlcfixturedefcache.h"
#include "spatialmodel.h"

using namespace VectorworksMVR;

namespace {

constexpr double kEps = 1e-9;

// Identity rotation + zero translation.
STransformMatrix mvrIdentity()
{
    STransformMatrix m{};
    m.ux = 1; m.uy = 0; m.uz = 0;
    m.vx = 0; m.vy = 1; m.vz = 0;
    m.wx = 0; m.wy = 0; m.wz = 1;
    m.ox = 0; m.oy = 0; m.oz = 0;
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

void MvrIO_Test::convertMatrix_identity()
{
    STransformMatrix mvr = mvrIdentity();
    rigmath::RigidTransform t = MvrIO::convertMvrMatrix(mvr);

    QCOMPARE(t.pos[0], 0.0);
    QCOMPARE(t.pos[1], 0.0);
    QCOMPARE(t.pos[2], 0.0);

    // rot should be identity
    QCOMPARE(t.rot[0], 1.0); QCOMPARE(t.rot[1], 0.0); QCOMPARE(t.rot[2], 0.0);
    QCOMPARE(t.rot[3], 0.0); QCOMPARE(t.rot[4], 1.0); QCOMPARE(t.rot[5], 0.0);
    QCOMPARE(t.rot[6], 0.0); QCOMPARE(t.rot[7], 0.0); QCOMPARE(t.rot[8], 1.0);
}

void MvrIO_Test::convertMatrix_translationMillimetersToMeters()
{
    STransformMatrix mvr = mvrIdentity();
    mvr.ox = 1500.0;   // 1.5 m
    mvr.oy = -2000.0;  // -2 m
    mvr.oz = 3250.0;   // 3.25 m

    rigmath::RigidTransform t = MvrIO::convertMvrMatrix(mvr);

    QCOMPARE(t.pos[0], 1.5);
    QCOMPARE(t.pos[1], -2.0);
    QCOMPARE(t.pos[2], 3.25);
}

void MvrIO_Test::convertMatrix_pan90Degrees()
{
    // Pan 90° around Z axis: local X → world Y, local Y → world -X, local Z → world Z.
    // Columns: u=(0,1,0), v=(-1,0,0), w=(0,0,1).
    STransformMatrix mvr{};
    mvr.ux = 0; mvr.uy = 1; mvr.uz = 0;
    mvr.vx = -1; mvr.vy = 0; mvr.vz = 0;
    mvr.wx = 0; mvr.wy = 0; mvr.wz = 1;

    rigmath::RigidTransform t = MvrIO::convertMvrMatrix(mvr);

    // Row-major 3x3 built from those columns:
    QVERIFY(std::abs(t.rot[0] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[1] - (-1.0)) < kEps);
    QVERIFY(std::abs(t.rot[2] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[3] - 1.0) < kEps);
    QVERIFY(std::abs(t.rot[4] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[5] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[6] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[7] - 0.0) < kEps);
    QVERIFY(std::abs(t.rot[8] - 1.0) < kEps);

    // Transforming local X (1,0,0) should give world (0,1,0).
    double x = 1, y = 0, z = 0;
    t.transform_direction(x, y, z);
    QVERIFY(std::abs(x - 0.0) < kEps);
    QVERIFY(std::abs(y - 1.0) < kEps);
    QVERIFY(std::abs(z - 0.0) < kEps);
}

void MvrIO_Test::convertMatrix_tilt90Degrees()
{
    // Tilt 90° around X axis: local X unchanged, local Y → world Z, local Z → world -Y.
    // Columns: u=(1,0,0), v=(0,0,1), w=(0,-1,0).
    STransformMatrix mvr{};
    mvr.ux = 1; mvr.uy = 0; mvr.uz = 0;
    mvr.vx = 0; mvr.vy = 0; mvr.vz = 1;
    mvr.wx = 0; mvr.wy = -1; mvr.wz = 0;

    rigmath::RigidTransform t = MvrIO::convertMvrMatrix(mvr);

    // Local Y (0,1,0) -> world Z (0,0,1)
    double x = 0, y = 1, z = 0;
    t.transform_direction(x, y, z);
    QVERIFY(std::abs(x - 0.0) < kEps);
    QVERIFY(std::abs(y - 0.0) < kEps);
    QVERIFY(std::abs(z - 1.0) < kEps);

    // Local Z (0,0,1) -> world -Y (0,-1,0)
    x = 0; y = 0; z = 1;
    t.transform_direction(x, y, z);
    QVERIFY(std::abs(x - 0.0) < kEps);
    QVERIFY(std::abs(y - (-1.0)) < kEps);
    QVERIFY(std::abs(z - 0.0) < kEps);
}

void MvrIO_Test::convertMatrix_roundTripRigmathToMvrAndBack()
{
    // Synthesize a rigmath transform, convert to MVR, then back.
    rigmath::RigidTransform original =
        rigmath::RigidTransform::from_pose(2.5, -1.75, 0.5, 0.3, 0.2, 0.1);

    STransformMatrix mvr{};
    MvrIO::convertToMvrMatrix(original, mvr);
    rigmath::RigidTransform back = MvrIO::convertMvrMatrix(mvr);

    for (int i = 0; i < 9; ++i)
        QVERIFY(std::abs(original.rot[i] - back.rot[i]) < 1e-12);
    for (int i = 0; i < 3; ++i)
        QVERIFY(std::abs(original.pos[i] - back.pos[i]) < 1e-12);

    // Sanity: translation survives the mm↔m round-trip.
    QVERIFY(std::abs(mvr.ox - 2500.0) < 1e-9);
    QVERIFY(std::abs(mvr.oy - (-1750.0)) < 1e-9);
    QVERIFY(std::abs(mvr.oz - 500.0) < 1e-9);
}

// ---------------------------------------------------------------------------
// Import — real libMVRgdtf round-trip
// ---------------------------------------------------------------------------

namespace {

// Write an MVR with one layer and optionally one fixture. Returns the path.
// If gdtfName is non-empty, the fixture is added but no GDTF is embedded —
// so the import must resolve it (and will fail, by design, for the
// missing-GDTF test).
QString writeSyntheticMvr(const QString &dir,
                          const QString &baseName,
                          const QString &gdtfName)
{
    const QString path = dir + QChar('/') + baseName + QStringLiteral(".mvr");

    IMediaRessourceVectorInterfacePtr mvr;
    if (mvr.Query(IID_MediaRessourceVectorInterface) != kVCOMError_NoError)
        return {};
    if (mvr->OpenForWrite(path.toUtf8().constData()) != kVCOMError_NoError)
        return {};

    mvr->AddProviderAndProviderVersion("bunnyhole-mvrio-test", "0.1");

    ISceneObjPtr layer;
    if (mvr->CreateLayerObject(MvrUUID(1, 2, 3, 4), "TestLayer", &layer)
        != kVCOMError_NoError)
    {
        return {};
    }

    if (!gdtfName.isEmpty())
    {
        ISceneObjPtr fixture;
        STransformMatrix mat{};
        mat.ux = 1; mat.vy = 1; mat.wz = 1;
        if (mvr->CreateFixture(MvrUUID(5, 6, 7, 8), mat,
                                "TestFixture", layer, &fixture)
            == kVCOMError_NoError)
        {
            fixture->SetGdtfName(gdtfName.toUtf8().constData());
            fixture->SetGdtfMode("Mode1");
            fixture->AddAdress(1, 0);
        }
    }

    mvr->Close();
    return path;
}

} // namespace

void MvrIO_Test::importEmptyMvr_succeedsWithZeroFixtures()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString mvrPath = writeSyntheticMvr(tmp.path(), "empty", QString());
    QVERIFY(!mvrPath.isEmpty());

    Doc doc(nullptr);
    MvrIO io(&doc);
    const bool ok = io.importMvr(mvrPath);

    QVERIFY2(ok, qPrintable(io.lastError()));
    QCOMPARE(io.importedFixtureCount(), 0);
    QCOMPARE(io.importedFocusPointCount(), 0);
    QVERIFY(io.missingGdtfs().isEmpty());
}

void MvrIO_Test::importMvr_missingGdtfIsSkippedNotAborted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString mvrPath = writeSyntheticMvr(
        tmp.path(), "one-missing", "nonexistent-fixture.gdtf");
    QVERIFY(!mvrPath.isEmpty());

    Doc doc(nullptr);
    MvrIO io(&doc);
    const bool ok = io.importMvr(mvrPath);

    // Import should succeed overall; the missing-GDTF fixture is skipped.
    QVERIFY2(ok, qPrintable(io.lastError()));
    QCOMPARE(io.importedFixtureCount(), 0);
    QVERIFY(io.missingGdtfs().contains(QStringLiteral("nonexistent-fixture.gdtf")));
}

QTEST_APPLESS_MAIN(MvrIO_Test)
