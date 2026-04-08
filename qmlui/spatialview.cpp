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
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

#include "spatialview.h"
#include "spatialrenderer.h"
#include "fixturescenegraph.h"
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

SpatialView *SpatialView::s_instance = nullptr;

SpatialView::SpatialView(Doc *doc)
    : QWindow()
    , m_doc(doc)
    , m_renderer(qlcrender::createBgfxRenderer())
{
    // Use RasterSurface — let bgfx create its own CAMetalLayer
    setSurfaceType(QSurface::RasterSurface);
    setTitle(QStringLiteral("QLC+ Spatial View"));
    setMinimumSize(QSize(400, 300));

    // Frame timer — 60Hz
    connect(&m_frameTimer, &QTimer::timeout, this, &SpatialView::onFrameTimer);

    // Listen to SpatialModel changes
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
    s_instance = nullptr;
}

void SpatialView::createAndShow(Doc *doc)
{
    if (s_instance != nullptr)
    {
        s_instance->show();
        s_instance->raise();
        s_instance->requestActivate();
        return;
    }

    s_instance = new SpatialView(doc);

    // Restore geometry
    QSettings settings;
    QVariant var = settings.value(SETTINGS_SPATIALVIEW_GEOMETRY);
    if (var.isValid())
    {
        s_instance->setGeometry(var.toRect());
    }
    else
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen)
        {
            QRect screenGeo = screen->availableGeometry();
            s_instance->resize(screenGeo.width() * 3 / 4, screenGeo.height() * 3 / 4);
        }
        else
        {
            s_instance->resize(1024, 768);
        }
    }

    s_instance->show();
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

        // Set mesh path to the bgfx-converted fixture meshes
        QString meshPath = QLCFile::systemDirectory(MESHESDIR).path()
                           + QDir::separator() + "fixtures"
                           + QDir::separator() + "bgfx" + QDir::separator();
        m_renderer->setMeshBasePath(meshPath.toStdString());
        qDebug() << "[SpatialView] Mesh path:" << meshPath;

        m_renderer->setCameraOrbit(m_cameraYaw, m_cameraPitch, m_cameraDistance);
        rebuildFixtures();
        rebuildEllipsoids();
        m_frameTimer.start(16);  // ~60Hz
        qDebug() << "[SpatialView] bgfx initialized" << w << "x" << h;
    }
    else
    {
        qWarning() << "[SpatialView] Failed to initialize bgfx";
    }
}

bool SpatialView::event(QEvent *event)
{
    if (event->type() == QEvent::Close)
    {
        // Save geometry, then just hide — don't destroy bgfx
        // (bgfx::init/shutdown can only be called once per process on Metal)
        QSettings settings;
        settings.setValue(SETTINGS_SPATIALVIEW_GEOMETRY, geometry());
        m_frameTimer.stop();
        hide();
        return true;  // consume the event — don't actually close/destroy
    }
    return QWindow::event(event);
}

void SpatialView::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    if (isExposed() && !m_bgfxReady)
        initBgfx();

    // Resume frame timer when re-shown
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

void SpatialView::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    if (event->button() == Qt::LeftButton)
        m_orbiting = true;
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
        m_panning = true;
}

void SpatialView::mouseMoveEvent(QMouseEvent *event)
{
    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

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
    Q_UNUSED(event);
    m_orbiting = false;
    m_panning = false;
}

void SpatialView::wheelEvent(QWheelEvent *event)
{
    float zoomDelta = -event->angleDelta().y() / 120.0f;
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
                            const rigmath::RigidTransform &t,
                            float r, float g, float b, float a,
                            const qlcrender::FixtureSceneGraph *sg = nullptr)
{
    qlcrender::RenderFixture rf;
    rf.id = id;
    rf.fixtureType = fixtureType;
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
            addFixtureEntry(fixtures, fxId, fxType, committed.value(),
                            1.0f, 0.6f, 0.2f, 1.0f, sg);  // orange solid
        }
        else
        {
            // New fixture (no committed) — show agentDerived as primary if present
            auto agent = sm->agentDerivedTransform(id);
            if (agent.has_value())
            {
                addFixtureEntry(fixtures, fxId, fxType, agent.value(),
                                0.2f, 0.9f, 0.3f, 0.8f, sg);  // green, slightly translucent
            }
            else
            {
                // Truly new — show at origin, cyan
                addFixtureEntry(fixtures, fxId, fxType,
                                rigmath::RigidTransform::identity(),
                                0.2f, 0.8f, 0.9f, 1.0f, sg);  // cyan
            }
            // For new fixtures, don't show separate ghosts — the primary is already the proposal
            continue;
        }

        // agentDerived ghost (green, translucent) — only if committed exists
        auto agentDerived = sm->agentDerivedTransform(id);
        if (agentDerived.has_value())
        {
            addFixtureEntry(fixtures, fxId, fxType, agentDerived.value(),
                            0.2f, 0.9f, 0.3f, 0.4f, sg);  // green ghost
        }

        // solverDerived ghost (cyan, translucent) — only if committed exists
        auto solverDerived = sm->solverDerivedTransform(id);
        if (solverDerived.has_value())
        {
            addFixtureEntry(fixtures, fxId, fxType, solverDerived.value(),
                            0.2f, 0.8f, 0.9f, 0.4f, sg);  // cyan ghost
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

    // Note: don't early-return on !hasSolverViz() — manual placements
    // have default uncertainty ellipsoids even without solver data.

    for (const QString &id : sm->fixtureIds())
    {
        SpatialModel::FixtureViz viz = sm->fixtureViz(id);

        // Skip if no ellipsoid data (all axes zero)
        if (viz.ellipsoidAxes[0] <= 0.0 && viz.ellipsoidAxes[1] <= 0.0 && viz.ellipsoidAxes[2] <= 0.0)
            continue;

        qlcrender::RenderEllipsoid ell;
        ell.fixtureId = id.toUInt();

        // Center at fixture position
        rigmath::RigidTransform t = sm->fixtureTransform(id);
        ell.center[0] = float(t.pos[0]);
        ell.center[1] = float(t.pos[1]);
        ell.center[2] = float(t.pos[2]);

        // Semi-axes: convert cm → meters
        ell.semiAxes[0] = float(viz.ellipsoidAxes[0] / 100.0);
        ell.semiAxes[1] = float(viz.ellipsoidAxes[1] / 100.0);
        ell.semiAxes[2] = float(viz.ellipsoidAxes[2] / 100.0);

        // Eigenvector rotation (row-major 3x3)
        for (int i = 0; i < 9; i++)
            ell.rotation[i] = float(viz.ellipsoidRot[i]);

        // Color by quality
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
        else  // unconstrained
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
    // Copy transform
    for (int i = 0; i < 16; i++)
        sceneNode.localTransform[i] = geoNode.localTransform[i];

    // Resolve mesh: prefer glTF model, fall back to primitive
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

    // Recurse
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

    // Build scene graph from geometry data
    qlcrender::FixtureSceneGraph graph;

    // Get the primitive gen from the renderer — we need a reference to the
    // BgfxRenderer's PrimitiveGen. Since SpatialView creates the renderer,
    // we can access it via a static helper on the renderer.
    // For now, use a local PrimitiveGen that shares the same bgfx context.
    static qlcrender::PrimitiveGen s_primGen;
    static bool s_primInit = false;
    if (!s_primInit)
    {
        s_primGen.init();
        s_primInit = true;
    }

    // Shared mesh cache for glTF models (persists across rebuilds)
    static std::unordered_map<std::string, qlcrender::LoadedMesh> s_gltfMeshCache;

    buildSceneNode(geoData->root, graph.root, geoData->meshData,
                   s_primGen, s_gltfMeshCache);
    graph.valid = true;

    auto result = m_sceneGraphCache.emplace(key, std::move(graph));
    return &result.first->second;
}
