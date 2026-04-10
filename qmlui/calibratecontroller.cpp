/*
  Q Light Controller Plus
  calibratecontroller.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "calibratecontroller.h"
#include "calibrationmodel.h"
#include "spatialmodel.h"
#include "doc.h"
#include "fixture.h"

#include <QVariantMap>

CalibrateController::CalibrateController(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    connect(cm, &CalibrationModel::observationsChanged, this, &CalibrateController::changed);
    connect(cm, &CalibrationModel::solveCompleted, this, &CalibrateController::changed);
}

// ---------------------------------------------------------------------------
// Read-only properties
// ---------------------------------------------------------------------------

int CalibrateController::observationCount() const
{
    return m_doc->calibrationModel()->observationCount();
}

bool CalibrateController::hasSolverResult() const
{
    return m_doc->calibrationModel()->hasSolveResult();
}

bool CalibrateController::solverConverged() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return false;
    return m_doc->calibrationModel()->lastResult().converged;
}

double CalibrateController::solverRms() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return 0.0;
    return m_doc->calibrationModel()->lastResult().rms_residual;
}

// ---------------------------------------------------------------------------
// Observation CRUD
// ---------------------------------------------------------------------------

int CalibrateController::addPositionObs(int fixtureId, int axis, double value, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addPositionObservation(QString::number(fixtureId), axis, value, certainty);
    emit changed();
    return id;
}

int CalibrateController::addRotationObs(int fixtureId, int axis, double valueDeg, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addRotationObservation(QString::number(fixtureId), axis, valueDeg, certainty);
    emit changed();
    return id;
}

int CalibrateController::addDistanceObs(int fixtureIdA, int fixtureIdB, double distance, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addDistanceObservation(QString::number(fixtureIdA),
                                         QString::number(fixtureIdB),
                                         distance, certainty);
    emit changed();
    return id;
}

void CalibrateController::removeObs(int obsId)
{
    m_doc->calibrationModel()->removeObservation(obsId);
    emit changed();
}

void CalibrateController::clearAllObs()
{
    m_doc->calibrationModel()->clearObservations();
    m_doc->spatialModel()->clearSolverViz();
    emit changed();
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

bool CalibrateController::runSolve()
{
    CalibrationModel *cm = m_doc->calibrationModel();
    bool result = cm->solve();
    emit changed();
    return result;
}

void CalibrateController::acceptSolverResults()
{
    SpatialModel *sm = m_doc->spatialModel();
    for (const QString &id : sm->fixtureIds())
    {
        if (sm->solverDerivedTransform(id).has_value())
            sm->promoteTransform(id, SpatialModel::SolverDerived);
    }
    m_doc->calibrationModel()->clearSolverResult();
    emit changed();
}

void CalibrateController::dismissSolverResults()
{
    m_doc->spatialModel()->clearSolverViz();
    m_doc->calibrationModel()->clearSolverResult();
    emit changed();
}

void CalibrateController::acceptFixtureResult(int fixtureId)
{
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(fixtureId);
    if (sm->solverDerivedTransform(id).has_value())
    {
        sm->promoteTransform(id, SpatialModel::SolverDerived);
        emit changed();
    }
}

void CalibrateController::dismissFixtureResult(int fixtureId)
{
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(fixtureId);
    sm->clearTransformLayer(id, SpatialModel::SolverDerived);
    SpatialModel::FixtureViz emptyViz;
    sm->setFixtureViz(id, emptyViz);
    emit changed();
}

int CalibrateController::addAimObs(int fixtureId, double targetX, double targetY,
                                    double targetZ, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    std::vector<double> emptyDmx;
    int id = cm->addAimObservation(QString::number(fixtureId), emptyDmx,
                                    targetX, targetY, targetZ, certainty);
    emit changed();
    return id;
}

// ---------------------------------------------------------------------------
// Constraints
// ---------------------------------------------------------------------------

void CalibrateController::lockFixtureInSolver(int fixtureId)
{
    m_doc->calibrationModel()->lockFixture(QString::number(fixtureId));
}

void CalibrateController::setHeightConstraint(int fixtureId, double heightM, double certainty)
{
    m_doc->calibrationModel()->setConstraint(
        QString::number(fixtureId), 2 /* tz */, heightM, certainty);
}

// ---------------------------------------------------------------------------
// Data for QML lists
// ---------------------------------------------------------------------------

QString CalibrateController::fixtureName(const QString &fixtureId) const
{
    Fixture *fxi = m_doc->fixture(fixtureId.toUInt());
    return fxi ? fxi->name() : QString("Fixture %1").arg(fixtureId);
}

QVariantList CalibrateController::observationsList() const
{
    static const char *axisLabels[] = {"X", "Y", "Height"};
    static const char *rotLabels[] = {"Pitch", "Yaw", "Roll"};

    QVariantList list;
    CalibrationModel *cm = m_doc->calibrationModel();

    for (const CalibrationModel::Observation &obs : cm->observations())
    {
        QVariantMap map;
        std::visit([&](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            map["id"] = o.id;

            if constexpr (std::is_same_v<T, CalibrationModel::AimObs>)
            {
                map["type"] = "Aim";
                map["fixture"] = o.fixture;
                map["certainty"] = o.certainty;
                map["description"] = QString("[%1] aim -> (%2, %3, %4)")
                    .arg(fixtureName(o.fixture))
                    .arg(o.target[0], 0, 'f', 2)
                    .arg(o.target[1], 0, 'f', 2)
                    .arg(o.target[2], 0, 'f', 2);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::CrossingObs>)
            {
                map["type"] = "Crossing";
                QStringList names;
                for (const auto &f : o.fixtures)
                    names.append(fixtureName(f));
                map["certainty"] = o.certainty;
                map["description"] = QString("%1 crossing @ %2=%3")
                    .arg(names.join(" x "))
                    .arg(o.axis < 3 ? axisLabels[o.axis] : "?")
                    .arg(o.value, 0, 'f', 2);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::PositionObs>)
            {
                map["type"] = "Position";
                map["fixture"] = o.fixture;
                map["axis"] = o.axis;
                map["value"] = o.value;
                map["certainty"] = o.certainty;
                map["description"] = QString("[%1] %2 = %3m")
                    .arg(fixtureName(o.fixture))
                    .arg(o.axis >= 0 && o.axis < 3 ? axisLabels[o.axis] : "?")
                    .arg(o.value, 0, 'f', 2);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::RotationObs>)
            {
                map["type"] = "Rotation";
                map["fixture"] = o.fixture;
                map["axis"] = o.axis;
                map["value"] = o.valueDeg;
                map["certainty"] = o.certainty;
                int ri = o.axis >= 3 ? o.axis - 3 : o.axis;
                map["description"] = QString("[%1] %2 = %3 deg")
                    .arg(fixtureName(o.fixture))
                    .arg(ri >= 0 && ri < 3 ? rotLabels[ri] : "?")
                    .arg(o.valueDeg, 0, 'f', 1);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::BeamDirectionObs>)
            {
                map["type"] = "BeamDirection";
                map["fixture"] = o.fixture;
                map["certainty"] = o.certainty;
                QStringList parts;
                if (o.hasElevation)
                    parts.append(QString("el=%1").arg(o.elevationDeg, 0, 'f', 0));
                if (o.hasAzimuth)
                    parts.append(QString("az=%1").arg(o.azimuthDeg, 0, 'f', 0));
                map["description"] = QString("[%1] beam %2 deg")
                    .arg(fixtureName(o.fixture))
                    .arg(parts.join(", "));
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::DistanceObs>)
            {
                map["type"] = "Distance";
                map["fixtureA"] = o.fixtureA;
                map["fixtureB"] = o.fixtureB;
                map["distance"] = o.distance;
                map["certainty"] = o.certainty;
                map["description"] = QString("[%1] <-> [%2] = %3m")
                    .arg(fixtureName(o.fixtureA))
                    .arg(fixtureName(o.fixtureB))
                    .arg(o.distance, 0, 'f', 2);
            }
        }, obs);

        list.append(map);
    }
    return list;
}

QVariantList CalibrateController::solverFixtureResults() const
{
    QVariantList list;
    CalibrationModel *cm = m_doc->calibrationModel();
    if (!cm->hasSolveResult())
        return list;

    const auto &result = cm->lastResult();
    for (const auto &[sid, pose] : result.poses)
    {
        QString fid = QString::fromStdString(sid);
        auto unc = cm->fixtureUncertainty(fid);

        QVariantMap map;
        map["fixtureId"] = fid;
        map["fixtureName"] = fixtureName(fid);
        map["quality"] = QString::fromStdString(unc.quality());
        map["xCm"] = unc.x_cm;
        map["yCm"] = unc.y_cm;
        map["zCm"] = unc.z_cm;
        list.append(map);
    }
    return list;
}
