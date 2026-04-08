/*
  Q Light Controller Plus
  spatialview.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef SPATIALVIEW_H
#define SPATIALVIEW_H

#include <QWindow>
#include <QTimer>
#include <QPoint>
#include <memory>
#include <unordered_map>
#include <string>

namespace qlcrender {
    class SpatialRenderer;
    struct FixtureSceneGraph;
    struct Ray;
}
class SpatialModel;
class Doc;
class QLCFixtureDefCache;
struct GDTFGeometryData;

/**
 * @brief QWindow hosting the bgfx-based 3D spatial viewport.
 *
 * Uses QWindow (not QWidget) because bgfx renders directly into the
 * native window handle. Intended to be embedded in a QWidget layout
 * via QWidget::createWindowContainer().
 *
 * Does NOT manage its own lifecycle — the owning SpatialViewWindow
 * handles show/hide, geometry, and singleton semantics.
 */
class SpatialView : public QWindow
{
    Q_OBJECT
    Q_DISABLE_COPY(SpatialView)

public:
    explicit SpatialView(Doc *doc, QWindow *parent = nullptr);
    ~SpatialView();

    /** Start/stop the 60Hz frame timer (called by owning widget on show/hide). */
    void startRendering();
    void stopRendering();
    bool isRendering() const { return m_frameTimer.isActive(); }

    /** Request a screenshot of the bgfx viewport from the GPU framebuffer. */
    void requestViewportScreenshot();

    /** Retrieve the screenshot if ready. Returns empty QImage if not ready. */
    QImage takeViewportScreenshot();

    /** Select a fixture by ID (-1 to deselect). */
    void selectFixture(int32_t fixtureId);

    /** Get the renderer (for screen position projection, etc). */
    qlcrender::SpatialRenderer *renderer() const { return m_renderer.get(); }

    /** Set camera orbit and update view. */
    void setCameraOrbit(float yaw, float pitch, float distance);

    /** Get current camera state. */
    float cameraYaw() const { return m_cameraYaw; }
    float cameraPitch() const { return m_cameraPitch; }
    float cameraDistance() const { return m_cameraDistance; }

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void initBgfx();
    void rebuildFixtures();
    void rebuildEllipsoids();
    const qlcrender::FixtureSceneGraph *getOrBuildSceneGraph(
        const QString &manufacturer, const QString &model,
        const GDTFGeometryData *geoData);

    /** Convert mouse position to device-pixel coordinates for renderer. */
    void mouseToViewport(const QPoint &pos, float &mx, float &my,
                         uint32_t &vw, uint32_t &vh) const;

private slots:
    void onFrameTimer();
    void onSpatialTransformChanged(const QString &id);
    void onSolverVizChanged();

private:
    Doc *m_doc;
    std::unique_ptr<qlcrender::SpatialRenderer> m_renderer;
    QTimer m_frameTimer;
    bool m_bgfxReady = false;

    // Mouse interaction
    QPoint m_lastMousePos;
    QPoint m_pressPos;
    bool m_orbiting = false;
    bool m_panning = false;
    bool m_draggingGizmo = false;

    // Gizmo drag state
    float m_dragStartPos[3] = {0, 0, 0};  // fixture position at drag start

    // Default: front-of-house view (audience looking at stage)
    float m_cameraYaw = -90.0f;
    float m_cameraPitch = 30.0f;
    float m_cameraDistance = 10.0f;

    // GDTF scene graph cache: one per fixture def (manufacturer+model)
    std::unordered_map<std::string, qlcrender::FixtureSceneGraph> m_sceneGraphCache;
};

#endif // SPATIALVIEW_H
