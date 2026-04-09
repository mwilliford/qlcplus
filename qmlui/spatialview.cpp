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
#include "primitivegen.h"
#include "spatialmodel.h"
#include "doc.h"
#include "fixture.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "gdtfgeometrydata.h"
#include "qlcfile.h"
#include "qlcconfig.h"

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

        QString meshPath = QLCFile::systemDirectory(MESHESDIR).path()
                           + QDir::separator() + "fixtures"
                           + QDir::separator() + "bgfx" + QDir::separator();
        m_renderer->setMeshBasePath(meshPath.toStdString());
        qDebug() << "[SpatialView] Mesh path:" << meshPath;

        m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
        rebuildFixtures();
        rebuildEllipsoids();
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
                SpatialModel *sm = m_doc->spatialModel();
                for (int32_t id : m_renderer->selectedIds())
                {
                    QString sid = QString::number(id);
                    rigmath::RigidTransform t = sm->fixtureTransform(sid);
                    m_dragStartPositions[id][0] = t.pos[0];
                    m_dragStartPositions[id][1] = t.pos[1];
                    m_dragStartPositions[id][2] = t.pos[2];
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
    if (event->button() == Qt::LeftButton && m_draggingGizmo)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (bgfxR)
            bgfxR->gizmo().setActiveAxis(qlcrender::GizmoAxis::None);
        m_draggingGizmo = false;
        qDebug() << "[SpatialView] Translate drag completed";
        return;
    }

    if (event->button() == Qt::LeftButton && m_draggingRotate)
    {
        auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(m_renderer.get());
        if (bgfxR)
            bgfxR->rotateGizmo().setActiveAxis(qlcrender::GizmoAxis::None);
        m_draggingRotate = false;
        qDebug() << "[SpatialView] Rotate drag completed";
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
}

void SpatialView::onSolverVizChanged()
{
    rebuildFixtures();
    rebuildEllipsoids();
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

        // Look up GDTF scene graph for this fixture
        const qlcrender::FixtureSceneGraph *sg = nullptr;
        if (fxi && fxi->fixtureDef())
        {
            const QString &mfg = fxi->fixtureDef()->manufacturer();
            const QString &mdl = fxi->fixtureDef()->model();
            const GDTFGeometryData *geoData = defCache->gdtfGeometry(mfg, mdl);
            if (geoData)
                sg = getOrBuildSceneGraph(mfg, mdl, geoData);
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

// ---------------------------------------------------------------------------
// GDTF scene graph builder
// ---------------------------------------------------------------------------

static void buildSceneNode(const GDTFGeometryNode &geoNode,
                           qlcrender::SceneNode &sceneNode,
                           const QMap<QString, QByteArray> &meshData,
                           qlcrender::PrimitiveGen &primGen,
                           std::unordered_map<std::string, qlcrender::LoadedMesh> &meshCache)
{
    for (int i = 0; i < 16; i++)
        sceneNode.localTransform[i] = geoNode.localTransform[i];

    if (!geoNode.meshRef.isEmpty() && meshData.contains(geoNode.meshRef))
    {
        std::string key = geoNode.meshRef.toStdString();
        auto it = meshCache.find(key);
        if (it == meshCache.end())
        {
            const QByteArray &glbData = meshData[geoNode.meshRef];
            auto mesh = qlcrender::GltfLoader::loadFromMemory(
                reinterpret_cast<const unsigned char *>(glbData.constData()),
                glbData.size(), key);
            it = meshCache.emplace(key, mesh).first;
        }
        if (it->second.isValid())
            sceneNode.mesh = &it->second;
    }

    if (!sceneNode.mesh && geoNode.primitiveType > 0)
        sceneNode.mesh = primGen.getPrimitive(geoNode.primitiveType);

    for (const auto &childGeo : geoNode.children)
    {
        sceneNode.children.emplace_back();
        buildSceneNode(childGeo, sceneNode.children.back(), meshData, primGen, meshCache);
    }
}

const qlcrender::FixtureSceneGraph *SpatialView::getOrBuildSceneGraph(
    const QString &manufacturer, const QString &model,
    const GDTFGeometryData *geoData)
{
    std::string key = manufacturer.toStdString() + '\0' + model.toStdString();
    auto it = m_sceneGraphCache.find(key);
    if (it != m_sceneGraphCache.end())
        return &it->second;

    qlcrender::FixtureSceneGraph graph;

    static qlcrender::PrimitiveGen s_primGen;
    static bool s_primInit = false;
    if (!s_primInit)
    {
        s_primGen.init();
        s_primInit = true;
    }

    static std::unordered_map<std::string, qlcrender::LoadedMesh> s_gltfMeshCache;

    buildSceneNode(geoData->root, graph.root, geoData->meshData,
                   s_primGen, s_gltfMeshCache);
    graph.valid = true;

    auto result = m_sceneGraphCache.emplace(key, std::move(graph));
    return &result.first->second;
}
