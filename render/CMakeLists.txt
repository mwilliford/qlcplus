set(module_name "qlcplusrender")

# bgfx — cross-platform rendering library (BSD-2)
# Using the CMake port: https://github.com/bkaradzic/bgfx.cmake
include(FetchContent)
FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        v1.142.9197-527
    GIT_SHALLOW    TRUE
)
set(BGFX_BUILD_TOOLS    ON  CACHE BOOL "" FORCE)  # Need shaderc for shader compilation
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(bgfx)

add_library(${module_name} STATIC
    src/spatialrenderer.h
    src/bgfxrenderer.h src/bgfxrenderer.cpp
    src/orbitcamera.h src/orbitcamera.cpp
    src/meshgen.h src/meshgen.cpp
    src/meshloader.h src/meshloader.cpp
    src/rendertypes.h
)

target_include_directories(${module_name} PUBLIC src/)
target_compile_features(${module_name} PRIVATE cxx_std_20)

target_link_libraries(${module_name} PUBLIC
    bgfx
    bx
    bimg
)

if(APPLE)
    target_link_libraries(${module_name} PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
    )
endif()
