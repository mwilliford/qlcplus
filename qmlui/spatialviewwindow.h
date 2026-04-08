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

#define SETTINGS_SPATIALVIEW_GEOMETRY "spatialview/geometry"

/** Show (or create) the Spatial View window. */
void showSpatialViewWindow(Doc *doc);

#endif // SPATIALVIEWWINDOW_H
