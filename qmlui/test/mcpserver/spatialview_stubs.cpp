/*
  Q Light Controller Plus - Unit test stubs
  spatialview_stubs.cpp

  Stub implementations of the spatial view free functions declared in
  spatialviewwindow.h. These replace the real implementations (which live in
  spatialviewwindow.cpp and require a running QWidget window) so the McpServer
  tests can run headlessly.
*/

#include "spatialview_stubs.h"

#include <QImage>
#include <QJsonArray>

// spatialviewwindow.h forward declarations (avoid pulling in the full header
// which requires Qt3D and creates QTransform collisions)
class Doc;
class SimpleDesk;

// ---------------------------------------------------------------------------
// Stub state
// ---------------------------------------------------------------------------

namespace McpTestStubs {

int     lastSelectedFixtureId     = -999;
bool    lastSelectAdd             = false;
int     lastGizmoMode             = -1;
QString lastAlignAxis;
int     lastSpatialMode           = -1;
float   lastCameraYaw             = 0;
float   lastCameraPitch           = 0;
float   lastCameraDistance        = 0;

QString   nextFocusPointId        = "fp0";
bool      focusPointOpSucceeds    = true;
QJsonArray focusPointsList;
QJsonArray fixtureScreenPositions;

void reset()
{
    lastSelectedFixtureId  = -999;
    lastSelectAdd          = false;
    lastGizmoMode          = -1;
    lastAlignAxis.clear();
    lastSpatialMode        = -1;
    lastCameraYaw          = 0;
    lastCameraPitch        = 0;
    lastCameraDistance     = 0;
    nextFocusPointId       = "fp0";
    focusPointOpSucceeds   = true;
    focusPointsList        = QJsonArray{};
    fixtureScreenPositions = QJsonArray{};
}

} // namespace McpTestStubs

// ---------------------------------------------------------------------------
// Stub implementations of spatialviewwindow.h free functions
// ---------------------------------------------------------------------------

void showSpatialViewWindow(Doc *) {}
void spatialViewSetSimpleDesk(SimpleDesk *) {}

QImage grabSpatialViewWindow()
{
    return QImage{}; // null → triggers "Spatial View not open" error in screenshot tool
}

void spatialViewSelectFixture(int32_t id)
{
    McpTestStubs::lastSelectedFixtureId = id;
    McpTestStubs::lastSelectAdd = false;
}

void spatialViewAddSelectedFixture(int32_t id)
{
    McpTestStubs::lastSelectedFixtureId = id;
    McpTestStubs::lastSelectAdd = true;
}

QJsonArray spatialViewGetFixtureScreenPositions()
{
    return McpTestStubs::fixtureScreenPositions;
}

void spatialViewSetCamera(float yaw, float pitch, float distance)
{
    McpTestStubs::lastCameraYaw      = yaw;
    McpTestStubs::lastCameraPitch    = pitch;
    McpTestStubs::lastCameraDistance = distance;
}

void spatialViewDrag(float, float, float, float, int) {}

void spatialViewSetGizmoMode(int mode)
{
    McpTestStubs::lastGizmoMode = mode;
}

void spatialViewAlignSelection(const QString &axis)
{
    McpTestStubs::lastAlignAxis = axis;
}

void spatialViewAddTruss(const QString &, double, double, double, double, double, double) {}

void spatialViewSetMode(int mode)
{
    McpTestStubs::lastSpatialMode = mode;
}

QString spatialViewCreateFocusPoint(double, double, double, const QString &)
{
    return McpTestStubs::nextFocusPointId;
}

bool spatialViewDeleteFocusPoint(const QString &)
{
    return McpTestStubs::focusPointOpSucceeds;
}

bool spatialViewMoveFocusPoint(const QString &, double, double, double)
{
    return McpTestStubs::focusPointOpSucceeds;
}

bool spatialViewRenameFocusPoint(const QString &, const QString &)
{
    return McpTestStubs::focusPointOpSucceeds;
}

bool spatialViewAssignFixtureToFocusPoint(const QString &, int)
{
    return McpTestStubs::focusPointOpSucceeds;
}

bool spatialViewUnassignFixtureFromFocusPoint(const QString &, int)
{
    return McpTestStubs::focusPointOpSucceeds;
}

bool spatialViewAimAtFocusPoint(const QString &)
{
    return McpTestStubs::focusPointOpSucceeds;
}

void spatialViewSelectFocusPoint(const QString &) {}

QJsonArray spatialViewGetFocusPoints()
{
    return McpTestStubs::focusPointsList;
}
