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

namespace qlcrender { class SpatialRenderer; }
class SpatialModel;
class Doc;

#define SETTINGS_SPATIALVIEW_GEOMETRY "spatialview/geometry"

/**
 * @brief Floating QWindow hosting the bgfx-based 3D spatial view.
 *
 * Uses QWindow (not QWidget) to avoid QTransform name collision between
 * QtGui and Qt3DCore in the qmlui target. bgfx renders directly into
 * the native window handle.
 */
class SpatialView : public QWindow
{
    Q_OBJECT
    Q_DISABLE_COPY(SpatialView)

public:
    static SpatialView *instance() { return s_instance; }
    static void createAndShow(Doc *doc);
    ~SpatialView();

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;

private:
    explicit SpatialView(Doc *doc);

    void initBgfx();
    void rebuildFixtures();
    void rebuildEllipsoids();

private slots:
    void onFrameTimer();
    void onSpatialTransformChanged(const QString &id);
    void onSolverVizChanged();

private:
    static SpatialView *s_instance;

    Doc *m_doc;
    std::unique_ptr<qlcrender::SpatialRenderer> m_renderer;
    QTimer m_frameTimer;
    bool m_bgfxReady = false;

    // Mouse interaction
    QPoint m_lastMousePos;
    bool m_orbiting = false;
    bool m_panning = false;

    float m_cameraYaw = 45.0f;
    float m_cameraPitch = 30.0f;
    float m_cameraDistance = 10.0f;
};

#endif // SPATIALVIEW_H
