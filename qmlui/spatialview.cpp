/*
  Q Light Controller Plus
  spatialview.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QExposeEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

#include "spatialview.h"
#include "spatialrenderer.h"
#include "bgfxrenderer.h"
#include "fixturescenegraph.h"
#include "gizmo.h"
#include "raypick.h"
#include "gltfloader.h"
#include "tdsloader.h"
#include "primitivegen.h"
#include "spatialmodel.h"
#include "calibrationmodel.h"
#include "tardis/tardis.h"
#include "doc.h"
#include "fixture.h"
#include "fixturekinematics.h"
#include "inputoutputmap.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "gdtfgeometrydata.h"
#include "gdtfkinematics.h"
#include "qlcfile.h"
#include "qlcconfig.h"

#include <rigmath/kinematic_chain.hpp>

#ifdef Q_OS_MACOS
extern void *setupMetalLayerForView(void *nativeHandle);
#endif

SpatialView::SpatialView(Doc *doc, QWindow *parent)
    : QWindow(parent)
    , m_doc(doc)
    , m_renderer(qlcrender::createBgfxRenderer())
{
    setSurfaceType(QSurface::RasterSurface);
    setMinimumSize(QSize(400, 300));

    connect(&m_frameTimer, &QTimer::timeout, this, &SpatialView::onFrameTimer);

    SpatialModel *sm = m_doc->spatialModel();
    connect(sm, &SpatialModel::fixtureTransformChanged,
            this, &SpatialView::onSpatialTransformChanged);
    connect(sm, &SpatialModel::solverVizChanged,
            this, &SpatialView::onSolverVizChanged);
    connect(sm, &SpatialModel::trussesChanged,
            this, [this]() { rebuildTrusses(); });

    CalibrationModel *cm = m_doc->calibrationModel();
    connect(cm, &CalibrationModel::observationsChanged,
            this, [this]() { rebuildObservationLines(); });
    connect(cm, &CalibrationModel::solveCompleted,
            this, [this](bool) { rebuildObservationLines(); });

    // Subscribe to live DMX — deep-copied snapshots per universe tick.
    // Used by Calibrate/Focus mode beam cones to render the actual DMX state.
    if (InputOutputMap *ioMap = m_doc->inputOutputMap())
    {
        connect(ioMap, &InputOutputMap::universeWritten,
                this, &SpatialView::onUniverseWritten);
    }
}

SpatialView::~SpatialView()
{
    m_frameTimer.stop();
    if (m_bgfxReady)
        m_renderer->shutdown();
}

void SpatialView::startRendering()
{
    if (m_bgfxReady && !m_frameTimer.isActive())
        m_frameTimer.start(16);
}

void SpatialView::stopRendering()
{
    m_frameTimer.stop();
}

void SpatialView::selectFixture(int32_t fixtureId)
{
    if (m_bgfxReady)
        m_renderer->setSelectedFixture(fixtureId);

    rebuildBeamCones();

    // Notify the controller (QML panel) of selection change
    if (m_selectionCallback)
        m_selectionCallback(fixtureId, int(m_renderer->selectedIds().size()));
}

void SpatialView::setCameraOrbit(float yaw, float pitch, float distance)
{
    m_cameraYaw = yaw;
    m_cameraPitch = qBound(-89.0f, pitch, 89.0f);
    m_cameraDistance = qBound(0.5f, distance, 200.0f);
    if (m_bgfxReady)
        m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
}

void SpatialView::requestViewportScreenshot()
{
    if (!m_bgfxReady)
        return;
    auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
    if (bgfxR)
        bgfxR->callback().requestScreenshot();
}

QImage SpatialView::takeViewportScreenshot()
{
    if (!m_bgfxReady)
        return QImage();
    auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
    if (!bgfxR)
        return QImage();

    std::vector<uint8_t> rgba;
    uint32_t w, h;
    if (!bgfxR->callback().takeScreenshot(rgba, w, h))
        return QImage();

    // Convert raw RGBA to QImage
    QImage img(w, h, QImage::Format_RGBA8888);
    for (uint32_t y = 0; y < h; y++)
        memcpy(img.scanLine(y), rgba.data() + y * w * 4, w * 4);

    return img;
}

void SpatialView::initBgfx()
{
    if (m_bgfxReady)
        return;

    void *nwh = reinterpret_cast<void *>(winId());
#ifdef Q_OS_MACOS
    nwh = setupMetalLayerForView(nwh);
#endif
    uint32_t w = uint32_t(width() * devicePixelRatio());
    uint32_t h = uint32_t(height() * devicePixelRatio());

    if (w == 0 || h == 0)
        return;

    if (m_renderer->init(nwh, w, h))
    {
        m_bgfxReady = true;

        m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
        rebuildFixtures();
        rebuildEllipsoids();
        rebuildTrusses();
        rebuildObservationLines();
        rebuildBeamCones();
        m_frameTimer.start(16);
        qDebug() << "[SpatialView] bgfx initialized" << w << "x" << h;
    }
    else
    {
        qWarning() << "[SpatialView] Failed to initialize bgfx";
    }
}

void SpatialView::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    if (isExposed() && !m_bgfxReady)
        initBgfx();

    if (isExposed() && m_bgfxReady && !m_frameTimer.isActive())
        m_frameTimer.start(16);
}

void SpatialView::resizeEvent(QResizeEvent *event)
{
    QWindow::resizeEvent(event);
    if (m_bgfxReady)
    {
        uint32_t w = uint32_t(width() * devicePixelRatio());
        uint32_t h = uint32_t(height() * devicePixelRatio());
        m_renderer->resize(w, h);
    }
}

// --- Mouse interaction ---

void SpatialView::mouseToViewport(const QPoint &pos, float &mx, float &my,
                                   uint32_t &vw, uint32_t &vh) const
{
    float dpr = float(devicePixelRatio());
    mx = float(pos.x()) * dpr;
    my = float(pos.y()) * dpr;
    vw = uint32_t(width() * dpr);
    vh = uint32_t(height() * dpr);
}

void SpatialView::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    m_pressPos = event->pos();

    if (event->button() == Qt::LeftButton && m_bgfxReady)
    {
        float mx, my;
        uint32_t vw, vh;
        mouseToViewport(event->pos(), mx, my, vw, vh);

        int gizmoMode = m_gizmoModeCallback ? m_gizmoModeCallback() : 0;
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());

        // Focus mode: left-click either selects a fixture (if the click hits
        // a fixture body) or commits an aim point on the floor (if it misses
        // all fixtures). Checked BEFORE gizmo hit-tests so camera orbit
        // doesn't fight the aim.
        if (m_focusModeCallback && m_focusModeCallback() && bgfxR)
        {
            // Hit-test fixture AABBs first. If we hit one, fall through to
            // the normal click-release path so mouseReleaseEvent's existing
            // selection logic can handle it (same as Layout mode). Don't set
            // m_orbiting — stop here so dragging on a fixture is a no-op
            // rather than rotating the camera.
            int32_t hitId = m_renderer->hitTest(mx, my, vw, vh);
            if (hitId >= 0)
            {
                m_orbiting = false;
                return;
            }

            // Missed all fixtures — aim click. Commit immediately and enter
            // drag mode so mouseMoveEvent sweeps the aim.
            float view[16], proj[16];
            bgfxR->camera().viewMatrix(view);
            float aspect = float(vw) / float(vh);
            bgfxR->camera().projMatrix(proj, aspect, true);
            qlcrender::Ray ray = qlcrender::screenToRay(mx, my, vw, vh, view, proj);

            float hit[3];
            if (qlcrender::rayIntersectsPlaneZ(ray, 0.0f, hit))
            {
                m_focusDragging = true;
                m_orbiting = false;
                if (m_focusAimCallback)
                    m_focusAimCallback(double(hit[0]), double(hit[1]), double(hit[2]));
            }
            return;
        }

        if (gizmoMode == 0)
        {
            // Translate mode — check translate gizmo
            qlcrender::GizmoAxis axis = m_renderer->gizmoHitTest(mx, my, vw, vh);
            if (axis != qlcrender::GizmoAxis::None && bgfxR)
            {
                m_draggingGizmo = true;
                m_orbiting = false;
                bgfxR->gizmo().setActiveAxis(axis);
                bgfxR->gizmo().getPosition(m_dragStartPos);

                m_dragStartPositions.clear();
                m_dragStartTransforms.clear();
                SpatialModel *sm = m_doc->spatialModel();
                for (int32_t id : m_renderer->selectedIds())
                {
                    QString sid = QString::number(id);
                    rigmath::RigidTransform t = sm->fixtureTransform(sid);
                    m_dragStartPositions[id][0] = t.pos[0];
                    m_dragStartPositions[id][1] = t.pos[1];
                    m_dragStartPositions[id][2] = t.pos[2];
                    double ax, ay, az;
                    t.get_axis_angle(ax, ay, az);
                    m_dragStartTransforms[id][0] = t.pos[0];
                    m_dragStartTransforms[id][1] = t.pos[1];
                    m_dragStartTransforms[id][2] = t.pos[2];
                    m_dragStartTransforms[id][3] = ax;
                    m_dragStartTransforms[id][4] = ay;
                    m_dragStartTransforms[id][5] = az;
                }
                return;
            }
        }
        else if (gizmoMode == 1 && bgfxR)
        {
            // Rotate mode — check rotate gizmo
            float view[16], proj[16];
            bgfxR->camera().viewMatrix(view);
            float aspect = float(vw) / float(vh);
            bgfxR->camera().projMatrix(proj, aspect, true);
            qlcrender::Ray ray = qlcrender::screenToRay(mx, my, vw, vh, view, proj);

            qlcrender::GizmoAxis axis = bgfxR->rotateGizmo().hitTest(ray);
            if (axis != qlcrender::GizmoAxis::None)
            {
                m_draggingRotate = true;
                m_orbiting = false;
                bgfxR->rotateGizmo().setActiveAxis(axis);
                // Capture start transforms for undo
                m_dragStartTransforms.clear();
                SpatialModel *sm = m_doc->spatialModel();
                for (int32_t id : m_renderer->selectedIds())
                {
                    rigmath::RigidTransform t = sm->fixtureTransform(QString::number(id));
                    double ax, ay, az;
                    t.get_axis_angle(ax, ay, az);
                    m_dragStartTransforms[id][0] = t.pos[0];
                    m_dragStartTransforms[id][1] = t.pos[1];
                    m_dragStartTransforms[id][2] = t.pos[2];
                    m_dragStartTransforms[id][3] = ax;
                    m_dragStartTransforms[id][4] = ay;
                    m_dragStartTransforms[id][5] = az;
                }
                return;
            }
        }

        m_orbiting = true;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_panning = true;
    }
}

void SpatialView::mouseMoveEvent(QMouseEvent *event)
{
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_focusDragging && m_bgfxReady)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (!bgfxR)
            return;

        float mx, my;
        uint32_t vw, vh;
        mouseToViewport(event->pos(), mx, my, vw, vh);

        float view[16], proj[16];
        bgfxR->camera().viewMatrix(view);
        float aspect = float(vw) / float(vh);
        bgfxR->camera().projMatrix(proj, aspect, true);
        qlcrender::Ray ray = qlcrender::screenToRay(mx, my, vw, vh, view, proj);

        float hit[3];
        if (qlcrender::rayIntersectsPlaneZ(ray, 0.0f, hit))
        {
            if (m_focusAimCallback)
                m_focusAimCallback(double(hit[0]), double(hit[1]), double(hit[2]));
        }
        return;
    }

    if (m_draggingRotate && m_bgfxReady)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (!bgfxR)
            return;

        float mx, my, mxStart, myStart;
        uint32_t vw, vh;
        mouseToViewport(event->pos(), mx, my, vw, vh);
        mouseToViewport(m_pressPos, mxStart, myStart, vw, vh);

        float view[16], proj[16];
        bgfxR->camera().viewMatrix(view);
        float aspect = float(vw) / float(vh);
        bgfxR->camera().projMatrix(proj, aspect, true);

        qlcrender::Ray currentRay = qlcrender::screenToRay(mx, my, vw, vh, view, proj);
        qlcrender::Ray startRay = qlcrender::screenToRay(mxStart, myStart, vw, vh, view, proj);

        float angle = bgfxR->rotateGizmo().projectRotation(currentRay, startRay);
        if (std::abs(angle) > 1e-6f)
        {
            qlcrender::GizmoAxis activeAxis = bgfxR->rotateGizmo().activeAxis();
            SpatialModel *sm = m_doc->spatialModel();

            for (int32_t id : m_renderer->selectedIds())
            {
                QString sid = QString::number(id);
                rigmath::RigidTransform t = sm->fixtureTransform(sid);

                // Build axis-angle rotation
                double ax = 0, ay = 0, az = 0;
                if (activeAxis == qlcrender::GizmoAxis::X) ax = double(angle);
                else if (activeAxis == qlcrender::GizmoAxis::Y) ay = double(angle);
                else if (activeAxis == qlcrender::GizmoAxis::Z) az = double(angle);

                auto deltaRot = rigmath::RigidTransform::from_axis_angle(ax, ay, az);
                // Apply rotation: new_rot = deltaRot.rot * current.rot
                double newRot[9];
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        newRot[r*3+c] = deltaRot.rot[r*3+0]*t.rot[0*3+c]
                                      + deltaRot.rot[r*3+1]*t.rot[1*3+c]
                                      + deltaRot.rot[r*3+2]*t.rot[2*3+c];
                std::copy(std::begin(newRot), std::end(newRot), std::begin(t.rot));
                sm->setFixtureTransform(sid, t, SpatialModel::Committed);
            }

            // Reset the start ray to current for incremental rotation
            m_pressPos = event->pos();
        }
        return;
    }

    if (m_draggingGizmo && m_bgfxReady)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (!bgfxR)
            return;

        float mx, my, mxStart, myStart;
        uint32_t vw, vh;
        mouseToViewport(event->pos(), mx, my, vw, vh);
        mouseToViewport(m_pressPos, mxStart, myStart, vw, vh);

        // Get camera matrices for ray computation
        float view[16], proj[16];
        bgfxR->camera().viewMatrix(view);
        float aspect = float(vw) / float(vh);
        bgfxR->camera().projMatrix(proj, aspect, true);  // Metal = homogeneous depth

        qlcrender::Ray currentRay = qlcrender::screenToRay(mx, my, vw, vh, view, proj);
        qlcrender::Ray startRay = qlcrender::screenToRay(mxStart, myStart, vw, vh, view, proj);

        float outDelta[3];
        if (bgfxR->gizmo().projectDrag(currentRay, startRay, m_dragStartPos, outDelta))
        {
            double dx = outDelta[0], dy = outDelta[1], dz = outDelta[2];

            // Compute primary fixture's new position (for gizmo + snap)
            double newX = m_dragStartPos[0] + dx;
            double newY = m_dragStartPos[1] + dy;
            double newZ = m_dragStartPos[2] + dz;

            // Apply grid snap to primary fixture, then recompute delta
            if (m_snapCallback)
            {
                m_snapCallback(newX, newY, newZ);
                dx = newX - m_dragStartPos[0];
                dy = newY - m_dragStartPos[1];
                dz = newZ - m_dragStartPos[2];
            }

            // Update gizmo position
            bgfxR->gizmo().setPosition(float(newX), float(newY), float(newZ));

            // Move all selected fixtures by the same delta
            SpatialModel *sm = m_doc->spatialModel();
            for (const auto &pair : m_dragStartPositions)
            {
                QString id = QString::number(pair.first);
                rigmath::RigidTransform t = sm->fixtureTransform(id);
                t.pos[0] = pair.second[0] + dx;
                t.pos[1] = pair.second[1] + dy;
                t.pos[2] = pair.second[2] + dz;
                sm->setFixtureTransform(id, t, SpatialModel::Committed);
            }
        }
        return;
    }

    if (m_orbiting)
    {
        m_cameraYaw -= delta.x() * 0.3f;
        m_cameraPitch += delta.y() * 0.3f;
        m_cameraPitch = qBound(-89.0f, m_cameraPitch, 89.0f);
        m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
    }
    else if (m_panning)
    {
        float panScale = m_cameraDistance * 0.002f;
        m_renderer->panCamera(-delta.x() * panScale, delta.y() * panScale);
    }
}

void SpatialView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_focusDragging)
    {
        m_focusDragging = false;
        return;
    }

    if (event->button() == Qt::LeftButton && m_draggingGizmo)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (bgfxR)
            bgfxR->gizmo().setActiveAxis(qlcrender::GizmoAxis::None);
        m_draggingGizmo = false;

        // Enqueue undo for each fixture that was dragged.
        // Tardis batches actions within 150ms, so all fixtures in the same
        // drag get a single undo step.
        enqueueSpatialUndoActions();
        return;
    }

    if (event->button() == Qt::LeftButton && m_draggingRotate)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (bgfxR)
            bgfxR->rotateGizmo().setActiveAxis(qlcrender::GizmoAxis::None);
        m_draggingRotate = false;

        enqueueSpatialUndoActions();
        return;
    }

    // Detect click vs drag: if mouse didn't move more than 4px, it's a click
    if (event->button() == Qt::LeftButton
        && (event->pos() - m_pressPos).manhattanLength() < 5
        && m_bgfxReady)
    {
        float mx, my;
        uint32_t vw, vh;
        mouseToViewport(event->pos(), mx, my, vw, vh);

        qDebug() << "[SpatialView] Click at logical:" << event->pos()
                 << "viewport(device):" << mx << my
                 << "viewportSize:" << vw << vh
                 << "windowSize:" << width() << height()
                 << "geometry:" << geometry()
                 << "dpr:" << devicePixelRatio();

        int32_t hitId = m_renderer->hitTest(mx, my, vw, vh);
        bool shiftHeld = event->modifiers() & Qt::ShiftModifier;

        if (shiftHeld && hitId >= 0)
        {
            // Shift+click: toggle fixture in selection
            if (m_renderer->isSelected(hitId))
                m_renderer->removeSelectedFixture(hitId);
            else
                m_renderer->addSelectedFixture(hitId);
        }
        else
        {
            // Plain click: replace selection
            m_renderer->setSelectedFixture(hitId);
        }

        rebuildBeamCones();

        // Notify the controller (and QML panel) of selection change
        if (m_selectionCallback)
            m_selectionCallback(m_renderer->selectedFixture(),
                                int(m_renderer->selectedIds().size()));

        if (hitId >= 0)
            qDebug() << "[SpatialView] Selected fixture:" << hitId
                     << "total:" << m_renderer->selectedIds().size();
        else
            qDebug() << "[SpatialView] Deselected";
    }

    m_orbiting = false;
    m_panning = false;
    m_draggingGizmo = false;
    m_draggingRotate = false;
}

void SpatialView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_W && m_gizmoModeSetCallback)
        m_gizmoModeSetCallback(0);  // Translate
    else if (event->key() == Qt::Key_E && m_gizmoModeSetCallback)
        m_gizmoModeSetCallback(1);  // Rotate
    else if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier))
    {
        Tardis *tardis = Tardis::instance();
        if (tardis)
        {
            if (event->modifiers() & Qt::ShiftModifier)
                tardis->redoAction();
            else
                tardis->undoAction();
        }
    }
    else
        QWindow::keyPressEvent(event);
}

void SpatialView::wheelEvent(QWheelEvent *event)
{
    float zoomDelta = event->angleDelta().y() / 120.0f;
    m_cameraDistance *= (1.0f + zoomDelta * 0.1f);
    m_cameraDistance = qBound(0.5f, m_cameraDistance, 200.0f);
    m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
}

// --- Data binding ---

void SpatialView::onFrameTimer()
{
    if (m_bgfxReady && isExposed())
        m_renderer->frame();
}

void SpatialView::onSpatialTransformChanged(const QString &id)
{
    Q_UNUSED(id);
    rebuildFixtures();
    rebuildEllipsoids();
    rebuildObservationLines();
    rebuildBeamCones();
}

void SpatialView::onSolverVizChanged()
{
    rebuildFixtures();
    rebuildEllipsoids();
    rebuildObservationLines();
    rebuildBeamCones();
}

// --- Undo helpers ---

static QVariantList transformToVariantList(const rigmath::RigidTransform &t)
{
    double ax, ay, az;
    t.get_axis_angle(ax, ay, az);
    return QVariantList{t.pos[0], t.pos[1], t.pos[2], ax, ay, az};
}

void SpatialView::enqueueSpatialUndoActions()
{
    SpatialModel *sm = m_doc->spatialModel();
    Tardis *tardis = Tardis::instance();
    if (!tardis)
        return;

    for (const auto &pair : m_dragStartTransforms)
    {
        int32_t id = pair.first;
        const double *start = pair.second;

        // Current (post-drag) transform
        rigmath::RigidTransform endT = sm->fixtureTransform(QString::number(id));

        // Start transform (pre-drag)
        rigmath::RigidTransform startT = rigmath::RigidTransform::from_pose(
            start[0], start[1], start[2], start[3], start[4], start[5]);

        // Only enqueue if something actually changed
        bool moved = std::abs(endT.pos[0] - startT.pos[0]) > 1e-9
                  || std::abs(endT.pos[1] - startT.pos[1]) > 1e-9
                  || std::abs(endT.pos[2] - startT.pos[2]) > 1e-9;
        double eax, eay, eaz;
        endT.get_axis_angle(eax, eay, eaz);
        bool rotated = std::abs(eax - start[3]) > 1e-9
                    || std::abs(eay - start[4]) > 1e-9
                    || std::abs(eaz - start[5]) > 1e-9;

        if (moved || rotated)
        {
            tardis->enqueueAction(
                Tardis::SpatialFixtureSetTransform,
                quint32(id),
                QVariant::fromValue(QVariantList{start[0], start[1], start[2],
                                                 start[3], start[4], start[5]}),
                QVariant::fromValue(transformToVariantList(endT))
            );
        }
    }
    m_dragStartTransforms.clear();
}

void SpatialView::onUniverseWritten(quint32 universeId, const QByteArray &data)
{
    // Deep-copied snapshot from the DMX tick — store for live-DMX beam cone
    // rendering in Calibrate/Focus modes. QByteArray uses copy-on-write, so
    // this assignment is cheap.
    m_universeSnapshots[universeId] = data;

    // Only rebuild cones in modes that care about live DMX. Layout mode
    // uses home-position forward kinematics and is unaffected.
    if (m_liveDmxModeCallback && m_liveDmxModeCallback())
    {
        rebuildFixtureDofs();
        rebuildBeamCones();
    }
}

static void addFixtureEntry(std::vector<qlcrender::RenderFixture> &out,
                            uint32_t id, int fixtureType,
                            const std::string &name,
                            const rigmath::RigidTransform &t,
                            float r, float g, float b, float a,
                            const qlcrender::FixtureSceneGraph *sg = nullptr)
{
    qlcrender::RenderFixture rf;
    rf.id = id;
    rf.fixtureType = fixtureType;
    rf.name = name;
    rf.sceneGraph = sg;

    double d[16];
    t.to_4x4_column_major(d);
    for (int i = 0; i < 16; i++)
        rf.transform[i] = float(d[i]);

    rf.color[0] = r; rf.color[1] = g; rf.color[2] = b; rf.color[3] = a;
    out.push_back(rf);
}

void SpatialView::rebuildFixtures()
{
    if (!m_bgfxReady)
        return;

    SpatialModel *sm = m_doc->spatialModel();
    std::vector<qlcrender::RenderFixture> fixtures;

    QLCFixtureDefCache *defCache = m_doc->fixtureDefCache();

    for (const QString &id : sm->fixtureIds())
    {
        Fixture *fxi = m_doc->fixture(id.toUInt());
        int fxType = fxi ? fxi->type() : -1;
        uint32_t fxId = id.toUInt();
        std::string fxName = fxi ? fxi->name().toStdString() : ("Fixture " + id.toStdString());

        // Look up GDTF scene graph for this fixture.
        // Every fixture gets a scene graph — GDTF fixtures from real geometry,
        // QXF fixtures from synthesized GDTF data.
        const qlcrender::FixtureSceneGraph *sg = nullptr;
        if (fxi && fxi->fixtureDef())
        {
            const QString &mfg = fxi->fixtureDef()->manufacturer();
            const QString &mdl = fxi->fixtureDef()->model();
            const GDTFGeometryData *geoData = defCache->gdtfGeometry(mfg, mdl);

            // QXF fixture without GDTF data — synthesize and cache
            if (!geoData)
            {
                std::string cacheKey = mfg.toStdString() + '\0' + mdl.toStdString();
                auto it = m_synthesizedGeoCache.find(cacheKey);
                if (it == m_synthesizedGeoCache.end())
                {
                    const QLCFixtureMode *mode = fxi->fixtureMode();
                    if (mode)
                    {
                        QLCPhysical phy = mode->physical();
                        double panRange = phy.focusPanMax() > 0 ? phy.focusPanMax() : 540.0;
                        double tiltRange = phy.focusTiltMax() > 0 ? phy.focusTiltMax() : 270.0;
                        bool isMirror = phy.focusType().compare(
                            QStringLiteral("Mirror"), Qt::CaseInsensitive) == 0;
                        quint32 panMSB = mode->channelNumber(QLCChannel::Pan, QLCChannel::MSB);
                        quint32 panLSB = mode->channelNumber(QLCChannel::Pan, QLCChannel::LSB);
                        quint32 tiltMSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
                        quint32 tiltLSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::LSB);

                        auto synth = std::make_unique<GDTFGeometryData>();
                        GDTFDmxModeInfo modeInfo;
                        synthesizeGDTFFromQXF(
                            panMSB != QLCChannel::invalid(),
                            tiltMSB != QLCChannel::invalid(),
                            panRange, tiltRange, isMirror,
                            panMSB != QLCChannel::invalid() ? int(panMSB) : -1,
                            panLSB != QLCChannel::invalid() ? int(panLSB) : -1,
                            tiltMSB != QLCChannel::invalid() ? int(tiltMSB) : -1,
                            tiltLSB != QLCChannel::invalid() ? int(tiltLSB) : -1,
                            phy.lensDegreesMin(), phy.lensDegreesMax(),
                            *synth, modeInfo);
                        it = m_synthesizedGeoCache.emplace(cacheKey, std::move(synth)).first;
                    }
                }
                if (it != m_synthesizedGeoCache.end())
                    geoData = it->second.get();
            }

            if (geoData)
            {
                // Look up mode info and build axisTags for DOF tagging
                QString modeName;
                std::vector<AxisDofTag> axisTags;
                const QLCFixtureMode *mode = fxi->fixtureMode();
                if (mode && !geoData->dmxModes.isEmpty())
                {
                    modeName = mode->name();
                    const GDTFDmxModeInfo *mi = nullptr;
                    for (const auto &m : geoData->dmxModes)
                    {
                        if (m.modeName == modeName)
                        {
                            mi = &m;
                            break;
                        }
                    }
                    if (!mi)
                    {
                        mi = &geoData->dmxModes.first();
                        modeName = mi->modeName;
                    }
                    GDTFKinematicsResult kinResult = buildGDTFKinematics(geoData->rootForMode(modeName), *mi);
                    axisTags = std::move(kinResult.axisTags);
                }
                sg = getOrBuildSceneGraph(mfg, mdl, modeName, geoData, axisTags);
            }
        }

        // Committed (solid)
        auto committed = sm->committedTransform(id);
        if (committed.has_value())
        {
            addFixtureEntry(fixtures, fxId, fxType, fxName, committed.value(),
                            1.0f, 0.6f, 0.2f, 1.0f, sg);  // orange solid
        }
        else
        {
            auto agent = sm->agentDerivedTransform(id);
            if (agent.has_value())
            {
                addFixtureEntry(fixtures, fxId, fxType, fxName, agent.value(),
                                0.2f, 0.9f, 0.3f, 0.8f, sg);  // green, slightly translucent
            }
            else
            {
                addFixtureEntry(fixtures, fxId, fxType, fxName,
                                rigmath::RigidTransform::identity(),
                                0.2f, 0.8f, 0.9f, 1.0f, sg);  // cyan
            }
            continue;
        }

        // agentDerived ghost (green, translucent)
        auto agentDerived = sm->agentDerivedTransform(id);
        if (agentDerived.has_value())
        {
            addFixtureEntry(fixtures, fxId, fxType, fxName, agentDerived.value(),
                            0.2f, 0.9f, 0.3f, 0.4f, sg);
        }

        // solverDerived ghost (cyan, translucent)
        auto solverDerived = sm->solverDerivedTransform(id);
        if (solverDerived.has_value())
        {
            addFixtureEntry(fixtures, fxId, fxType, fxName, solverDerived.value(),
                            0.2f, 0.8f, 0.9f, 0.4f, sg);
        }
    }

    m_renderer->setFixtures(fixtures);
}

void SpatialView::rebuildEllipsoids()
{
    if (!m_bgfxReady)
        return;

    SpatialModel *sm = m_doc->spatialModel();
    std::vector<qlcrender::RenderEllipsoid> ellipsoids;

    for (const QString &id : sm->fixtureIds())
    {
        SpatialModel::FixtureViz viz = sm->fixtureViz(id);

        if (viz.ellipsoidAxes[0] <= 0.0 && viz.ellipsoidAxes[1] <= 0.0 && viz.ellipsoidAxes[2] <= 0.0)
            continue;

        qlcrender::RenderEllipsoid ell;
        ell.fixtureId = id.toUInt();

        rigmath::RigidTransform t = sm->fixtureTransform(id);
        ell.center[0] = float(t.pos[0]);
        ell.center[1] = float(t.pos[1]);
        ell.center[2] = float(t.pos[2]);

        ell.semiAxes[0] = float(viz.ellipsoidAxes[0] / 100.0);
        ell.semiAxes[1] = float(viz.ellipsoidAxes[1] / 100.0);
        ell.semiAxes[2] = float(viz.ellipsoidAxes[2] / 100.0);

        for (int i = 0; i < 9; i++)
            ell.rotation[i] = float(viz.ellipsoidRot[i]);

        if (viz.quality == "good")
        {
            ell.color[0] = 0.2f; ell.color[1] = 0.8f; ell.color[2] = 0.2f; ell.color[3] = 0.3f;
        }
        else if (viz.quality == "moderate")
        {
            ell.color[0] = 0.9f; ell.color[1] = 0.8f; ell.color[2] = 0.1f; ell.color[3] = 0.3f;
        }
        else if (viz.quality == "poor")
        {
            ell.color[0] = 0.9f; ell.color[1] = 0.2f; ell.color[2] = 0.2f; ell.color[3] = 0.3f;
        }
        else
        {
            ell.color[0] = 0.5f; ell.color[1] = 0.5f; ell.color[2] = 0.5f; ell.color[3] = 0.2f;
        }

        ellipsoids.push_back(ell);
    }

    m_renderer->setCalibrationOverlays(ellipsoids);
}

void SpatialView::rebuildTrusses()
{
    if (!m_bgfxReady)
        return;

    SpatialModel *sm = m_doc->spatialModel();
    std::vector<qlcrender::RenderTruss> trusses;

    for (const SpatialModel::Truss &t : sm->trusses())
    {
        qlcrender::RenderTruss rt;
        rt.start[0] = float(t.start[0]); rt.start[1] = float(t.start[1]); rt.start[2] = float(t.start[2]);
        rt.end[0] = float(t.end[0]); rt.end[1] = float(t.end[1]); rt.end[2] = float(t.end[2]);
        trusses.push_back(rt);
    }

    m_renderer->setTrusses(trusses);
}

void SpatialView::rebuildObservationLines()
{
    if (!m_bgfxReady)
        return;

    CalibrationModel *cm = m_doc->calibrationModel();
    SpatialModel *sm = m_doc->spatialModel();
    std::vector<qlcrender::RenderLine> lines;

    for (const CalibrationModel::Observation &obs : cm->observations())
    {
        std::visit([&](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, CalibrationModel::DistanceObs>)
            {
                rigmath::RigidTransform tA = sm->fixtureTransform(o.fixtureA);
                rigmath::RigidTransform tB = sm->fixtureTransform(o.fixtureB);

                qlcrender::RenderLine line;
                line.start[0] = float(tA.pos[0]);
                line.start[1] = float(tA.pos[1]);
                line.start[2] = float(tA.pos[2]);
                line.end[0] = float(tB.pos[0]);
                line.end[1] = float(tB.pos[1]);
                line.end[2] = float(tB.pos[2]);
                // Orange-yellow for distance
                line.color[0] = 1.0f; line.color[1] = 0.8f;
                line.color[2] = 0.2f; line.color[3] = 0.9f;
                lines.push_back(line);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::AimObs>)
            {
                rigmath::RigidTransform t = sm->fixtureTransform(o.fixture);

                qlcrender::RenderLine line;
                line.start[0] = float(t.pos[0]);
                line.start[1] = float(t.pos[1]);
                line.start[2] = float(t.pos[2]);
                line.end[0] = float(o.target[0]);
                line.end[1] = float(o.target[1]);
                line.end[2] = float(o.target[2]);
                // Cyan for aim
                line.color[0] = 0.3f; line.color[1] = 0.9f;
                line.color[2] = 0.9f; line.color[3] = 0.9f;
                lines.push_back(line);
            }
            // Position, Rotation, Crossing, BeamDirection: no line viz for v1
        }, obs);
    }

    m_renderer->setObservationLines(lines);
}

void SpatialView::rebuildFixtureDofs()
{
    if (!m_bgfxReady)
        return;

    SpatialModel *sm = m_doc->spatialModel();

    for (const QString &id : sm->fixtureIds())
    {
        Fixture *fxi = m_doc->fixture(id.toUInt());
        if (!fxi)
            continue;

        FixtureKinematics fk = buildFixtureKinematics(fxi);
        if (!fk.chain || !fk.channelMap || fk.dofCount() == 0)
            continue;

        int numDofs = fk.dofCount();
        std::vector<double> dofs(numDofs, 0.0);

        auto it = m_universeSnapshots.find(fk.universeId);
        if (it != m_universeSnapshots.end())
            dofs = dmxSnapshotToDofs(fk, it.value());

        // Convert double → float for the render layer
        std::vector<float> angles(numDofs);
        for (int i = 0; i < numDofs; i++)
            angles[i] = static_cast<float>(dofs[i]);

        m_renderer->updateFixtureDofAngles(id.toUInt(), angles);
    }
}

void SpatialView::rebuildBeamCones()
{
    if (!m_bgfxReady)
        return;

    const bool useLiveDmx = m_liveDmxModeCallback && m_liveDmxModeCallback();

    std::vector<qlcrender::RenderBeamCone> cones;
    SpatialModel *sm = m_doc->spatialModel();
    auto selectedIds = m_renderer->selectedIds();

    for (int32_t fid : selectedIds)
    {
        Fixture *fxi = m_doc->fixture(quint32(fid));
        if (!fxi)
            continue;

        const QLCFixtureMode *mode = fxi->fixtureMode();
        if (!mode)
            continue;

        // Build the channel map — this gives us kinematics chain + ChannelMap
        // for ANY fixture type (GDTF or synthesized QXF). No manual channel
        // detection needed.
        FixtureKinematics fk = buildFixtureKinematics(fxi);
        if (!fk.chain || !fk.channelMap)
            continue;

        int numDofs = fk.dofCount();

        // Get DOF angles: from live DMX in Calibrate/Focus, or home (all zeros)
        // in Layout. The ChannelMap handles all DOF combinations generically.
        std::vector<double> dofs(numDofs, 0.0);
        if (useLiveDmx && numDofs > 0)
        {
            auto it = m_universeSnapshots.find(fk.universeId);
            if (it != m_universeSnapshots.end())
                dofs = dmxSnapshotToDofs(fk, it.value());
        }

        QLCPhysical phy = mode->physical();
        rigmath::RigidTransform xf = sm->fixtureTransform(QString::number(fid));

        // Beam half-angle (use widest end of zoom range, default 5°)
        double halfAngle = phy.lensDegreesMax() > 0
                             ? phy.lensDegreesMax() / 2.0
                             : 5.0;

        // Emit one cone per beam emitter in the chain.
        int numBeams = fk.beamCount();
        if (numBeams == 0)
            numBeams = 1;

        for (int bi = 0; bi < numBeams; bi++)
        {
            rigmath::Ray worldRay = fk.chain->forward_world(xf, dofs, bi);

            // Clip beam length at the Z=0 floor plane
            float beamLength = 10.0f;
            if (worldRay.oz > 0.05 && worldRay.dz < -1e-3)
            {
                double tFloor = -worldRay.oz / worldRay.dz;
                beamLength = float(std::min(tFloor, 15.0));
            }

            qlcrender::RenderBeamCone cone;
            cone.fixtureId = uint32_t(fid);
            cone.origin[0] = float(worldRay.ox);
            cone.origin[1] = float(worldRay.oy);
            cone.origin[2] = float(worldRay.oz);
            cone.direction[0] = float(worldRay.dx);
            cone.direction[1] = float(worldRay.dy);
            cone.direction[2] = float(worldRay.dz);
            cone.halfAngleDeg = float(halfAngle);
            cone.length = beamLength;
            cone.color[0] = 0.3f;
            cone.color[1] = 0.9f;
            cone.color[2] = 1.0f;
            cone.color[3] = 0.7f;
            cones.push_back(cone);
        }
    }

    m_renderer->setBeamCones(cones);
}

// ---------------------------------------------------------------------------
// GDTF scene graph builder
// ---------------------------------------------------------------------------

static void buildSceneNode(const GDTFGeometryNode &geoNode,
                           qlcrender::SceneNode &sceneNode,
                           const QMap<QString, QByteArray> &meshData,
                           std::unordered_map<std::string, qlcrender::LoadedMesh> &meshCache,
                           const std::vector<AxisDofTag> &axisTags)
{
    // localTransform = GDTF Position matrix ONLY — never modified after this.
    // All mesh sizing is baked into vertex data at load time.
    for (int i = 0; i < 16; i++)
        sceneNode.localTransform[i] = geoNode.localTransform[i];

    // Tag GeometryAxis nodes with DOF info from kinematics
    if (geoNode.type == GeometryAxis)
    {
        for (const auto &tag : axisTags)
        {
            if (tag.geometryName == geoNode.name)
            {
                sceneNode.dofIndex = tag.dofIndex;
                sceneNode.dofAxis[0] = tag.axis[0];
                sceneNode.dofAxis[1] = tag.axis[1];
                sceneNode.dofAxis[2] = tag.axis[2];
                break;
            }
        }
    }

    // Tag beam nodes (Lamp/Laser) for future scene-graph-based beam walking
    if (geoNode.type == GeometryLamp || geoNode.type == GeometryLaser)
    {
        sceneNode.isBeamNode = true;
        sceneNode.beamAngle = geoNode.beamAngle;
    }

    // Load mesh from embedded GDTF model files (3DS or glTF).
    // Both loaders scale vertices to GDTF dimensions at load time.
    // Cache key includes dimensions to avoid collisions across fixtures.
    if (!geoNode.meshRef.isEmpty() && meshData.contains(geoNode.meshRef))
    {
        char dimSuffix[64] = "";
        snprintf(dimSuffix, sizeof(dimSuffix), ":%.4f:%.4f:%.4f",
                 geoNode.modelLength, geoNode.modelWidth, geoNode.modelHeight);
        std::string key = geoNode.meshRef.toStdString() + dimSuffix;

        auto it = meshCache.find(key);
        if (it == meshCache.end())
        {
            const QByteArray &rawData = meshData[geoNode.meshRef];
            qlcrender::LoadedMesh mesh;
            if (geoNode.meshRef.endsWith(QStringLiteral(".3ds"), Qt::CaseInsensitive))
            {
                mesh = qlcrender::TdsLoader::loadFromMemory(
                    reinterpret_cast<const unsigned char *>(rawData.constData()),
                    rawData.size(), key,
                    geoNode.modelLength, geoNode.modelWidth, geoNode.modelHeight);
            }
            else
            {
                mesh = qlcrender::GltfLoader::loadFromMemory(
                    reinterpret_cast<const unsigned char *>(rawData.constData()),
                    rawData.size(), key,
                    geoNode.modelLength, geoNode.modelWidth, geoNode.modelHeight);
            }
            it = meshCache.emplace(key, mesh).first;
        }
        if (it->second.isValid())
            sceneNode.mesh = &it->second;
    }

    // Fallback: generate primitive mesh at GDTF dimensions.
    // Vertices are pre-sized — no localTransform modification needed.
    if (!sceneNode.mesh && geoNode.primitiveType > 0)
    {
        char primKey[128];
        snprintf(primKey, sizeof(primKey), "__prim:%d:%.4f:%.4f:%.4f",
                 geoNode.primitiveType,
                 geoNode.modelLength, geoNode.modelWidth, geoNode.modelHeight);
        std::string key(primKey);

        auto it = meshCache.find(key);
        if (it == meshCache.end())
        {
            qlcrender::LoadedMesh mesh = qlcrender::PrimitiveGen::generate(
                geoNode.primitiveType,
                geoNode.modelLength, geoNode.modelWidth, geoNode.modelHeight);
            it = meshCache.emplace(key, mesh).first;
        }
        if (it->second.isValid())
            sceneNode.mesh = &it->second;
    }

    for (const auto &childGeo : geoNode.children)
    {
        sceneNode.children.emplace_back();
        buildSceneNode(childGeo, sceneNode.children.back(), meshData, meshCache,
                       axisTags);
    }
}

const qlcrender::FixtureSceneGraph *SpatialView::getOrBuildSceneGraph(
    const QString &manufacturer, const QString &model,
    const QString &modeName,
    const GDTFGeometryData *geoData,
    const std::vector<AxisDofTag> &axisTags)
{
    std::string key = manufacturer.toStdString() + '\0' + model.toStdString()
                      + '\0' + modeName.toStdString();
    auto it = m_sceneGraphCache.find(key);
    if (it != m_sceneGraphCache.end())
        return &it->second;

    qlcrender::FixtureSceneGraph graph;

    // Unified mesh cache: stores 3DS, glTF, and primitive meshes.
    // Keys include dimensions to avoid collisions.
    static std::unordered_map<std::string, qlcrender::LoadedMesh> s_meshCache;

    // Use the mode-specific root geometry (handles multi-root GDTF fixtures
    // where different modes use different geometry trees).
    const GDTFGeometryNode &geoRoot = geoData->rootForMode(modeName);
    buildSceneNode(geoRoot, graph.root, geoData->meshData,
                   s_meshCache, axisTags);
    graph.valid = true;

    auto result = m_sceneGraphCache.emplace(key, std::move(graph));
    return &result.first->second;
}
