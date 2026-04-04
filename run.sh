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

# Ensure fixture definitions are accessible from the build directory.
# On macOS, QLC+ looks for fixtures at <binary_dir>/../Resources/Fixtures.
# The build tree has an empty Resources/fixtures/ from cmake — symlink
# the FixturesMap.xml and manufacturer dirs from the source tree.
FIXTURES_BUILD="$BUILD_DIR/Resources/fixtures"
FIXTURES_SRC="$SCRIPT_DIR/resources/fixtures"
if [ -d "$FIXTURES_BUILD" ] && [ ! -e "$FIXTURES_BUILD/FixturesMap.xml" ]; then
    ln -sf "$FIXTURES_SRC/FixturesMap.xml" "$FIXTURES_BUILD/FixturesMap.xml" 2>/dev/null
    # Symlink each manufacturer directory
    for mfr in "$FIXTURES_SRC"/*/; do
        base="$(basename "$mfr")"
        [ ! -e "$FIXTURES_BUILD/$base" ] && ln -sf "$mfr" "$FIXTURES_BUILD/$base"
    done
fi

# Symlink Gobos directory for 2D view gobo images
GOBOS_BUILD="$BUILD_DIR/Resources/gobos"
GOBOS_SRC="$SCRIPT_DIR/resources/gobos"
if [ -d "$GOBOS_BUILD" ] && [ ! -e "$GOBOS_BUILD/Others" ]; then
    for gdir in "$GOBOS_SRC"/*/; do
        base="$(basename "$gdir")"
        [ ! -e "$GOBOS_BUILD/$base" ] && ln -sf "$gdir" "$GOBOS_BUILD/$base"
    done
fi

# Symlink 3D mesh files (fixtures, generic, stage)
MESHES_BUILD="$BUILD_DIR/Resources/Meshes"
MESHES_SRC="$SCRIPT_DIR/resources/meshes"
for subdir in fixtures generic stage; do
    srcdir="$MESHES_SRC/$subdir"
    dstdir="$MESHES_BUILD/$subdir"
    if [ -d "$srcdir" ] && [ -d "$dstdir" ]; then
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
    QT_PLUGIN_PATH="/opt/homebrew/opt/qt/share/qt/plugins" \
    DYLD_LIBRARY_PATH="$BUILD_DIR/engine/src:$BUILD_DIR/webaccess/src" \
        exec "$BUILD_DIR/qmlui/qlcplus-qml" "$@"
fi
