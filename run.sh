#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Parse version argument: v4 (default) or v5
VERSION="${1:-v4}"
shift 2>/dev/null || true
case "$VERSION" in
    v4|v5) ;;
    *)  echo "Usage: $0 [v4|v5] [-- qlcplus args...]"; exit 1 ;;
esac

BUILD_DIR="$SCRIPT_DIR/build-$VERSION"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory $BUILD_DIR not found. Run: ./build.sh $VERSION"
    exit 1
fi

# Plugins are built to build/PlugIns/<name>/src/*.dylib (nested subdirs) but
# the app's IOPluginCache scans build/PlugIns/*.dylib (flat, no recursion).
# Flatten by symlinking each .dylib into the top-level PlugIns dir.
PLUGIN_DIR="$BUILD_DIR/PlugIns"
if [ -d "$PLUGIN_DIR" ]; then
    find "$PLUGIN_DIR" -mindepth 2 -name "*.dylib" 2>/dev/null | while IFS= read -r dylib; do
        base="$(basename "$dylib")"
        [ ! -e "$PLUGIN_DIR/$base" ] && ln -sf "$dylib" "$PLUGIN_DIR/$base"
    done
fi

if [ "$VERSION" = "v4" ]; then
    QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
    DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/ui/src:$BUILD_DIR/webaccess/src" \
        exec "$BUILD_DIR/main/qlcplus" "$@"
else
    QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
    DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/webaccess/src" \
        exec "$BUILD_DIR/qmlui/qlcplus-qml" "$@"
fi
