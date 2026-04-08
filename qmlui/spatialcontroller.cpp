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

void SpatialController::notifySelectionChanged(int fixtureId)
{
    if (m_selectedFixtureId == fixtureId)
        return;
    m_selectedFixtureId = fixtureId;
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

void SpatialController::setRotPitch(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotPitch))
        return;
    m_rotPitch = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    double ax = v * M_PI / 180.0;
    double ay = m_rotYaw * M_PI / 180.0;
    double az = m_rotRoll * M_PI / 180.0;
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    auto rotT = rigmath::RigidTransform::from_axis_angle(ax, ay, az);
    std::copy(std::begin(rotT.rot), std::end(rotT.rot), std::begin(t.rot));
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

void SpatialController::setRotYaw(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotYaw))
        return;
    m_rotYaw = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    double ax = m_rotPitch * M_PI / 180.0;
    double ay = v * M_PI / 180.0;
    double az = m_rotRoll * M_PI / 180.0;
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    auto rotT = rigmath::RigidTransform::from_axis_angle(ax, ay, az);
    std::copy(std::begin(rotT.rot), std::end(rotT.rot), std::begin(t.rot));
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
}

void SpatialController::setRotRoll(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotRoll))
        return;
    m_rotRoll = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    double ax = m_rotPitch * M_PI / 180.0;
    double ay = m_rotYaw * M_PI / 180.0;
    double az = v * M_PI / 180.0;
    rigmath::RigidTransform t = sm->fixtureTransform(id);
    auto rotT = rigmath::RigidTransform::from_axis_angle(ax, ay, az);
    std::copy(std::begin(rotT.rot), std::end(rotT.rot), std::begin(t.rot));
    sm->setFixtureTransform(id, t, SpatialModel::Committed);
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

    double ax, ay, az;
    t.get_axis_angle(ax, ay, az);
    m_rotPitch = ax * 180.0 / M_PI;
    m_rotYaw = ay * 180.0 / M_PI;
    m_rotRoll = az * 180.0 / M_PI;
}
