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
#include "doc.h"
#include "fixture.h"

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
