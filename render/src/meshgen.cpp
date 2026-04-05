#include "meshgen.h"
#include <bgfx/bgfx.h>

namespace qlcrender {

bgfx::VertexLayout PosColorVertex::layout;
bgfx::VertexLayout PosVertex::layout;

void PosColorVertex::init()
{
    layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
}

void PosVertex::init()
{
    layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
}

// Unit cube: 8 vertices, 36 indices (12 triangles)
static const PosColorVertex s_cubeVertices[] =
{
    { -0.5f, -0.5f, -0.5f, 0xffffffff },
    {  0.5f, -0.5f, -0.5f, 0xffffffff },
    {  0.5f,  0.5f, -0.5f, 0xffffffff },
    { -0.5f,  0.5f, -0.5f, 0xffffffff },
    { -0.5f, -0.5f,  0.5f, 0xffffffff },
    {  0.5f, -0.5f,  0.5f, 0xffffffff },
    {  0.5f,  0.5f,  0.5f, 0xffffffff },
    { -0.5f,  0.5f,  0.5f, 0xffffffff },
};

static const uint16_t s_cubeIndices[] =
{
    0, 1, 2, 0, 2, 3,  // bottom
    4, 6, 5, 4, 7, 6,  // top
    0, 4, 5, 0, 5, 1,  // front
    2, 6, 7, 2, 7, 3,  // back
    0, 3, 7, 0, 7, 4,  // left
    1, 5, 6, 1, 6, 2,  // right
};

void createCubeMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh)
{
    vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices)),
        PosColorVertex::layout
    );

    ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(s_cubeIndices, sizeof(s_cubeIndices))
    );
}

void destroyCubeMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh)
{
    if (bgfx::isValid(vbh))
        bgfx::destroy(vbh);
    if (bgfx::isValid(ibh))
        bgfx::destroy(ibh);
    vbh = BGFX_INVALID_HANDLE;
    ibh = BGFX_INVALID_HANDLE;
}

} // namespace qlcrender
