# AI QLC+ Client

**Do NOT commit unless explicitly asked by the user.**

**License: Apache 2.0** (open source). This repo is a fork of QLC+ and must remain open source. Do NOT include server implementation details, API keys, proprietary algorithms, or references to server internals in code, comments, or docs. The client communicates with the server via the public WebSocket protocol defined in `../docs/protocol.md`.

Forked QLC+ 4.14.3 with embedded AI agent client. Branch: `feature/agent-client`.

## Quick Start

```bash
cd ai-qlcplus
./build.sh          # cmake + make (uses Qt6 from /opt/homebrew/opt/qt)
./run.sh            # launch with correct DYLD_LIBRARY_PATH
```

## Agent-Specific Files

All agent code is isolated to these files (everything else is upstream QLC+):

```
engine/src/
├── agentconnection.h/.cpp   ← WebSocket client, command handlers, workspace sync, deltas

ui/src/
├── agentchatpanel.h/.cpp    ← Floating chat window (Monitor-style)
├── app.h/.cpp               ← Modified: creates AgentConnection, toolbar button, SimpleDesk bridge
```

Build system changes:
- `CMakeLists.txt` — added `WebSockets` to find_package
- `engine/src/CMakeLists.txt` — added agentconnection + Qt::WebSockets link
- `ui/src/CMakeLists.txt` — added agentchatpanel

## Key Design Decisions

- **AgentConnection in engine layer** — operates on Doc, no UI dependencies
- **AgentChatPanel as separate window** — Qt::Window flag, non-modal, multi-monitor friendly
- **File open triggers disconnect** — Doc::loading signal disconnects, user reconnects manually
- **Client is source of truth** — all state changes (including agent-initiated) send workspace_delta to server; server relies on deltas, not optimistic updates
- **SimpleDesk bridge via signal** — AgentConnection emits simpleDeskRequested(), App connects to SimpleDesk::instance()
- **Auth token in QSettings** — key `agent/token`, defaults to `dev-token-change-me`

## Supported Commands

| Command | Handler | What it does |
|---------|---------|-------------|
| create_scene | handleCreateScene | Scene(doc) + setValue + addFunction |
| create_chaser | handleCreateChaser | Chaser + ChaserStep per step |
| create_efx | handleCreateEfx | EFX + addFixture per fixture |
| create_collection | handleCreateCollection | Collection + addFunction per ID |
| start_function | handleStartFunction | fn->start(masterTimer, FunctionParent::master()) |
| stop_function | handleStopFunction | fn->stop(FunctionParent::master()) |
| stop_all | handleStopAll | Stop all running functions |
| set_simple_desk | handleSetSimpleDesk | Emits signal → SimpleDesk::setAbsoluteChannelValue |
| set_grand_master | handleSetGrandMaster | InputOutputMap::setGrandMasterValue |
| set_blackout | handleSetBlackout | InputOutputMap::setBlackout |

## Testing

Unit tests use QTest framework. Tests are in `engine/test/<feature>/`.

**Pattern for new test suites:**
1. Create `engine/test/<feature>/` directory
2. Add `<feature>_test.h` (QObject with Q_OBJECT, private slots for each test)
3. Add `<feature>_test.cpp` (implementations + `QTEST_APPLESS_MAIN` macro)
4. Add `CMakeLists.txt` (link `Qt::Test` + `Qt::WebSockets` + `qlcplusengine`)
5. Add subdirectory to `engine/test/CMakeLists.txt`

**Run a single suite:**
```bash
./build.sh
DYLD_LIBRARY_PATH=build/engine/src ./build/engine/test/<feature>/<feature>_test
```

**Agent-specific test suites:**
- `agentcontext/` — Unit: AgentContext XML round-trip, serialization, command handler (22 tests)
- `agentintegration/` — Integration: full protocol flow with real server (requires server running)

**When adding a new command handler:** Add a unit test in `agentcontext/` for the handler logic, and an integration test in `agentintegration/` for the full round-trip.

See `../docs/PRD-testing.md` for full test architecture.

**UI verification after coding:** After completing UI-facing changes (Spatial View, QML panels, rendering, gizmo, etc.), run the `qlcplus-ui-test` agent to verify visually:
```
Agent(subagent_type="qlcplus-ui-test", prompt="Build, launch, and verify <what you changed>", run_in_background=true)
```
The agent launches QLC+, takes screenshots via the embedded MCP server, and reports what it sees. It works in the background — no need for manual testing or app focus.

## Related Docs

- **`TECH_SPEC.md`** — Architecture, code layout, engine APIs used (AI quick reference)
- Product requirements: `../docs/PRD.md`
- Test architecture: `../docs/PRD-testing.md`
- Build progress: `../PLAN.md`
- Server repo: `../ai-qlcplus-server/`
