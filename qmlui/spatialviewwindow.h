/*
  Q Light Controller Plus
  spatialviewwindow.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef SPATIALVIEWWINDOW_H
#define SPATIALVIEWWINDOW_H

//
// This header intentionally does NOT include <QWidget> to avoid
// the QTransform name collision between QtGui and Qt3DCore that
// exists in the qmlui target. Callers that only need to launch the
// Spatial View (e.g., ContextManager) include this header and call
// the free function below.
//
// The actual QWidget-derived class lives entirely in the .cpp file.
//

class Doc;
class QImage;
class QJsonArray;

#define SETTINGS_SPATIALVIEW_GEOMETRY "spatialview/geometry"

/** Show (or create) the Spatial View window. */
void showSpatialViewWindow(Doc *doc);

/**
 * Grab a screenshot of the Spatial View.
 * Composites the bgfx 3D viewport (GPU readback) with the QML panel (QWidget::grab).
 * Works even when the window is behind other windows.
 */
QImage grabSpatialViewWindow();

/** Select a fixture by ID in the Spatial View (-1 to deselect). */
void spatialViewSelectFixture(int32_t fixtureId);

/** Add a fixture to the current selection (Shift+click equivalent). */
void spatialViewAddSelectedFixture(int32_t fixtureId);

/**
 * Get fixture screen positions projected through the current camera.
 * Returns JSON array: [{id, screenX, screenY, visible, worldX, worldY, worldZ}]
 * Coordinates are in logical pixels relative to the SpatialView viewport.
 */
QJsonArray spatialViewGetFixtureScreenPositions();

/** Set camera by preset name ("foh", "top", "front", "side") or explicit values. */
void spatialViewSetCamera(float yaw, float pitch, float distance);

/** Simulate a mouse drag in the Spatial View viewport (logical pixels). */
void spatialViewDrag(float x1, float y1, float x2, float y2, int steps = 10);

/** Set gizmo mode: 0=Translate, 1=Rotate. */
void spatialViewSetGizmoMode(int mode);

/** Align all selected fixtures on an axis ("X", "Y", or "Z"). */
void spatialViewAlignSelection(const QString &axis);

/** Add a truss to the scene. */
void spatialViewAddTruss(const QString &name, double x1, double y1, double z1,
                          double x2, double y2, double z2);

/** Set the Spatial View panel mode: 0=Layout, 1=Calibrate, 2=Focus, 3=Live. */
void spatialViewSetMode(int mode);

#endif // SPATIALVIEWWINDOW_H
