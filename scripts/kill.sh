#!/bin/bash
# Kill any running QLC+ instance. Safe to call even if nothing is running.
pkill -f qlcplus-qml 2>/dev/null
sleep 1
if pgrep -f qlcplus-qml >/dev/null 2>&1; then
    pkill -9 -f qlcplus-qml 2>/dev/null
    sleep 1
fi
pgrep -f qlcplus-qml >/dev/null 2>&1 && echo "ERROR: QLC+ still running" && exit 1
echo "QLC+ stopped"
