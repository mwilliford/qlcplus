/*
  Q Light Controller Plus - Unit test
  calibrationmodel_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QtTest>
#include <QBuffer>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

#include "calibrationmodel_test.h"
#include "calibrationmodel.h"
#include "spatialmodel.h"
#include "doc.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "../common/resource_paths.h"

#include <rigmath/moving_head.hpp>
#include <rigmath/channel_transform.hpp>

// --- Helpers ---

static Doc *createDoc()
{
    Doc *doc = new Doc(nullptr);
    return doc;
}

/** Add a moving-head fixture to Doc with pan/tilt channels. Returns fixture ID. */
static quint32 addMovingHead(Doc *doc, const QString &name, double panRange, double tiltRange)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel(name);

    QLCChannel *panCh = new QLCChannel();
    panCh->setName("Pan");
    panCh->setGroup(QLCChannel::Pan);
    panCh->setControlByte(QLCChannel::MSB);
    def->addChannel(panCh);

    QLCChannel *tiltCh = new QLCChannel();
    tiltCh->setName("Tilt");
    tiltCh->setGroup(QLCChannel::Tilt);
    tiltCh->setControlByte(QLCChannel::MSB);
    def->addChannel(tiltCh);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("2ch");
    mode->insertChannel(panCh, 0);
    mode->insertChannel(tiltCh, 1);

    QLCPhysical phy;
    phy.setFocusPanMax(panRange);
    phy.setFocusTiltMax(tiltRange);
    mode->setPhysical(phy);

    def->addMode(mode);
    doc->fixtureDefCache()->addFixtureDef(def);

    Fixture *fxi = new Fixture(doc);
    fxi->setFixtureDefinition(def, mode);
    fxi->setName(name);
    // Auto-assign address to avoid overlap with previous fixtures
    fxi->setUniverse(0);
    fxi->setAddress(doc->fixtures().size() * 16);
    doc->addFixture(fxi);

    return fxi->id();
}

/** Compute normalized [0,1] DMX values for a moving head aimed at a target from a given position. */
static std::vector<double> computeAimDmx(
    double fx, double fy, double fz,
    double panRange, double tiltRange,
    double tx, double ty, double tz)
{
    double lx = tx - fx, ly = ty - fy, lz = tz - fz;
    rigmath::MovingHeadKinematics kin(panRange, tiltRange);
    auto result = kin.inverse_local(lx, ly, lz);
    rigmath::LinearTransform panXform(panRange);
    rigmath::LinearTransform tiltXform(tiltRange);
    return {panXform.from_physical(result.angles[0]),
            tiltXform.from_physical(result.angles[1])};
}

// --- Tests ---

void CalibrationModel_Test::addAndRemoveObservation()
{
    CalibrationModel model;
    QCOMPARE(model.observationCount(), 0);

    CalibrationModel::PositionObs obs;
    obs.fixture = "1";
    obs.axis = 2;
    obs.value = 3.5;
    obs.certainty = 0.95;

    int id = model.addObservation(obs);
    QCOMPARE(model.observationCount(), 1);

    model.removeObservation(id);
    QCOMPARE(model.observationCount(), 0);
}

void CalibrationModel_Test::addAllObservationTypes()
{
    CalibrationModel model;

    // Aim
    CalibrationModel::AimObs aim;
    aim.fixture = "1";
    aim.dmxNormalized = {0.5, 0.5};
    aim.target[0] = 1.0; aim.target[1] = 2.0; aim.target[2] = 0.0;
    aim.certainty = 0.95;
    model.addObservation(aim);

    // Crossing
    CalibrationModel::CrossingObs crossing;
    crossing.fixtures = {"1", "2"};
    crossing.dmxValues = {{0.5, 0.5}, {0.3, 0.7}};
    crossing.axis = 2;
    crossing.value = 0.0;
    crossing.certainty = 0.85;
    model.addObservation(crossing);

    // Position
    CalibrationModel::PositionObs pos;
    pos.fixture = "1";
    pos.axis = 2;
    pos.value = 3.5;
    model.addObservation(pos);

    // Rotation
    CalibrationModel::RotationObs rot;
    rot.fixture = "1";
    rot.axis = 5;
    rot.valueDeg = 45.0;
    model.addObservation(rot);

    // BeamDirection
    CalibrationModel::BeamDirectionObs beam;
    beam.fixture = "1";
    beam.dmxNormalized = {0.5, 0.5};
    beam.hasElevation = true;
    beam.elevationDeg = -45.0;
    model.addObservation(beam);

    // Distance
    CalibrationModel::DistanceObs dist;
    dist.fixtureA = "1";
    dist.fixtureB = "2";
    dist.distance = 3.0;
    model.addObservation(dist);

    QCOMPARE(model.observationCount(), 6);
}

void CalibrationModel_Test::clearObservations()
{
    CalibrationModel model;
    model.addPositionObservation("1", 2, 3.5, 0.95);
    model.addPositionObservation("2", 2, 3.2, 0.95);
    QCOMPARE(model.observationCount(), 2);

    model.clearObservations();
    QCOMPARE(model.observationCount(), 0);
}

void CalibrationModel_Test::convenienceBuilders()
{
    CalibrationModel model;

    int id1 = model.addPositionObservation("1", 2, 3.5, 0.95);
    int id2 = model.addRotationObservation("1", 5, 90.0, 0.80);
    int id3 = model.addDistanceObservation("1", "2", 2.5, 0.90);
    int id4 = model.addAimObservation("1", {0.5, 0.5}, 0.0, 0.0, 0.0, 0.95);
    int id5 = model.addBeamDirectionObservation("1", {0.5, 0.5}, -45.0, 0.0, true, false, 0.85);
    int id6 = model.addCrossingObservation({"1", "2"}, {{0.5, 0.5}, {0.3, 0.7}}, 2, 0.0, 0.85);

    QCOMPARE(model.observationCount(), 6);
    // IDs should be sequential
    QCOMPARE(id1, 0);
    QCOMPARE(id2, 1);
    QCOMPARE(id3, 2);
    QCOMPARE(id4, 3);
    QCOMPARE(id5, 4);
    QCOMPARE(id6, 5);
}

void CalibrationModel_Test::setAndClearConstraints()
{
    CalibrationModel model;
    model.setConstraint("1", 2, 3.5, 0.95);  // tz = 3.5m with certainty 0.95
    model.setConstraint("1", 2, 3.2, 0.90);  // update same DOF
    model.clearConstraints("1");
    // No crash — constraints are internal, we just verify no assertion failure
    QVERIFY(true);
}

void CalibrationModel_Test::lockFixture()
{
    CalibrationModel model;
    model.lockFixture("1");
    // Verify no crash, constraints are used internally during solve
    QVERIFY(true);
}

void CalibrationModel_Test::solveWithAimObservations()
{
    Doc *doc = createDoc();
    SpatialModel *sm = doc->spatialModel();
    CalibrationModel *cm = doc->calibrationModel();

    // True position of the fixture
    double trueX = 2.0, trueY = 1.0, trueZ = 3.5;
    double panRange = 540.0, tiltRange = 270.0;

    // Add a moving head fixture
    quint32 fid = addMovingHead(doc, "Spot 1", panRange, tiltRange);
    QString sid = QString::number(fid);

    // Set initial guess (perturbed from truth)
    sm->setFixtureTransform(sid,
        rigmath::RigidTransform::translation(trueX + 0.5, trueY - 0.2, trueZ + 0.3),
        SpatialModel::Committed);

    // Disable auto-layout priors for this test — we want pure observation solving
    for (int dof = 0; dof < 6; dof++)
        cm->setTolerance(sid, dof, 0.0);

    // Add aim observations from ground truth
    double targets[][3] = {{0, 0, 0}, {4, 3, 0}, {-1, 4, 0}};
    for (auto &t : targets)
    {
        auto dmx = computeAimDmx(trueX, trueY, trueZ, panRange, tiltRange, t[0], t[1], t[2]);
        cm->addAimObservation(sid, dmx, t[0], t[1], t[2], 0.95);
    }

    // Add height observation
    cm->addPositionObservation(sid, 2, trueZ, 0.95);

    // Solve
    bool converged = cm->solve();
    QVERIFY(converged);

    // Check that solver-derived transform is close to truth
    auto solverT = sm->solverDerivedTransform(sid);
    QVERIFY(solverT.has_value());
    QVERIFY(std::abs(solverT->pos[0] - trueX) < 0.15);
    QVERIFY(std::abs(solverT->pos[1] - trueY) < 0.15);
    QVERIFY(std::abs(solverT->pos[2] - trueZ) < 0.15);

    delete doc;
}

void CalibrationModel_Test::solveReportsConvergence()
{
    CalibrationModel model;
    SpatialModel sm;
    model.setSpatialModel(&sm);

    // No doc, no fixtures → should fail gracefully
    bool converged = model.solve();
    QVERIFY(!converged);
}

void CalibrationModel_Test::solveUpdatesCovariance()
{
    Doc *doc = createDoc();
    SpatialModel *sm = doc->spatialModel();
    CalibrationModel *cm = doc->calibrationModel();

    double trueX = 2.0, trueY = 1.0, trueZ = 3.5;
    double panRange = 540.0, tiltRange = 270.0;

    quint32 fid = addMovingHead(doc, "Spot 1", panRange, tiltRange);
    QString sid = QString::number(fid);

    sm->setFixtureTransform(sid,
        rigmath::RigidTransform::translation(trueX + 0.3, trueY - 0.1, trueZ + 0.2),
        SpatialModel::Committed);

    double targets[][3] = {{0, 0, 0}, {4, 3, 0}, {-1, 4, 0}};
    for (auto &t : targets)
    {
        auto dmx = computeAimDmx(trueX, trueY, trueZ, panRange, tiltRange, t[0], t[1], t[2]);
        cm->addAimObservation(sid, dmx, t[0], t[1], t[2], 0.95);
    }
    cm->addPositionObservation(sid, 2, trueZ, 0.95);

    cm->solve();
    QVERIFY(cm->hasSolveResult());

    // Covariance should be available
    auto unc = cm->fixtureUncertainty(sid);
    QVERIFY(unc.x_cm > 0);
    QVERIFY(unc.y_cm > 0);
    QVERIFY(unc.z_cm > 0);

    // RMS should be small
    QVERIFY(cm->lastResult().rms_residual < 0.5);

    delete doc;
}

void CalibrationModel_Test::solveUsesLayoutPriors()
{
    // Verify that committed positions act as soft priors: with only a weak
    // observation, the solver should stay near the committed position.
    Doc *doc = createDoc();
    SpatialModel *sm = doc->spatialModel();
    CalibrationModel *cm = doc->calibrationModel();

    double placedX = 2.0, placedY = 1.5, placedZ = 3.0;

    quint32 fid1 = addMovingHead(doc, "Spot 1", 540.0, 270.0);
    quint32 fid2 = addMovingHead(doc, "Spot 2", 540.0, 270.0);
    QString sid1 = QString::number(fid1);
    QString sid2 = QString::number(fid2);

    sm->setFixtureTransform(sid1,
        rigmath::RigidTransform::translation(placedX, placedY, placedZ),
        SpatialModel::Committed);
    sm->setFixtureTransform(sid2,
        rigmath::RigidTransform::translation(placedX + 3.0, placedY, placedZ),
        SpatialModel::Committed);

    // Tight Z tolerance (measured), loose X/Y (eyeballed)
    cm->setTolerance(sid1, 0, 1.0);  // X ±1m
    cm->setTolerance(sid1, 1, 1.0);  // Y ±1m
    cm->setTolerance(sid1, 2, 0.05); // Z ±5cm (measured)
    cm->setTolerance(sid2, 0, 1.0);
    cm->setTolerance(sid2, 1, 1.0);
    cm->setTolerance(sid2, 2, 0.05);

    // One distance observation
    cm->addDistanceObservation(sid1, sid2, 3.0, 0.90);

    bool converged = cm->solve();
    QVERIFY(converged);

    // With tight Z prior, Z should stay very close to placed value
    auto solved1 = sm->solverDerivedTransform(sid1);
    QVERIFY(solved1.has_value());
    QVERIFY(std::abs(solved1->pos[2] - placedZ) < 0.15);

    // Uncertainty should exist
    auto unc = cm->fixtureUncertainty(sid1);
    QVERIFY(unc.x_cm > 0);

    delete doc;
}

void CalibrationModel_Test::setAndGetTolerance()
{
    CalibrationModel model;

    // Default when not set
    QCOMPARE(model.getTolerance("1", 0), CalibrationModel::kDefaultPosTolerance);
    QCOMPARE(model.getTolerance("1", 3), CalibrationModel::kDefaultRotTolerance);

    // Set and read back
    model.setTolerance("1", 2, 0.05);
    QCOMPARE(model.getTolerance("1", 2), 0.05);

    // Other DOFs still default
    QCOMPARE(model.getTolerance("1", 0), CalibrationModel::kDefaultPosTolerance);

    // Reset
    model.resetTolerances("1");
    QCOMPARE(model.getTolerance("1", 2), CalibrationModel::kDefaultPosTolerance);
}

void CalibrationModel_Test::saveAndLoadTolerances()
{
    CalibrationModel model;
    model.setTolerance("1", 0, 0.1);
    model.setTolerance("1", 2, 0.05);
    model.setTolerance("2", 3, 5.0);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    model.saveXML(writer);
    writer.writeEndDocument();
    buffer.close();

    CalibrationModel loaded;
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    loaded.loadXML(reader);
    buffer.close();

    QCOMPARE(loaded.getTolerance("1", 0), 0.1);
    QCOMPARE(loaded.getTolerance("1", 2), 0.05);
    QCOMPARE(loaded.getTolerance("2", 3), 5.0);
}

void CalibrationModel_Test::saveAndLoadObservations()
{
    CalibrationModel model;
    model.addPositionObservation("1", 2, 3.5, 0.95);
    model.addPositionObservation("2", 0, 1.0, 0.80);

    // Save to XML
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    model.saveXML(writer);
    writer.writeEndDocument();
    buffer.close();

    // Load from XML
    CalibrationModel loaded;
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();  // <Calibration>
    loaded.loadXML(reader);
    buffer.close();

    QCOMPARE(loaded.observationCount(), 2);
}

void CalibrationModel_Test::saveAndLoadConstraints()
{
    CalibrationModel model;
    model.setConstraint("1", 2, 3.5, 0.95);
    model.setConstraint("1", 3, 0.0, 1.0);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    model.saveXML(writer);
    writer.writeEndDocument();
    buffer.close();

    CalibrationModel loaded;
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    loaded.loadXML(reader);
    buffer.close();

    // Constraints are internal, verify no crash on re-load
    QVERIFY(true);
}

void CalibrationModel_Test::saveAndLoadAllTypes()
{
    CalibrationModel model;
    model.addAimObservation("1", {0.5, 0.5}, 1.0, 2.0, 0.0, 0.95);
    model.addCrossingObservation({"1", "2"}, {{0.5, 0.5}, {0.3, 0.7}}, 2, 0.0, 0.85);
    model.addPositionObservation("1", 2, 3.5, 0.95);
    model.addRotationObservation("1", 5, 45.0, 0.80);
    model.addBeamDirectionObservation("1", {0.5, 0.5}, -45.0, 90.0, true, true, 0.85);
    model.addDistanceObservation("1", "2", 3.0, 0.90);
    QCOMPARE(model.observationCount(), 6);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    model.saveXML(writer);
    writer.writeEndDocument();
    buffer.close();

    CalibrationModel loaded;
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    loaded.loadXML(reader);
    buffer.close();

    QCOMPARE(loaded.observationCount(), 6);
}

void CalibrationModel_Test::clearAll()
{
    CalibrationModel model;
    model.addPositionObservation("1", 2, 3.5, 0.95);
    model.setConstraint("1", 2, 3.5, 0.95);
    QCOMPARE(model.observationCount(), 1);

    model.clear();
    QCOMPARE(model.observationCount(), 0);
    QVERIFY(!model.hasSolveResult());
}

QTEST_APPLESS_MAIN(CalibrationModel_Test)
