# Handoff: SV-4 Phase 2 — Focus Panel UI + Mouse Interaction

**Context**: SV-4 Phase 1 shipped 2026-04-16 — see PR #1 in mwilliford/qlcplus and `docs/handoff-gdtf-rendering-fixes.md` for background. Phase 1 delivered the FocusPoint data model, rendering, `SpatialController` Q_INVOKABLE API, and 9 MCP tools. Everything is functional end-to-end via MCP; no user-facing UI yet.

**Goal for Phase 2**: expose FocusPoint creation/editing through the QML Focus panel and add mouse interaction in the Spatial View so a user never needs MCP.

---

## What Phase 1 already gives you

### Data model (`engine/src/spatialmodel.h`)
```cpp
struct FocusPoint {
    QString id;                        // e.g. "fp0"
    QString name;                      // display name
    double position[3] = {0,0,0};      // meters, world-space
    QStringList assignedFixtureIds;    // fixture ids (as strings)
};
```

Signal: `SpatialModel::focusPointsChanged()` — emitted on any mutation.

### Controller API (`qmlui/spatialcontroller.h`) — all `Q_INVOKABLE`
```cpp
QString createFocusPoint(double wx, double wy, double wz, const QString &name = QString());
void    deleteFocusPoint(const QString &id);
void    moveFocusPoint(const QString &id, double wx, double wy, double wz);
void    renameFocusPoint(const QString &id, const QString &name);
bool    assignFixtureToFocusPoint(const QString &fpId, int fixtureId);
bool    unassignFixtureFromFocusPoint(const QString &fpId, int fixtureId);
void    aimAtFocusPoint(const QString &id);
QString selectedFocusPointId() const;
void    setSelectedFocusPointId(const QString &id);
```

Signal: `selectedFocusPointChanged()`.

### Rendering (`render/src/bgfxrenderer.cpp`)
- Amber sphere + billboard-text label (with `[N]` assignment count) already rendering
- `hitTestFocusPoint(mouseX, mouseY, w, h)` — screen-space hit test, returns id (empty if no hit)
- Selection highlight auto-applied when `SpatialController::selectedFocusPointId()` matches

### MCP tools (for scripted testing)
9 tools on `localhost:9876/mcp`: `create_focus_point`, `list_focus_points`, `delete_focus_point`, `move_focus_point`, `rename_focus_point`, `assign_fixture_to_focus_point`, `unassign_fixture_from_focus_point`, `aim_at_focus_point`, `select_focus_point`.

---

## Phase 2 scope

### 1. Focus panel QML section
**File**: `qmlui/qml/spatial/LayoutPanel.qml`

**Pattern to copy**: the Workspace/Truss section that appears in mode === 0. There's already a mode === 1 section for Calibrate; add a mode === 2 section for Focus.

**Content needed**:
- Header ("Focus Points")
- "+ Add Focus Point" button — calls `spatialController.createFocusPoint(0, 0, 1.5)` to create at stage center, 1.5 m up
- ListView of focus points — reads from a QML-exposed model (see below)
- Each row: selected-indicator dot, name (editable on double-click), position (3 CompactSpin inputs in meters), assigned-fixtures badge `[N]`, Aim button, Delete ✕
- "Assign selected fixture(s)" button — iterates current fixture selection and calls `assignFixtureToFocusPoint` for the selected focus point

**Exposing the model to QML**: `SpatialController` currently returns `QList<FocusPoint>` only via non-invokable `spatialModel->focusPoints()`. For QML binding, add:
```cpp
Q_PROPERTY(QVariantList focusPoints READ focusPointsList NOTIFY focusPointsChanged)
QVariantList focusPointsList() const;  // returns list of {id, name, x, y, z, assignedCount, selected}
```
Emit `focusPointsChanged` by connecting to `SpatialModel::focusPointsChanged` in the constructor.

### 2. Click-to-select focus point
**File**: `qmlui/spatialview.cpp` — `mousePressEvent()`

**Current hit-test order** (Focus mode, line ~243):
1. If focus mode → `rayIntersectsPlaneZ` → fire `m_focusAimCallback` (ephemeral aim)

**New order**:
1. `hitTestFocusPoint(mouseX, mouseY, w, h)` — if hit, call `spatialController.setSelectedFocusPointId(id)` and stop
2. Existing fixture AABB hit-test — if hit, select fixture and stop
3. If shift held → `createFocusPoint(x, y, z)` at the ray-plane intersection (the "Shift+click creates" flow)
4. Otherwise → existing ephemeral aim flow

Note: `SpatialController` isn't currently accessible from `SpatialView`. Either (a) add a callback like `m_selectedFocusPointSetCallback` and wire in `spatialviewwindow.cpp`, or (b) pass a `SpatialController*` to the view. Option (a) matches the existing pattern.

### 3. Drag selected focus point to move
**File**: `qmlui/spatialview.cpp` — `mouseMoveEvent()`

If a focus point is selected AND the mouse is being dragged (`m_focusDragging` pattern) → unproject to world plane, call `moveFocusPoint(id, x, y, z)` each frame. Same truss-snap + grid-snap filters as fixture drags (see `m_snapCallback`).

### 4. Tardis undo
**Files**: `qmlui/tardis/tardis.h` + `.cpp`, `qmlui/spatialcontroller.cpp`

**Pattern to copy**: `Tardis::SpatialFixtureSetTransform` action. Enqueue before writing new state; undo swaps old↔new.

**New action types**:
- `FocusPointAdded` — undo: `deleteFocusPoint(id)`; redo: re-add with stored name + pos
- `FocusPointRemoved` — inverse of added
- `FocusPointMoved` — enqueue before `moveFocusPoint`, store old/new position
- `FocusPointRenamed` — store old/new name
- `FocusPointAssignmentChanged` — store fixtureId + boolean was-assigned

Enqueue points live in the `SpatialController` Q_INVOKABLE methods (same way `setPosX` already does).

---

## Files to touch (summary)

| File | Change |
|---|---|
| `qmlui/qml/spatial/LayoutPanel.qml` | Add mode === 2 section with Focus Points ListView |
| `qmlui/spatialcontroller.h` / `.cpp` | Add `focusPoints` `Q_PROPERTY` + QVariantList getter; enqueue Tardis actions |
| `qmlui/spatialview.h` / `.cpp` | Hit-test order in `mousePressEvent`; drag-move handling in `mouseMoveEvent`; `setSelectedFocusPointSetCallback` |
| `qmlui/spatialviewwindow.cpp` | Wire the new callback + connect `selectedFocusPointChanged` to view's rebuild |
| `qmlui/tardis/tardis.h` / `.cpp` | Add 5 new action types |
| `qmlui/tardis/tardis.cpp` (`execute`) | Undo/redo handlers for the 5 new actions |

Estimate: ~300 LOC across ~6 files.

---

## Testing strategy

### Unit tests (C++)
Nothing new for the data model — Phase 1 covered it. Add controller-level test if the Q_INVOKABLE `focusPointsList()` shape is non-trivial.

### Visual smoke test (via MCP)
The 9 MCP tools already work. After Phase 2, you can additionally verify:
- Click in Spatial View → `select_focus_point` state updates (use `list_focus_points` to check `selected` field)
- Shift+click → a new focus point appears (use `list_focus_points` to verify count)

### Visual smoke test (via UI)
1. Launch with `proj1.qxw` (has 3 fixtures, 1 scanner useful for aim)
2. Open Spatial View, switch to Focus mode
3. "+ Add Focus Point" → sphere appears at world origin
4. Double-click name → rename
5. Adjust X/Y/Z spinboxes → sphere moves
6. Select fixture 1 → "Assign selected fixture" → `[1]` badge appears
7. "Aim" → beam swings through sphere
8. Save `.bhx` → reload → verify focus point persists

---

## Things NOT in Phase 2 scope

- Multi-plane targeting (walls) — that's Phase 3; uses `rigmath::Beam::hit_plane(point, normal)` from v1.1
- Fan/spread controls — Phase 3
- Auto shutter/dimmer on Focus entry — Phase 3
- Speed-limited aim ramp — Phase 3

Keep Phase 2 tight. Polish items are Phase 3 for a reason.

---

## Gotchas

- **Hit-test ordering**: make sure focus points win over fixtures. A user selecting a focus point expects to move/delete it, not the fixture behind it.
- **Shift+click semantics**: today Shift+click on a fixture multi-selects. Shift+click on empty space currently doesn't do anything. The "create focus point" rule only fires when the Shift+click misses everything; don't break multi-select.
- **QML rebuild on selection**: the renderer already re-reads `selectedFocusPointCallback` during `rebuildFocusPoints()`. SpatialController must trigger this rebuild when selection changes — Phase 1 already wired that in `spatialviewwindow.cpp`, just verify it fires.
- **Z-plane for Shift+click**: default to `z=0` (floor) unless a focus point is already selected, in which case inherit its z. Document this in the QML section.
- **Disabled state**: when not in Focus mode, the focus panel section is hidden (mode === 2 binding). Don't try to gate individual features — just hide the whole section.
