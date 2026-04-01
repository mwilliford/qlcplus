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

### Tardis Integration — TODO
- [ ] **Undo for agent mutations**: Call `Tardis::instance()->enqueueAction()` after agent commands (create_scene, etc.) so they're undoable. Requires wiring in App or AgentConnection.

### Virtual Console — TODO
- [ ] **VC serializer for v5**: v5 has its own VirtualConsole implementation. May need adapted vcserializer/vccommandhandler or new approach using v5's VC API.

## Bugs: Chat panel

- [x] **Input disabled during streaming**: Fixed in v5 QML — input always enabled, send during streaming does implicit cancel.

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

## Stage Layout / Monitor Position Deltas

- [ ] **Add signals to MonitorProperties**: Emit `fixturePositionChanged(quint32 fid)` (and rotation/gelColor) from setters so AgentConnection can detect changes.
- [ ] **Send stage layout delta**: AgentConnection listens to MonitorProperties signals, sends `stage_layout_changed` delta with updated fixture position/rotation/gelColor.
- [ ] **Currently**: Stage layout only sent on initial workspace_sync; moving a fixture in Monitor view while connected does NOT notify the agent.

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
