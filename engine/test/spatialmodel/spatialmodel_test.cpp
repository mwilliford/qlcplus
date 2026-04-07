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
    sm.setFixtureTransform("1", t, SpatialModel::Committed);

    QVERIFY(sm.hasFixture("1"));
    QCOMPARE(sm.fixtureIds().size(), 1);
    QVERIFY(sm.fixtureIds().contains("1"));

    // Committed accessor returns the value
    auto committed = sm.committedTransform("1");
    QVERIFY(committed.has_value());
    QCOMPARE(committed->pos[0], t.pos[0]);
    QCOMPARE(committed->pos[1], t.pos[1]);
    QCOMPARE(committed->pos[2], t.pos[2]);

    // renderTransform/fixtureTransform returns committed
    rigmath::RigidTransform got = sm.fixtureTransform("1");
    QCOMPARE(got.pos[0], t.pos[0]);
    QCOMPARE(got.pos[1], t.pos[1]);
    QCOMPARE(got.pos[2], t.pos[2]);
}

void SpatialModel_Test::fixtureMatrix4x4()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    double m[16];
    sm.fixtureMatrix4x4("1", m);

    QCOMPARE(m[0], 1.0);
    QCOMPARE(m[5], 1.0);
    QCOMPARE(m[10], 1.0);
    QCOMPARE(m[15], 1.0);
    QCOMPARE(m[12], 0.0);
    QCOMPARE(m[13], 0.0);
    QCOMPARE(m[14], 0.0);

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

void SpatialModel_Test::identityForUnknown()
{
    SpatialModel sm;
    rigmath::RigidTransform t = sm.fixtureTransform("nonexistent");
    QCOMPARE(t.pos[0], 0.0);
    QCOMPARE(t.pos[1], 0.0);
    QCOMPARE(t.pos[2], 0.0);
    QCOMPARE(t.rot[0], 1.0);
    QCOMPARE(t.rot[4], 1.0);
    QCOMPARE(t.rot[8], 1.0);
}

// ---------------------------------------------------------------------------
// Three-layer model
// ---------------------------------------------------------------------------

void SpatialModel_Test::isNewFixture()
{
    SpatialModel sm;

    // Unknown fixture is "new"
    QVERIFY(sm.isNew("1"));

    // Fixture with only agentDerived is still "new"
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::AgentDerived);
    QVERIFY(sm.isNew("1"));

    // Fixture with only solverDerived is still "new"
    sm.setFixtureTransform("2", rigmath::RigidTransform::identity(), SpatialModel::SolverDerived);
    QVERIFY(sm.isNew("2"));

    // Fixture with committed is NOT new
    sm.setFixtureTransform("3", rigmath::RigidTransform::identity(), SpatialModel::Committed);
    QVERIFY(!sm.isNew("3"));
}

void SpatialModel_Test::committedClearsProposals()
{
    SpatialModel sm;

    // Set up agentDerived and solverDerived proposals
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::AgentDerived);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(2, 0, 0), SpatialModel::SolverDerived);
    QVERIFY(sm.agentDerivedTransform("1").has_value());
    QVERIFY(sm.solverDerivedTransform("1").has_value());

    // Writing committed clears both proposals
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::Committed);
    QVERIFY(sm.committedTransform("1").has_value());
    QVERIFY(!sm.agentDerivedTransform("1").has_value());
    QVERIFY(!sm.solverDerivedTransform("1").has_value());
    QCOMPARE(sm.committedTransform("1")->pos[0], 3.0);
}

void SpatialModel_Test::agentDerivedDoesNotTouchOtherLayers()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(2, 0, 0), SpatialModel::AgentDerived);

    QVERIFY(sm.committedTransform("1").has_value());
    QCOMPARE(sm.committedTransform("1")->pos[0], 1.0);
    QVERIFY(sm.agentDerivedTransform("1").has_value());
    QCOMPARE(sm.agentDerivedTransform("1")->pos[0], 2.0);
}

void SpatialModel_Test::solverDerivedDoesNotTouchOtherLayers()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::AgentDerived);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(5, 0, 0), SpatialModel::SolverDerived);

    QCOMPARE(sm.committedTransform("1")->pos[0], 1.0);
    QCOMPARE(sm.agentDerivedTransform("1")->pos[0], 3.0);
    QCOMPARE(sm.solverDerivedTransform("1")->pos[0], 5.0);
}

void SpatialModel_Test::renderTransformPriority()
{
    SpatialModel sm;

    // No layers: identity
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(5, 0, 0), SpatialModel::SolverDerived);
    // Only solverDerived
    QCOMPARE(sm.renderTransform("1").pos[0], 5.0);

    // agentDerived takes priority over solverDerived
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::AgentDerived);
    QCOMPARE(sm.renderTransform("1").pos[0], 3.0);

    // committed takes priority over agentDerived (and clears proposals)
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    QCOMPARE(sm.renderTransform("1").pos[0], 1.0);

    // Unknown fixture: identity
    QCOMPARE(sm.renderTransform("nonexistent").pos[0], 0.0);
}

void SpatialModel_Test::promoteAgentDerived()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(2, 0, 0), SpatialModel::AgentDerived);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::SolverDerived);

    sm.promoteTransform("1", SpatialModel::AgentDerived);

    // committed = agentDerived value, proposals cleared
    QCOMPARE(sm.committedTransform("1")->pos[0], 2.0);
    QVERIFY(!sm.agentDerivedTransform("1").has_value());
    QVERIFY(!sm.solverDerivedTransform("1").has_value());
}

void SpatialModel_Test::promoteSolverDerived()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::AgentDerived);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(5, 0, 0), SpatialModel::SolverDerived);

    sm.promoteTransform("1", SpatialModel::SolverDerived);

    QCOMPARE(sm.committedTransform("1")->pos[0], 5.0);
    QVERIFY(!sm.agentDerivedTransform("1").has_value());
    QVERIFY(!sm.solverDerivedTransform("1").has_value());
}

void SpatialModel_Test::promoteEmptyIsNoOp()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);

    // No agentDerived to promote — committed unchanged
    sm.promoteTransform("1", SpatialModel::AgentDerived);
    QCOMPARE(sm.committedTransform("1")->pos[0], 1.0);

    // Non-existent fixture — no crash
    sm.promoteTransform("nonexistent", SpatialModel::AgentDerived);
}

void SpatialModel_Test::clearTransformLayer()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 0, 0), SpatialModel::Committed);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(2, 0, 0), SpatialModel::AgentDerived);
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(3, 0, 0), SpatialModel::SolverDerived);

    // Clear agentDerived — committed and solverDerived untouched
    sm.clearTransformLayer("1", SpatialModel::AgentDerived);
    QVERIFY(!sm.agentDerivedTransform("1").has_value());
    QVERIFY(sm.committedTransform("1").has_value());
    QVERIFY(sm.solverDerivedTransform("1").has_value());

    // Clear solverDerived
    sm.clearTransformLayer("1", SpatialModel::SolverDerived);
    QVERIFY(!sm.solverDerivedTransform("1").has_value());
    QVERIFY(sm.committedTransform("1").has_value());

    // Clear committed makes fixture "new" again
    sm.clearTransformLayer("1", SpatialModel::Committed);
    QVERIFY(sm.isNew("1"));

    // Non-existent fixture — no crash
    sm.clearTransformLayer("nonexistent", SpatialModel::AgentDerived);
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

    SpatialModel sm2;
    QVERIFY(readXML(sm2, data));

    QCOMPARE(sm2.fixtureIds().size(), 2);
    QVERIFY(sm2.hasFixture("1"));
    QVERIFY(sm2.hasFixture("2"));

    // Loaded fixtures have committed transforms
    QVERIFY(!sm2.isNew("1"));
    QVERIFY(!sm2.isNew("2"));

    rigmath::RigidTransform t1 = sm2.fixtureTransform("1");
    QVERIFY(std::abs(t1.pos[0] - 1.5) < 1e-6);
    QVERIFY(std::abs(t1.pos[1] - (-2.0)) < 1e-6);
    QVERIFY(std::abs(t1.pos[2] - 3.0) < 1e-6);

    rigmath::RigidTransform t2 = sm2.fixtureTransform("2");
    QVERIFY(std::abs(t2.pos[0] - (-0.5)) < 1e-6);
    QVERIFY(std::abs(t2.pos[1] - 1.0) < 1e-6);
    QVERIFY(std::abs(t2.pos[2] - 2.5) < 1e-6);

    QCOMPARE(sm2.planes().size(), 1);
    QCOMPARE(sm2.planes()[0].name, QString("floor"));

    // No Source attribute in XML anymore
    QVERIFY(!data.contains("Source="));
}

void SpatialModel_Test::saveSkipsEmpty()
{
    SpatialModel sm;
    QByteArray data = writeXML(sm);
    QVERIFY(!data.contains("SpatialModel"));
}

void SpatialModel_Test::saveSkipsNewFixtures()
{
    SpatialModel sm;
    // Fixture with only agentDerived — not persisted
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::AgentDerived);
    QByteArray data = writeXML(sm);
    QVERIFY(!data.contains("SpatialModel"));

    // Add a committed fixture — should persist
    sm.setFixtureTransform("2", rigmath::RigidTransform::translation(1, 2, 3), SpatialModel::Committed);
    data = writeXML(sm);
    QVERIFY(data.contains("SpatialModel"));
    // Only fixture 2 should be in XML
    QVERIFY(data.contains("ID=\"2\""));
    QVERIFY(!data.contains("ID=\"1\""));
}

// ---------------------------------------------------------------------------
// Legacy migration
// ---------------------------------------------------------------------------

void SpatialModel_Test::migrateFromMonitorProperties()
{
    MonitorProperties monProps;
    monProps.setFixturePosition(0, 0, 0, QVector3D(2000, 1000, 3000));
    monProps.setFixtureRotation(0, 0, 0, QVector3D(0, 0, 0));

    SpatialModel sm;
    sm.migrateFromMonitorProperties(&monProps);

    QVERIFY(sm.hasFixture("0"));
    QVERIFY(!sm.isNew("0"));  // migrated to committed
    rigmath::RigidTransform t = sm.fixtureTransform("0");
    QVERIFY(std::abs(t.pos[0] - 2.0) < 1e-6);
    QVERIFY(std::abs(t.pos[1] - 1.0) < 1e-6);
    QVERIFY(std::abs(t.pos[2] - 3.0) < 1e-6);
}

void SpatialModel_Test::migratePositionConversion()
{
    MonitorProperties monProps;
    monProps.setFixturePosition(1, 0, 0, QVector3D(500, 0, 1500));
    monProps.setFixtureRotation(1, 0, 0, QVector3D(0, 0, 90));

    SpatialModel sm;
    sm.migrateFromMonitorProperties(&monProps);

    rigmath::RigidTransform t = sm.fixtureTransform("1");
    QVERIFY(std::abs(t.pos[0] - 0.5) < 1e-6);
    QVERIFY(std::abs(t.pos[1] - 0.0) < 1e-6);
    QVERIFY(std::abs(t.pos[2] - 1.5) < 1e-6);

    double ax, ay, az;
    t.get_axis_angle(ax, ay, az);
    QVERIFY(std::abs(ax) < 1e-6);
    QVERIFY(std::abs(ay) < 1e-6);
    QVERIFY(std::abs(az - M_PI / 2.0) < 1e-4);
}

// ---------------------------------------------------------------------------
// Solver visualization
// ---------------------------------------------------------------------------

void SpatialModel_Test::applySolverVisualizationWritesSolverDerived()
{
    SpatialModel sm;
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity());

    QJsonObject msg;

    // Transforms — now written to solverDerived
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

    // committed is unchanged (still identity at origin)
    QVERIFY(sm.committedTransform("1").has_value());
    QCOMPARE(sm.committedTransform("1")->pos[0], 0.0);

    // solverDerived was written
    QVERIFY(sm.solverDerivedTransform("1").has_value());
    QCOMPARE(sm.solverDerivedTransform("1")->pos[0], 1.0);
    QCOMPARE(sm.solverDerivedTransform("1")->pos[1], 2.0);
    QCOMPARE(sm.solverDerivedTransform("1")->pos[2], 3.0);

    // renderTransform returns committed (higher priority)
    QCOMPARE(sm.renderTransform("1").pos[0], 0.0);

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
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(5, 0, 0), SpatialModel::SolverDerived);

    QJsonObject msg;
    msg["transforms"] = QJsonObject();
    msg["ellipsoids"] = QJsonObject();
    msg["rms_residual"] = 0.05;
    msg["converged"] = true;
    sm.applySolverVisualization(msg);
    QVERIFY(sm.hasSolverViz());
    QVERIFY(sm.solverDerivedTransform("1").has_value());

    sm.clearSolverViz();
    QVERIFY(!sm.hasSolverViz());
    QCOMPARE(sm.rmsResidual(), 0.0);
    QCOMPARE(sm.converged(), false);
    // clearSolverViz also clears solverDerived transforms
    QVERIFY(!sm.solverDerivedTransform("1").has_value());
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

    // Signals from promote and clear
    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::AgentDerived);
    QCOMPARE(spy.count(), 3);

    sm.promoteTransform("1", SpatialModel::AgentDerived);
    QCOMPARE(spy.count(), 4);

    sm.setFixtureTransform("1", rigmath::RigidTransform::identity(), SpatialModel::SolverDerived);
    sm.clearTransformLayer("1", SpatialModel::SolverDerived);
    QCOMPARE(spy.count(), 6);
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
// Coordinate conversion round-trip (mm/degrees <-> meters/radians)
// ---------------------------------------------------------------------------

void SpatialModel_Test::mmDegreesRoundTrip()
{
    SpatialModel sm;

    QVector3D posMm(2500.0f, -1500.0f, 3000.0f);
    sm.setFixturePositionMm("1", posMm);

    QVector3D got = sm.fixturePositionMm("1");
    QVERIFY(std::abs(got.x() - posMm.x()) < 0.1f);
    QVERIFY(std::abs(got.y() - posMm.y()) < 0.1f);
    QVERIFY(std::abs(got.z() - posMm.z()) < 0.1f);

    rigmath::RigidTransform t = sm.fixtureTransform("1");
    QVERIFY(std::abs(t.pos[0] - 2.5) < 1e-6);
    QVERIFY(std::abs(t.pos[1] - (-1.5)) < 1e-6);
    QVERIFY(std::abs(t.pos[2] - 3.0) < 1e-6);
}

void SpatialModel_Test::mmDegreesRoundTripWithRotation()
{
    SpatialModel sm;

    sm.setFixturePositionMm("1", QVector3D(1000, 2000, 3000));
    sm.setFixtureRotationDeg("1", QVector3D(0, 0, 90));

    QVector3D rotDeg = sm.fixtureRotationDeg("1");
    QVERIFY(std::abs(rotDeg.x()) < 1.0f);
    QVERIFY(std::abs(rotDeg.y()) < 1.0f);
    QVERIFY(std::abs(rotDeg.z() - 90.0f) < 1.0f);

    QVector3D posMm = sm.fixturePositionMm("1");
    QVERIFY(std::abs(posMm.x() - 1000.0f) < 0.1f);
    QVERIFY(std::abs(posMm.y() - 2000.0f) < 0.1f);
    QVERIFY(std::abs(posMm.z() - 3000.0f) < 0.1f);

    sm.setFixtureRotationDeg("2", QVector3D(45, 30, 60));
    sm.setFixturePositionMm("2", QVector3D(500, -500, 1500));

    QVector3D rot2 = sm.fixtureRotationDeg("2");
    QVERIFY(std::abs(rot2.x() - 45.0f) < 2.0f);
    QVERIFY(std::abs(rot2.y() - 30.0f) < 2.0f);
    QVERIFY(std::abs(rot2.z() - 60.0f) < 2.0f);
}

void SpatialModel_Test::mmPositionPreservesRotation()
{
    SpatialModel sm;

    sm.setFixturePositionMm("1", QVector3D(0, 0, 0));
    sm.setFixtureRotationDeg("1", QVector3D(0, 0, 45));

    sm.setFixturePositionMm("1", QVector3D(5000, 3000, 2000));

    QVector3D rot = sm.fixtureRotationDeg("1");
    QVERIFY(std::abs(rot.z() - 45.0f) < 1.0f);

    QVector3D pos = sm.fixturePositionMm("1");
    QVERIFY(std::abs(pos.x() - 5000.0f) < 0.1f);
    QVERIFY(std::abs(pos.y() - 3000.0f) < 0.1f);
    QVERIFY(std::abs(pos.z() - 2000.0f) < 0.1f);
}

void SpatialModel_Test::degRotationPreservesPosition()
{
    SpatialModel sm;

    sm.setFixturePositionMm("1", QVector3D(1234, -5678, 9012));

    sm.setFixtureRotationDeg("1", QVector3D(90, 0, 0));

    QVector3D pos = sm.fixturePositionMm("1");
    QVERIFY(std::abs(pos.x() - 1234.0f) < 0.1f);
    QVERIFY(std::abs(pos.y() - (-5678.0f)) < 0.1f);
    QVERIFY(std::abs(pos.z() - 9012.0f) < 0.1f);

    QVector3D rot = sm.fixtureRotationDeg("1");
    QVERIFY(std::abs(rot.x() - 90.0f) < 1.0f);
}

void SpatialModel_Test::mmWritesToCommitted()
{
    SpatialModel sm;

    // Set up agentDerived proposal
    sm.setFixtureTransform("1", rigmath::RigidTransform::translation(1, 2, 3), SpatialModel::AgentDerived);
    QVERIFY(sm.isNew("1"));

    // setFixturePositionMm writes to committed and clears proposals
    sm.setFixturePositionMm("1", QVector3D(5000, 0, 0));
    QVERIFY(!sm.isNew("1"));
    QVERIFY(!sm.agentDerivedTransform("1").has_value());
    QCOMPARE(sm.committedTransform("1")->pos[0], 5.0);  // 5000mm = 5m
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
