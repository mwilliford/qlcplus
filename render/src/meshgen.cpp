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

// ---------------------------------------------------------------------------
// Unit sphere (UV sphere with normals)
// ---------------------------------------------------------------------------

bgfx::VertexLayout PosNormalVertex::layout;

void PosNormalVertex::init()
{
    layout
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
}

void createSphereMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh,
                      int subdivisions)
{
    const int stacks = subdivisions;
    const int slices = subdivisions * 2;
    const float pi = 3.14159265358979323846f;

    // Generate vertices
    std::vector<PosNormalVertex> vertices;
    vertices.reserve((stacks + 1) * (slices + 1));

    for (int i = 0; i <= stacks; i++)
    {
        float phi = pi * float(i) / float(stacks);
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);

        for (int j = 0; j <= slices; j++)
        {
            float theta = 2.0f * pi * float(j) / float(slices);
            float sinTheta = sinf(theta);
            float cosTheta = cosf(theta);

            float x = sinPhi * cosTheta;
            float y = sinPhi * sinTheta;
            float z = cosPhi;

            vertices.push_back({x, y, z, x, y, z});  // position = normal for unit sphere
        }
    }

    // Generate indices
    std::vector<uint16_t> indices;
    indices.reserve(stacks * slices * 6);

    for (int i = 0; i < stacks; i++)
    {
        for (int j = 0; j < slices; j++)
        {
            uint16_t a = uint16_t(i * (slices + 1) + j);
            uint16_t b = uint16_t(a + slices + 1);

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(uint16_t(a + 1));

            indices.push_back(uint16_t(a + 1));
            indices.push_back(b);
            indices.push_back(uint16_t(b + 1));
        }
    }

    vbh = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(PosNormalVertex))),
        PosNormalVertex::layout
    );

    ibh = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t)))
    );
}

void destroySphereMesh(bgfx::VertexBufferHandle& vbh, bgfx::IndexBufferHandle& ibh)
{
    if (bgfx::isValid(vbh))
        bgfx::destroy(vbh);
    if (bgfx::isValid(ibh))
        bgfx::destroy(ibh);
    vbh = BGFX_INVALID_HANDLE;
    ibh = BGFX_INVALID_HANDLE;
}

} // namespace qlcrender
