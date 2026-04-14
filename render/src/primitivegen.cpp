/*
  Q Light Controller Plus
  primitivegen.cpp

  Copyright (C) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "primitivegen.h"
#include <bgfx/bgfx.h>
#include <cmath>
#include <vector>

// Must match GDTFPrimitiveType in gdtfgeometrydata.h
enum
{
    PrimitiveUndefined_ = 0,
    PrimitiveCube_ = 1,
    PrimitiveCylinder_ = 2,
    PrimitiveSphere_ = 3,
    PrimitiveBase_ = 4,
    PrimitiveYoke_ = 5,
    PrimitiveHead_ = 6,
    PrimitiveScanner_ = 7,
    PrimitiveConventional_ = 8,
    PrimitivePigtail_ = 9,
    PrimitiveBase1_1_ = 10,
    PrimitiveScanner1_1_ = 11,
    PrimitiveConventional1_1_ = 12,
};

namespace qlcrender {

struct PNVertex
{
    float x, y, z;
    float nx, ny, nz;
};

static bgfx::VertexLayout s_layout;
static bool s_layoutInit = false;

static void ensureLayout()
{
    if (!s_layoutInit)
    {
        s_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .end();
        s_layoutInit = true;
    }
}

static LoadedMesh createMesh(const std::vector<PNVertex> &verts,
                              const std::vector<uint16_t> &indices)
{
    LoadedMesh mesh;
    ensureLayout();
    mesh.layout = s_layout;
    mesh.numVertices = static_cast<uint32_t>(verts.size());
    mesh.numIndices = static_cast<uint32_t>(indices.size());

    const bgfx::Memory *vbMem = bgfx::copy(verts.data(), mesh.numVertices * sizeof(PNVertex));
    mesh.vbh = bgfx::createVertexBuffer(vbMem, s_layout);

    const bgfx::Memory *ibMem = bgfx::copy(indices.data(), mesh.numIndices * sizeof(uint16_t));
    mesh.ibh = bgfx::createIndexBuffer(ibMem);

    return mesh;
}

static LoadedMesh makeCube(float sx, float sy, float sz)
{
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    std::vector<PNVertex> v;
    std::vector<uint16_t> idx;

    auto face = [&](float nx, float ny, float nz,
                     float ax, float ay, float az,
                     float bx, float by, float bz)
    {
        uint16_t base = static_cast<uint16_t>(v.size());
        float cx = nx * hx, cy = ny * hy, cz = nz * hz;
        v.push_back({cx - ax - bx, cy - ay - by, cz - az - bz, nx, ny, nz});
        v.push_back({cx + ax - bx, cy + ay - by, cz + az - bz, nx, ny, nz});
        v.push_back({cx + ax + bx, cy + ay + by, cz + az + bz, nx, ny, nz});
        v.push_back({cx - ax + bx, cy - ay + by, cz - az + bz, nx, ny, nz});
        idx.insert(idx.end(), {base, uint16_t(base + 1), uint16_t(base + 2),
                                base, uint16_t(base + 2), uint16_t(base + 3)});
    };

    face( 0, 0, 1,  hx, 0, 0,  0, hy, 0);  // +Z
    face( 0, 0,-1, -hx, 0, 0,  0, hy, 0);  // -Z
    face( 1, 0, 0,  0, 0,-hz,  0, hy, 0);  // +X
    face(-1, 0, 0,  0, 0, hz,  0, hy, 0);  // -X
    face( 0, 1, 0,  hx, 0, 0,  0, 0,-hz);  // +Y
    face( 0,-1, 0,  hx, 0, 0,  0, 0, hz);  // -Y

    return createMesh(v, idx);
}

static LoadedMesh makeCylinder(float radius, float height, int segments)
{
    std::vector<PNVertex> v;
    std::vector<uint16_t> idx;
    float hh = height * 0.5f;
    const float pi = 3.14159265358979323846f;

    for (int i = 0; i <= segments; i++)
    {
        float angle = 2.0f * pi * float(i) / float(segments);
        float cs = cosf(angle);
        float sn = sinf(angle);
        float x = radius * cs;
        float z = radius * sn;
        v.push_back({x,  hh, z, cs, 0, sn});  // top ring
        v.push_back({x, -hh, z, cs, 0, sn});  // bottom ring
    }

    for (int i = 0; i < segments; i++)
    {
        uint16_t t0 = i * 2, t1 = t0 + 1, t2 = t0 + 2, t3 = t0 + 3;
        idx.insert(idx.end(), {t0, t1, t2, t2, t1, t3});
    }

    // Top cap
    uint16_t topCenter = static_cast<uint16_t>(v.size());
    v.push_back({0, hh, 0, 0, 1, 0});
    for (int i = 0; i < segments; i++)
    {
        uint16_t a = static_cast<uint16_t>(i * 2);
        uint16_t b = static_cast<uint16_t>((i + 1) * 2);
        idx.insert(idx.end(), {topCenter, a, b});
    }

    // Bottom cap
    uint16_t botCenter = static_cast<uint16_t>(v.size());
    v.push_back({0, -hh, 0, 0, -1, 0});
    for (int i = 0; i < segments; i++)
    {
        uint16_t a = static_cast<uint16_t>(i * 2 + 1);
        uint16_t b = static_cast<uint16_t>((i + 1) * 2 + 1);
        idx.insert(idx.end(), {botCenter, b, a});
    }

    return createMesh(v, idx);
}

static LoadedMesh makeSphere(float radius, int stacks, int slices)
{
    std::vector<PNVertex> v;
    std::vector<uint16_t> idx;
    const float pi = 3.14159265358979323846f;

    for (int i = 0; i <= stacks; i++)
    {
        float phi = pi * float(i) / float(stacks);
        float sinPhi = sinf(phi), cosPhi = cosf(phi);
        for (int j = 0; j <= slices; j++)
        {
            float theta = 2.0f * pi * float(j) / float(slices);
            float x = sinPhi * cosf(theta);
            float y = sinPhi * sinf(theta);
            float z = cosPhi;
            v.push_back({x * radius, y * radius, z * radius, x, y, z});
        }
    }

    for (int i = 0; i < stacks; i++)
    {
        for (int j = 0; j < slices; j++)
        {
            uint16_t a = i * (slices + 1) + j;
            uint16_t b = a + slices + 1;
            idx.insert(idx.end(), {a, b, uint16_t(a + 1),
                                   uint16_t(a + 1), b, uint16_t(b + 1)});
        }
    }

    return createMesh(v, idx);
}

PrimitiveGen::PrimitiveGen() = default;
PrimitiveGen::~PrimitiveGen() { shutdown(); }

void PrimitiveGen::init()
{
    ensureLayout();

    // Cube: 0.2m unit cube
    m_meshes[PrimitiveCube_] = makeCube(0.2f, 0.2f, 0.2f);

    // Cylinder: r=0.1m, h=0.3m
    m_meshes[PrimitiveCylinder_] = makeCylinder(0.1f, 0.3f, 16);

    // Sphere: r=0.1m
    m_meshes[PrimitiveSphere_] = makeSphere(0.1f, 12, 24);

    // Base: flat box
    m_meshes[PrimitiveBase_] = makeCube(0.3f, 0.05f, 0.3f);

    // Yoke: taller, narrower box (simplified U-bracket)
    m_meshes[PrimitiveYoke_] = makeCube(0.25f, 0.25f, 0.08f);

    // Head: compact box
    m_meshes[PrimitiveHead_] = makeCube(0.18f, 0.12f, 0.2f);

    // Scanner: flat body
    m_meshes[PrimitiveScanner_] = makeCube(0.3f, 0.1f, 0.15f);

    // Conventional: cylinder (PAR can / laser housing shape)
    m_meshes[PrimitiveConventional_] = makeCylinder(0.15f, 0.35f, 16);

    // Pigtail: small cylinder
    m_meshes[PrimitivePigtail_] = makeCylinder(0.01f, 0.1f, 8);

    // 1.1 variants: generate their own meshes (same shapes as 1.0 counterparts)
    m_meshes[PrimitiveBase1_1_] = makeCube(0.3f, 0.05f, 0.3f);
    m_meshes[PrimitiveScanner1_1_] = makeCube(0.3f, 0.1f, 0.15f);
    m_meshes[PrimitiveConventional1_1_] = makeCylinder(0.15f, 0.35f, 16);
}

const LoadedMesh *PrimitiveGen::getPrimitive(int primitiveType) const
{
    auto it = m_meshes.find(primitiveType);
    if (it != m_meshes.end())
        return &it->second;

    // Fallback to cube for unknown types
    it = m_meshes.find(PrimitiveCube_);
    if (it != m_meshes.end())
        return &it->second;

    return nullptr;
}

void PrimitiveGen::shutdown()
{
    for (auto &pair : m_meshes)
        pair.second.destroy();
    m_meshes.clear();
}

} // namespace qlcrender
