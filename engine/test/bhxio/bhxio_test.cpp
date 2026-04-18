/*
  Q Light Controller Plus - Unit test
  bhxio_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include <private/qzipreader_p.h>

#include "bhxio_test.h"

#include "bhxio.h"
#include "doc.h"
#include "fixture.h"
#include "gdtfparser.h"
#include "qlcchannel.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "scene.h"
#include "scenevalue.h"
#include "spatialmodel.h"

namespace {

// Load a GDTF off disk, parse it, stash raw bytes, register in Doc's global cache.
// Same approach as mvrio_test — `.bhx` needs at least one real fixture def so the
// MVR rig half contains a parsable GDTF on round-trip.
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

// Make a plain 1-channel Dimmer def (no GDTF) — exercises the QXF-only
// synthesis path under BhxIO/MvrIO.
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

void BhxIO_Test::saveEmptyWorkspace_producesOpenableBhx()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    BhxIO io(&doc);
    const QString path = tmp.path() + QStringLiteral("/empty.bhx");
    QVERIFY2(io.saveBhx(path), qPrintable(io.lastError()));
    QVERIFY(QFile::exists(path));

    // All four extension streams plus manifest must be present in the archive,
    // even for an empty workspace — openers rely on missing paths meaning
    // "optional absent", not "bug".
    QZipReader zr(path);
    QVERIFY(zr.isReadable());
    QVERIFY(!zr.fileData(QStringLiteral("manifest.json")).isEmpty());
    QVERIFY(!zr.fileData(QStringLiteral("programming/functions.xml")).isEmpty());
    QVERIFY(!zr.fileData(QStringLiteral("io/universes.xml")).isEmpty());
    QVERIFY(!zr.fileData(QStringLiteral("calibration/spatial.xml")).isEmpty());
    zr.close();

    // Re-open round-trip.
    Doc doc2(nullptr);
    BhxIO io2(&doc2);
    QVERIFY2(io2.openBhx(path), qPrintable(io2.lastError()));
    QCOMPARE(doc2.fixtures().size(), 0);
    QCOMPARE(doc2.functions().size(), 0);
    QVERIFY(io2.consoleXml().isEmpty());
}

void BhxIO_Test::saveAndReopen_preservesFixturesFunctionsAndSpatial()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    QLCFixtureDef *def = parseTestGdtfIntoCache(
        doc.fixtureDefCache(), QStringLiteral(BHXIO_TEST_GDTF_PATH));
    QVERIFY(def != nullptr);
    QLCFixtureMode *mode = def->modes().first();

    const auto xA = rigmath::RigidTransform::translation(2.5, -1.0, 3.75);
    const auto xB = rigmath::RigidTransform::translation(-0.5, 4.25, 1.0);
    const quint32 idA = patchFixture(&doc, QStringLiteral("FixA"),
                                     def, mode, 0, 0, xA);
    const quint32 idB = patchFixture(&doc, QStringLiteral("FixB"),
                                     def, mode, 0, 64, xB);
    QVERIFY(idA != Fixture::invalidId());
    QVERIFY(idB != Fixture::invalidId());

    // Scene referencing fixture A's first channel.
    auto *scene = new Scene(&doc);
    scene->setName(QStringLiteral("TestScene"));
    scene->setValue(SceneValue(idA, 0, 255));
    QVERIFY(doc.addFunction(scene));
    const quint32 sceneId = scene->id();

    const QString path = tmp.path() + QStringLiteral("/populated.bhx");
    BhxIO saver(&doc);
    QVERIFY2(saver.saveBhx(path), qPrintable(saver.lastError()));

    // Fresh Doc, open, inspect.
    Doc doc2(nullptr);
    BhxIO opener(&doc2);
    QVERIFY2(opener.openBhx(path), qPrintable(opener.lastError()));

    QCOMPARE(doc2.fixtures().size(), 2);

    // Fixture IDs survive the round-trip (this is why programming/functions.xml
    // carries the canonical Fixture XML — MVR rig half alone would regenerate
    // IDs and break Scene references).
    QVERIFY(doc2.fixture(idA) != nullptr);
    QVERIFY(doc2.fixture(idB) != nullptr);

    // Scene survives and its fixture reference still resolves.
    Function *roundFn = doc2.function(sceneId);
    QVERIFY(roundFn != nullptr);
    QCOMPARE(roundFn->type(), Function::SceneType);
    Scene *roundScene = qobject_cast<Scene*>(roundFn);
    QVERIFY(roundScene != nullptr);
    QList<SceneValue> values = roundScene->values();
    QCOMPARE(values.size(), 1);
    QCOMPARE(values.first().fxi, idA);
    QCOMPARE(values.first().channel, quint32(0));
    QCOMPARE(values.first().value, uchar(255));

    // Spatial transforms survive (within mm precision — MVR uses millimeters).
    auto tA = doc2.spatialModel()->committedTransform(QString::number(idA));
    auto tB = doc2.spatialModel()->committedTransform(QString::number(idB));
    QVERIFY(tA.has_value());
    QVERIFY(tB.has_value());
    QVERIFY(std::abs(tA->pos[0] - 2.5)  < 1e-3);
    QVERIFY(std::abs(tA->pos[1] - -1.0) < 1e-3);
    QVERIFY(std::abs(tA->pos[2] - 3.75) < 1e-3);
    QVERIFY(std::abs(tB->pos[0] - -0.5) < 1e-3);
    QVERIFY(std::abs(tB->pos[1] - 4.25) < 1e-3);
    QVERIFY(std::abs(tB->pos[2] - 1.0)  < 1e-3);
}

void BhxIO_Test::saveAndReopen_preservesConsoleXmlBytes()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    Doc doc(nullptr);
    BhxIO saver(&doc);

    // BhxIO is agnostic about what's inside `console/virtualconsole.xml` —
    // VirtualConsole lives in the UI layer. Engine test passes a sentinel buffer
    // through and asserts it round-trips byte-for-byte.
    const QByteArray vc = QByteArrayLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<VirtualConsole>\n"
        "  <Widget id=\"42\"/>\n"
        "</VirtualConsole>\n");

    const QString path = tmp.path() + QStringLiteral("/with-console.bhx");
    QVERIFY2(saver.saveBhx(path, vc), qPrintable(saver.lastError()));

    Doc doc2(nullptr);
    BhxIO opener(&doc2);
    QVERIFY2(opener.openBhx(path), qPrintable(opener.lastError()));
    QCOMPARE(opener.consoleXml(), vc);
}

void BhxIO_Test::bhx_isValidMvrRig()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // One QXF-only fixture — exercises the GDTF synthesis path inside
    // MvrIO, which we piggy-back on for .bhx saves.
    Doc doc(nullptr);
    QLCFixtureDef *qxfDef = makeBareQxfDef(QStringLiteral("Acme"),
                                            QStringLiteral("DIM1"));
    QVERIFY(doc.fixtureDefCache()->addFixtureDef(qxfDef));
    QVERIFY(patchFixture(&doc, QStringLiteral("D1"),
                          qxfDef, qxfDef->modes().first(),
                          0, 0, rigmath::RigidTransform::identity())
              != Fixture::invalidId());

    const QString path = tmp.path() + QStringLiteral("/valid-mvr.bhx");
    BhxIO saver(&doc);
    QVERIFY2(saver.saveBhx(path), qPrintable(saver.lastError()));

    // The rig half at the archive root must be a spec-valid MVR: zip with
    // GeneralSceneDescription.xml at the top level, at least one embedded
    // .gdtf, and our extension folders on the side (ignored by MVR hosts).
    QZipReader zr(path);
    QVERIFY(zr.isReadable());

    bool hasSceneDescription = false;
    bool hasGdtf = false;
    bool hasProgramming = false;
    const auto entries = zr.fileInfoList();
    for (const auto &info : entries)
    {
        if (info.filePath == QStringLiteral("GeneralSceneDescription.xml"))
            hasSceneDescription = true;
        else if (info.filePath.endsWith(QStringLiteral(".gdtf")))
            hasGdtf = true;
        else if (info.filePath == QStringLiteral("programming/functions.xml"))
            hasProgramming = true;
    }
    zr.close();

    QVERIFY2(hasSceneDescription,
             "MVR rig half must have GeneralSceneDescription.xml at root");
    QVERIFY2(hasGdtf, "MVR rig half must embed at least one .gdtf");
    QVERIFY2(hasProgramming,
             ".bhx must embed programming/functions.xml alongside MVR rig");
}

void BhxIO_Test::openMissingFile_returnsErrorWithoutCrash()
{
    Doc doc(nullptr);
    BhxIO io(&doc);
    QVERIFY(!io.openBhx(QStringLiteral("/no/such/file.bhx")));
    QVERIFY(!io.lastError().isEmpty());
}

void BhxIO_Test::openNonBhxZip_returnsErrorWithoutCrash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Any non-zip bytes — BhxIO must report a clean error, not crash.
    const QString path = tmp.path() + QStringLiteral("/bogus.bhx");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a zip at all");
    f.close();

    Doc doc(nullptr);
    BhxIO io(&doc);
    QVERIFY(!io.openBhx(path));
    QVERIFY(!io.lastError().isEmpty());
}

QTEST_APPLESS_MAIN(BhxIO_Test)
