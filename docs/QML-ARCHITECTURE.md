# QML Architecture Guide

How the Spatial View QML panels bind to C++ controllers. Reference this before
writing any new QML panel or adding properties to `SpatialController`.

---

## Overview

The Spatial View uses a thin C++ controller layer exposed to QML via Qt's context
property mechanism. QML files never touch engine or render objects directly — they
go through `SpatialController` (layout/focus/live) or `CalibrateController`
(calibration).

```
BgfxRenderer (render layer)
      │  selection callback
      ▼
SpatialViewWindow (qmlui)
      │  setContextProperty
      ▼
QQmlEngine (side panel)
      │  context property "spatialController"
      ▼
LayoutPanel.qml
```

---

## Context Property Registration

Both controllers are registered in `SpatialViewWindow::initSidePanel()`
(`spatialviewwindow.cpp`, line 211):

```cpp
m_sidePanel->rootContext()->setContextProperty("spatialController", m_controller);
m_sidePanel->rootContext()->setContextProperty("calibrateController", m_calibrateController);
```

QML files access them directly by name — no import needed:

```qml
// spatialController is available in any QML file loaded by m_sidePanel
onClicked: spatialController.mode = 0
```

---

## SpatialController API

### Q_PROPERTY Reference

All properties emit a notify signal. QML binds to them with standard property
binding syntax.

**Selection**

| Property | Type | Notify | Description |
|----------|------|--------|-------------|
| `selectedFixtureId` | `int` | `selectionChanged` | Primary selected fixture ID, -1 if none |
| `selectedFixtureName` | `QString` | `selectionChanged` | Display name of primary selected fixture |
| `hasSelection` | `bool` | `selectionChanged` | True when at least one fixture selected |
| `selectionCount` | `int` | `selectionChanged` | Number of currently selected fixtures |

**Position** (meters, world space)

| Property | Type | Notify | Description |
|----------|------|--------|-------------|
| `posX` | `double` | `transformChanged` | World X of primary selection |
| `posY` | `double` | `transformChanged` | World Y of primary selection |
| `posZ` | `double` | `transformChanged` | World Z of primary selection |

**Rotation** (degrees, Euler)

| Property | Type | Notify | Description |
|----------|------|--------|-------------|
| `rotPitch` | `double` | `transformChanged` | Pitch |
| `rotYaw` | `double` | `transformChanged` | Yaw |
| `rotRoll` | `double` | `transformChanged` | Roll |

**Mode**

| Property | Type | Notify | Values |
|----------|------|--------|--------|
| `mode` | `int` | `modeChanged` | `Layout=0`, `Calibrate=1`, `Focus=2`, `Live=3` |

**Grid / Axis / Gizmo**

| Property | Type | Notify | Description |
|----------|------|--------|-------------|
| `gridSnap` | `bool` | `gridSnapChanged` | Snap to grid when dragging |
| `gridSize` | `double` | `gridSizeChanged` | Grid cell size in meters (default 0.5) |
| `axisMode` | `int` | `axisModeChanged` | `0=World`, `1=Local` |
| `gizmoMode` | `int` | `gizmoModeChanged` | `0=Translate`, `1=Rotate` |

### Q_INVOKABLE Methods

```cpp
// Align all selected fixtures on an axis to primary fixture's coordinate.
// axis: "X", "Y", or "Z"
void alignSelection(const QString &axis);

// Returns all currently selected fixture IDs.
QVariantList selectedFixtureIds() const;

// Add a default 6m horizontal truss at Z=3m.
void addDefaultTruss();

// Remove a truss by its string id.
void removeTruss(const QString &id);

// Jump camera to a named preset.
// preset: "foh", "top", "front", "side" (case-insensitive)
void setCameraPreset(const QString &preset);

// Aim selected fixtures at a world point (meters). Runs IK, emits focusDmxWrite.
void setFocusAim(double wx, double wy, double wz);

// Release DMX channels held by Focus mode. Emits focusDmxReset per channel.
void clearFocusAim();

// Focus point management — all positions in world meters.
QString createFocusPoint(double wx, double wy, double wz, const QString &name = {});
void    deleteFocusPoint(const QString &id);
void    moveFocusPoint(const QString &id, double wx, double wy, double wz);
void    renameFocusPoint(const QString &id, const QString &name);
bool    assignFixtureToFocusPoint(const QString &fpId, int fixtureId);
bool    unassignFixtureFromFocusPoint(const QString &fpId, int fixtureId);
void    aimAtFocusPoint(const QString &id);
void    setSelectedFocusPointId(const QString &id);
```

### Signals

```cpp
void selectionChanged();
void transformChanged();
void modeChanged();
void gridSnapChanged();
void gridSizeChanged();
void axisModeChanged();
void gizmoModeChanged();
void focusDmxWrite(uint absChannel, uchar value);   // Focus mode: write DMX channel
void focusDmxReset(uint absChannel);                 // Focus mode: release channel
void focusAimChanged();
void selectedFocusPointChanged();
```

---

## Binding Patterns

### One-way read (property → QML display)

```qml
text: "X: " + spatialController.posX.toFixed(2) + " m"
visible: spatialController.hasSelection
```

### Two-way with `updating` guard

SpinBoxes need a guard to avoid a feedback loop (user edits → C++ property
changes → signal fires → spinbox updates → another edit):

```qml
property bool updating: false

Connections {
    target: spatialController
    function onTransformChanged() {
        posXSpin.updating = true
        posXSpin.value = Math.round(spatialController.posX * 100)
        posXSpin.updating = false
    }
}

CompactSpin {
    id: posXSpin
    onValueModified: {
        if (!updating)
            spatialController.posX = value / 100.0
    }
}
```

### Mode-conditional visibility

```qml
// Only show in Layout mode
visible: spatialController.mode === 0

// Only show when something is selected
visible: spatialController.hasSelection
```

### Calling invokables

```qml
Button {
    text: "Align X"
    onClicked: spatialController.alignSelection("X")
}

Button {
    text: "Add Truss"
    onClicked: spatialController.addDefaultTruss()
}
```

---

## Mode Switching

Mode is a plain integer property. Enum values are defined in C++ but not exposed
to QML as named constants — use literals:

```qml
// Four-button row for mode selector
Repeater {
    model: ["Layout", "Calibrate", "Focus", "Live"]
    Button {
        text: modelData
        checked: spatialController.mode === index
        onClicked: spatialController.mode = index
    }
}
```

Mode changes trigger `modeChanged`. Leaving Focus mode automatically calls
`clearFocusAim()` in C++ to release any held DMX channels — QML doesn't need
to handle this.

---

## Fixture Selection Flow

1. User clicks in bgfx viewport (`SpatialView`)
2. Renderer fires selection callback → `SpatialViewWindow` receives it:
   ```cpp
   m_spatialView->setSelectionCallback([this](int32_t fixtureId, int count) {
       m_controller->notifySelectionChanged(fixtureId, count);
   });
   ```
3. `notifySelectionChanged()` updates `m_selectedFixtureId` + `m_selectionCount`,
   calls `updateTransformFromModel()`, then emits `selectionChanged()` and
   `transformChanged()`.
4. QML `Connections` blocks react to both signals — the properties panel refreshes
   its spinboxes and the header updates the fixture name.

**Multi-select (Shift+click):** renderer calls `addSelectedFixture()`. The
`selectedIdsCallback` lambda returns all IDs from `renderer()->selectedIds()`.
`selectionCount` reflects the total; `selectedFixtureId` reflects only the
primary (first) fixture for the properties panel.

---

## Adding a New Panel

1. Add properties/invokables to `SpatialController` (`.h` + `.cpp`).
2. Register any additional data models via `setContextProperty` in
   `SpatialViewWindow::initSidePanel()`.
3. Create `qmlui/qml/spatial/YourPanel.qml`. Add mode-conditional visibility.
4. Wire it into `LayoutPanel.qml` under the correct mode section.
5. Use `Connections { target: spatialController; function onYourSignal() {} }`
   for C++ → QML updates.
6. Use `onYourInvokable: spatialController.yourMethod(...)` for QML → C++ calls.
