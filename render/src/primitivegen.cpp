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
#include <algorithm>
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

// Generate a unit cylinder (r=0.5, h=1.0, Y-axis aligned) into raw buffers.
// Extent is 1×1×1, centered at origin.
static void generateUnitCylinder(std::vector<PNVertex> &v, std::vector<uint16_t> &idx,
                                  int segments)
{
    const float radius = 0.5f;
    const float hh = 0.5f;
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
}

// Generate a unit sphere (r=0.5) into raw buffers. Extent is 1×1×1.
static void generateUnitSphere(std::vector<PNVertex> &v, std::vector<uint16_t> &idx,
                                int stacks, int slices)
{
    const float radius = 0.5f;
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
}

// Scale raw vertex positions by (sx, sy, sz) and fix normals for non-uniform scale.
static void scaleRawVertices(std::vector<PNVertex> &verts, float sx, float sy, float sz)
{
    for (auto &v : verts)
    {
        v.x *= sx;
        v.y *= sy;
        v.z *= sz;

        // Normals: multiply by inverse scale, renormalize
        float nx = v.nx / sx;
        float ny = v.ny / sy;
        float nz = v.nz / sz;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-7f) { nx /= len; ny /= len; nz /= len; }
        v.nx = nx; v.ny = ny; v.nz = nz;
    }
}

// Legacy wrappers used by BgfxRenderer fallback cube
static LoadedMesh makeCylinder(float radius, float height, int segments)
{
    std::vector<PNVertex> v;
    std::vector<uint16_t> idx;
    generateUnitCylinder(v, idx, segments);
    scaleRawVertices(v, radius * 2.0f, height, radius * 2.0f);
    return createMesh(v, idx);
}

static LoadedMesh makeSphere(float radius, int stacks, int slices)
{
    std::vector<PNVertex> v;
    std::vector<uint16_t> idx;
    generateUnitSphere(v, idx, stacks, slices);
    float d = radius * 2.0f;
    scaleRawVertices(v, d, d, d);
    return createMesh(v, idx);
}

// ---------------------------------------------------------------------------
// Default dimensions for each primitive type (used when GDTF doesn't specify)
// ---------------------------------------------------------------------------
static void defaultDimensions(int primitiveType, float &length, float &width, float &height)
{
    switch (primitiveType)
    {
    case PrimitiveCube_:           length = 0.2f;  width = 0.2f;  height = 0.2f;  break;
    case PrimitiveCylinder_:       length = 0.2f;  width = 0.3f;  height = 0.2f;  break;
    case PrimitiveSphere_:         length = 0.2f;  width = 0.2f;  height = 0.2f;  break;
    case PrimitiveBase_:
    case PrimitiveBase1_1_:        length = 0.3f;  width = 0.05f; height = 0.3f;  break;
    case PrimitiveYoke_:           length = 0.25f; width = 0.25f; height = 0.08f; break;
    case PrimitiveHead_:           length = 0.18f; width = 0.12f; height = 0.2f;  break;
    case PrimitiveScanner_:
    case PrimitiveScanner1_1_:     length = 0.3f;  width = 0.1f;  height = 0.15f; break;
    case PrimitiveConventional_:
    case PrimitiveConventional1_1_:length = 0.3f;  width = 0.35f; height = 0.3f;  break;
    case PrimitivePigtail_:        length = 0.02f; width = 0.1f;  height = 0.02f; break;
    default:                       length = 0.2f;  width = 0.2f;  height = 0.2f;  break;
    }
}

// ---------------------------------------------------------------------------
// Public factory method
// ---------------------------------------------------------------------------
LoadedMesh PrimitiveGen::generate(int primitiveType, float length, float width, float height)
{
    // Use defaults if no valid dimensions provided
    if (length < 0.001f || width < 0.001f || height < 0.001f)
        defaultDimensions(primitiveType, length, width, height);

    // Clamp to minimum sensible size
    length = std::max(length, 0.01f);
    width  = std::max(width,  0.01f);
    height = std::max(height, 0.01f);

    switch (primitiveType)
    {
    // Cube-based types: direct box at GDTF dimensions
    case PrimitiveCube_:
    case PrimitiveBase_:
    case PrimitiveBase1_1_:
    case PrimitiveYoke_:
    case PrimitiveHead_:
    case PrimitiveScanner_:
    case PrimitiveScanner1_1_:
        return makeCube(length, width, height);

    // Cylinder-based types: generate unit cylinder then scale by (L, W, H).
    // This produces non-uniform scaling (elliptical cross-section) matching
    // BlenderDMX's approach. A mirror disc (L=100, W=155, H=15mm) becomes
    // a flat disc, not a tall pipe.
    case PrimitiveCylinder_:
    case PrimitiveConventional_:
    case PrimitiveConventional1_1_:
    case PrimitivePigtail_:
    {
        std::vector<PNVertex> v;
        std::vector<uint16_t> idx;
        generateUnitCylinder(v, idx, 16);
        scaleRawVertices(v, length, width, height);
        return createMesh(v, idx);
    }

    // Sphere: generate unit sphere then scale by (L, W, H) for ellipsoids.
    case PrimitiveSphere_:
    {
        std::vector<PNVertex> v;
        std::vector<uint16_t> idx;
        generateUnitSphere(v, idx, 12, 24);
        scaleRawVertices(v, length, width, height);
        return createMesh(v, idx);
    }

    // Unknown type: fallback to cube
    default:
        return makeCube(length, width, height);
    }
}

} // namespace qlcrender
