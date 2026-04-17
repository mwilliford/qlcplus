# MCP Protocol Specification

The QLC+ client embeds an MCP server that exposes the application to AI agents
(Claude Code, qlcplus-ui-test agent). This doc is the single source of truth for
tool names, parameters, and return formats.

**Port:** 9876 (localhost only)  
**Protocol:** JSON-RPC 2.0 over HTTP POST `/mcp`  
**Proxy:** `scripts/mcp-proxy.py` bridges stdio ↔ HTTP for Claude Code

---

## Transport

All requests are HTTP POST to `http://localhost:9876/mcp` with
`Content-Type: application/json`.

After initialization, include the session ID header:
```
Mcp-Session-Id: <uuid>
```

Sessions are deleted with `HTTP DELETE /mcp` + the session header. The proxy
handles session lifecycle automatically.

---

## Initialization

**Request:**
```json
{ "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {} }
```

**Response:**
```json
{
  "jsonrpc": "2.0", "id": 1,
  "result": {
    "protocolVersion": "2025-03-26",
    "capabilities": { "tools": { "listChanged": true } },
    "serverInfo": { "name": "qlcplus-mcp", "version": "0.1.0" },
    "_sessionId": "<uuid>"
  }
}
```

---

## Error Codes

| Code | Meaning |
|------|---------|
| `-32700` | JSON parse error |
| `-32600` | Invalid session or invalid request |
| `-32601` | Method not found / unknown tool |

Tool-specific errors are returned as a successful JSON-RPC response with
`"isError": true` inside the `result.content` array.

---

## Tool Reference

### Visual & Input

---

#### `screenshot`
Capture a screenshot of the application window.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `window` | string | no | `"main"` | `"main"` or `"3d"` |

**Returns:** `{ content: [{ type: "image", data: "<base64>", mimeType: "image/png" }] }`

---

#### `click`
Click at pixel coordinates in a window.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `x` | number | yes | — | Logical pixels (not retina) |
| `y` | number | yes | — | Logical pixels (not retina) |
| `button` | string | no | `"left"` | `"left"`, `"right"`, `"middle"` |
| `window` | string | no | `"main"` | `"main"` or `"3d"` |

**Returns:** `{ content: [{ type: "text", text: "OK" }] }`

---

#### `type_text`
Type text or press a named key.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `text` | string | no | Text to type |
| `key` | string | no | `Return`, `Escape`, `Tab`, `Up`, `Down`, `Left`, `Right` |

**Returns:** `{ content: [{ type: "text", text: "OK" }] }`

---

#### `drag`
Simulate a mouse drag between two points.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `x1` | number | yes | — | Start X, logical pixels |
| `y1` | number | yes | — | Start Y, logical pixels |
| `x2` | number | yes | — | End X, logical pixels |
| `y2` | number | yes | — | End Y, logical pixels |
| `steps` | integer | no | `10` | Intermediate move events |
| `window` | string | no | `"3d"` | `"main"` or `"3d"` |

**Returns:** `{ content: [{ type: "text", text: "Drag completed" }] }`

---

#### `find_element`
Find QML elements by `objectName`. Returns position, size, visibility, and
requested properties.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `objectName` | string | yes | Matches QML `objectName` property |
| `properties` | array of strings | no | Extra property values to include |

**Returns:** Array of matching elements:
```json
[{ "objectName": "...", "x": 0, "y": 0, "width": 100, "height": 30,
   "visible": true, "properties": {} }]
```

---

### App State

---

#### `get_dmx_values`
Read current DMX output values.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `universe` | integer | no | `0` | Universe index (0-based) |
| `startChannel` | integer | no | `1` | First channel (1-based) |
| `count` | integer | no | `512` | Number of channels |

**Returns:** `{ content: [{ type: "text", text: "Ch1: 255, Ch2: 0, ..." }] }`

---

#### `list_fixtures`
List all fixtures in the workspace.

**Returns:**
```json
{ "content": [{ "type": "text", "text": "[{\"id\":0,\"name\":\"Spot 1\",
  \"manufacturer\":\"...\",\"model\":\"...\",\"universe\":0,\"address\":1,
  \"channels\":16}]" }] }
```

---

#### `list_functions`
List all functions (scenes, chasers, etc.) in the workspace.

**Returns:**
```json
{ "content": [{ "type": "text", "text": "[{\"id\":0,\"name\":\"Scene 1\",
  \"type\":\"Scene\",\"running\":false}]" }] }
```

---

### Spatial View

---

#### `show_spatial_view`
Open the 3D Spatial View window programmatically.

**Parameters:** none

**Returns:** `{ content: [{ type: "text", text: "Spatial View opened" }] }`

---

#### `set_spatial_mode`
Set the active panel mode.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `mode` | integer | yes | `0=Layout`, `1=Calibrate`, `2=Focus`, `3=Live` |

**Returns:** `{ content: [{ type: "text", text: "Mode set" }] }`

---

#### `select_fixture`
Select a fixture by ID in the Spatial View.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `id` | integer | yes | — | Fixture ID; `-1` to deselect all |
| `add` | boolean | no | `false` | Add to selection (Shift+click) |

**Returns:** `{ content: [{ type: "text", text: "Fixture 0 selected" }] }`

---

#### `get_fixture_screen_positions`
Get screen positions of all fixtures projected through the current camera.
Coordinates are logical pixels relative to the SpatialView viewport.

**Parameters:** none

**Returns:**
```json
{ "content": [{ "type": "text", "text": "[{\"id\":0,\"screenX\":320,
  \"screenY\":240,\"visible\":true,\"worldX\":0.0,\"worldY\":3.0,
  \"worldZ\":2.5}]" }] }
```

---

#### `set_camera`
Set camera orientation or jump to a named preset.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `preset` | string | no | `"foh"`, `"top"`, `"front"`, `"side"` |
| `yaw` | number | no | Degrees |
| `pitch` | number | no | Degrees |
| `distance` | number | no | Meters |

**Returns:** `{ content: [{ type: "text", text: "Camera set" }] }`

---

### Gizmo & Alignment

---

#### `set_gizmo_mode`
Switch the active gizmo tool.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `mode` | integer | yes | `0=Translate` (W key), `1=Rotate` (E key) |

**Returns:** `{ content: [{ type: "text", text: "Gizmo mode set" }] }`

---

#### `align_selection`
Align all selected fixtures to the primary fixture's coordinate on one axis.
Requires 2+ fixtures selected.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `axis` | string | yes | `"X"`, `"Y"`, or `"Z"` |

**Returns:** `{ content: [{ type: "text", text: "Fixtures aligned" }] }`

---

#### `add_truss`
Add a truss pipe to the scene. Fixtures snap to trusses when dragged nearby.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `x1` | number | no | `-3.0` | Start X, meters |
| `y1` | number | no | `0.0` | Start Y, meters |
| `z1` | number | no | `3.0` | Start Z, meters |
| `x2` | number | no | `3.0` | End X, meters |
| `y2` | number | no | `0.0` | End Y, meters |
| `z2` | number | no | `3.0` | End Z, meters |
| `name` | string | no | auto | Display name |

**Returns:** `{ content: [{ type: "text", text: "Truss added" }] }`

---

### Focus Points

Focus points are persistent named targets (rendered as amber spheres) that
fixtures can be assigned to track. Requires **Focus mode** (`set_spatial_mode`
with `mode=2`) before calling `aim_at_focus_point`.

---

#### `create_focus_point`
Create a focus point at a world position.

| Parameter | Type | Required | Default | Notes |
|-----------|------|----------|---------|-------|
| `x` | number | yes | — | World X, meters |
| `y` | number | yes | — | World Y, meters |
| `z` | number | yes | — | World Z, meters |
| `name` | string | no | `"Point N"` | Display name |

**Returns:** `{ content: [{ type: "text", text: "fp0" }] }` — the generated ID

---

#### `list_focus_points`
List all focus points in the workspace.

**Returns:**
```json
{ "content": [{ "type": "text", "text": "[{\"id\":\"fp0\",\"name\":\"DS Center\",
  \"x\":0.0,\"y\":0.0,\"z\":0.9,\"assigned\":[0,1],\"selected\":false}]" }] }
```

---

#### `delete_focus_point`

| Parameter | Type | Required |
|-----------|------|----------|
| `id` | string | yes |

**Returns:** `{ content: [{ type: "text", text: "Deleted" }] }`

---

#### `move_focus_point`

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `id` | string | yes | |
| `x` | number | yes | World X, meters |
| `y` | number | yes | World Y, meters |
| `z` | number | yes | World Z, meters |

**Returns:** `{ content: [{ type: "text", text: "Moved" }] }`

---

#### `rename_focus_point`

| Parameter | Type | Required |
|-----------|------|----------|
| `id` | string | yes |
| `name` | string | yes |

**Returns:** `{ content: [{ type: "text", text: "Renamed" }] }`

---

#### `assign_fixture_to_focus_point`
Assign a fixture to track a focus point. The fixture is not aimed until
`aim_at_focus_point` is called.

| Parameter | Type | Required |
|-----------|------|----------|
| `id` | string | yes |
| `fixtureId` | integer | yes |

**Returns:** `{ content: [{ type: "text", text: "Assigned" }] }`

---

#### `unassign_fixture_from_focus_point`

| Parameter | Type | Required |
|-----------|------|----------|
| `id` | string | yes |
| `fixtureId` | integer | yes |

**Returns:** `{ content: [{ type: "text", text: "Unassigned" }] }`

---

#### `aim_at_focus_point`
Aim all fixtures assigned to the focus point. Runs IK per fixture and writes DMX.
Requires Focus mode to be active.

| Parameter | Type | Required |
|-----------|------|----------|
| `id` | string | yes |

**Returns:** `{ content: [{ type: "text", text: "Aimed" }] }`

---

#### `select_focus_point`
Visually select/highlight a focus point.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `id` | string | yes | Empty string to deselect |

**Returns:** `{ content: [{ type: "text", text: "Selected" }] }`

---

### Calibration

---

#### `calibrate_add_obs`
Add a spatial calibration observation.

| Parameter | Type | Required | Notes |
|-----------|------|----------|-------|
| `type` | string | yes | `"height"` or `"distance"` |
| `fixtureId` | integer | cond. | Required for `height` |
| `fixtureIdA` | integer | cond. | Required for `distance` |
| `fixtureIdB` | integer | cond. | Required for `distance` |
| `value` | number | yes | Meters |
| `certainty` | number | no | `0.9` | 0.0–1.0 |

**Returns:** `{ content: [{ type: "text", text: "Observation added" }] }`

---

#### `calibrate_run_solve`
Run the spatial calibration solver.

**Parameters:** none

**Returns:** `{ content: [{ type: "text", text: "Converged, RMS: 0.05m" }] }`
or `{ content: [{ type: "text", text: "Did not converge" }] }`

---

## mcp-proxy.py

`scripts/mcp-proxy.py` speaks MCP stdio protocol so Claude Code can connect
without knowing about the HTTP server.

**Usage (in `.mcp.json`):**
```json
{
  "mcpServers": {
    "qlcplus": {
      "command": "python3",
      "args": ["clients/qlcplus/scripts/mcp-proxy.py"]
    }
  }
}
```

**Behavior:**
- Always returns tool definitions even when QLC+ is not running
- Returns actionable error text when the app is down (not a crash)
- Auto-initializes a session on the first tool call
- Auto-retries once if the session expires (e.g., QLC+ restarted)
- Tool call timeout: 30 seconds
- Init timeout: 3 seconds

---

## Known Limitations

- **No input validation**: All tool handlers access `args[key]` directly — a
  missing required parameter will crash the tool handler. Planned fix: add a
  `validateArg()` wrapper.
- **Synchronous screenshot**: PNG encoding blocks the Qt event loop (~150 ms at
  1920×1440). Acceptable for occasional captures; may cause hitching at high
  frequency.
- **No rate limiting**: Per-session request rate is uncapped.
