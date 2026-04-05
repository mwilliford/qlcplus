/*
  Q Light Controller Plus - Unit test
  spatialmodel_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QtTest>
#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalSpy>
#include <cmath>

#include "spatialmodel_test.h"
#include "spatialmodel.h"
#include "monitorproperties.h"

// ---------------------------------------------------------------------------
// Transform basics
// ---------------------------------------------------------------------------

void SpatialModel_Test::setAndGetTransform()
{
    SpatialModel sm;
    rigmath::RigidTransform t = rigmath::RigidTransform::from_pose(1.5, -2.0, 3.0, 0.1, 0.2, 0.3);
    sm.setFixtureTransform("1", t, SpatialModel::Solver);

    QVERIFY(sm.hasFixture("1"));
    QCOMPARE(sm.fixtureIds().size(), 1);
    QVERIFY(sm.fixtureIds().contains("1"));

    rigmath::RigidTransform got = sm.fixtureTransform("1");
    QCOMPARE(got.pos[0], t.pos[0]);
    QCOMPARE(got.pos[1], t.pos[1]);
    QCOMPARE(got.pos[2], t.pos[2]);
}

void SpatialModel_Test::fixtureMatrix4x4()
{
    SpatialModel sm;
    // Identity transform at origin
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    double m[16];
    sm.fixtureMatrix4x4("1", m);

    // Column-major identity:
    // [1 0 0 0]  →  m[0]=1, m[1]=0, m[2]=0, m[3]=0
    // [0 1 0 0]     m[4]=0, m[5]=1, m[6]=0, m[7]=0
    // [0 0 1 0]     m[8]=0, m[9]=0, m[10]=1, m[11]=0
    // [0 0 0 1]     m[12]=0, m[13]=0, m[14]=0, m[15]=1
    QCOMPARE(m[0], 1.0);
    QCOMPARE(m[5], 1.0);
    QCOMPARE(m[10], 1.0);
    QCOMPARE(m[15], 1.0);
    QCOMPARE(m[12], 0.0);  // translation x
    QCOMPARE(m[13], 0.0);  // translation y
    QCOMPARE(m[14], 0.0);  // translation z

    // With translation
    sm.setFixtureTransform("2", rigmath::RigidTransform::translation(1.5, -2.0, 3.0));
    sm.fixtureMatrix4x4("2", m);
    QCOMPARE(m[12], 1.5);
    QCOMPARE(m[13], -2.0);
    QCOMPARE(m[14], 3.0);
}

void SpatialModel_Test::removeFixture()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());
    sm.setFixtureTransform("2", rigmath::RigidTransform::identity());
    QCOMPARE(sm.fixtureIds().size(), 2);

    sm.removeFixture("1");
    QCOMPARE(sm.fixtureIds().size(), 1);
    QVERIFY(!sm.hasFixture("1"));
    QVERIFY(sm.hasFixture("2"));
}

void SpatialModel_Test::sourceTracking()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::Manual);
    sm.setFixtureTransform("2", rigmath::RigidTransform::identity(), SpatialModel::Solver);

    QCOMPARE(sm.fixtureSource("1"), SpatialModel::Manual);
    QCOMPARE(sm.fixtureSource("2"), SpatialModel::Solver);

    // Unknown fixture defaults to Manual
    QCOMPARE(sm.fixtureSource("999"), SpatialModel::Manual);
}

void SpatialModel_Test::identityForUnknown()
{
    SpatialModel sm;
    rigmath::RigidTransform t = sm.fixtureTransform("nonexistent");
    // Should return identity
    QCOMPARE(t.pos[0], 0.0);
    QCOMPARE(t.pos[1], 0.0);
    QCOMPARE(t.pos[2], 0.0);
    QCOMPARE(t.rot[0], 1.0);  // identity rotation
    QCOMPARE(t.rot[4], 1.0);
    QCOMPARE(t.rot[8], 1.0);
}

// ---------------------------------------------------------------------------
// Named planes
// ---------------------------------------------------------------------------

void SpatialModel_Test::setAndGetPlanes()
{
    SpatialModel sm;
    QList<SpatialModel::Plane> planes;

    SpatialModel::Plane floor;
    floor.name = "floor";
    floor.normal[0] = 0; floor.normal[1] = 0; floor.normal[2] = 1;
    floor.distance = 0;
    planes.append(floor);

    SpatialModel::Plane wall;
    wall.name = "back-wall";
    wall.normal[0] = 0; wall.normal[1] = 1; wall.normal[2] = 0;
    wall.distance = -3.0;
    planes.append(wall);

    sm.setPlanes(planes);
    QCOMPARE(sm.planes().size(), 2);
    QCOMPARE(sm.planes()[0].name, QString("floor"));
    QCOMPARE(sm.planes()[1].distance, -3.0);
}

// ---------------------------------------------------------------------------
// XML round-trip
// ---------------------------------------------------------------------------

static QByteArray writeXML(const SpatialModel &sm)
{
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    sm.saveXML(writer);
    writer.writeEndDocument();
    return data;
}

static bool readXML(SpatialModel &sm, const QByteArray &data)
{
    QXmlStreamReader reader(data);
    while (!reader.atEnd())
    {
        if (reader.readNext() == QXmlStreamReader::StartElement)
        {
            if (reader.name() == QLatin1String("SpatialModel"))
                return sm.loadXML(reader);
        }
    }
    return false;
}

void SpatialModel_Test::saveAndLoadXML()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::from_pose(1.5, -2.0, 3.0, 0.0, 0.0, 0.785));
    sm.setFixtureTransform("2", rigmath::RigidTransform::translation(-0.5, 1.0, 2.5));

    QList<SpatialModel::Plane> planes;
    SpatialModel::Plane floor;
    floor.name = "floor";
    floor.normal[0] = 0; floor.normal[1] = 0; floor.normal[2] = 1;
    floor.distance = 0;
    planes.append(floor);
    sm.setPlanes(planes);

    QByteArray data = writeXML(sm);
    QVERIFY(!data.isEmpty());

    // Load into a fresh model
    SpatialModel sm2;
    QVERIFY(readXML(sm2, data));

    // Verify fixtures
    QCOMPARE(sm2.fixtureIds().size(), 2);
    QVERIFY(sm2.hasFixture("1"));
    QVERIFY(sm2.hasFixture("2"));

    rigmath::RigidTransform t1 = sm2.fixtureTransform("1");
    QVERIFY(std::abs(t1.pos[0] - 1.5) < 1e-6);
    QVERIFY(std::abs(t1.pos[1] - (-2.0)) < 1e-6);
    QVERIFY(std::abs(t1.pos[2] - 3.0) < 1e-6);

    rigmath::RigidTransform t2 = sm2.fixtureTransform("2");
    QVERIFY(std::abs(t2.pos[0] - (-0.5)) < 1e-6);
    QVERIFY(std::abs(t2.pos[1] - 1.0) < 1e-6);
    QVERIFY(std::abs(t2.pos[2] - 2.5) < 1e-6);

    // Verify planes
    QCOMPARE(sm2.planes().size(), 1);
    QCOMPARE(sm2.planes()[0].name, QString("floor"));
}

void SpatialModel_Test::saveSkipsEmpty()
{
    SpatialModel sm;
    QByteArray data = writeXML(sm);
    // Empty model should not write a SpatialModel element
    QVERIFY(!data.contains("SpatialModel"));
}

void SpatialModel_Test::loadPreservesSource()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::Solver);
    sm.setFixtureTransform("2", rigmath::RigidTransform::identity(), SpatialModel::Manual);

    QByteArray data = writeXML(sm);

    SpatialModel sm2;
    QVERIFY(readXML(sm2, data));
    QCOMPARE(sm2.fixtureSource("1"), SpatialModel::Solver);
    QCOMPARE(sm2.fixtureSource("2"), SpatialModel::Manual);
}

// ---------------------------------------------------------------------------
// Legacy migration
// ---------------------------------------------------------------------------

void SpatialModel_Test::migrateFromMonitorProperties()
{
    MonitorProperties monProps;
    // Set fixture at (2000, 1000, 3000) mm → should become (2.0, 1.0, 3.0) meters
    monProps.setFixturePosition(0, 0, 0, QVector3D(2000, 1000, 3000));
    monProps.setFixtureRotation(0, 0, 0, QVector3D(0, 0, 0));

    SpatialModel sm;
    sm.migrateFromMonitorProperties(&monProps);

    QVERIFY(sm.hasFixture("0"));
    rigmath::RigidTransform t = sm.fixtureTransform("0");
    QVERIFY(std::abs(t.pos[0] - 2.0) < 1e-6);
    QVERIFY(std::abs(t.pos[1] - 1.0) < 1e-6);
    QVERIFY(std::abs(t.pos[2] - 3.0) < 1e-6);
    QCOMPARE(sm.fixtureSource("0"), SpatialModel::Manual);
}

void SpatialModel_Test::migratePositionConversion()
{
    MonitorProperties monProps;
    // Fixture with rotation: 90 degrees around Z
    monProps.setFixturePosition(1, 0, 0, QVector3D(500, 0, 1500));
    monProps.setFixtureRotation(1, 0, 0, QVector3D(0, 0, 90));

    SpatialModel sm;
    sm.migrateFromMonitorProperties(&monProps);

    rigmath::RigidTransform t = sm.fixtureTransform("1");
    // Position: 500mm → 0.5m, 0mm → 0m, 1500mm → 1.5m
    QVERIFY(std::abs(t.pos[0] - 0.5) < 1e-6);
    QVERIFY(std::abs(t.pos[1] - 0.0) < 1e-6);
    QVERIFY(std::abs(t.pos[2] - 1.5) < 1e-6);

    // Rotation: 90 degrees around Z → axis-angle should be ~(0, 0, pi/2)
    double ax, ay, az;
    t.get_axis_angle(ax, ay, az);
    QVERIFY(std::abs(ax) < 1e-6);
    QVERIFY(std::abs(ay) < 1e-6);
    QVERIFY(std::abs(az - M_PI / 2.0) < 1e-4);
}

// ---------------------------------------------------------------------------
// Solver visualization
// ---------------------------------------------------------------------------

void SpatialModel_Test::applySolverVisualization()
{
    SpatialModel sm;
    // Pre-populate a fixture
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    QJsonObject msg;

    // Transforms
    QJsonObject transforms;
    QJsonObject t1;
    t1["pos"] = QJsonArray{1.0, 2.0, 3.0};
    t1["axis_angle"] = QJsonArray{0.0, 0.0, 0.0};
    transforms["1"] = t1;
    msg["transforms"] = transforms;

    // Ellipsoids
    QJsonObject ellipsoids;
    QJsonObject e1;
    e1["axes"] = QJsonArray{5.0, 3.0, 1.0};
    e1["rot"] = QJsonArray{1,0,0, 0,1,0, 0,0,1};
    e1["quality"] = "moderate";
    ellipsoids["1"] = e1;
    msg["ellipsoids"] = ellipsoids;

    msg["rms_residual"] = 0.023;
    msg["converged"] = true;
    msg["poorly_constrained"] = QJsonArray{"fixture_1_rz"};

    sm.applySolverVisualization(msg);

    // Transform updated
    rigmath::RigidTransform t = sm.fixtureTransform("1");
    QCOMPARE(t.pos[0], 1.0);
    QCOMPARE(t.pos[1], 2.0);
    QCOMPARE(t.pos[2], 3.0);
    QCOMPARE(sm.fixtureSource("1"), SpatialModel::Solver);

    // Viz data
    QVERIFY(sm.hasSolverViz());
    QCOMPARE(sm.rmsResidual(), 0.023);
    QCOMPARE(sm.converged(), true);
    QCOMPARE(sm.poorlyConstrained().size(), 1);

    SpatialModel::FixtureViz viz = sm.fixtureViz("1");
    QCOMPARE(viz.ellipsoidAxes[0], 5.0);
    QCOMPARE(viz.ellipsoidAxes[1], 3.0);
    QCOMPARE(viz.ellipsoidAxes[2], 1.0);
    QCOMPARE(viz.quality, QString("moderate"));
}

void SpatialModel_Test::clearSolverViz()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    // Apply some viz
    QJsonObject msg;
    QJsonObject transforms;
    QJsonObject t1;
    t1["pos"] = QJsonArray{1.0, 2.0, 3.0};
    t1["axis_angle"] = QJsonArray{0.0, 0.0, 0.0};
    transforms["1"] = t1;
    msg["transforms"] = transforms;
    msg["ellipsoids"] = QJsonObject();
    msg["rms_residual"] = 0.05;
    msg["converged"] = true;
    sm.applySolverVisualization(msg);
    QVERIFY(sm.hasSolverViz());

    sm.clearSolverViz();
    QVERIFY(!sm.hasSolverViz());
    QCOMPARE(sm.rmsResidual(), 0.0);
    QCOMPARE(sm.converged(), false);
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

void SpatialModel_Test::transformChangedSignal()
{
    SpatialModel sm;
    QSignalSpy spy(&sm, &SpatialModel::fixtureTransformChanged);

    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("1"));

    sm.setFixtureTransform("2", rigmath::RigidTransform::identity());
    QCOMPARE(spy.count(), 2);
}

void SpatialModel_Test::solverVizChangedSignal()
{
    SpatialModel sm;
    QSignalSpy spy(&sm, &SpatialModel::solverVizChanged);

    QJsonObject msg;
    msg["transforms"] = QJsonObject();
    msg["ellipsoids"] = QJsonObject();
    sm.applySolverVisualization(msg);
    QCOMPARE(spy.count(), 1);

    sm.clearSolverViz();
    QCOMPARE(spy.count(), 2);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void SpatialModel_Test::clearAll()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    QList<SpatialModel::Plane> planes;
    SpatialModel::Plane p;
    p.name = "floor";
    p.normal[0] = 0; p.normal[1] = 0; p.normal[2] = 1;
    p.distance = 0;
    planes.append(p);
    sm.setPlanes(planes);

    sm.clear();
    QCOMPARE(sm.fixtureIds().size(), 0);
    QCOMPARE(sm.planes().size(), 0);
    QVERIFY(!sm.hasSolverViz());
}

QTEST_APPLESS_MAIN(SpatialModel_Test)
