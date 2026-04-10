/*
  Q Light Controller Plus
  spatialcontroller.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "spatialcontroller.h"
#include "spatialview.h"
#include "spatialmodel.h"
#include "calibrationmodel.h"
#include "doc.h"
#include "fixture.h"

#include <QVariantMap>

#include <cmath>

// --- Euler angle helpers (intrinsic XYZ / pitch-yaw-roll) ---
// Rotation order: R = Rz(roll) * Ry(yaw) * Rx(pitch)
// rot[] is 3x3 row-major

static void eulerToRotationMatrix(double pitch, double yaw, double roll, double rot[9])
{
    double cp = std::cos(pitch), sp = std::sin(pitch);
    double cy = std::cos(yaw),   sy = std::sin(yaw);
    double cr = std::cos(roll),  sr = std::sin(roll);

    rot[0] = cy * cr;
    rot[1] = sp * sy * cr - cp * sr;
    rot[2] = cp * sy * cr + sp * sr;

    rot[3] = cy * sr;
    rot[4] = sp * sy * sr + cp * cr;
    rot[5] = cp * sy * sr - sp * cr;

    rot[6] = -sy;
    rot[7] = sp * cy;
    rot[8] = cp * cy;
}

static void rotationMatrixToEuler(const double rot[9], double &pitch, double &yaw, double &roll)
{
    // rot[6] = -sin(yaw)
    if (std::abs(rot[6]) < 0.99999)
    {
        yaw = std::asin(-rot[6]);
        double cy = std::cos(yaw);
        pitch = std::atan2(rot[7] / cy, rot[8] / cy);
        roll  = std::atan2(rot[3] / cy, rot[0] / cy);
    }
    else
    {
        // Gimbal lock
        yaw = (rot[6] < 0) ? M_PI / 2.0 : -M_PI / 2.0;
        pitch = std::atan2(rot[1], rot[4]);
        roll = 0;
    }
}

SpatialController::SpatialController(Doc *doc, SpatialView *view, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_view(view)
{
    // When the model updates a fixture's transform (e.g. from gizmo drag),
    // refresh our cached values if it's the selected fixture.
    SpatialModel *sm = m_doc->spatialModel();
    connect(sm, &SpatialModel::fixtureTransformChanged, this, [this](const QString &id) {
        if (m_selectedFixtureId >= 0 && id == QString::number(m_selectedFixtureId))
        {
            updateTransformFromModel();
            emit transformChanged();
        }
    });

    // Relay CalibrationModel observation changes to QML
    CalibrationModel *cm = m_doc->calibrationModel();
    connect(cm, &CalibrationModel::observationsChanged, this, &SpatialController::calibrationChanged);
    connect(cm, &CalibrationModel::solveCompleted, this, &SpatialController::calibrationChanged);
}

void SpatialController::setSelectedFixtureId(int id)
{
    if (m_selectedFixtureId == id)
        return;
    m_selectedFixtureId = id;
    m_view->selectFixture(id);
    updateTransformFromModel();
    emit selectionChanged();
    emit transformChanged();
}

void SpatialController::notifySelectionChanged(int fixtureId, int count)
{
    m_selectedFixtureId = fixtureId;
    m_selectionCount = count;
    updateTransformFromModel();
    emit selectionChanged();
    emit transformChanged();
}

QString SpatialController::selectedFixtureName() const
{
    if (m_selectedFixtureId < 0)
        return QString();
    Fixture *fxi = m_doc->fixture(quint32(m_selectedFixtureId));
    return fxi ? fxi->name() : QString("Fixture %1").arg(m_selectedFixtureId);
}

// --- Position ---

double SpatialController::posX() const { return m_posX; }
double SpatialController::posY() const { return m_posY; }
double SpatialController::posZ() const { return m_posZ; }

void SpatialController::setPosX(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_posX))
        return;
    m_posX = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    t.pos[0] = v;
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

void SpatialController::setPosY(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_posY))
        return;
    m_posY = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    t.pos[1] = v;
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

void SpatialController::setPosZ(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_posZ))
        return;
    m_posZ = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    t.pos[2] = v;
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

// --- Rotation ---

double SpatialController::rotPitch() const { return m_rotPitch; }
double SpatialController::rotYaw() const { return m_rotYaw; }
double SpatialController::rotRoll() const { return m_rotRoll; }

static void applyEulerRotation(SpatialModel *sm, const QString &id,
                               double pitchDeg, double yawDeg, double rollDeg)
{
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    double pitch = pitchDeg * M_PI / 180.0;
    double yaw   = yawDeg   * M_PI / 180.0;
    double roll  = rollDeg  * M_PI / 180.0;
    eulerToRotationMatrix(pitch, yaw, roll, t.rot);
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

void SpatialController::setRotPitch(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotPitch))
        return;
    m_rotPitch = v;
    applyEulerRotation(m_doc->spatialModel(), QString::number(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

void SpatialController::setRotYaw(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotYaw))
        return;
    m_rotYaw = v;
    applyEulerRotation(m_doc->spatialModel(), QString::number(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

void SpatialController::setRotRoll(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotRoll))
        return;
    m_rotRoll = v;
    applyEulerRotation(m_doc->spatialModel(), QString::number(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

// --- Mode ---

void SpatialController::setMode(int m)
{
    if (m_mode == m)
        return;
    m_mode = m;
    emit modeChanged();
}

// --- Snap ---

void SpatialController::setGridSnap(bool v)
{
    if (m_gridSnap == v)
        return;
    m_gridSnap = v;
    emit gridSnapChanged();
}

void SpatialController::setGridSize(double v)
{
    if (qFuzzyCompare(m_gridSize, v) || v <= 0)
        return;
    m_gridSize = v;
    emit gridSizeChanged();
}

// --- Axis mode ---

void SpatialController::setAxisMode(int m)
{
    if (m_axisMode == m)
        return;
    m_axisMode = m;
    emit axisModeChanged();
}

// --- Align ---

void SpatialController::alignSelection(const QString &axis)
{
    if (m_selectedFixtureId < 0 || !m_selectedIdsCallback)
        return;

    std::vector<int32_t> ids = m_selectedIdsCallback();
    if (ids.size() < 2)
        return;

    SpatialModel *sm = m_doc->spatialModel();

    // Get primary fixture's position as the alignment target
    rigmath::RigidTransform primary = sm->fixtureTransform(QString::number(m_selectedFixtureId));

    for (int32_t id : ids)
    {
        if (id == m_selectedFixtureId)
            continue;
        QString sid = QString::number(id);
        rigmath::RigidTransform t = sm->fixtureTransform(sid);
        if (axis == "X")
            t.pos[0] = primary.pos[0];
        else if (axis == "Y")
            t.pos[1] = primary.pos[1];
        else if (axis == "Z")
            t.pos[2] = primary.pos[2];
        sm->setFixtureTransform(sid, t, SpatialModel::Committed);
    }
}

// --- Truss ---

void SpatialController::addDefaultTruss()
{
    SpatialModel *sm = m_doc->spatialModel();
    static int trussCounter = 0;
    SpatialModel::Truss truss;
    truss.id = QString("truss_%1").arg(trussCounter++);
    truss.name = QString("Truss %1").arg(trussCounter);
    // Default: 6m horizontal pipe at 3m height, centered on stage
    truss.start[0] = -3.0; truss.start[1] = 0.0; truss.start[2] = 3.0;
    truss.end[0]   =  3.0; truss.end[1]   = 0.0; truss.end[2]   = 3.0;
    sm->addTruss(truss);
}

void SpatialController::removeTruss(const QString &id)
{
    m_doc->spatialModel()->removeTruss(id);
}

// --- Gizmo mode ---

void SpatialController::setGizmoMode(int m)
{
    if (m_gizmoMode == m)
        return;
    m_gizmoMode = m;
    emit gizmoModeChanged();
}

// --- Camera ---

void SpatialController::setCameraPreset(const QString &preset)
{
    if (preset == "foh" || preset == "FOH")
        m_view->setCameraOrbit(-90.0f, 30.0f, 10.0f);
    else if (preset == "top" || preset == "Top")
        m_view->setCameraOrbit(-90.0f, 89.0f, 10.0f);
    else if (preset == "front" || preset == "Front")
        m_view->setCameraOrbit(-90.0f, 0.0f, 10.0f);
    else if (preset == "side" || preset == "Side")
        m_view->setCameraOrbit(0.0f, 0.0f, 10.0f);
}

// --- Calibration ---

bool SpatialController::hasSolverResult() const
{
    return m_doc->calibrationModel()->hasSolveResult();
}

bool SpatialController::solverConverged() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return false;
    return m_doc->calibrationModel()->lastResult().converged;
}

double SpatialController::solverRms() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return 0.0;
    return m_doc->calibrationModel()->lastResult().rms_residual;
}

bool SpatialController::runSolve()
{
    CalibrationModel *cm = m_doc->calibrationModel();
    bool result = cm->solve();
    emit calibrationChanged();
    return result;
}

void SpatialController::acceptSolverResults()
{
    SpatialModel *sm = m_doc->spatialModel();
    for (const QString &id : sm->fixtureIds())
    {
        if (sm->solverDerivedTransform(id).has_value())
            sm->promoteTransform(id, SpatialModel::SolverDerived);
    }
    emit calibrationChanged();
}

void SpatialController::dismissSolverResults()
{
    m_doc->spatialModel()->clearSolverViz();
    // Clear solver result (but keep observations and constraints)
    m_doc->calibrationModel()->clearSolverResult();
    emit calibrationChanged();
}

int SpatialController::addPositionObs(int fixtureId, int axis, double value, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addPositionObservation(QString::number(fixtureId), axis, value, certainty);
    emit calibrationChanged();
    return id;
}

int SpatialController::addRotationObs(int fixtureId, int axis, double valueDeg, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addRotationObservation(QString::number(fixtureId), axis, valueDeg, certainty);
    emit calibrationChanged();
    return id;
}

int SpatialController::addDistanceObs(int fixtureIdA, int fixtureIdB, double distance, double certainty)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addDistanceObservation(QString::number(fixtureIdA),
                                         QString::number(fixtureIdB),
                                         distance, certainty);
    emit calibrationChanged();
    return id;
}

void SpatialController::removeObs(int obsId)
{
    m_doc->calibrationModel()->removeObservation(obsId);
    emit calibrationChanged();
}

void SpatialController::clearAllObs()
{
    m_doc->calibrationModel()->clearObservations();
    emit calibrationChanged();
}

int SpatialController::observationCount() const
{
    return m_doc->calibrationModel()->observationCount();
}

void SpatialController::lockFixtureInSolver(int fixtureId)
{
    m_doc->calibrationModel()->lockFixture(QString::number(fixtureId));
}

void SpatialController::setHeightConstraint(int fixtureId, double heightM, double certainty)
{
    m_doc->calibrationModel()->setConstraint(
        QString::number(fixtureId), 2 /* tz */, heightM, certainty);
}

// --- Observation/solver data for QML ---

QString SpatialController::fixtureName(const QString &fixtureId) const
{
    Fixture *fxi = m_doc->fixture(fixtureId.toUInt());
    return fxi ? fxi->name() : QString("Fixture %1").arg(fixtureId);
}

QVariantList SpatialController::observationsList() const
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
                map["fixtureName"] = fixtureName(o.fixture);
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
                map["fixtures"] = o.fixtures;
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
                map["fixtureName"] = fixtureName(o.fixture);
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
                map["fixtureName"] = fixtureName(o.fixture);
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
                map["fixtureName"] = fixtureName(o.fixture);
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

QVariantList SpatialController::solverFixtureResults() const
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

// --- Internal ---

void SpatialController::updateTransformFromModel()
{
    if (m_selectedFixtureId < 0)
    {
        m_posX = m_posY = m_posZ = 0;
        m_rotPitch = m_rotYaw = m_rotRoll = 0;
        return;
    }

    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform t = sm->fixtureTransform(id);

    m_posX = t.pos[0];
    m_posY = t.pos[1];
    m_posZ = t.pos[2];

    double pitch, yaw, roll;
    rotationMatrixToEuler(t.rot, pitch, yaw, roll);
    m_rotPitch = pitch * 180.0 / M_PI;
    m_rotYaw = yaw * 180.0 / M_PI;
    m_rotRoll = roll * 180.0 / M_PI;
}
