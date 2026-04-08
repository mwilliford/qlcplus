#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Parse arguments: [v4|v5] [filename.qxw ...]
# If first arg is a file, default to v5 and pass it through
VERSION="v5"
if [ "${1:-}" = "v4" ] || [ "${1:-}" = "v5" ]; then
    VERSION="$1"
    shift
fi

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

# Determine the Resources directory.
# For a macOS .app bundle (v5), Resources lives inside the bundle.
# For a flat build (v4 or non-bundle), it's at $BUILD_DIR/Resources.
APP_BUNDLE="$BUILD_DIR/qmlui/qlcplus-qml.app"
if [ "$VERSION" = "v5" ] && [ -d "$APP_BUNDLE" ]; then
    RESOURCES_DIR="$APP_BUNDLE/Contents/Resources"
    mkdir -p "$RESOURCES_DIR"
else
    RESOURCES_DIR="$BUILD_DIR/Resources"
fi

# Ensure fixture definitions are accessible.
# QLC+ looks for fixtures at <binary_dir>/../Resources/Fixtures.
FIXTURES_BUILD="$RESOURCES_DIR/fixtures"
FIXTURES_SRC="$SCRIPT_DIR/resources/fixtures"
mkdir -p "$FIXTURES_BUILD"
if [ ! -e "$FIXTURES_BUILD/FixturesMap.xml" ]; then
    ln -sf "$FIXTURES_SRC/FixturesMap.xml" "$FIXTURES_BUILD/FixturesMap.xml" 2>/dev/null
    # Symlink each manufacturer directory
    for mfr in "$FIXTURES_SRC"/*/; do
        base="$(basename "$mfr")"
        [ ! -e "$FIXTURES_BUILD/$base" ] && ln -sf "$mfr" "$FIXTURES_BUILD/$base"
    done
fi

# Symlink Gobos directory for 2D view gobo images
GOBOS_BUILD="$RESOURCES_DIR/gobos"
GOBOS_SRC="$SCRIPT_DIR/resources/gobos"
mkdir -p "$GOBOS_BUILD"
if [ ! -e "$GOBOS_BUILD/Others" ]; then
    for gdir in "$GOBOS_SRC"/*/; do
        base="$(basename "$gdir")"
        [ ! -e "$GOBOS_BUILD/$base" ] && ln -sf "$gdir" "$GOBOS_BUILD/$base"
    done
fi

# Symlink 3D mesh files (fixtures, generic, stage)
MESHES_BUILD="$RESOURCES_DIR/Meshes"
MESHES_SRC="$SCRIPT_DIR/resources/meshes"
for subdir in fixtures generic stage; do
    srcdir="$MESHES_SRC/$subdir"
    dstdir="$MESHES_BUILD/$subdir"
    mkdir -p "$dstdir"
    if [ -d "$srcdir" ]; then
        for f in "$srcdir"/*; do
            [ -f "$f" ] || continue
            base="$(basename "$f")"
            [ ! -e "$dstdir/$base" ] && ln -sf "$f" "$dstdir/$base"
        done
    fi
done

if [ "$VERSION" = "v4" ]; then
    QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
    DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/ui/src:$BUILD_DIR/webaccess/src" \
        exec "$BUILD_DIR/main/qlcplus" "$@"
else
    # v5 builds as a macOS .app bundle
    APP_BUNDLE="$BUILD_DIR/qmlui/qlcplus-qml.app"
    if [ -d "$APP_BUNDLE" ]; then
        QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
        DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/webaccess/src:$BUILD_DIR/render/src" \
            exec "$APP_BUNDLE/Contents/MacOS/qlcplus-qml" "$@"
    else
        # Fallback for non-bundle builds
        QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
        DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/webaccess/src:$BUILD_DIR/render/src" \
            exec "$BUILD_DIR/qmlui/qlcplus-qml" "$@"
    fi
fi
