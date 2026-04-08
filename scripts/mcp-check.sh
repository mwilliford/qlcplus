#!/bin/bash
# Verify the QLC+ MCP server is responding. Exit 0 if OK, 1 if not.
curl -sf -X POST http://localhost:9876/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"initialize","params":{},"id":1}' \
  | python3 -c "import json,sys; r=json.load(sys.stdin); print('MCP OK:', r['result']['serverInfo']['name'], r['result']['serverInfo']['version'])" 2>/dev/null

if [ $? -ne 0 ]; then
    echo "MCP not responding on localhost:9876"
    exit 1
fi
