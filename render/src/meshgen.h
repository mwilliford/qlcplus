#pragma once

#include <bgfx/bgfx.h>
#include <vector>
#include <cstdint>

namespace qlcrender {

/**
 * @brief Procedural mesh generators for the spatial 3D view.
 */
struct PosColorVertex
{
    float x, y, z;
    uint32_t abgr;

    static bgfx::VertexLayout layout;
    static void init();
};

struct PosVertex
{
    float x, y, z;

    static bgfx::VertexLayout layout;
    static void init();
};

/** Create a unit cube (1x1x1 centered at origin) vertex/index buffer. */
void createCubeMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh);
void destroyCubeMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh);

/** Create a unit sphere (radius 1, centered at origin) with normals. */
void createSphereMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh,
                      int subdivisions = 16);
void destroySphereMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh);

/** Vertex with position + normal (for lit geometry like spheres). */
struct PosNormalVertex
{
    float x, y, z;
    float nx, ny, nz;

    static bgfx::VertexLayout layout;
    static void init();
};

} // namespace qlcrender
