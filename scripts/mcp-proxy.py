#!/usr/bin/env python3
"""
MCP stdio proxy for QLC+.

Speaks MCP protocol over stdin/stdout (for Claude Code), forwards tool calls
to the QLC+ embedded HTTP MCP server at localhost:9876.

Always available — returns tool definitions even when QLC+ isn't running.
Tool calls return actionable errors when the app is down.
"""

import json
import sys
import urllib.request
import urllib.error

QLC_MCP_URL = "http://localhost:9876/mcp"
SESSION_ID = None

TOOLS = [
    {
        "name": "screenshot",
        "description": "Capture a screenshot of a QLC+ window. Returns base64 PNG.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "window": {
                    "type": "string",
                    "enum": ["main", "3d"],
                    "default": "main",
                    "description": "Which window: main (QML app) or 3d (Spatial View with bgfx viewport + QML panel)"
                }
            }
        }
    },
    {
        "name": "click",
        "description": "Click at (x,y) in a QLC+ window. Coordinates in logical pixels.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "number", "description": "X in logical pixels"},
                "y": {"type": "number", "description": "Y in logical pixels"},
                "button": {"type": "string", "enum": ["left", "right", "middle"], "default": "left"},
                "window": {"type": "string", "enum": ["main", "3d"], "default": "main"}
            },
            "required": ["x", "y"]
        }
    },
    {
        "name": "drag",
        "description": "Mouse drag in the Spatial View viewport. For gizmo/fixture dragging. Logical pixels.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x1": {"type": "number", "description": "Start X"},
                "y1": {"type": "number", "description": "Start Y"},
                "x2": {"type": "number", "description": "End X"},
                "y2": {"type": "number", "description": "End Y"},
                "steps": {"type": "integer", "default": 10, "description": "Intermediate move steps"},
                "window": {"type": "string", "enum": ["main", "3d"], "default": "3d"}
            },
            "required": ["x1", "y1", "x2", "y2"]
        }
    },
    {
        "name": "type_text",
        "description": "Type text or press a key in QLC+.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": {"type": "string", "description": "Text to type"},
                "key": {"type": "string", "description": "Special key: Return, Escape, Tab, Up, Down, Left, Right"}
            }
        }
    },
    {
        "name": "find_element",
        "description": "Find QML elements by objectName. Returns position, size, visibility.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "objectName": {"type": "string"},
                "properties": {"type": "array", "items": {"type": "string"}, "description": "Extra properties to read"}
            }
        }
    },
    {
        "name": "show_spatial_view",
        "description": "Open the 3D Spatial View window programmatically.",
        "inputSchema": {"type": "object", "properties": {}}
    },
    {
        "name": "select_fixture",
        "description": "Select a fixture by ID in the Spatial View. id=-1 to deselect all. add=true for multi-select.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "id": {"type": "integer", "description": "Fixture ID, or -1 to deselect all"},
                "add": {"type": "boolean", "default": False, "description": "Add to selection (multi-select) instead of replacing"}
            },
            "required": ["id"]
        }
    },
    {
        "name": "get_fixture_screen_positions",
        "description": "Get screen positions of all fixtures projected through current camera. Returns logical pixel coords. Camera-independent.",
        "inputSchema": {"type": "object", "properties": {}}
    },
    {
        "name": "set_camera",
        "description": "Set Spatial View camera. Presets: foh, top, front, side. Or explicit yaw/pitch/distance.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "preset": {"type": "string", "enum": ["foh", "top", "front", "side"]},
                "yaw": {"type": "number"},
                "pitch": {"type": "number"},
                "distance": {"type": "number"}
            }
        }
    },
    {
        "name": "get_dmx_values",
        "description": "Read DMX channel values for a universe.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "universe": {"type": "integer", "default": 0},
                "startChannel": {"type": "integer", "default": 1},
                "count": {"type": "integer", "default": 512}
            }
        }
    },
    {
        "name": "list_fixtures",
        "description": "List all fixtures with id, name, manufacturer, model, universe, address, channels.",
        "inputSchema": {"type": "object", "properties": {}}
    },
    {
        "name": "list_functions",
        "description": "List all functions (scenes, chasers) with id, name, type, running status.",
        "inputSchema": {"type": "object", "properties": {}}
    },
    {
        "name": "align_selection",
        "description": "Align all selected fixtures on an axis. Sets all to the primary fixture's coordinate.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "axis": {"type": "string", "enum": ["X", "Y", "Z"], "description": "Axis to align on"}
            },
            "required": ["axis"]
        }
    },
    {
        "name": "set_gizmo_mode",
        "description": "Switch gizmo tool: 0=Translate (Move), 1=Rotate. Keyboard shortcuts: W=Move, E=Rotate.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "mode": {"type": "integer", "enum": [0, 1], "description": "0=Translate, 1=Rotate"}
            },
            "required": ["mode"]
        }
    },
]


def forward_to_qlcplus(tool_name, arguments):
    """Forward a tool call to the QLC+ MCP server via HTTP."""
    global SESSION_ID

    # Initialize session if needed
    if SESSION_ID is None:
        try:
            init_req = json.dumps({
                "jsonrpc": "2.0",
                "method": "initialize",
                "params": {},
                "id": 0
            }).encode()
            req = urllib.request.Request(QLC_MCP_URL, data=init_req,
                                         headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=3) as resp:
                result = json.loads(resp.read())
                SESSION_ID = result.get("result", {}).get("_sessionId")
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            return {
                "content": [{"type": "text", "text": "QLC+ is not running. Launch it with:\n  cd clients/qlcplus && ./scripts/launch.sh"}],
                "isError": True
            }

    # Forward tool call
    call_req = json.dumps({
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": arguments},
        "id": 1
    }).encode()
    headers = {"Content-Type": "application/json"}
    if SESSION_ID:
        headers["Mcp-Session-Id"] = SESSION_ID

    try:
        req = urllib.request.Request(QLC_MCP_URL, data=call_req, headers=headers)
        with urllib.request.urlopen(req, timeout=30) as resp:
            result = json.loads(resp.read())
            if "error" in result:
                err = result["error"]
                # Session expired (QLC+ restarted) — re-initialize and retry once
                if err.get("code") == -32600 and "session" in err.get("message", "").lower():
                    SESSION_ID = None
                    return forward_to_qlcplus(tool_name, arguments)
                return {
                    "content": [{"type": "text", "text": f"MCP error: {err}"}],
                    "isError": True
                }
            return result.get("result", {})
    except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
        SESSION_ID = None  # reset session on connection error
        return {
            "content": [{"type": "text", "text": f"QLC+ connection lost: {e}. Relaunch with scripts/launch.sh"}],
            "isError": True
        }


def handle_request(req):
    """Handle a JSON-RPC request and return a response."""
    method = req.get("method", "")
    req_id = req.get("id")
    params = req.get("params", {})

    if method == "initialize":
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": {
                "protocolVersion": "2025-03-26",
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "qlcplus-proxy", "version": "0.1.0"}
            }
        }

    if method == "notifications/initialized":
        return None  # notification, no response

    if method == "ping":
        return {"jsonrpc": "2.0", "id": req_id, "result": {}}

    if method == "tools/list":
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": {"tools": TOOLS}
        }

    if method == "tools/call":
        tool_name = params.get("name", "")
        arguments = params.get("arguments", {})
        result = forward_to_qlcplus(tool_name, arguments)
        return {"jsonrpc": "2.0", "id": req_id, "result": result}

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "error": {"code": -32601, "message": f"Method not found: {method}"}
    }


def main():
    """Read JSON-RPC from stdin, write responses to stdout."""
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            resp = {"jsonrpc": "2.0", "id": None,
                    "error": {"code": -32700, "message": "Parse error"}}
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()
            continue

        resp = handle_request(req)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
