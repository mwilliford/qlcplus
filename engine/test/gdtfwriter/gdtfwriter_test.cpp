/*
  Q Light Controller Plus - Unit test
  gdtfwriter_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QtTest>
#include <QByteArray>
#include <QString>

#include "Include/VectorworksMVR.h"

#include "gdtfwriter_test.h"
#include "gdtfwriter.h"
#include "qlcchannel.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

using namespace VectorworksMVR;
using namespace VectorworksMVR::GdtfDefines;

namespace {

// Build a moving-head-style def with Pan MSB + Pan LSB + Tilt MSB + Tilt LSB +
// Dimmer (5 channels). Physical: 540° pan, 270° tilt, not a mirror.
QLCFixtureDef *makeMovingHeadDef()
{
    auto *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("TestMfg"));
    def->setModel(QStringLiteral("MovingHead"));
    def->setType(QLCFixtureDef::MovingHead);

    auto *panMsb = new QLCChannel();
    panMsb->setName(QStringLiteral("Pan"));
    panMsb->setGroup(QLCChannel::Pan);
    panMsb->setControlByte(QLCChannel::MSB);
    def->addChannel(panMsb);

    auto *panLsb = new QLCChannel();
    panLsb->setName(QStringLiteral("Pan Fine"));
    panLsb->setGroup(QLCChannel::Pan);
    panLsb->setControlByte(QLCChannel::LSB);
    def->addChannel(panLsb);

    auto *tiltMsb = new QLCChannel();
    tiltMsb->setName(QStringLiteral("Tilt"));
    tiltMsb->setGroup(QLCChannel::Tilt);
    tiltMsb->setControlByte(QLCChannel::MSB);
    def->addChannel(tiltMsb);

    auto *tiltLsb = new QLCChannel();
    tiltLsb->setName(QStringLiteral("Tilt Fine"));
    tiltLsb->setGroup(QLCChannel::Tilt);
    tiltLsb->setControlByte(QLCChannel::LSB);
    def->addChannel(tiltLsb);

    auto *dimmer = new QLCChannel();
    dimmer->setName(QStringLiteral("Dimmer"));
    dimmer->setGroup(QLCChannel::Intensity);
    dimmer->setPreset(QLCChannel::IntensityDimmer);
    def->addChannel(dimmer);

    auto *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("Standard"));
    mode->insertChannel(panMsb,  0);
    mode->insertChannel(panLsb,  1);
    mode->insertChannel(tiltMsb, 2);
    mode->insertChannel(tiltLsb, 3);
    mode->insertChannel(dimmer,  4);

    QLCPhysical phy;
    phy.setFocusType(QStringLiteral("Head"));
    phy.setFocusPanMax(540);
    phy.setFocusTiltMax(270);
    phy.setLensDegreesMin(15);
    phy.setLensDegreesMax(25);
    mode->setPhysical(phy);

    def->addMode(mode);
    return def;
}

// Pan-only fixture: Pan (MSB) + Dimmer (2 channels).
QLCFixtureDef *makePanOnlyDef()
{
    auto *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("TestMfg"));
    def->setModel(QStringLiteral("PanOnly"));

    auto *pan = new QLCChannel();
    pan->setName(QStringLiteral("Pan"));
    pan->setGroup(QLCChannel::Pan);
    pan->setControlByte(QLCChannel::MSB);
    def->addChannel(pan);

    auto *dim = new QLCChannel();
    dim->setName(QStringLiteral("Dimmer"));
    dim->setGroup(QLCChannel::Intensity);
    dim->setPreset(QLCChannel::IntensityDimmer);
    def->addChannel(dim);

    auto *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("2ch"));
    mode->insertChannel(pan, 0);
    mode->insertChannel(dim, 1);

    QLCPhysical phy;
    phy.setFocusType(QStringLiteral("Head"));
    phy.setFocusPanMax(360);
    mode->setPhysical(phy);

    def->addMode(mode);
    return def;
}

// Mirror-scanner: Pan+Tilt single-byte + Dimmer. focusType = "Mirror".
QLCFixtureDef *makeMirrorScannerDef()
{
    auto *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("TestMfg"));
    def->setModel(QStringLiteral("Scanner"));
    def->setType(QLCFixtureDef::Scanner);

    auto *pan = new QLCChannel();
    pan->setName(QStringLiteral("Pan"));
    pan->setGroup(QLCChannel::Pan);
    pan->setControlByte(QLCChannel::MSB);
    def->addChannel(pan);

    auto *tilt = new QLCChannel();
    tilt->setName(QStringLiteral("Tilt"));
    tilt->setGroup(QLCChannel::Tilt);
    tilt->setControlByte(QLCChannel::MSB);
    def->addChannel(tilt);

    auto *dim = new QLCChannel();
    dim->setName(QStringLiteral("Dimmer"));
    dim->setGroup(QLCChannel::Intensity);
    dim->setPreset(QLCChannel::IntensityDimmer);
    def->addChannel(dim);

    auto *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("3ch"));
    mode->insertChannel(pan, 0);
    mode->insertChannel(tilt, 1);
    mode->insertChannel(dim, 2);

    QLCPhysical phy;
    phy.setFocusType(QStringLiteral("Mirror"));
    phy.setFocusPanMax(180);
    phy.setFocusTiltMax(90);
    mode->setPhysical(phy);

    def->addMode(mode);
    return def;
}

// Fixed par: Dimmer only (no pan, no tilt).
QLCFixtureDef *makeFixedParDef()
{
    auto *def = new QLCFixtureDef();
    def->setManufacturer(QStringLiteral("TestMfg"));
    def->setModel(QStringLiteral("Par"));
    def->setType(QLCFixtureDef::Dimmer);

    auto *dim = new QLCChannel();
    dim->setName(QStringLiteral("Dimmer"));
    dim->setGroup(QLCChannel::Intensity);
    dim->setPreset(QLCChannel::IntensityDimmer);
    def->addChannel(dim);

    auto *mode = new QLCFixtureMode(def);
    mode->setName(QStringLiteral("1ch"));
    mode->insertChannel(dim, 0);
    def->addMode(mode);
    return def;
}

// Count nested geometry nodes in a parsed GDTF fixture. Starts from the
// fixture's top-level geometry children, recursing into each.
int countGeometriesRecursive(IGdtfGeometry *geo)
{
    if (geo == nullptr)
        return 0;
    int count = 1;
    size_t childCount = 0;
    geo->GetInternalGeometryCount(childCount);
    for (size_t i = 0; i < childCount; ++i)
    {
        IGdtfGeometryPtr child;
        if (geo->GetInternalGeometryAt(i, &child) == kVCOMError_NoError)
            count += countGeometriesRecursive(child);
    }
    return count;
}

// Find a geometry with the given name anywhere in the tree rooted at `geo`.
// Returns a smart pointer (takes a ref) so the found geometry survives beyond
// the recursion's local scope. Returning a raw pointer would dangle as soon
// as the local IGdtfGeometryPtr that found it went out of scope.
IGdtfGeometryPtr findGeometryByName(IGdtfGeometry *geo, const QString &name)
{
    IGdtfGeometryPtr result;
    if (geo == nullptr)
        return result;
    if (QString::fromUtf8(geo->GetName()) == name)
    {
        result = geo;  // AddRef via assignment
        return result;
    }
    size_t childCount = 0;
    geo->GetInternalGeometryCount(childCount);
    for (size_t i = 0; i < childCount; ++i)
    {
        IGdtfGeometryPtr child;
        if (geo->GetInternalGeometryAt(i, &child) != kVCOMError_NoError)
            continue;
        IGdtfGeometryPtr hit = findGeometryByName(child, name);
        if (static_cast<IGdtfGeometry *>(hit) != nullptr)
            return hit;
    }
    return result;
}

// Scan top-level geometries for a name.
IGdtfGeometryPtr findTopLevelGeometry(IGdtfFixture *fx, const QString &name)
{
    IGdtfGeometryPtr result;
    size_t count = 0;
    fx->GetGeometryCount(count);
    for (size_t i = 0; i < count; ++i)
    {
        IGdtfGeometryPtr geo;
        if (fx->GetGeometryAt(i, &geo) != kVCOMError_NoError)
            continue;
        IGdtfGeometryPtr hit = findGeometryByName(geo, name);
        if (static_cast<IGdtfGeometry *>(hit) != nullptr)
            return hit;
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Input validation
// ---------------------------------------------------------------------------

void GDTFWriter_Test::writeSynthetic_nullDef_returnsEmptyWithError()
{
    QString err;
    QByteArray out = GDTFWriter::writeSynthetic(nullptr, nullptr, &err);
    QVERIFY(out.isEmpty());
    QVERIFY(!err.isEmpty());
}

void GDTFWriter_Test::writeSynthetic_nullMode_returnsEmptyWithError()
{
    QScopedPointer<QLCFixtureDef> def(makeFixedParDef());
    QString err;
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), nullptr, &err);
    QVERIFY(out.isEmpty());
    QVERIFY(!err.isEmpty());
}

// ---------------------------------------------------------------------------
// Archive shape
// ---------------------------------------------------------------------------

void GDTFWriter_Test::writeSynthetic_producesValidZipBytes()
{
    QScopedPointer<QLCFixtureDef> def(makeMovingHeadDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());

    QVERIFY(!out.isEmpty());
    QVERIFY(out.size() > 100);
    // ZIP local file header magic: "PK\x03\x04"
    QCOMPARE(static_cast<uchar>(out.at(0)), uchar('P'));
    QCOMPARE(static_cast<uchar>(out.at(1)), uchar('K'));
    QCOMPARE(static_cast<uchar>(out.at(2)), uchar(0x03));
    QCOMPARE(static_cast<uchar>(out.at(3)), uchar(0x04));
}

void GDTFWriter_Test::writeSynthetic_bytesRoundTripThroughFromBuffer()
{
    QScopedPointer<QLCFixtureDef> def(makeMovingHeadDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());
    QVERIFY(!out.isEmpty());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    const VCOMError err = read->FromBuffer(out.constData(), out.size());
    QVERIFY2(err == kVCOMError_NoError,
             qPrintable(QStringLiteral("FromBuffer err=%1").arg(err)));

    QCOMPARE(QString::fromUtf8(read->GetName()), QStringLiteral("MovingHead"));
    QCOMPARE(QString::fromUtf8(read->GetManufacturer()), QStringLiteral("TestMfg"));

    // DMX mode count.
    size_t modeCount = 0;
    read->GetDmxModeCount(modeCount);
    QCOMPARE(static_cast<int>(modeCount), 1);
}

// ---------------------------------------------------------------------------
// Geometry tree
// ---------------------------------------------------------------------------

void GDTFWriter_Test::writeSynthetic_panOnlyFixture_buildsBaseYokeLamp()
{
    QScopedPointer<QLCFixtureDef> def(makePanOnlyDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());
    QVERIFY(!out.isEmpty());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Base")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Yoke")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Head")) == nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Lamp")) != nullptr);
}

void GDTFWriter_Test::writeSynthetic_panTiltMovingHead_buildsBaseYokeHeadLamp()
{
    QScopedPointer<QLCFixtureDef> def(makeMovingHeadDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());
    QVERIFY(!out.isEmpty());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Base")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Yoke")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Head")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Lamp")) != nullptr);
}

void GDTFWriter_Test::writeSynthetic_mirrorScanner_usesScannerPrimitive()
{
    QScopedPointer<QLCFixtureDef> def(makeMirrorScannerDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());
    QVERIFY(!out.isEmpty());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    IGdtfGeometryPtr head = findTopLevelGeometry(read, QStringLiteral("Head"));
    QVERIFY(static_cast<IGdtfGeometry *>(head) != nullptr);

    IGdtfModelPtr model;
    QCOMPARE(head->GetModel(&model), kVCOMError_NoError);
    EGdtfModel_PrimitiveType prim = eGdtfModel_PrimitiveType_Undefined;
    QCOMPARE(model->GetPrimitiveType(prim), kVCOMError_NoError);
    QCOMPARE(prim, eGdtfModel_PrimitiveType_Scanner);
}

void GDTFWriter_Test::writeSynthetic_fixedPar_buildsBaseLampOnly()
{
    QScopedPointer<QLCFixtureDef> def(makeFixedParDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());
    QVERIFY(!out.isEmpty());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Base")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Lamp")) != nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Yoke")) == nullptr);
    QVERIFY(findTopLevelGeometry(read, QStringLiteral("Head")) == nullptr);
}

// ---------------------------------------------------------------------------
// DMX channels
// ---------------------------------------------------------------------------

void GDTFWriter_Test::writeSynthetic_emitsOneChannelPerQxfSlot()
{
    // FixedPar has 1 QLC channel (Dimmer) → 1 GDTF DmxChannel.
    QScopedPointer<QLCFixtureDef> def(makeFixedParDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    IGdtfDmxModePtr mode;
    QCOMPARE(read->GetDmxModeAt(0, &mode), kVCOMError_NoError);

    size_t chCount = 0;
    mode->GetDmxChannelCount(chCount);
    QCOMPARE(static_cast<int>(chCount), 1);
}

void GDTFWriter_Test::writeSynthetic_collapsesMsbLsbPairIntoSingleGdtfChannel()
{
    // MovingHead: PanMSB+PanLSB, TiltMSB+TiltLSB, Dimmer → 3 GDTF DmxChannels
    // (two 16-bit pairs collapse to one each, dimmer stays one).
    QScopedPointer<QLCFixtureDef> def(makeMovingHeadDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    IGdtfDmxModePtr mode;
    QCOMPARE(read->GetDmxModeAt(0, &mode), kVCOMError_NoError);

    size_t chCount = 0;
    mode->GetDmxChannelCount(chCount);
    QCOMPARE(static_cast<int>(chCount), 3);

    // Find the Pan channel (coarse=1) and verify Fine offset is 2.
    bool foundPan = false;
    bool foundTilt = false;
    for (size_t i = 0; i < chCount; ++i)
    {
        IGdtfDmxChannelPtr ch;
        QCOMPARE(mode->GetDmxChannelAt(i, &ch), kVCOMError_NoError);
        Sint32 coarse = 0, fine = 0;
        ch->GetCoarse(coarse);
        ch->GetFine(fine);
        if (coarse == 1) { foundPan = true;  QCOMPARE(fine, 2); }
        if (coarse == 3) { foundTilt = true; QCOMPARE(fine, 4); }
    }
    QVERIFY(foundPan);
    QVERIFY(foundTilt);
}

void GDTFWriter_Test::writeSynthetic_panChannelWiresPanAttribute()
{
    QScopedPointer<QLCFixtureDef> def(makeMovingHeadDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    IGdtfDmxModePtr mode;
    QCOMPARE(read->GetDmxModeAt(0, &mode), kVCOMError_NoError);
    size_t chCount = 0;
    mode->GetDmxChannelCount(chCount);

    bool found = false;
    for (size_t i = 0; i < chCount; ++i)
    {
        IGdtfDmxChannelPtr ch;
        mode->GetDmxChannelAt(i, &ch);
        Sint32 coarse = 0;
        ch->GetCoarse(coarse);
        if (coarse != 1) continue;  // find Pan

        size_t logicalCount = 0;
        ch->GetLogicalChannelCount(logicalCount);
        QVERIFY(logicalCount >= 1);

        IGdtfDmxLogicalChannelPtr logCh;
        ch->GetLogicalChannelAt(0, &logCh);

        // Channel function 0 should have attribute == "Pan".
        size_t fnCount = 0;
        logCh->GetDmxFunctionCount(fnCount);
        QVERIFY(fnCount >= 1);
        IGdtfDmxChannelFunctionPtr fn;
        logCh->GetDmxFunctionAt(0, &fn);
        IGdtfAttributePtr attr;
        QCOMPARE(fn->GetAttribute(&attr), kVCOMError_NoError);
        QCOMPARE(QString::fromUtf8(attr->GetName()), QStringLiteral("Pan"));

        // PhysicalStart/End encode ±270 (540/2), non-mirror so -270 to 270.
        double physFrom = 0, physTo = 0;
        fn->GetPhysicalStart(physFrom);
        fn->GetPhysicalEnd(physTo);
        QVERIFY(std::abs(physFrom - (-270.0)) < 1e-6);
        QVERIFY(std::abs(physTo - 270.0) < 1e-6);

        found = true;
        break;
    }
    QVERIFY(found);
}

void GDTFWriter_Test::writeSynthetic_dimmerChannelWiresDimmerAttribute()
{
    QScopedPointer<QLCFixtureDef> def(makeFixedParDef());
    QByteArray out = GDTFWriter::writeSynthetic(def.data(), def->modes().first());

    IGdtfFixturePtr read(IID_IGdtfFixture);
    QCOMPARE(read->FromBuffer(out.constData(), out.size()),
             kVCOMError_NoError);

    IGdtfDmxModePtr mode;
    QCOMPARE(read->GetDmxModeAt(0, &mode), kVCOMError_NoError);

    IGdtfDmxChannelPtr ch;
    QCOMPARE(mode->GetDmxChannelAt(0, &ch), kVCOMError_NoError);

    IGdtfDmxLogicalChannelPtr logCh;
    ch->GetLogicalChannelAt(0, &logCh);
    IGdtfDmxChannelFunctionPtr fn;
    logCh->GetDmxFunctionAt(0, &fn);
    IGdtfAttributePtr attr;
    fn->GetAttribute(&attr);
    QCOMPARE(QString::fromUtf8(attr->GetName()), QStringLiteral("Dimmer"));
}

// ---------------------------------------------------------------------------
// Deterministic UUID
// ---------------------------------------------------------------------------

void GDTFWriter_Test::writeSynthetic_sameInputsProduceSameUuid()
{
    QScopedPointer<QLCFixtureDef> def1(makeMovingHeadDef());
    QScopedPointer<QLCFixtureDef> def2(makeMovingHeadDef());

    QByteArray a = GDTFWriter::writeSynthetic(def1.data(), def1->modes().first());
    QByteArray b = GDTFWriter::writeSynthetic(def2.data(), def2->modes().first());
    QVERIFY(!a.isEmpty());
    QVERIFY(!b.isEmpty());

    IGdtfFixturePtr readA(IID_IGdtfFixture);
    IGdtfFixturePtr readB(IID_IGdtfFixture);
    readA->FromBuffer(a.constData(), a.size());
    readB->FromBuffer(b.constData(), b.size());

    MvrUUID uuidA, uuidB;
    readA->GetFixtureGUID(uuidA);
    readB->GetFixtureGUID(uuidB);
    QVERIFY(uuidA == uuidB);
}

void GDTFWriter_Test::writeSynthetic_differentInputsProduceDifferentUuids()
{
    QScopedPointer<QLCFixtureDef> def1(makeMovingHeadDef());
    QScopedPointer<QLCFixtureDef> def2(makeMirrorScannerDef());

    QByteArray a = GDTFWriter::writeSynthetic(def1.data(), def1->modes().first());
    QByteArray b = GDTFWriter::writeSynthetic(def2.data(), def2->modes().first());
    QVERIFY(!a.isEmpty());
    QVERIFY(!b.isEmpty());

    IGdtfFixturePtr readA(IID_IGdtfFixture);
    IGdtfFixturePtr readB(IID_IGdtfFixture);
    readA->FromBuffer(a.constData(), a.size());
    readB->FromBuffer(b.constData(), b.size());

    MvrUUID uuidA, uuidB;
    readA->GetFixtureGUID(uuidA);
    readB->GetFixtureGUID(uuidB);
    QVERIFY(!(uuidA == uuidB));
}

QTEST_APPLESS_MAIN(GDTFWriter_Test)
