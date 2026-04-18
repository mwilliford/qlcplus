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
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

#include "Include/VectorworksMVR.h"

#include "mvrio_test.h"
#include "mvrio.h"
#include "doc.h"
#include "fixture.h"
#include "gdtfparser.h"
#include "qlcchannel.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
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

// ---------------------------------------------------------------------------
// Project-scoped GDTF registry
// ---------------------------------------------------------------------------

namespace {

// Write an MVR with `fixtureCount` fixtures, all referencing the same embedded
// GDTF (loaded from `gdtfSourcePath`, stored in the archive as `gdtfNameInMvr`).
// Returns the path to the written .mvr, or empty string on failure.
QString writeMvrWithEmbeddedGdtf(const QString &dir,
                                 const QString &baseName,
                                 const QString &gdtfSourcePath,
                                 const QString &gdtfNameInMvr,
                                 int fixtureCount)
{
    QFile src(gdtfSourcePath);
    if (!src.open(QIODevice::ReadOnly))
        return {};
    QByteArray gdtfBytes = src.readAll();
    src.close();
    if (gdtfBytes.isEmpty())
        return {};

    const QString path = dir + QChar('/') + baseName + QStringLiteral(".mvr");

    IMediaRessourceVectorInterfacePtr mvr;
    if (mvr.Query(IID_MediaRessourceVectorInterface) != kVCOMError_NoError)
        return {};
    if (mvr->OpenForWrite(path.toUtf8().constData()) != kVCOMError_NoError)
        return {};

    mvr->AddProviderAndProviderVersion("bunnyhole-mvrio-test", "0.1");

    // Embed the GDTF as an attached file in the MVR zip.
    if (mvr->AddBufferToMvrFile(gdtfNameInMvr.toUtf8().constData(),
                                 gdtfBytes.data(),
                                 static_cast<size_t>(gdtfBytes.size()))
        != kVCOMError_NoError)
    {
        return {};
    }

    ISceneObjPtr layer;
    if (mvr->CreateLayerObject(MvrUUID(1, 2, 3, 4), "TestLayer", &layer)
        != kVCOMError_NoError)
    {
        return {};
    }

    for (int i = 0; i < fixtureCount; ++i)
    {
        ISceneObjPtr fixture;
        STransformMatrix mat{};
        mat.ux = 1; mat.vy = 1; mat.wz = 1;
        mat.ox = i * 500.0;
        if (mvr->CreateFixture(MvrUUID(5 + i, 6, 7, 8), mat,
                                QStringLiteral("Fixture %1").arg(i + 1)
                                    .toUtf8().constData(),
                                layer, &fixture)
            == kVCOMError_NoError)
        {
            fixture->SetGdtfName(gdtfNameInMvr.toUtf8().constData());
            fixture->SetGdtfMode("Standard Mode (32 ch)");
            // AddAdress(absoluteDmxAddress, breakId). allWorking.gdtf occupies
            // 54 channels per fixture, so step by 64 to keep them disjoint.
            fixture->AddAdress(static_cast<size_t>(1 + i * 64), 0);
        }
    }

    mvr->Close();
    return path;
}

} // namespace

void MvrIO_Test::importMvr_dedupsGdtfWithinSameFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString mvrPath = writeMvrWithEmbeddedGdtf(
        tmp.path(), "two-fixtures",
        QStringLiteral(MVRIO_TEST_GDTF_PATH),
        QStringLiteral("shared.gdtf"), 2);
    QVERIFY2(!mvrPath.isEmpty(),
             "Failed to synthesize MVR — check MVRIO_TEST_GDTF_PATH");

    Doc doc(nullptr);
    MvrIO io(&doc);
    QVERIFY2(io.importMvr(mvrPath), qPrintable(io.lastError()));

    QCOMPARE(io.importedFixtureCount(), 2);
    QVERIFY(io.missingGdtfs().isEmpty());

    // Both imported Fixtures must share the same QLCFixtureDef — the GDTF
    // was parsed exactly once and reused.
    QList<Fixture*> fxis = doc.fixtures();
    QCOMPARE(fxis.size(), 2);
    QVERIFY(fxis[0]->fixtureDef() != nullptr);
    QCOMPARE(fxis[0]->fixtureDef(), fxis[1]->fixtureDef());

    // That single def must live in the project cache (not the global cache).
    QLCFixtureDef *projDef = doc.projectFixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800"));
    QVERIFY(projDef != nullptr);
    QCOMPARE(fxis[0]->fixtureDef(), projDef);
}

void MvrIO_Test::importMvr_projectCacheIsolatedFromGlobalCache()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);

    // Pre-populate the *global* cache with a bare def at the same
    // (manufacturer, model) coordinates that the embedded GDTF declares.
    // A production user's global cache can easily hit this case — the
    // MVR import must not collide with, overwrite, or delete it.
    QLCFixtureDef *preExistingGlobal = new QLCFixtureDef();
    preExistingGlobal->setManufacturer(QStringLiteral("Clay Paky"));
    preExistingGlobal->setModel(QStringLiteral("Alpha Spot QWO 800"));
    QVERIFY(doc.fixtureDefCache()->addFixtureDef(preExistingGlobal));

    const QString mvrPath = writeMvrWithEmbeddedGdtf(
        tmp.path(), "one-fixture",
        QStringLiteral(MVRIO_TEST_GDTF_PATH),
        QStringLiteral("alpha.gdtf"), 1);
    QVERIFY(!mvrPath.isEmpty());

    MvrIO io(&doc);
    QVERIFY2(io.importMvr(mvrPath), qPrintable(io.lastError()));
    QCOMPARE(io.importedFixtureCount(), 1);

    // The project cache got its own, freshly-parsed def.
    QLCFixtureDef *projDef = doc.projectFixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800"));
    QVERIFY(projDef != nullptr);
    QVERIFY(projDef != preExistingGlobal);

    // Global cache still has the pre-existing bare def, unmodified.
    QLCFixtureDef *globalDef = doc.fixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800"));
    QCOMPARE(globalDef, preExistingGlobal);
}

void MvrIO_Test::clearContents_purgesProjectCacheButNotGlobal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);

    QLCFixtureDef *preExistingGlobal = new QLCFixtureDef();
    preExistingGlobal->setManufacturer(QStringLiteral("Clay Paky"));
    preExistingGlobal->setModel(QStringLiteral("Alpha Spot QWO 800"));
    QVERIFY(doc.fixtureDefCache()->addFixtureDef(preExistingGlobal));

    const QString mvrPath = writeMvrWithEmbeddedGdtf(
        tmp.path(), "to-clear",
        QStringLiteral(MVRIO_TEST_GDTF_PATH),
        QStringLiteral("alpha.gdtf"), 1);
    QVERIFY(!mvrPath.isEmpty());

    MvrIO io(&doc);
    QVERIFY2(io.importMvr(mvrPath), qPrintable(io.lastError()));
    QVERIFY(doc.projectFixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800")) != nullptr);

    doc.clearContents();

    // Project cache is wiped.
    QVERIFY(doc.projectFixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800")) == nullptr);

    // Global cache survives.
    QCOMPARE(doc.fixtureDefCache()->fixtureDef(
        QStringLiteral("Clay Paky"),
        QStringLiteral("Alpha Spot QWO 800")),
        preExistingGlobal);
}

// ---------------------------------------------------------------------------
// Export — MVR-2
// ---------------------------------------------------------------------------

namespace {

// Build a minimal QLCFixtureDef from scratch (2 modes, 2 channels) — gives us
// a def that is NOT backed by a .gdtf file, so embedGdtfForFixture() will
// hit the QXF-only skip path.
QLCFixtureDef *makeBareQxfDef(const QString &mfg, const QString &model)
{
    auto *def = new QLCFixtureDef();
    def->setManufacturer(mfg);
    def->setModel(model);
    def->setType(QLCFixtureDef::Dimmer);

    auto *ch = new QLCChannel();
    ch->setName(QStringLiteral("Intensity"));
    def->addChannel(ch);

    auto *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("1 Channel"));
    mode->insertChannel(ch, 0);
    def->addMode(mode);

    return def;
}

// Import a GDTF from the test fixtures directory into the given cache and
// return the parsed def + its raw bytes. The raw bytes are stashed on the
// def so subsequent exports can round-trip the archive.
QLCFixtureDef *parseTestGdtfIntoCache(QLCFixtureDefCache *cache,
                                      const QString &gdtfPath)
{
    QFile f(gdtfPath);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty())
        return nullptr;

    auto *def = new QLCFixtureDef();
    GDTFParser parser;
    if (!parser.loadGDTFFromBuffer(bytes, def))
    {
        delete def;
        return nullptr;
    }
    def->setRawGdtfBytes(bytes);
    def->setDefinitionSourceFile(gdtfPath);
    if (!cache->addFixtureDef(def))
    {
        delete def;
        return cache->fixtureDef(def->manufacturer(), def->model());
    }
    return def;
}

// Add a patched Fixture to Doc with the given def+mode, universe, address
// and committed transform. Returns the Doc-assigned fixture id, or
// Fixture::invalidId() on failure.
quint32 patchFixture(Doc *doc,
                     const QString &name,
                     QLCFixtureDef *def,
                     QLCFixtureMode *mode,
                     int universe, int address,
                     const rigmath::RigidTransform &xform)
{
    auto *fxi = new Fixture(doc);
    fxi->setName(name);
    fxi->setFixtureDefinition(def, mode);
    fxi->setUniverse(universe);
    fxi->setAddress(address);
    if (!doc->addFixture(fxi))
    {
        delete fxi;
        return Fixture::invalidId();
    }
    doc->spatialModel()->setFixtureTransform(
        QString::number(fxi->id()), xform, SpatialModel::Committed);
    return fxi->id();
}

} // namespace

void MvrIO_Test::exportMvr_emptyDocProducesOpenableArchive()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    MvrIO io(&doc);
    const QString path = tmp.path() + QStringLiteral("/empty.mvr");
    QVERIFY2(io.exportMvr(path), qPrintable(io.lastError()));

    QCOMPARE(io.exportedFixtureCount(), 0);
    QCOMPARE(io.exportedGdtfCount(), 0);
    QVERIFY(io.skippedOnExport().isEmpty());
    QVERIFY(QFile::exists(path));

    // Sanity: re-import succeeds with zero fixtures/focus points.
    Doc doc2(nullptr);
    MvrIO io2(&doc2);
    QVERIFY2(io2.importMvr(path), qPrintable(io2.lastError()));
    QCOMPARE(io2.importedFixtureCount(), 0);
    QCOMPARE(io2.importedFocusPointCount(), 0);
}

void MvrIO_Test::exportMvr_roundTripPreservesFixturesAndPositions()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    QLCFixtureDef *def = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(MVRIO_TEST_GDTF_PATH));
    QVERIFY(def != nullptr);
    QVERIFY(def->modes().size() > 0);
    QLCFixtureMode *mode = def->modes().first();

    // Two fixtures at distinct positions.
    const auto xA = rigmath::RigidTransform::translation(2.5, -1.0, 3.75);
    const auto xB = rigmath::RigidTransform::translation(-0.5, 4.25, 1.0);
    const quint32 idA = patchFixture(&doc, QStringLiteral("FixA"),
                                     def, mode, 0, 0, xA);
    const quint32 idB = patchFixture(&doc, QStringLiteral("FixB"),
                                     def, mode, 0, 64, xB);
    QVERIFY(idA != Fixture::invalidId());
    QVERIFY(idB != Fixture::invalidId());

    const QString path = tmp.path() + QStringLiteral("/round-trip.mvr");
    MvrIO exporter(&doc);
    QVERIFY2(exporter.exportMvr(path), qPrintable(exporter.lastError()));
    QCOMPARE(exporter.exportedFixtureCount(), 2);
    QCOMPARE(exporter.exportedGdtfCount(), 1);

    // Fresh Doc, re-import.
    Doc doc2(nullptr);
    MvrIO importer(&doc2);
    QVERIFY2(importer.importMvr(path), qPrintable(importer.lastError()));
    QCOMPARE(importer.importedFixtureCount(), 2);
    QVERIFY(importer.missingGdtfs().isEmpty());

    // Positions survive to within mm precision (MVR stores ox/oy/oz in mm).
    QList<Fixture*> round = doc2.fixtures();
    QCOMPARE(round.size(), 2);

    auto posVec = [&](Fixture *fxi) -> rigmath::RigidTransform {
        auto t = doc2.spatialModel()->committedTransform(
            QString::number(fxi->id()));
        return t.value_or(rigmath::RigidTransform::identity());
    };
    const auto t0 = posVec(round[0]);
    const auto t1 = posVec(round[1]);
    const bool a_first = std::abs(t0.pos[0] - 2.5) < 1e-3;
    const rigmath::RigidTransform &tA = a_first ? t0 : t1;
    const rigmath::RigidTransform &tB = a_first ? t1 : t0;
    QVERIFY(std::abs(tA.pos[0] - 2.5)  < 1e-3);
    QVERIFY(std::abs(tA.pos[1] - -1.0) < 1e-3);
    QVERIFY(std::abs(tA.pos[2] - 3.75) < 1e-3);
    QVERIFY(std::abs(tB.pos[0] - -0.5) < 1e-3);
    QVERIFY(std::abs(tB.pos[1] - 4.25) < 1e-3);
    QVERIFY(std::abs(tB.pos[2] - 1.0)  < 1e-3);
}

void MvrIO_Test::exportMvr_dedupsGdtfAcrossFixtures()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    QLCFixtureDef *def = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(MVRIO_TEST_GDTF_PATH));
    QVERIFY(def != nullptr);
    QLCFixtureMode *mode = def->modes().first();

    // Three fixtures, all sharing the same def → one GDTF in the archive.
    QVERIFY(patchFixture(&doc, QStringLiteral("F1"), def, mode, 0, 0,
                         rigmath::RigidTransform::identity()) != Fixture::invalidId());
    QVERIFY(patchFixture(&doc, QStringLiteral("F2"), def, mode, 0, 64,
                         rigmath::RigidTransform::identity()) != Fixture::invalidId());
    QVERIFY(patchFixture(&doc, QStringLiteral("F3"), def, mode, 0, 128,
                         rigmath::RigidTransform::identity()) != Fixture::invalidId());

    const QString path = tmp.path() + QStringLiteral("/dedup.mvr");
    MvrIO io(&doc);
    QVERIFY2(io.exportMvr(path), qPrintable(io.lastError()));
    QCOMPARE(io.exportedFixtureCount(), 3);
    QCOMPARE(io.exportedGdtfCount(), 1);  // deduplicated
    QVERIFY(io.skippedOnExport().isEmpty());
}

void MvrIO_Test::exportMvr_fallsBackToIdentityWithoutSpatialModel()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    QLCFixtureDef *def = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(MVRIO_TEST_GDTF_PATH));
    QVERIFY(def != nullptr);
    QLCFixtureMode *mode = def->modes().first();

    // Patch fixture but DO NOT write a committed transform.
    auto *fxi = new Fixture(&doc);
    fxi->setName(QStringLiteral("Unplaced"));
    fxi->setFixtureDefinition(def, mode);
    fxi->setUniverse(0);
    fxi->setAddress(0);
    QVERIFY(doc.addFixture(fxi));
    QVERIFY(!doc.spatialModel()->committedTransform(
                QString::number(fxi->id())).has_value());

    const QString path = tmp.path() + QStringLiteral("/no-spatial.mvr");
    MvrIO exporter(&doc);
    QVERIFY2(exporter.exportMvr(path), qPrintable(exporter.lastError()));

    // Re-import: the fallback fixture lands at the origin.
    Doc doc2(nullptr);
    MvrIO importer(&doc2);
    QVERIFY2(importer.importMvr(path), qPrintable(importer.lastError()));
    QCOMPARE(importer.importedFixtureCount(), 1);
    Fixture *imported = doc2.fixtures().first();
    auto t = doc2.spatialModel()->committedTransform(
        QString::number(imported->id()));
    QVERIFY(t.has_value());
    QVERIFY(std::abs(t->pos[0]) < 1e-6);
    QVERIFY(std::abs(t->pos[1]) < 1e-6);
    QVERIFY(std::abs(t->pos[2]) < 1e-6);
}

void MvrIO_Test::exportMvr_synthesizesGdtfForQxfOnlyFixtures()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);

    // One proper GDTF-backed fixture + one QXF-only (no gdtf bytes, no path).
    // With MVR-3a, QXF-only fixtures no longer skip — GDTFWriter synthesizes
    // a minimal GDTF on the fly from the mode's channel list.
    QLCFixtureDef *gdtfDef = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(MVRIO_TEST_GDTF_PATH));
    QVERIFY(gdtfDef != nullptr);
    QLCFixtureMode *gdtfMode = gdtfDef->modes().first();

    QLCFixtureDef *qxfDef = makeBareQxfDef(QStringLiteral("Acme"),
                                            QStringLiteral("DIM1"));
    QVERIFY(doc.fixtureDefCache()->addFixtureDef(qxfDef));
    QLCFixtureMode *qxfMode = qxfDef->modes().first();

    QVERIFY(patchFixture(&doc, QStringLiteral("Good"),
                         gdtfDef, gdtfMode, 0, 0,
                         rigmath::RigidTransform::identity()) != Fixture::invalidId());
    QVERIFY(patchFixture(&doc, QStringLiteral("Synth"),
                         qxfDef, qxfMode, 0, 64,
                         rigmath::RigidTransform::identity()) != Fixture::invalidId());

    const QString path = tmp.path() + QStringLiteral("/mixed.mvr");
    MvrIO io(&doc);
    QVERIFY2(io.exportMvr(path), qPrintable(io.lastError()));

    // Both fixtures are written and both get an embedded GDTF — the QXF-only
    // one is synthesized, not skipped.
    QCOMPARE(io.exportedFixtureCount(), 2);
    QCOMPARE(io.exportedGdtfCount(), 2);
    QCOMPARE(io.skippedOnExport().size(), 0);

    // Round-trip: import the MVR into a fresh Doc and verify the synthetic
    // GDTF parses back into a usable fixture def.
    Doc doc2(nullptr);
    MvrIO importer(&doc2);
    QVERIFY2(importer.importMvr(path), qPrintable(importer.lastError()));
    QCOMPARE(importer.importedFixtureCount(), 2);
}

void MvrIO_Test::exportMvr_preservesMvrUuidOnRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Round 1: export a fresh fixture. Fixture gets a newly-minted UUID.
    Doc doc(nullptr);
    QLCFixtureDef *def = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(MVRIO_TEST_GDTF_PATH));
    QVERIFY(def != nullptr);
    const quint32 id = patchFixture(&doc, QStringLiteral("Stable"),
                                     def, def->modes().first(), 0, 0,
                                     rigmath::RigidTransform::identity());
    QVERIFY(id != Fixture::invalidId());
    Fixture *fxi = doc.fixture(id);
    QVERIFY(fxi != nullptr);
    QVERIFY(fxi->mvrUuid().isEmpty());  // None yet.

    const QString path = tmp.path() + QStringLiteral("/uuid.mvr");
    MvrIO exporter(&doc);
    QVERIFY2(exporter.exportMvr(path), qPrintable(exporter.lastError()));

    const QString assignedUuid = fxi->mvrUuid();
    QVERIFY(!assignedUuid.isEmpty());

    // Round 2: import into a fresh Doc. Imported fixture has the same UUID.
    Doc doc2(nullptr);
    MvrIO importer(&doc2);
    QVERIFY2(importer.importMvr(path), qPrintable(importer.lastError()));
    QCOMPARE(importer.importedFixtureCount(), 1);
    Fixture *roundTripped = doc2.fixtures().first();
    QCOMPARE(roundTripped->mvrUuid(), assignedUuid);

    // Round 3: re-export and confirm the UUID is preserved (not regenerated).
    const QString path2 = tmp.path() + QStringLiteral("/uuid2.mvr");
    MvrIO exporter2(&doc2);
    QVERIFY2(exporter2.exportMvr(path2), qPrintable(exporter2.lastError()));
    QCOMPARE(roundTripped->mvrUuid(), assignedUuid);
}

QTEST_APPLESS_MAIN(MvrIO_Test)
