# TODO — AI QLC+ Client (C++)

## Phase 3: File Formats & Agent Context (PRD §6.1)

### .aqw Workspace Format
- [x] **AgentContext XML read/write on all objects**: `<AgentContext>` with `<UserNote>` and `<AgentNote>` on Fixture, Function (Scene, Chaser, EFX, Collection), and Doc (workspace level). Widget annotation deferred.
- [x] **Register .aqw file extension**: Open dialog shows both `.aqw` and `.qxw`. Save As defaults to `.aqw`.
- [x] **Save format logic**: Save As defaults to `.aqw`. Saving as `.qxw` strips agent context.
- [x] **Strip AgentContext on .qxw export**: `AgentContext::s_stripOnSave` flag skips writing when saving `.qxw`.

### .aqf Fixture Definition Format
- [x] **AgentContext on QLCFixtureDef**: `<AgentContext>` on QLCFixtureDef loadXML/saveXML.
- [x] **FixtureDefCache loads .aqf**: Scans for both `*.qxf` and `*.aqf` in user fixture directory.
- ~~Save fixture defs as .aqf~~ — **Deferred to Phase 3c** (fixture editor). No UI to edit fixture def notes yet; notes set by agent in memory are not persisted. Will save `.aqf` when fixture editor enables creating/editing definitions.

### workspace_sync with Agent Context
- [x] **Send AgentContext in workspace_sync**: Workspace, fixtures, fixture defs, and functions include agentContext in workspace_sync JSON.

## Phase 3: Integrated Fixture Editor (PRD §6.2)

**Deferred to v5 (QML).** QLC+ 5 already has an integrated fixture editor in QML. Building a Widgets version for v4 would be thrown away on migration. Users can use the standalone `qlcplus-fixtureeditor` for manual edits until then. Agent-driven fixture creation (from PDFs/vision) will also target v5.

## Phase 3: Sessions (PRD §6.3)

- [x] **Session index in .aqw**: AgentSession struct with sessionId, title, goals, createdAt in `<AgentContext>` XML.
- [x] **Collapsible session sidebar**: QListWidget with hamburger toggle, newest-first display.
- [x] **New session on first message**: Implicit creation, server returns session_id, stored in Doc.
- [x] **Resume session**: Double-click sidebar item → sendSessionResume → chat populates from history.
- [x] **Handle session not found**: expired flag checked, user notified, fresh session started.
- [x] **Delete session**: Right-click context menu → Doc::removeSession → sidebar refreshed.
- [x] **Agent updates session metadata**: handleUpdateSessionMetadata updates Doc, server tool wired.

## Phase 3: Adaptive Behavior (PRD §6.3)

- [x] **Agent updates AgentNote via command**: `update_agent_note` command handler updates AgentNote on fixture, fixtureDef, function, or workspace.

## QLC+ v5 Port — Remaining

### Agent Chat Panel (QML) — done
- [x] AgentConnection wired in App::startup(), exposed to QML
- [x] AgentChatPanel.qml — floating window, streaming, auth flow, cancel/queue
- [x] Script v4/v5 AgentContext save/load
- [x] .aqw/.aqf file extension support in v5 file dialogs and save logic
- [x] Destructor crash fix (WebSocket signals during shutdown)

### Session Sidebar — DONE (v5 QML)
- [x] Implemented in v5 QML port (see PLAN.md Phase C)

### Account Info — TODO
- [ ] **Show email when connected**: Decode JWT client-side (base64 payload) to extract `email` claim. Display in status bar (e.g., "Connected — user@example.com"). JWT also has `user_id`, `sub`, `groups`.
- [ ] **No balance display**: Users check the website for billing info. Don't show balance in the client.

### Notes UI — TODO
- [ ] **Workspace notes**: View/edit UserNote + read-only AgentNote for the workspace. Location TBD (chat panel section, or separate dialog).
- [ ] **Per-object notes**: Add notes section to fixture/function property editors in v5. Lower priority.

### Tardis Integration
- [x] **Undo for Spatial View gizmo drags**: Translate and rotate drags enqueue `SpatialFixtureSetTransform` on mouse release. Multi-fixture drags batch into one undo step.
- [x] **Undo for Properties panel edits**: Position (X/Y/Z) and rotation (Pitch/Yaw/Roll) changes enqueue undo before writing to SpatialModel.
- [x] **Ctrl+Z / Ctrl+Shift+Z in Spatial View**: Forwarded from SpatialView::keyPressEvent to Tardis.
- [ ] **Undo for agent mutations**: Call `Tardis::instance()->enqueueAction()` after agent commands (create_scene, etc.) so they're undoable. Requires wiring in App or AgentConnection.
- [ ] **Undo for calibration observation add/remove**: Enqueue when observations are added or removed in CalibrateController.
- [ ] **Undo for solver accept/dismiss**: Enqueue when solver results are accepted (committed transform changes) or dismissed.
- [ ] **Undo for truss add/remove**: Enqueue when trusses are added or removed via SpatialModel.

### Virtual Console — TODO
- [ ] **VC serializer for v5**: v5 has its own VirtualConsole implementation. May need adapted vcserializer/vccommandhandler or new approach using v5's VC API.

## SV-4 Focus Mode

### Phase 1: Focus Point object model — DONE (2026-04-16)
- [x] `SpatialModel::FocusPoint` (id, name, position, assignedFixtureIds) + API (add/remove/update/assign/unassign/lookup) + `focusPointsChanged` signal
- [x] `.aqw` XML persistence (`<FocusPoint>` + `<AssignedFixture>` children)
- [x] `RenderFocusPoint` + sphere-with-label rendering + screen-space hit-test in BgfxRenderer
- [x] `SpatialController` Q_INVOKABLE API: create, delete, move, rename, assign, unassign, aim, select
- [x] `aimAtFocusPoint(id)` reuses the click-to-aim IK helper (single source of truth)
- [x] 9 MCP tools: create_focus_point, list_focus_points, delete_focus_point, move_focus_point, rename_focus_point, assign_fixture_to_focus_point, unassign_fixture_from_focus_point, aim_at_focus_point, select_focus_point
- [x] Unit tests: 7 new SpatialModel tests (38 total, all passing)
- [x] End-to-end visual test verified through MCP: sphere renders, selection highlights, `[N]` assignment badge, beam visibly swings to target, DMX channels written

### Phase 2: Focus panel UI + mouse interaction — DONE (2026-04-16)
- [x] QML Focus panel (mode === 2) — list, create, rename, delete, fixture assignment UI
- [x] Click-to-select in Spatial View (wires `hitTestFocusPoint` from BgfxRenderer)
- [x] Drag selected focus point to move (on its z-plane, with grid/truss snap)
- [x] Shift+click empty space → create focus point at floor hit
- [x] Tardis undo actions for focus point mutations (add/remove/move/rename/assign)

### Phase 2 UX polish — DONE (2026-04-16)
- [x] Left-click orbit in Focus mode (uniform camera across all modes)
- [x] Hold-F gesture for ephemeral aim (replaces click-to-aim)
- [x] `selectedFocusPointId` promoted to Q_PROPERTY (QML reactivity fix)
- [x] "Assign selected fixture" button styled blue when enabled
- [x] Highlight (H key): industry-standard toggle that opens shutter and sets
      dimmer to 100% on the selected fixtures (or the selected focus point's
      assigned fixtures if no fixture is selected). Follows selection changes.
      Auto-clears on Focus mode exit.

### Phase 3: Polish — TODO
- [ ] **Commit aim / highlight state to Scene** — Focus mode's aim + highlight
      are live DMX overrides that release on mode exit. Add a "Save as Scene"
      button in the Focus panel that captures the current override state as a
      QLC+ Scene (SceneValue per channel), so the aim persists across modes
      and can be played back later. Required for practical use: after aiming,
      the user needs a way to keep the beam where they put it.
- [ ] Multi-plane targeting (walls, custom planes) via `rigmath::Beam::hit_plane(point, normal)`
- [ ] Fan/spread controls for multi-fixture aim
- [ ] Speed-limited aim ramp (smooth DMX transition instead of instant jump)
- [ ] Calibration verification workflow (aim all at one point, check convergence)
- [ ] **Per-fixture highlight pin** — optional: an alternative to the current
      snapshot-at-toggle semantics, letting each fixture have an independent
      "keep lit" state that ignores both selection AND highlight-toggle. Matches
      grandMA's stage-lock concept. Only needed if the snapshot approach proves
      too coarse in practice.

### Known bugs (tracked elsewhere)
- [ ] IK branch-selection bug in rigmath: `KinematicChain::inverse_world` may
      pick the 180°-flipped solution when the target is far from current pose.
      Likely cause: residual treats the beam as an infinite line rather than a
      forward ray. Same class of fix as the v1.1 AimFactor forward-ray fix but
      for IK instead of calibration. Addressed separately in rigmath repo.

## Future: Live Attribute Bank (Focus mode)

Live encoders/sliders for common non-position attributes on selected fixtures —
the industry-standard "attribute bank" concept (grandMA preset types, Hog
parameter banks, Eos CIA). Overlaps with scene programming; needs proper
design work before building.

- Category tabs in Focus panel: Intensity, Color, Beam, Gobo, Shutter, Shape
- Show attributes in the intersection of selected fixtures (skip non-applicable)
- Encoders write live DMX via SimpleDesk overrides (same mechanism as Highlight)
- Percentage display + capability name ("Gobo 3 — Swirl") for readability
- Question to resolve: does "release" write back or reset? Hold-vs-latch UX?

## Future: Palette / Preset System

Save named attribute states (e.g., "Red", "Gobo Swirl", "Open White") and
recall them on selected fixtures. Converges our tool with commercial console
paradigms. Integrates with focus points: a focus point could optionally carry
a palette reference so "aim at Point 1" also sets color/gobo/intensity.

- `.aqw` persistence: `<Palette>` elements with per-attribute values
- Palette editor UI + "apply to selection" action
- Focus point optionally references a palette (composition, not inheritance)
- Future hook for cue system: palette + aim + time = a cue

## SV-5: Live Mode + Venue Import

### Passive DMX Visualizer Mode

Receive Art-Net/sACN from an external console and render the 3D result — no show programming needed. Nearly free since all pieces exist.

- [ ] **Visualizer mode toggle in SpatialController**: `setVisualizerMode(bool)` — when true, stop engine playback and set all universes to passthrough
- [ ] **Universe passthrough wiring**: `Universe::setPassthrough(true)` already exists. Add UI flow: "Listen on Universe N" → configure artnet/E1.31 input plugin + enable passthrough for that universe
- [ ] **SpatialView Live mode reads universe buffer**: on each frame, read `Universe::postGMValues()` → translate DMX bytes → update fixture geometry via kinematic chain
- [ ] **No fixture engine changes needed**: passthrough + existing input plugins is sufficient

### MVR Non-Fixture Elements

When importing an MVR, beyond Fixture nodes:

- [ ] **FocusPoint** — required. Fixture nodes reference FocusPoints by UUID (`<Focus>` field). Parse and resolve UUIDs; store in SpatialModel. Render as a small marker in SpatialView.
- [ ] **GroupObject** — parse for hierarchy traversal; nothing to render. Needed for scene graph reconstruction.
- [ ] **Truss / Support / SceneObject** — static 3D geometry. Each references an embedded 3D model file (glTF/3DS in the MVR ZIP). Extract from ZIP, load via MeshLoader, add to bgfx scene as non-interactive static mesh. Skip if glTF-only for now (defer 3DS/OBJ venue geometry to later).
- [ ] **VideoScreen / Projector** — ignore for now.

### MVR Test Files

- [ ] **Synthetic test MVR**: write a generator using libMVRgdtf write API (see `build-v5/_deps/libmvrgdtf-src/unittest/MvrUnittest.cpp` for pattern). Creates 2-3 GDTF fixtures + 1 FocusPoint + 1 truss SceneObject.
- [ ] **Real-world test MVR**: export from grandMA3 onPC (free download) or grab from BlenderDMX GitHub repo sample scenes.

## Bugs: Chat panel

- [x] **Input disabled during streaming**: Fixed in v5 QML — input always enabled, send during streaming does implicit cancel.

## Bugs: Connection

- [ ] **Segfault on WebSocket 403 rejection**: v5 client crashes when the WebSocket upgrade is rejected (e.g., wrong URL path, auth failure). The auth-refresh-on-disconnect path dereferences a destroyed QTcpSocket (`QObject::disconnect: wildcard call disconnects from destroyed signal of QTcpSocket::unnamed`). Likely null pointer in AgentAuthManager or AgentConnection reconnect logic after socket teardown.

## Bugs: Missing signal handlers (deletion safety)

- [ ] **RGBMatrix: fixtureGroupRemoved** — RGBMatrix stores raw `m_group` pointer
  (rgbmatrix.h:152) and `m_fixtureGroupID` but does NOT connect to
  `Doc::fixtureGroupRemoved`. Deleting a fixture group while an RGBMatrix references it
  causes a dangling pointer crash when the matrix accesses `m_group->fixtureList()`.
  Fix: connect signal, clear `m_group`/`m_fixtureGroupID`, invalidate algorithm state.
  **Workaround:** Server prompt instructs agent to delete RGBMatrix before its group.

- [ ] **VCSlider: functionRemoved for playbackFunction** — VCSlider stores
  `m_playbackFunction` (vcslider.h:411) but does NOT connect to `Doc::functionRemoved`.
  Deleting a function bound as a slider's playback function leaves a stale ID.
  Fix: connect signal, reset `m_playbackFunction` to `Function::invalidId()` when match.

## Bugs: UI not refreshing on agent-initiated changes

- [x] **FunctionManager: functionRemoved** — tree didn't update when agent deleted functions. Fixed: connected Doc::functionRemoved → updateTree().
- [x] **FixtureManager: fixtureAdded, fixtureChanged** — connected Doc::fixtureAdded/fixtureChanged → updateTree(). Also added fixtureGroupAdded → updateTree() (was missing in upstream QLC+).
- [x] **VCMatrix: functionRemoved** — dangling pointer. Fixed: connected Doc::functionRemoved → setFunction(invalidId()).
- [x] **VCXYPad: functionRemoved** — dangling raw pointer crash. Fixed: stopAndWait + null pointer + preset cleanup.
- [x] **VCSpeedDial: functionRemoved** — stale function ID list. Fixed: remove matching entries on signal.
- [x] **ChaserEditor: functionRemoved** — crash on Q_ASSERT(function != NULL). Fixed: connected signal, null-guard, tree rebuild.
- [x] **CollectionEditor: functionRemoved** — same crash pattern. Fixed: connected signal, null-guard, list rebuild.

## Workspace Serialization Completeness (QXW ↔ JSON bijection)

**Goal**: The JSON workspace_sync should contain everything the QXW stores in the `<Engine>` section. This enables:
1. Full DAG traversal (inspect_connections) — chaser steps, collection members, EFX fixtures
2. Bug report capture — `/bug` command saves workspace JSON, support can reconstruct QXW
3. Serializer completeness proof — round-trip test catches missing fields

**Core function types serialized.** Remaining gaps listed below.

### Missing from workspace_sync — function types not yet serialized
- [ ] **Script**: `scriptData` (command text referencing functions by name), `scriptDir`
- [ ] **Show**: timeline tracks with function references, timing data
- [ ] **Audio**: `sourceFileName`, `fadeIn/fadeOut`, `audioDevice`
- [ ] **Video**: `sourceFileName`, `screen`, `fullscreen`, `resolution`

These function types are handled as basic FunctionSummary (id/name/type/path/tempoType) but their type-specific fields are missing. The agent can see they exist but can't inspect or create them.

### Missing from workspace_sync — non-function objects
- [x] **Virtual Console widgets**: All 11 widget types serialized with function/fixture references, layout, and input sources. Recursive Frame/SoloFrame children. Via `vcserializer.h/.cpp` (UI layer) + `std::function` callback bridge.
- [x] **Input/Output mappings**: Universe → plugin/device/profile bindings + available input profiles. Via `serializeInputOutputMap()` in AgentConnection.
- [ ] **Input profiles**: Full MIDI controller channel definitions. Currently only name/manufacturer/model/type sent (not full channel maps).

### Step 1: Complete `serializeFunction()` — all function types
- [x] **Chaser**: `steps: [{functionId, fadeIn, hold, fadeOut, duration, note}]` from `chaser->steps()`
- [x] **Collection**: `memberFunctionIds: [int]` from `collection->functions()`
- [x] **EFX**: `efxFixtures: [{fixtureId, headIndex, startOffset}]` from EFX fixture list, plus algorithm/width/height/rotation/xOffset/yOffset/startOffset/propagationMode
- [x] **Sequence**: `boundSceneId: int` from `sequence->boundSceneID()`, plus steps with per-step values
- [x] **RGBMatrix**: fixtureGroupId, algorithmName, colors
- [x] **All types**: `tempoType` from `fn->tempoType()`

### Step 2: Complete other serialize methods
- [x] **serializeFixture()**: Add `forcedHTP`, `forcedLTP` arrays
- [x] **serializeStageLayout()**: Add `pointOfView`, `stageType`
- [x] **buildWorkspaceSync()**: Serialize real `fixtureGroups` via `serializeFixtureGroups()`, real `universeCount` from `m_doc->inputOutputMap()->universes()`

### Step 3: QTest — load QXW, serialize to JSON, validate completeness
- [x] **New test**: `serializeSampleWorkspace` in agentcontext_test
  - Loads Sample.qxw (13 fixtures, 114 functions: 96 scenes, 13 chasers, 5 EFXs)
  - Validates: all scenes have values, all chasers have steps, all EFXs have fixtures+algorithm
  - Validates: tempoType on all functions, universeCount > 0
  - Writes 259KB JSON to `ai-qlcplus-server/tests/fixtures/sample_workspace.json`
- [x] **JSON output** written to `ai-qlcplus-server/tests/fixtures/sample_workspace.json`

### Step 4: Server-side JSON → AQW utility (reverse direction)
See server TODO for this step. Python utility that takes workspace_sync JSON and emits `.aqw` XML. Manual diff on first run, then automated round-trip test.

### Step 5: Protocol spec update
- [x] **Document new FunctionSummary fields** per type in protocol.md — full type-specific tables for Scene, Chaser, Collection, EFX, Sequence, RGBMatrix
- [x] **Updated FixtureGroup Object** in protocol.md to match actual serialization (size + fixtureIds)

## Phase 4: New Object Support & modify_function

### All CRUD commands (COMPLETE)

- [x] **create_fixture** — handler, delta, protocol
- [x] **modify_function** — all function types (Scene, Chaser, EFX, Collection), all change types
- [x] **Channel Groups** — create/modify/delete handlers, protocol, deltas, workspace sync
- [x] **Palettes** — create/modify/delete handlers, protocol, deltas, workspace sync
- [x] **Fixture Groups** — create/modify/delete handlers, protocol, deltas, workspace sync

## Phase 0: Fix 2D/3D Rendering (macOS) — PREREQUISITE

**Branch:** `worktree-fix-3d` (started — GLSL syntax fix + null texture fallbacks)

Prerequisite for all spatial/calibration visualization work. Can't test color-coded constraints, fixture dragging as observations, or calibration overlays without working rendering.

- [ ] **2D Monitor renders fixture positions correctly** on macOS
- [ ] **3D venue view renders with fixture models** on macOS (GLSL/OpenGL fixes)
- [ ] **Fixture drag/drop works** in both 2D and 3D views
- [ ] **Merge `worktree-fix-3d` into `feature/agent-client`**

## Stage Layout / Monitor Position Deltas

- [ ] **Add signals to MonitorProperties**: Emit `fixturePositionChanged(quint32 fid)` (and rotation/gelColor) from setters so AgentConnection can detect changes.
- [ ] **Send stage layout delta**: AgentConnection listens to MonitorProperties signals, sends `stage_layout_changed` delta with updated fixture position/rotation/gelColor.
- [ ] **Currently**: Stage layout only sent on initial workspace_sync; moving a fixture in Monitor view while connected does NOT notify the agent.

## rigmath Integration — Client Side

Design doc: `../docs/PRD-rigmath-integration.md` (TBD). Plan: `../.claude/plans/vivid-fluttering-piglet.md`.

### Phase 3: Calibration UI + 3D Visualization (depends on Phase 0)
- [ ] **Color-coded constraint groups in 3D view**: Green (connected), Yellow (weak), Red (isolated), Gray (excluded). Per-island colors for disconnected groups.
- [ ] **Handle `calibration_state_update` message** (server→client): Parse fixture constraint status, observation list, connectivity groups. Update 3D view fixture colors.
- [ ] **Send `add_observation` message** (client→server): Client-initiated observation entry bypassing LLM.
- [ ] **Record Crossing button**: Read current DMX for selected fixtures, send as crossing observation.
- [ ] **Fixture drag = position observation**: Dragging fixture in 3D view sends `known_position` observation (MEASURED certainty).
- [ ] **Calibration panel**: Docked panel — fixture list with status chips, observation list, action buttons, solvable indicator.
- [ ] **set_grid_size command handler**: Agent resizes Monitor grid to match stage dimensions.

### Phase 4: rigmath C++ Integration
- [ ] **Link rigmath C++ lib**: CMake ExternalProject or submodule. `rigmath_core` static lib target (C++ only, no pybind11).
- [ ] **Handle `calibration_config` message** (server→client): Parse solved fixture kinematics + mount data.
- [ ] **CalibrationModel class**: Holds rigmath C++ fixture instances from pushed config.
- [ ] **Client-side `aim_at`**: Real-time world→DMX via rigmath C++ inverse kinematics, no server round-trip.

## Build / CI

- [ ] **Migrate macOS CI to Qt6**: macOS workflow uses Qt 5.15.2 (macos-13 Intel). Linux and Windows already use Qt 6.8.1. Migrate to Qt6 + macos-14 (ARM). Qt6 supports both ARM and Intel via universal binaries, so Intel support is NOT dropped — just needs `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for universal build. Alternatively, build ARM-only and drop Intel (macOS 13+ is the practical minimum for Qt6).
- [ ] **Add qtkeychain to CI builds**: Install qtkeychain on all three platforms so keychain-based credential storage builds in CI. Without it, builds fall back to QSettings (works but insecure).

## Existing Features (pre-Phase 3)

- [x] **delete_function command**: Agent can create and delete functions.
- [x] **Delta format fix**: Client now sends `changes` array format per protocol spec (was silently ignored before).
- ~~**save_workspace command**~~ — deferred pending product decision.
- [x] **create_sequence command**: handleCreateSequence with bound scene, inline DMX values per step.
- [x] **Operate mode awareness**: Mode sent in workspace_sync and delta; server includes in prompt summary. Agent adapts naturally.

## Done

_(moved to PLAN.md on completion)_
