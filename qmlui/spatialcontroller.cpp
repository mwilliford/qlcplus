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
#include "tardis/tardis.h"
#include "doc.h"
#include "fixture.h"
#include "fixturekinematics.h"
#include "fixtureattributes.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "scene.h"

#include <rigmath/kinematic_chain.hpp>
#include <rigmath/rigid_transform.hpp>

#include <QHash>
#include <QVariantMap>
#include <cmath>

// Helper: pack a RigidTransform into a QVariantList [x, y, z, rx, ry, rz]
// matching Tardis::SpatialFixtureSetTransform format.
static QVariantList transformToVariantList(const rigmath::RigidTransform &t)
{
    double ax, ay, az;
    t.get_axis_angle(ax, ay, az);
    return QVariantList{t.pos[0], t.pos[1], t.pos[2], ax, ay, az};
}

// Enqueue a Tardis undo action for a fixture transform change.
// Call BEFORE writing the new transform to SpatialModel.
static void enqueueSpatialTransformUndo(quint32 fixtureId,
                                         const rigmath::RigidTransform &oldT,
                                         const rigmath::RigidTransform &newT)
{
    Tardis *tardis = Tardis::instance();
    if (!tardis) return;
    tardis->enqueueAction(
        Tardis::SpatialFixtureSetTransform,
        fixtureId,
        QVariant::fromValue(transformToVariantList(oldT)),
        QVariant::fromValue(transformToVariantList(newT))
    );
}

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

    // Any focus-point mutation (add/remove/update/assign) or a selection
    // change refreshes the QML list. Coalesce both signals into one.
    connect(sm, &SpatialModel::focusPointsChanged, this, &SpatialController::focusPointsChanged);
    connect(this, &SpatialController::selectedFocusPointChanged, this, &SpatialController::focusPointsChanged);

    // Highlight uses snapshot semantics: when H is pressed, we capture the
    // currently-selected fixtures (or selected focus point's assigned fixtures)
    // and hold them lit until H is pressed again. Changing selection does NOT
    // re-target the highlight — this enables cross-beam calibration workflows
    // where you light fixtures 1+2, then select each individually to aim them
    // at a shared target without losing visibility on the other one.
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
    rigmath::RigidTransform oldT = sm->fixtureTransform(id);
    rigmath::RigidTransform newT = oldT;
    newT.pos[0] = v;
    enqueueSpatialTransformUndo(quint32(m_selectedFixtureId), oldT, newT);
    sm->setFixtureTransform(id, newT, SpatialModel::Committed);
}

void SpatialController::setPosY(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_posY))
        return;
    m_posY = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform oldT = sm->fixtureTransform(id);
    rigmath::RigidTransform newT = oldT;
    newT.pos[1] = v;
    enqueueSpatialTransformUndo(quint32(m_selectedFixtureId), oldT, newT);
    sm->setFixtureTransform(id, newT, SpatialModel::Committed);
}

void SpatialController::setPosZ(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_posZ))
        return;
    m_posZ = v;
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(m_selectedFixtureId);
    rigmath::RigidTransform oldT = sm->fixtureTransform(id);
    rigmath::RigidTransform newT = oldT;
    newT.pos[2] = v;
    enqueueSpatialTransformUndo(quint32(m_selectedFixtureId), oldT, newT);
    sm->setFixtureTransform(id, newT, SpatialModel::Committed);
}

// --- Rotation ---

double SpatialController::rotPitch() const { return m_rotPitch; }
double SpatialController::rotYaw() const { return m_rotYaw; }
double SpatialController::rotRoll() const { return m_rotRoll; }

static void applyEulerRotation(SpatialModel *sm, quint32 fixtureId,
                               double pitchDeg, double yawDeg, double rollDeg)
{
    QString id = QString::number(fixtureId);
    rigmath::RigidTransform oldT = sm->fixtureTransform(id);
    rigmath::RigidTransform newT = oldT;
    double pitch = pitchDeg * M_PI / 180.0;
    double yaw   = yawDeg   * M_PI / 180.0;
    double roll  = rollDeg  * M_PI / 180.0;
    eulerToRotationMatrix(pitch, yaw, roll, newT.rot);
    enqueueSpatialTransformUndo(fixtureId, oldT, newT);
    sm->setFixtureTransform(id, newT, SpatialModel::Committed);
}

void SpatialController::setRotPitch(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotPitch))
        return;
    m_rotPitch = v;
    applyEulerRotation(m_doc->spatialModel(), quint32(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

void SpatialController::setRotYaw(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotYaw))
        return;
    m_rotYaw = v;
    applyEulerRotation(m_doc->spatialModel(), quint32(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

void SpatialController::setRotRoll(double v)
{
    if (m_selectedFixtureId < 0 || qFuzzyCompare(v, m_rotRoll))
        return;
    m_rotRoll = v;
    applyEulerRotation(m_doc->spatialModel(), quint32(m_selectedFixtureId),
                        m_rotPitch, m_rotYaw, m_rotRoll);
}

// --- Mode ---

void SpatialController::setMode(int m)
{
    if (m_mode == m)
        return;

    // Programmer-persistent model (grandMA / Eos / Hog convention): DMX
    // overrides persist across ALL view switches. The programmer is only
    // released by explicit user action (e.g. a future "Clear / Release"
    // button), not by navigation. In Layout we still render fixtures at
    // home pose in the UI (liveDmxModeCallback is false), but the underlying
    // DMX output continues — so physical fixtures don't flinch and returning
    // to Focus/Calibrate/Live picks up exactly where you left off.
    qDebug() << "[SpatialMode]" << m_mode << "->" << m
             << "— overrides persist (focus:" << m_focusControlledChannels.size()
             << "highlight-fixtures:" << m_highlightedFixtureIds.size() << ")";
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

// --- Selection query ---

QVariantList SpatialController::selectedFixtureIds() const
{
    QVariantList list;
    if (!m_selectedIdsCallback)
    {
        // Fall back to primary selection
        if (m_selectedFixtureId >= 0)
            list.append(m_selectedFixtureId);
        return list;
    }
    for (int32_t id : m_selectedIdsCallback())
        list.append(int(id));
    return list;
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

// --- Focus mode aim ---

void SpatialController::getFocusAim(double *x, double *y, double *z) const
{
    if (x) *x = m_focusAim[0];
    if (y) *y = m_focusAim[1];
    if (z) *z = m_focusAim[2];
}

// Shared helper: compute IK for a set of fixtures and emit DMX writes.
// Used by both setFocusAim (ephemeral click-to-aim) and aimAtFocusPoint
// (persistent focus point following).
static void aimFixturesImpl(SpatialController *self,
                             Doc *doc,
                             SpatialModel *sm,
                             const std::vector<int32_t> &ids,
                             double wx, double wy, double wz,
                             QSet<uint> &outControlledChannels)
{
    for (int32_t fid : ids)
    {
        Fixture *fxi = doc->fixture(quint32(fid));
        if (!fxi)
            continue;

        FixtureKinematics fk = buildFixtureKinematics(fxi);
        if (!fk.chain || fk.dofCount() == 0)
            continue;  // fixed fixture — no motion DOFs

        // IK: find DOF values that aim the primary beam at the target.
        rigmath::RigidTransform xf = sm->fixtureTransform(QString::number(fid));
        rigmath::AngleResult result = fk.chain->inverse_world(xf, wx, wy, wz);

        // Clamp to reachable range per DOF.
        std::vector<double> clamped = fk.chain->clamp_to_reachable(result.angles);

        // Convert physical angles to DMX writes.
        std::vector<FocusDmxWrite> writes = anglesToDmxWrites(fk, clamped);
        for (const FocusDmxWrite &w : writes)
        {
            emit self->focusDmxWrite(w.absAddr, w.value);
            outControlledChannels.insert(w.absAddr);
        }
    }
}

void SpatialController::setFocusAim(double wx, double wy, double wz)
{
    m_focusAim[0] = wx;
    m_focusAim[1] = wy;
    m_focusAim[2] = wz;
    m_focusAimValid = true;

    // Iterate all selected fixtures; only moving heads get aimed.
    std::vector<int32_t> ids;
    if (m_selectedIdsCallback)
        ids = m_selectedIdsCallback();
    else if (m_selectedFixtureId >= 0)
        ids.push_back(m_selectedFixtureId);

    aimFixturesImpl(this, m_doc, m_doc->spatialModel(), ids,
                    wx, wy, wz, m_focusControlledChannels);

    emit focusAimChanged();
    emit programmerContentChanged();
}

void SpatialController::clearFocusAim()
{
    for (uint addr : m_focusControlledChannels)
        emit focusDmxReset(addr);
    m_focusControlledChannels.clear();

    m_focusAimValid = false;
    m_focusAim[0] = m_focusAim[1] = m_focusAim[2] = 0.0;

    emit focusAimChanged();
    emit programmerContentChanged();
}

// ---------------------------------------------------------------------------
// Focus points (persistent named aim targets)
// ---------------------------------------------------------------------------

// Build a full-state payload for Add/Remove undo entries. Captures position,
// name, and assignments so a deleted focus point can be fully restored.
static QVariantMap focusPointToMap(const SpatialModel::FocusPoint &fp)
{
    QVariantMap m;
    m["id"] = fp.id;
    m["name"] = fp.name;
    m["x"] = fp.position[0];
    m["y"] = fp.position[1];
    m["z"] = fp.position[2];
    m["assigned"] = QVariant(fp.assignedFixtureIds);
    return m;
}

// Small helper so the objID we hand to Tardis is stable per focus point —
// keeps batching coalesced by id (drag-move of one point stays one undo step).
static quint32 focusPointObjId(const QString &fpId)
{
    return static_cast<quint32>(qHash(fpId));
}

QString SpatialController::createFocusPoint(double wx, double wy, double wz,
                                             const QString &name)
{
    SpatialModel *sm = m_doc->spatialModel();

    // Generate a unique id by taking the highest existing "fp<N>" plus one.
    // This survives save/load because ids embedded in the XML are preserved.
    int maxNum = m_nextFocusPointIdNum - 1;
    for (const auto &fp : sm->focusPoints())
    {
        if (fp.id.startsWith(QStringLiteral("fp")))
        {
            bool ok = false;
            int n = fp.id.mid(2).toInt(&ok);
            if (ok && n > maxNum)
                maxNum = n;
        }
    }
    int nextNum = maxNum + 1;
    m_nextFocusPointIdNum = nextNum + 1;

    SpatialModel::FocusPoint fp;
    fp.id = QStringLiteral("fp%1").arg(nextNum);
    fp.name = name.isEmpty() ? QStringLiteral("Point %1").arg(nextNum + 1) : name;
    fp.position[0] = wx;
    fp.position[1] = wy;
    fp.position[2] = wz;
    sm->addFocusPoint(fp);

    if (Tardis *tardis = Tardis::instance())
    {
        QVariantMap payload = focusPointToMap(fp);
        tardis->enqueueAction(Tardis::SpatialFocusPointAdd, focusPointObjId(fp.id),
                              QVariant(payload), QVariant(payload));
    }

    return fp.id;
}

void SpatialController::deleteFocusPoint(const QString &id)
{
    if (id.isEmpty())
        return;
    SpatialModel *sm = m_doc->spatialModel();

    // Capture full state BEFORE removal so undo can restore it.
    QVariantMap payload;
    if (const auto *fp = sm->focusPoint(id))
        payload = focusPointToMap(*fp);

    sm->removeFocusPoint(id);
    if (m_selectedFocusPointId == id)
    {
        m_selectedFocusPointId.clear();
        emit selectedFocusPointChanged();
    }

    if (!payload.isEmpty())
    {
        if (Tardis *tardis = Tardis::instance())
        {
            tardis->enqueueAction(Tardis::SpatialFocusPointRemove, focusPointObjId(id),
                                  QVariant(payload), QVariant(payload));
        }
    }
}

void SpatialController::moveFocusPoint(const QString &id,
                                        double wx, double wy, double wz)
{
    SpatialModel *sm = m_doc->spatialModel();
    const auto *existing = sm->focusPoint(id);
    if (!existing)
        return;
    // Short-circuit no-op moves so drag frames with identical positions
    // (e.g. grid-snap plateaus) don't spam Tardis.
    if (qFuzzyCompare(existing->position[0], wx)
        && qFuzzyCompare(existing->position[1], wy)
        && qFuzzyCompare(existing->position[2], wz))
        return;

    QVariantMap oldPayload;
    oldPayload["id"] = id;
    oldPayload["x"] = existing->position[0];
    oldPayload["y"] = existing->position[1];
    oldPayload["z"] = existing->position[2];

    SpatialModel::FocusPoint updated = *existing;
    updated.position[0] = wx;
    updated.position[1] = wy;
    updated.position[2] = wz;
    sm->updateFocusPoint(updated);

    if (Tardis *tardis = Tardis::instance())
    {
        QVariantMap newPayload;
        newPayload["id"] = id;
        newPayload["x"] = wx;
        newPayload["y"] = wy;
        newPayload["z"] = wz;
        // Same objID per fp coalesces a drag into one undo step.
        tardis->enqueueAction(Tardis::SpatialFocusPointMove, focusPointObjId(id),
                              QVariant(oldPayload), QVariant(newPayload));
    }
}

void SpatialController::renameFocusPoint(const QString &id, const QString &name)
{
    SpatialModel *sm = m_doc->spatialModel();
    const auto *existing = sm->focusPoint(id);
    if (!existing || existing->name == name)
        return;

    QVariantMap oldPayload{{"id", id}, {"name", existing->name}};
    QVariantMap newPayload{{"id", id}, {"name", name}};

    SpatialModel::FocusPoint updated = *existing;
    updated.name = name;
    sm->updateFocusPoint(updated);

    if (Tardis *tardis = Tardis::instance())
    {
        tardis->enqueueAction(Tardis::SpatialFocusPointRename, focusPointObjId(id),
                              QVariant(oldPayload), QVariant(newPayload));
    }
}

bool SpatialController::assignFixtureToFocusPoint(const QString &fpId, int fixtureId)
{
    if (fixtureId < 0) return false;
    SpatialModel *sm = m_doc->spatialModel();
    bool ok = sm->assignFixtureToFocusPoint(fpId, QString::number(fixtureId));
    if (ok)
    {
        if (Tardis *tardis = Tardis::instance())
        {
            QVariantMap oldPayload{{"fpId", fpId}, {"fixtureId", fixtureId}, {"assigned", false}};
            QVariantMap newPayload{{"fpId", fpId}, {"fixtureId", fixtureId}, {"assigned", true}};
            tardis->enqueueAction(Tardis::SpatialFocusPointAssign, focusPointObjId(fpId),
                                  QVariant(oldPayload), QVariant(newPayload));
        }
    }
    return ok;
}

bool SpatialController::unassignFixtureFromFocusPoint(const QString &fpId, int fixtureId)
{
    if (fixtureId < 0) return false;
    SpatialModel *sm = m_doc->spatialModel();
    bool ok = sm->unassignFixtureFromFocusPoint(fpId, QString::number(fixtureId));
    if (ok)
    {
        if (Tardis *tardis = Tardis::instance())
        {
            QVariantMap oldPayload{{"fpId", fpId}, {"fixtureId", fixtureId}, {"assigned", true}};
            QVariantMap newPayload{{"fpId", fpId}, {"fixtureId", fixtureId}, {"assigned", false}};
            tardis->enqueueAction(Tardis::SpatialFocusPointAssign, focusPointObjId(fpId),
                                  QVariant(oldPayload), QVariant(newPayload));
        }
    }
    return ok;
}

void SpatialController::aimAtFocusPoint(const QString &id)
{
    SpatialModel *sm = m_doc->spatialModel();
    const auto *fp = sm->focusPoint(id);
    if (!fp)
        return;

    // Translate assigned fixture id strings to int32_t for IK helper.
    std::vector<int32_t> ids;
    ids.reserve(fp->assignedFixtureIds.size());
    for (const QString &fidStr : fp->assignedFixtureIds)
    {
        bool ok = false;
        int32_t fid = fidStr.toInt(&ok);
        if (ok)
            ids.push_back(fid);
    }

    if (ids.empty())
        return;

    // Reuse the same IK helper that powers click-to-aim. The aim point
    // becomes the "current" focus aim so the renderer shows the marker.
    m_focusAim[0] = fp->position[0];
    m_focusAim[1] = fp->position[1];
    m_focusAim[2] = fp->position[2];
    m_focusAimValid = true;

    aimFixturesImpl(this, m_doc, sm, ids,
                    fp->position[0], fp->position[1], fp->position[2],
                    m_focusControlledChannels);

    emit focusAimChanged();
    emit programmerContentChanged();
}

void SpatialController::setSelectedFocusPointId(const QString &id)
{
    if (m_selectedFocusPointId == id)
        return;
    m_selectedFocusPointId = id;
    emit selectedFocusPointChanged();
}

QVariantList SpatialController::focusPointsList() const
{
    QVariantList out;
    SpatialModel *sm = m_doc->spatialModel();
    for (const SpatialModel::FocusPoint &fp : sm->focusPoints())
    {
        QVariantMap row;
        row["id"] = fp.id;
        row["name"] = fp.name;
        row["x"] = fp.position[0];
        row["y"] = fp.position[1];
        row["z"] = fp.position[2];
        row["assignedCount"] = fp.assignedFixtureIds.size();
        row["selected"] = (fp.id == m_selectedFocusPointId);
        out.append(row);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Calibrate pan/tilt trackpad — raw DMX on primary selected fixture
// ---------------------------------------------------------------------------

bool SpatialController::selectedFixtureHasPanTilt() const
{
    if (m_selectedFixtureId < 0) return false;
    Fixture *fxi = m_doc->fixture(quint32(m_selectedFixtureId));
    if (!fxi) return false;
    const QLCFixtureMode *mode = fxi->fixtureMode();
    if (!mode) return false;
    const quint32 pan  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::MSB);
    const quint32 tilt = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
    return pan != QLCChannel::invalid() || tilt != QLCChannel::invalid();
}

// Read current pan/tilt % from live DMX for the primary selected fixture.
// Returns 50.0 when no data available so the trackpad doesn't jump.
static double axisPercent(const SpatialController *self,
                           std::function<QByteArray(quint32)> snapCb,
                           Fixture *fxi, QLCChannel::Group axis)
{
    if (!fxi) return 50.0;
    const QLCFixtureMode *mode = fxi->fixtureMode();
    if (!mode) return 50.0;
    const quint32 chNum = mode->channelNumber(axis, QLCChannel::MSB);
    if (chNum == QLCChannel::invalid()) return 50.0;

    if (!snapCb) return 50.0;
    QByteArray dmx = snapCb(fxi->universe());
    if (dmx.isEmpty()) return 50.0;
    const int absIdx = int(fxi->address()) + int(chNum);
    if (absIdx < 0 || absIdx >= dmx.size()) return 50.0;
    const uchar v = uchar(dmx.at(absIdx));
    (void)self;
    return double(v) / 2.55;  // 0-255 -> 0-100
}

double SpatialController::selectedFixturePanPercent() const
{
    if (m_selectedFixtureId < 0) return 50.0;
    Fixture *fxi = m_doc->fixture(quint32(m_selectedFixtureId));
    return axisPercent(this, m_universeSnapshotCallback, fxi, QLCChannel::Pan);
}

double SpatialController::selectedFixtureTiltPercent() const
{
    if (m_selectedFixtureId < 0) return 50.0;
    Fixture *fxi = m_doc->fixture(quint32(m_selectedFixtureId));
    return axisPercent(this, m_universeSnapshotCallback, fxi, QLCChannel::Tilt);
}

void SpatialController::notifyUniverseWritten()
{
    // Only emit if a mover is selected — avoids spamming the QML binding
    // when nothing cares.
    if (m_selectedFixtureId < 0) return;
    if (selectedFixtureHasPanTilt())
        emit livePanTiltChanged();
}

void SpatialController::setSelectedFixturePanTiltPercent(double panPct, double tiltPct)
{
    if (m_selectedFixtureId < 0) return;
    Fixture *fxi = m_doc->fixture(quint32(m_selectedFixtureId));
    if (!fxi) return;

    FixtureAttributes fa(fxi,
        [this](uint addr, uchar v){ emit focusDmxWrite(addr, v); });
    fa.setPanPercent(panPct);
    fa.setTiltPercent(tiltPct);
    // Track these as focus-mode overrides so they release on Programmer release.
    for (uint a : fa.controlledChannels())
        m_focusControlledChannels.insert(a);
}

// ---------------------------------------------------------------------------
// Highlight (Focus-mode visibility helper)
// ---------------------------------------------------------------------------

void SpatialController::lightFixture(int32_t fid)
{
    if (m_highlightedFixtureIds.contains(fid)) return;

    Fixture *fxi = m_doc->fixture(quint32(fid));
    if (!fxi) return;

    FixtureAttributes fa(fxi,
        [this](uint addr, uchar v){ emit focusDmxWrite(addr, v); });
    if (fa.hasDimmer())  fa.setDimmerFull();
    if (fa.hasShutter()) fa.openShutter();

    m_highlightedFixtureIds.insert(fid);
    m_highlightChannelsPerFixture[fid] = fa.controlledChannels();
    qDebug() << "[Highlight] lit fix" << fid << fxi->name()
             << "(" << fa.controlledChannels().size() << "channels)";
    emit programmerContentChanged();
}

void SpatialController::unlightFixture(int32_t fid)
{
    auto it = m_highlightChannelsPerFixture.find(fid);
    if (it == m_highlightChannelsPerFixture.end()) return;
    for (uint addr : it.value()) emit focusDmxReset(addr);
    m_highlightChannelsPerFixture.erase(it);
    m_highlightedFixtureIds.remove(fid);
    qDebug() << "[Highlight] unlit fix" << fid;
    emit programmerContentChanged();
}

void SpatialController::toggleHighlight()
{
    // Target fixtures: current selection (or selected focus point's assigned
    // fixtures as a fallback). Fixtures outside this set are untouched.
    std::vector<int32_t> ids;
    if (m_selectedIdsCallback)
        ids = m_selectedIdsCallback();
    else if (m_selectedFixtureId >= 0)
        ids.push_back(m_selectedFixtureId);

    if (ids.empty() && !m_selectedFocusPointId.isEmpty())
    {
        if (const auto *fp = m_doc->spatialModel()->focusPoint(m_selectedFocusPointId))
            for (const QString &s : fp->assignedFixtureIds)
            {
                bool ok = false; int32_t fid = s.toInt(&ok);
                if (ok) ids.push_back(fid);
            }
    }

    if (ids.empty())
    {
        qDebug() << "[Highlight] toggleHighlight — no selection, nothing to do";
        return;
    }

    // Decide whether to light or unlight: if ALL selected are already lit,
    // toggle them off. Otherwise light the ones not yet lit. This gives
    // intuitive behavior for the common cross-beam flow.
    bool allLit = true;
    for (int32_t fid : ids)
        if (!m_highlightedFixtureIds.contains(fid)) { allLit = false; break; }

    if (allLit)
        for (int32_t fid : ids) unlightFixture(fid);
    else
        for (int32_t fid : ids)
            if (!m_highlightedFixtureIds.contains(fid)) lightFixture(fid);

    emit highlightChanged();
}

void SpatialController::clearHighlight()
{
    if (m_highlightedFixtureIds.isEmpty()) return;
    QList<int32_t> fids(m_highlightedFixtureIds.begin(), m_highlightedFixtureIds.end());
    for (int32_t fid : fids) unlightFixture(fid);
    emit highlightChanged();
}

void SpatialController::releaseProgrammer()
{
    qDebug() << "[SpatialController] releaseProgrammer: focus=" << m_focusControlledChannels.size()
             << "highlight-fixtures=" << m_highlightedFixtureIds.size();
    clearFocusAim();
    clearHighlight();
}

std::vector<int32_t> SpatialController::highlightedFixtureIds() const
{
    std::vector<int32_t> out;
    out.reserve(m_highlightedFixtureIds.size());
    for (int32_t id : m_highlightedFixtureIds) out.push_back(id);
    return out;
}

QVariantList SpatialController::highlightedFixtureIdsList() const
{
    QVariantList out;
    for (int32_t id : m_highlightedFixtureIds) out.append(int(id));
    return out;
}

// ---------------------------------------------------------------------------
// Commit programmer state to a QLC+ Scene
// ---------------------------------------------------------------------------

void SpatialController::commitProgrammerToScene(const QString &name)
{
    // Union all programmer-controlled channels (aim + highlight)
    QSet<uint> allAddrs = m_focusControlledChannels;
    for (auto it = m_highlightChannelsPerFixture.begin();
         it != m_highlightChannelsPerFixture.end(); ++it)
        allAddrs.unite(it.value());

    if (allAddrs.isEmpty())
    {
        emit sceneSaved(-1, QString());
        return;
    }

    // Build reverse map: absAddr -> (fixtureId, channelIndex).
    // Fixture absolute address format: (universe << 9) | relativeAddr.
    QHash<uint, QPair<quint32, quint32>> addrToFxCh;
    for (Fixture *fxi : m_doc->fixtures())
    {
        if (!fxi) continue;
        quint32 base = fxi->universeAddress();
        for (quint32 ch = 0; ch < fxi->channels(); ++ch)
            addrToFxCh[base + ch] = qMakePair(fxi->id(), ch);
    }

    QString sceneName = name.trimmed().isEmpty()
                        ? QStringLiteral("Aim") : name.trimmed();
    Scene *scene = new Scene(m_doc);
    scene->setName(sceneName);

    for (uint addr : std::as_const(allAddrs))
    {
        auto it = addrToFxCh.find(addr);
        if (it == addrToFxCh.end()) continue;

        quint32 fid    = it->first;
        quint32 ch     = it->second;

        // Read current DMX from universe snapshot (same data the renderer uses)
        uchar val = 0;
        if (m_universeSnapshotCallback)
        {
            quint32 uni  = addr >> 9;
            int     rel  = static_cast<int>(addr & 0x1FF);
            QByteArray snap = m_universeSnapshotCallback(uni);
            if (rel < snap.size())
                val = static_cast<uchar>(snap.at(rel));
        }

        scene->setValue(fid, ch, val);
    }

    if (!m_doc->addFunction(scene))
    {
        delete scene;
        emit sceneSaveError(QStringLiteral("Failed to add scene to document"));
        return;
    }

    qDebug() << "[SpatialController] commitProgrammerToScene: saved"
             << scene->name() << "id=" << scene->id()
             << "channels=" << allAddrs.size();
    emit sceneSaved(static_cast<int>(scene->id()), scene->name());
}
