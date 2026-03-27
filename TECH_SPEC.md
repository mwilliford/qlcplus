# Tech Spec — AI QLC+ Client

## Runtime

- C++ / Qt6 (cmake build, `/opt/homebrew/opt/qt`)
- Forked from QLC+ 4.14.3, branch `feature/agent-client`
- Build: `./build.sh` (cmake + make with AGL workaround for macOS)
- Run: `./run.sh` (sets DYLD_LIBRARY_PATH for local libs)

## Agent Files (all other files are upstream QLC+)

### Engine Layer: AgentConnection

**`engine/src/agentconnection.h`** — Header with:
- `State` enum: `Disconnected`, `Connecting`, `WaitingForSync`, `Connected`
- Public API: `connectToServer()`, `disconnectFromServer()`, `sendChatMessage(text)`
- Signals: `stateChanged`, `chatTokenReceived`, `chatStreamEnded`, `commandExecuting`, `errorOccurred`, `simpleDeskRequested`
- Private: command handlers, serialization methods, delta handlers

**`engine/src/agentconnection.cpp`** — Implementation (~1000 lines):

**Connection lifecycle:**
- `connectToServer()` — creates QWebSocket, sets auth headers (`Authorization: Bearer <token>`, `X-Client-Version`, `X-Protocol-Version`), opens connection
- `onWsConnected()` — builds workspace_sync JSON via `buildWorkspaceSync()`, sends it
- `onWsDisconnected()` — starts reconnect timer (exponential backoff 1s→30s)
- Constructor connects `Doc::loading` signal → `onDocLoading()` which disconnects (clean session per workspace)

**Workspace sync serialization (`buildWorkspaceSync()`):**
- `serializeFixtures()` — iterates `m_doc->fixtures()`, outputs id/name/manufacturer/model/mode/universe/address/channels
- `serializeFixtureDefs()` — deduplicates by (manufacturer, model), accesses via `fixtureDefCache()->fixtureDef()` to trigger lazy load. Serializes channels with `group`, `defaultValue`, `controlByte`, `colour`, and capabilities with `preset`, `color`, `color2`
- `serializeFunctions()` — iterates `m_doc->functions()`, includes scene values for Scene type via `qobject_cast<Scene*>`
- `serializeStageLayout()` — reads `m_doc->monitorProperties()` grid size/units + fixture positions
- `serializeGrandMaster()` — reads from `m_doc->inputOutputMap()`

**Message dispatch (`onWsTextMessage()`):**
- Parses JSON, switches on `type` field
- Chat messages: emits `chatTokenReceived`/`chatStreamEnded` signals
- Commands: calls `handle*` methods with delta suppression (`m_suppressDelta = true` around engine mutations)
- Each handler sends `command_result` JSON back with `requestId`, `success`, and `functionId` for creates

**Command handlers:**

| Handler | Engine calls |
|---------|-------------|
| `handleCreateScene` | `new Scene(doc)` → `setValue(fxi, ch, val)` → `doc->addFunction(scene)` |
| `handleCreateChaser` | `new Chaser(doc)` → `setRunOrder/Direction/SpeedModes` → `addStep(ChaserStep)` → `doc->addFunction()` |
| `handleCreateEfx` | `new EFX(doc)` → `setAlgorithm/Width/Height/...` → `addFixture(fxi, head)` → `doc->addFunction()` |
| `handleCreateCollection` | `new Collection(doc)` → `addFunction(fid)` → `doc->addFunction()` |
| `handleStartFunction` | `doc->function(id)->start(doc->masterTimer(), FunctionParent::master())` |
| `handleStopFunction` | `doc->function(id)->stop(FunctionParent::master())` |
| `handleStopAll` | Iterate `doc->functions()`, stop each running one |
| `handleSetSimpleDesk` | Emits `simpleDeskRequested(absChannel, value)` — bridged to UI in App |
| `handleSetGrandMaster` | `doc->inputOutputMap()->setGrandMasterValue(value)` |
| `handleSetBlackout` | `doc->inputOutputMap()->setBlackout(bool)` |

All create handlers check `msg["autoStart"].toBool()` and call `fn->start()` if true.

**Workspace delta (`connectDocSignals()`):**
- Connects to: `Doc::functionAdded/Removed/Changed`, `Doc::fixtureAdded/Removed/Changed`, `Doc::modeChanged`, `InputOutputMap::grandMasterValueChanged`, `InputOutputMap::blackoutChanged`
- Each handler builds a `workspace_delta` JSON and calls `sendDelta()`
- `sendDelta()` checks `m_state == Connected && !m_suppressDelta` before sending
- `Doc::loading/loaded` signals connected in constructor (not in `connectDocSignals`) — always active

### UI Layer: AgentChatPanel

**`ui/src/agentchatpanel.h`** — Inherits `QWidget` (not QDockWidget). Singleton via `s_instance`.
- `static createAndShow(parent, connection)` — creates with `Qt::Window` flag, `WA_DeleteOnClose`, restores geometry from QSettings
- `static instance()` — returns singleton

**`ui/src/agentchatpanel.cpp`** — Implementation:
- Layout: status bar (QLabel + QPushButton) → QTextBrowser (dark theme, monospace) → input bar (QLineEdit + QPushButton)
- Streaming: `onChatToken()` appends text at cursor end. `onChatEnd()` finalizes with newline.
- HTML formatting: user messages green, agent messages blue, commands orange, errors red
- Geometry saved in `closeEvent()` and destructor to QSettings key `agentchatpanel/geometry`

### App Integration

**`ui/src/app.h`** — Added:
- `AgentConnection *m_agentConnection` member
- `void slotAgentPanel()` public slot

**`ui/src/app.cpp`** — Changes in `init()`:
- Creates `AgentConnection(m_doc, this)` — always exists, manages its own lifecycle
- Connects `simpleDeskRequested` signal to lambda that calls `SimpleDesk::instance()->setAbsoluteChannelValue()`
- Adds "AI Agent" QAction to toolbar → triggers `slotAgentPanel()` → calls `AgentChatPanel::createAndShow()`
- Destructor deletes `AgentChatPanel::instance()` if open

### Build System Changes

- **`CMakeLists.txt`** (root) — Added `WebSockets` to `find_package(Qt...)`
- **`engine/src/CMakeLists.txt`** — Added `agentconnection.cpp agentconnection.h` to sources, `Qt::WebSockets` to link
- **`ui/src/CMakeLists.txt`** — Added `agentchatpanel.cpp agentchatpanel.h` to sources

## Key QLC+ Engine APIs Used

| Class | Header | Methods used |
|-------|--------|-------------|
| `Doc` | `engine/src/doc.h` | `fixtures()`, `functions()`, `function(id)`, `fixture(id)`, `addFunction()`, `masterTimer()`, `inputOutputMap()`, `monitorProperties()`, `fixtureDefCache()`, `mode()` |
| `Fixture` | `engine/src/fixture.h` | `id()`, `name()`, `fixtureDef()`, `fixtureMode()`, `universe()`, `address()`, `channels()` |
| `QLCFixtureDef` | `engine/src/qlcfixturedef.h` | `manufacturer()`, `model()`, `type()`, `typeToString()`, `channels()`, `modes()` |
| `QLCChannel` | `engine/src/qlcchannel.h` | `name()`, `group()`, `groupToString()`, `defaultValue()`, `controlByte()`, `colour()`, `colourToString()`, `capabilities()` |
| `QLCCapability` | `engine/src/qlccapability.h` | `min()`, `max()`, `name()`, `preset()`, `presetToString()`, `presetType()`, `resource()` |
| `QLCFixtureMode` | `engine/src/qlcfixturemode.h` | `name()`, `channels()` |
| `QLCFixtureDefCache` | `engine/src/qlcfixturedefcache.h` | `fixtureDef(manufacturer, model)` (triggers lazy load) |
| `Scene` | `engine/src/scene.h` | `Scene(doc)`, `setName()`, `setValue(fxi, ch, val)`, `values()` |
| `Chaser` | `engine/src/chaser.h` | `Chaser(doc)`, `addStep()`, `setFadeInMode/FadeOutMode/DurationMode()`, `stringToSpeedMode()` |
| `ChaserStep` | `engine/src/chaserstep.h` | Constructor `(fid, fadeIn, hold, fadeOut)`, `.note` field |
| `EFX` | `engine/src/efx.h` | `EFX(doc)`, `setAlgorithm()`, `setWidth/Height/XOffset/YOffset/Rotation()`, `addFixture(fxi, head)`, `setPropagationMode()`, `stringToAlgorithm()`, `stringToPropagationMode()` |
| `Collection` | `engine/src/collection.h` | `Collection(doc)`, `addFunction(fid)` |
| `Function` | `engine/src/function.h` | `start(timer, parent)`, `stop(parent)`, `isRunning()`, `typeToString()`, `stringToRunOrder()`, `stringToDirection()`, `setRunOrder()`, `setDirection()`, `setName()`, `id()`, `name()`, `type()`, `path()`, `fadeInSpeed()`, `fadeOutSpeed()`, `duration()`, `runOrder()`, `direction()` |
| `FunctionParent` | `engine/src/functionparent.h` | `FunctionParent::master()` |
| `InputOutputMap` | `engine/src/inputoutputmap.h` | `grandMasterValue()`, `grandMasterValueMode()`, `grandMasterChannelMode()`, `setGrandMasterValue()`, `blackout()`, `setBlackout()` |
| `GrandMaster` | `engine/src/grandmaster.h` | `valueModeToString()`, `channelModeToString()` |
| `MonitorProperties` | `engine/src/monitorproperties.h` | `gridSize()`, `gridUnits()`, `labelsVisible()`, `fixtureItemsID()`, `fixtureProperties(fid)` |
| `SimpleDesk` | `ui/src/simpledesk.h` | `instance()`, `setAbsoluteChannelValue(address, value)` |

## Design Patterns

- **Delta suppression**: `m_suppressDelta = true` wraps all engine mutations from server commands. Prevents echoing our own changes back as workspace_delta messages.
- **Signal bridge for cross-layer**: AgentConnection (engine) can't access SimpleDesk (ui). Emits `simpleDeskRequested` signal, App connects it to `SimpleDesk::instance()`.
- **File load = disconnect**: `Doc::loading` signal triggers disconnect. User reconnects manually after loading. Prevents stale state and delta floods.
- **Singleton chat panel**: `AgentChatPanel::createAndShow()` / `instance()`, same pattern as `Monitor`. `WA_DeleteOnClose` with geometry persistence.
