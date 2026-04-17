/*
  Q Light Controller Plus - Unit test stubs
  spatialview_stubs.h

  Stub state for spatial view free functions used by McpServer.
  Tests read/write these globals to control stub behavior and verify calls.
*/

#ifndef SPATIALVIEW_STUBS_H
#define SPATIALVIEW_STUBS_H

#include <QString>
#include <QJsonArray>

namespace McpTestStubs {

// Last values passed to each stub
extern int    lastSelectedFixtureId;
extern bool   lastSelectAdd;
extern int    lastGizmoMode;
extern QString lastAlignAxis;
extern int    lastSpatialMode;
extern float  lastCameraYaw;
extern float  lastCameraPitch;
extern float  lastCameraDistance;

// Focus point stubs
extern QString nextFocusPointId;      // returned by spatialViewCreateFocusPoint
extern bool   focusPointOpSucceeds;   // returned by delete/move/rename/assign/unassign/aim
extern QJsonArray focusPointsList;    // returned by spatialViewGetFocusPoints

// Other stubs
extern QJsonArray fixtureScreenPositions;

// Reset all stub state between tests
void reset();

} // namespace McpTestStubs

#endif // SPATIALVIEW_STUBS_H
