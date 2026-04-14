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
#include <QHash>
#include <QByteArray>
#include <memory>
#include <functional>
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
struct AxisDofTag;

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

    /** Set a callback that fires when the user clicks to select/deselect a fixture.
     *  Args: primaryFixtureId, selectionCount */
    void setSelectionCallback(std::function<void(int32_t, int)> cb) { m_selectionCallback = std::move(cb); }

    /** Set snap callback: returns snapped position given raw position. */
    void setSnapCallback(std::function<void(double &x, double &y, double &z)> cb) { m_snapCallback = std::move(cb); }

    /** Set gizmo mode callback: getter returns 0=Translate, 1=Rotate. */
    void setGizmoModeCallback(std::function<int()> getter, std::function<void(int)> setter) {
        m_gizmoModeCallback = std::move(getter);
        m_gizmoModeSetCallback = std::move(setter);
    }

    /** Set focus-mode predicate callback. Returns true when in Focus mode. */
    void setFocusModeCallback(std::function<bool()> cb) { m_focusModeCallback = std::move(cb); }

    /** Set Focus aim commit callback — SpatialView calls it on mouse click/drag. */
    void setFocusAimCallback(std::function<void(double, double, double)> cb) {
        m_focusAimCallback = std::move(cb);
    }

    /** Set live-DMX predicate: true in Calibrate or Focus modes — beam cones
     *  use live DMX values instead of home position. */
    void setLiveDmxModeCallback(std::function<bool()> cb) { m_liveDmxModeCallback = std::move(cb); }

    /** Get current camera state. */
    float cameraYaw() const { return m_cameraYaw; }
    float cameraPitch() const { return m_cameraPitch; }
    float cameraDistance() const { return m_cameraDistance; }

    /** Rebuild the beam cones (public for mode change triggers). */
    void rebuildBeamCones();

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void initBgfx();
    void rebuildFixtures();
    void rebuildFixtureDofs();
    void rebuildEllipsoids();
    void rebuildTrusses();
    void rebuildObservationLines();
    const qlcrender::FixtureSceneGraph *getOrBuildSceneGraph(
        const QString &manufacturer, const QString &model,
        const QString &modeName,
        const GDTFGeometryData *geoData,
        const std::vector<AxisDofTag> &axisTags);

    /** Convert mouse position to device-pixel coordinates for renderer. */
    void mouseToViewport(const QPoint &pos, float &mx, float &my,
                         uint32_t &vw, uint32_t &vh) const;

    /** Enqueue one Tardis undo action per dragged fixture, comparing
     *  m_dragStartTransforms against current SpatialModel state. */
    void enqueueSpatialUndoActions();

private slots:
    void onFrameTimer();
    void onSpatialTransformChanged(const QString &id);
    void onSolverVizChanged();
    void onUniverseWritten(quint32 universeId, const QByteArray &data);

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
    bool m_draggingRotate = false;
    bool m_focusDragging = false;

    // Gizmo drag state
    float m_dragStartPos[3] = {0, 0, 0};  // primary fixture position at drag start
    std::unordered_map<int32_t, double[3]> m_dragStartPositions;  // all selected fixtures' start positions
    // Full transforms at drag start — used for undo (captures both position and rotation)
    std::unordered_map<int32_t, double[6]> m_dragStartTransforms;  // [x,y,z,rx,ry,rz] per fixture

    // Default: front-of-house view (audience looking at stage)
    float m_cameraYaw = -90.0f;
    float m_cameraPitch = 30.0f;
    float m_cameraDistance = 10.0f;

    // Selection callback (fired on click-select/deselect): primaryId, count
    std::function<void(int32_t, int)> m_selectionCallback;

    // Snap callback (applies grid snap to position)
    std::function<void(double &, double &, double &)> m_snapCallback;

    // Gizmo mode callbacks (0=Translate, 1=Rotate)
    std::function<int()> m_gizmoModeCallback;
    std::function<void(int)> m_gizmoModeSetCallback;

    // Focus mode callbacks
    std::function<bool()> m_focusModeCallback;
    std::function<void(double, double, double)> m_focusAimCallback;
    std::function<bool()> m_liveDmxModeCallback;

    // Universe DMX snapshots (deep-copied from InputOutputMap::universeWritten)
    QHash<quint32, QByteArray> m_universeSnapshots;

    // GDTF scene graph cache: one per fixture def (manufacturer+model)
    std::unordered_map<std::string, qlcrender::FixtureSceneGraph> m_sceneGraphCache;

    // Synthesized GDTF geometry for QXF fixtures (no real GDTF data)
    std::unordered_map<std::string, std::unique_ptr<GDTFGeometryData>> m_synthesizedGeoCache;
};

#endif // SPATIALVIEW_H
