/*
  Q Light Controller Plus - Test Unit
  monitorproperties_test.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QtTest>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QBuffer>
#define private public
#include "monitorproperties.h"
#undef private
#include "monitorproperties_test.h"

// Minimal XML for a legacy Monitor element (no CoordSys attribute)
static const char *LEGACY_XML =
    "<Monitor DisplayMode=\"1\" ShowLabels=\"1\">"
    " <Font>Arial,12,-1,5,400,0,0,0,0,0,0,0,0,0,0,1</Font>"
    " <ChannelStyle>0</ChannelStyle>"
    " <ValueStyle>0</ValueStyle>"
    " <Grid Width=\"5\" Height=\"3\" Depth=\"5\" Units=\"0\" POV=\"1\"/>"
    " <FxItem ID=\"0\" XPos=\"2247.83\" YPos=\"0\" ZPos=\"4675.67\"/>"
    " <FxItem ID=\"1\" XPos=\"4804.71\" YPos=\"0\" ZPos=\"3298.43\" XRot=\"10\" YRot=\"20\" ZRot=\"30\"/>"
    "</Monitor>";

void MonitorProperties_Test::defaults()
{
    MonitorProperties mp;

    QCOMPARE(mp.displayMode(), MonitorProperties::DMX);
    QCOMPARE(mp.channelStyle(), MonitorProperties::DMXChannels);
    QCOMPARE(mp.valueStyle(), MonitorProperties::DMXValues);
    // Z-up default grid: (width=5, depth=5, height=3)
    QCOMPARE(mp.gridSize(), QVector3D(5, 5, 3));
    QCOMPARE(mp.gridUnits(), MonitorProperties::Meters);
    QCOMPARE(mp.pointOfView(), MonitorProperties::Undefined);
    QCOMPARE(mp.stageType(), MonitorProperties::StageSimple);
    QCOMPARE(mp.labelsVisible(), false);
    QVERIFY(mp.commonBackgroundImage().isEmpty());
}

void MonitorProperties_Test::fixtureItems()
{
    MonitorProperties mp;

    mp.setFixturePosition(10, 0, 0, QVector3D(1, 2, 3));
    mp.setFixtureRotation(10, 0, 0, QVector3D(0, 90, 0));
    mp.setFixtureGelColor(10, 0, 0, QColor(Qt::red));
    mp.setFixtureName(10, 0, 0, "Main");
    mp.setFixtureFlags(10, 0, 0, MonitorProperties::HiddenFlag);

    QCOMPARE(mp.fixturePosition(10,0,0), QVector3D(1,2,3));
    QCOMPARE(mp.fixtureRotation(10,0,0), QVector3D(0,90,0));
    QCOMPARE(mp.fixtureGelColor(10,0,0), QColor(Qt::red));
    QCOMPARE(mp.fixtureName(10,0,0), QString("Main"));
    QCOMPARE(mp.fixtureFlags(10,0,0), quint32(MonitorProperties::HiddenFlag));

    mp.removeFixture(10);
    QCOMPARE(mp.containsFixture(10), false);
}

void MonitorProperties_Test::genericItems()
{
    MonitorProperties mp;

    quint32 id = 100;
    mp.setItemName(id, "Item");
    mp.setItemResource(id, "path");
    mp.setItemPosition(id, QVector3D(1,1,1));
    mp.setItemRotation(id, QVector3D(0,0,90));
    mp.setItemScale(id, QVector3D(2,2,2));
    mp.setItemFlags(id, MonitorProperties::InvertedPanFlag);

    QList<quint32> ids = mp.genericItemsID();
    QCOMPARE(ids.count(), 1);
    QCOMPARE(ids.first(), id);
    QCOMPARE(mp.itemName(id), QString("Item"));
    QCOMPARE(mp.itemResource(id), QString("path"));
    QCOMPARE(mp.itemPosition(id), QVector3D(1,1,1));
    QCOMPARE(mp.itemRotation(id), QVector3D(0,0,90));
    QCOMPARE(mp.itemScale(id), QVector3D(2,2,2));
    QCOMPARE(mp.itemFlags(id), quint32(MonitorProperties::InvertedPanFlag));

    mp.removeItem(id);
    QCOMPARE(mp.containsItem(id), false);
}

void MonitorProperties_Test::reset()
{
    MonitorProperties mp;
    mp.setGridSize(QVector3D(10,10,10));
    mp.setGridUnits(MonitorProperties::Feet);
    mp.setPointOfView(MonitorProperties::FrontView);
    mp.setStageType(MonitorProperties::StageBox);
    mp.setLabelsVisible(true);
    mp.setFixturePosition(1,0,0,QVector3D(1,2,3));
    mp.setItemName(2,"foo");
    mp.setCommonBackgroundImage("img.png");

    mp.reset();

    QCOMPARE(mp.gridSize(), QVector3D(5,5,3));
    QCOMPARE(mp.gridUnits(), MonitorProperties::Meters);
    QCOMPARE(mp.pointOfView(), MonitorProperties::Undefined);
    QCOMPARE(mp.stageType(), MonitorProperties::StageSimple);
    QCOMPARE(mp.labelsVisible(), false);
    QCOMPARE(mp.fixtureItemsID().count(), 0);
    QCOMPARE(mp.genericItemsID().count(), 0);
    QVERIFY(mp.commonBackgroundImage().isEmpty());
}

void MonitorProperties_Test::legacyToZUpConversion()
{
    // Legacy grid: 5m wide (X), 3m tall (Y), 5m deep (Z) → in mm: 5000, 3000, 5000
    QVector3D legacyGrid(5000, 3000, 5000);

    // A fixture at back-left corner (legacy origin) should map to (-2500, 2500, 0) in Z-up
    QVector3D pos = MonitorProperties::legacyToZUpPosition(QVector3D(0, 0, 0), legacyGrid);
    QCOMPARE(pos, QVector3D(-2500, 2500, 0));

    // Center of legacy grid: (2500, 1500, 2500) → (0, 0, 1500) in Z-up
    pos = MonitorProperties::legacyToZUpPosition(QVector3D(2500, 1500, 2500), legacyGrid);
    QCOMPARE(pos, QVector3D(0, 0, 1500));

    // Front-right corner: (5000, 0, 5000) → (2500, -2500, 0) in Z-up
    pos = MonitorProperties::legacyToZUpPosition(QVector3D(5000, 0, 5000), legacyGrid);
    QCOMPARE(pos, QVector3D(2500, -2500, 0));

    // Rotation: legacy (10, 20, 30) → Z-up (-10, 30, 20)
    QVector3D rot = MonitorProperties::legacyToZUpRotation(QVector3D(10, 20, 30));
    QCOMPARE(rot, QVector3D(-10, 30, 20));
}

void MonitorProperties_Test::zUpToLegacyConversion()
{
    // Z-up grid: 5m wide (X), 5m deep (Y), 3m tall (Z) → in mm: 5000, 5000, 3000
    QVector3D zUpGrid(5000, 5000, 3000);

    // Center stage floor (0, 0, 0) → legacy (2500, 0, 2500)
    QVector3D pos = MonitorProperties::zUpToLegacyPosition(QVector3D(0, 0, 0), zUpGrid);
    QCOMPARE(pos, QVector3D(2500, 0, 2500));

    // Rotation: Z-up (-10, 30, 20) → legacy (10, 20, 30)
    QVector3D rot = MonitorProperties::zUpToLegacyRotation(QVector3D(-10, 30, 20));
    QCOMPARE(rot, QVector3D(10, 20, 30));
}

void MonitorProperties_Test::doubleConversionFidelity()
{
    // Legacy → Z-up → Legacy must be identity
    QVector3D legacyGrid(5000, 3000, 5000);
    QVector3D origPos(1234.5, 567.8, 3456.7);
    QVector3D origRot(15, 45, 90);

    QVector3D zUpPos = MonitorProperties::legacyToZUpPosition(origPos, legacyGrid);
    QVector3D zUpGrid = MonitorProperties::legacyToZUpGridSize(legacyGrid / 1000.0f) * 1000.0f;
    QVector3D backPos = MonitorProperties::zUpToLegacyPosition(zUpPos, zUpGrid);

    QVERIFY(qFuzzyCompare(backPos.x(), origPos.x()));
    QVERIFY(qFuzzyCompare(backPos.y(), origPos.y()));
    QVERIFY(qFuzzyCompare(backPos.z(), origPos.z()));

    QVector3D zUpRot = MonitorProperties::legacyToZUpRotation(origRot);
    QVector3D backRot = MonitorProperties::zUpToLegacyRotation(zUpRot);

    QVERIFY(qFuzzyCompare(backRot.x(), origRot.x()));
    QVERIFY(qFuzzyCompare(backRot.y(), origRot.y()));
    QVERIFY(qFuzzyCompare(backRot.z(), origRot.z()));
}

void MonitorProperties_Test::gridSizeSwap()
{
    // Grid swap is its own inverse: (w, h, d) → (w, d, h) → (w, h, d)
    QVector3D legacy(5, 3, 7);
    QVector3D zUp = MonitorProperties::legacyToZUpGridSize(legacy);
    QCOMPARE(zUp, QVector3D(5, 7, 3));

    QVector3D back = MonitorProperties::zUpToLegacyGridSize(zUp);
    QCOMPARE(back, legacy);
}

void MonitorProperties_Test::loadLegacyXML()
{
    // Load legacy XML (no CoordSys) → should auto-convert to Z-up
    MonitorProperties mp;
    QXmlStreamReader reader(LEGACY_XML);
    reader.readNextStartElement(); // advance to <Monitor>
    QVERIFY(mp.loadXML(reader, nullptr));

    // Grid should be converted: legacy (5,3,5) → Z-up (5,5,3)
    QCOMPARE(mp.gridSize(), QVector3D(5, 5, 3));

    // Fixture 0: legacy (2247.83, 0, 4675.67) with legacy grid mm (5000, 3000, 5000)
    //   Z-up: x = 2247.83 - 2500 = -252.17, y = -(4675.67 - 2500) = -2175.67, z = 0
    QVector3D pos0 = mp.fixturePosition(0, 0, 0);
    QVERIFY(qFuzzyCompare(pos0.x(), -252.17f));
    QVERIFY(qFuzzyCompare(pos0.y(), -2175.67f));
    QVERIFY(qFuzzyCompare(pos0.z(), 0.0f));

    // Fixture 1: legacy rotation (10, 20, 30) → Z-up (-10, 30, 20)
    QVector3D rot1 = mp.fixtureRotation(1, 0, 0);
    QVERIFY(qFuzzyCompare(rot1.x(), -10.0f));
    QVERIFY(qFuzzyCompare(rot1.y(), 30.0f));
    QVERIFY(qFuzzyCompare(rot1.z(), 20.0f));
}

void MonitorProperties_Test::saveAsLegacyRoundTrip()
{
    // Load legacy → save as legacy (.qxw) → reload → positions should match original
    MonitorProperties mp;
    QXmlStreamReader reader(LEGACY_XML);
    reader.readNextStartElement();
    QVERIFY(mp.loadXML(reader, nullptr));

    // Save as legacy format
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buf);
    writer.setAutoFormatting(true);
    MonitorProperties::s_saveLegacyFormat = true;
    mp.saveXML(&writer, nullptr);
    MonitorProperties::s_saveLegacyFormat = false;
    buf.close();

    QString xml = QString::fromUtf8(buf.data());

    // Verify no CoordSys attribute in output
    QVERIFY(!xml.contains("CoordSys"));

    // Verify original legacy position values are restored
    QVERIFY(xml.contains("XPos=\"2247.83\""));
    QVERIFY(xml.contains("ZPos=\"4675.67\""));
    QVERIFY(xml.contains("YPos=\"0\""));

    // Verify original legacy rotation values are restored
    QVERIFY(xml.contains("XRot=\"10\""));
    QVERIFY(xml.contains("YRot=\"20\""));
    QVERIFY(xml.contains("ZRot=\"30\""));

    // Verify legacy grid size (Width=5, Height=3, Depth=5)
    QVERIFY(xml.contains("Width=\"5\""));
    QVERIFY(xml.contains("Height=\"3\""));
    QVERIFY(xml.contains("Depth=\"5\""));
}

void MonitorProperties_Test::saveAsZUpRoundTrip()
{
    // Load legacy → save as Z-up (.bhx) → reload → positions should be identical
    MonitorProperties mp;
    QXmlStreamReader reader(LEGACY_XML);
    reader.readNextStartElement();
    QVERIFY(mp.loadXML(reader, nullptr));

    // Remember Z-up values after conversion
    QVector3D zUpPos0 = mp.fixturePosition(0, 0, 0);
    QVector3D zUpRot1 = mp.fixtureRotation(1, 0, 0);
    QVector3D zUpGrid = mp.gridSize();

    // Save as Z-up format
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buf);
    writer.setAutoFormatting(true);
    MonitorProperties::s_saveLegacyFormat = false;
    mp.saveXML(&writer, nullptr);
    buf.close();

    QString xml = QString::fromUtf8(buf.data());

    // Verify CoordSys="Z-up" is present
    QVERIFY(xml.contains("CoordSys=\"Z-up\""));

    // Reload the Z-up XML
    MonitorProperties mp2;
    QXmlStreamReader reader2(xml);
    reader2.readNextStartElement();
    QVERIFY(mp2.loadXML(reader2, nullptr));

    // Grid should not be converted (already Z-up)
    QCOMPARE(mp2.gridSize(), zUpGrid);

    // Positions should match exactly (no conversion applied)
    QVector3D pos0 = mp2.fixturePosition(0, 0, 0);
    QVERIFY(qFuzzyCompare(pos0.x(), zUpPos0.x()));
    QVERIFY(qFuzzyCompare(pos0.y(), zUpPos0.y()));
    QVERIFY(qFuzzyCompare(pos0.z(), zUpPos0.z()));

    QVector3D rot1 = mp2.fixtureRotation(1, 0, 0);
    QVERIFY(qFuzzyCompare(rot1.x(), zUpRot1.x()));
    QVERIFY(qFuzzyCompare(rot1.y(), zUpRot1.y()));
    QVERIFY(qFuzzyCompare(rot1.z(), zUpRot1.z()));
}

QTEST_APPLESS_MAIN(MonitorProperties_Test)
