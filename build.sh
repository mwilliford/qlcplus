#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Create AGL workaround (removed in macOS 26 SDK but Qt 6.9 still links it)
mkdir -p cmake_overrides
cat > cmake_overrides/FindWrapOpenGL.cmake << 'OVERRIDE'
if(TARGET WrapOpenGL::WrapOpenGL)
    set(WrapOpenGL_FOUND ON)
    return()
endif()
set(WrapOpenGL_FOUND OFF)
find_package(OpenGL ${WrapOpenGL_FIND_VERSION})
if(OpenGL_FOUND)
    set(WrapOpenGL_FOUND ON)
    add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
    if(APPLE)
        get_target_property(__opengl_fw_lib_path OpenGL::GL IMPORTED_LOCATION)
        if(__opengl_fw_lib_path AND NOT __opengl_fw_lib_path MATCHES "/([^/]+)\\.framework$")
            get_filename_component(__opengl_fw_path "${__opengl_fw_lib_path}" DIRECTORY)
        endif()
        if(NOT __opengl_fw_path)
            set(__opengl_fw_path "-framework OpenGL")
        endif()
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE ${__opengl_fw_path})
    else()
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE OpenGL::GL)
    endif()
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WrapOpenGL DEFAULT_MSG WrapOpenGL_FOUND)
OVERRIDE

# Match CI flags: cmake -S . -B build -DCMAKE_PREFIX_PATH=...
cmake "$SCRIPT_DIR" \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_MODULE_PATH="$BUILD_DIR/cmake_overrides"

cmake --build . --parallel $(sysctl -n hw.ncpu)

echo ""
echo "Build complete. Run with:"
echo "  $SCRIPT_DIR/run.sh"
