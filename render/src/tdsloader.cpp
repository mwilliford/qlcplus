/*
  Q Light Controller Plus
  tdsloader.cpp

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

#include "tdsloader.h"
#include <bgfx/bgfx.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace qlcrender {

// 3DS chunk IDs
static constexpr uint16_t CHUNK_MAIN      = 0x4D4D;
static constexpr uint16_t CHUNK_EDITOR    = 0x3D3D;
static constexpr uint16_t CHUNK_OBJECT    = 0x4000;
static constexpr uint16_t CHUNK_TRIMESH   = 0x4100;
static constexpr uint16_t CHUNK_VERTICES  = 0x4110;
static constexpr uint16_t CHUNK_FACES     = 0x4120;

struct PosNormalVertex
{
    float x, y, z;
    float nx, ny, nz;
};

static bgfx::VertexLayout s_posNormalLayout;
static bool s_layoutInit = false;

static void ensureLayout()
{
    if (!s_layoutInit)
    {
        s_posNormalLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .end();
        s_layoutInit = true;
    }
}

static uint16_t readU16(const unsigned char *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t readU32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float readF32(const unsigned char *p) { float v; memcpy(&v, p, 4); return v; }

/**
 * Walk a 3DS chunk tree, collecting vertices and face indices from all
 * TRIMESH objects. Chunks are: 2-byte ID + 4-byte length (includes header).
 */
static void parseChunk(const unsigned char *data, size_t offset, size_t end,
                        std::vector<float> &allVerts,
                        std::vector<uint16_t> &allFaces,
                        uint16_t &vertBase)
{
    while (offset + 6 <= end)
    {
        uint16_t chunkId = readU16(data + offset);
        uint32_t chunkLen = readU32(data + offset + 2);
        if (chunkLen < 6 || offset + chunkLen > end)
            break;

        size_t chunkEnd = offset + chunkLen;
        size_t childStart = offset + 6;

        switch (chunkId)
        {
        case CHUNK_MAIN:
        case CHUNK_EDITOR:
        case CHUNK_TRIMESH:
            parseChunk(data, childStart, chunkEnd, allVerts, allFaces, vertBase);
            break;

        case CHUNK_OBJECT:
        {
            // Object name is a null-terminated string after the 6-byte header
            size_t nameEnd = childStart;
            while (nameEnd < chunkEnd && data[nameEnd] != 0)
                nameEnd++;
            if (nameEnd < chunkEnd)
                nameEnd++; // skip null terminator
            parseChunk(data, nameEnd, chunkEnd, allVerts, allFaces, vertBase);
            break;
        }

        case CHUNK_VERTICES:
        {
            if (childStart + 2 > chunkEnd) break;
            uint16_t count = readU16(data + childStart);
            size_t dataOff = childStart + 2;
            if (dataOff + count * 12 > chunkEnd) break;

            for (uint16_t i = 0; i < count; i++)
            {
                // 3DS uses X-right, Z-up convention. Our renderer uses Z-up too,
                // but 3DS Y is depth. Convert: 3DS(x,y,z) → render(x,z,y)
                // to swap Y↔Z if needed. Actually, keep as-is and let the
                // GDTF Position matrix handle orientation.
                allVerts.push_back(readF32(data + dataOff + i * 12 + 0));
                allVerts.push_back(readF32(data + dataOff + i * 12 + 4));
                allVerts.push_back(readF32(data + dataOff + i * 12 + 8));
            }
            break;
        }

        case CHUNK_FACES:
        {
            if (childStart + 2 > chunkEnd) break;
            uint16_t count = readU16(data + childStart);
            size_t dataOff = childStart + 2;
            if (dataOff + count * 8 > chunkEnd) break;  // 4 uint16 per face (v1,v2,v3,flags)

            for (uint16_t i = 0; i < count; i++)
            {
                uint16_t v0 = readU16(data + dataOff + i * 8 + 0) + vertBase;
                uint16_t v1 = readU16(data + dataOff + i * 8 + 2) + vertBase;
                uint16_t v2 = readU16(data + dataOff + i * 8 + 4) + vertBase;
                // flags at +6 ignored
                allFaces.push_back(v0);
                allFaces.push_back(v1);
                allFaces.push_back(v2);
            }
            // Update vertex base for the next object in this file
            vertBase = static_cast<uint16_t>(allVerts.size() / 3);
            break;
        }

        default:
            break;
        }

        offset = chunkEnd;
    }
}

LoadedMesh TdsLoader::loadFromMemory(const unsigned char *data, size_t length,
                                      const std::string &debugName,
                                      float targetLength, float targetWidth,
                                      float targetHeight)
{
    LoadedMesh result;
    result.vbh = BGFX_INVALID_HANDLE;
    result.ibh = BGFX_INVALID_HANDLE;
    result.numVertices = 0;
    result.numIndices = 0;

    ensureLayout();
    result.layout = s_posNormalLayout;

    if (length < 6)
    {
        fprintf(stderr, "TdsLoader: '%s' too small (%zu bytes)\n", debugName.c_str(), length);
        return result;
    }

    // Parse all mesh objects from the 3DS chunk tree
    std::vector<float> rawVerts;    // flat: x,y,z,x,y,z,...
    std::vector<uint16_t> rawFaces; // flat: v0,v1,v2,v0,v1,v2,...
    uint16_t vertBase = 0;

    parseChunk(data, 0, length, rawVerts, rawFaces, vertBase);

    size_t numRawVerts = rawVerts.size() / 3;
    size_t numTris = rawFaces.size() / 3;

    if (numRawVerts == 0 || numTris == 0)
    {
        fprintf(stderr, "TdsLoader: '%s' produced no geometry\n", debugName.c_str());
        return result;
    }

    // Compute bounding box for per-axis scaling.
    // Following BlenderDMX: scale each axis so mesh extent matches the
    // GDTF Model dimensions (Length→X, Width→Y, Height→Z).
    float bboxMin[3] = { 1e30f,  1e30f,  1e30f};
    float bboxMax[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < numRawVerts; i++)
    {
        for (int a = 0; a < 3; a++)
        {
            float v = rawVerts[i * 3 + a];
            if (v < bboxMin[a]) bboxMin[a] = v;
            if (v > bboxMax[a]) bboxMax[a] = v;
        }
    }
    float extents[3] = {
        bboxMax[0] - bboxMin[0],
        bboxMax[1] - bboxMin[1],
        bboxMax[2] - bboxMin[2]
    };
    float targets[3] = { targetLength, targetWidth, targetHeight };

    // Per-axis scale: target / mesh_extent. Skip axes with zero extent.
    float scaleFactors[3] = {1.0f, 1.0f, 1.0f};
    bool needsScale = false;
    for (int a = 0; a < 3; a++)
    {
        if (targets[a] > 1e-6f && extents[a] > 1e-6f)
        {
            scaleFactors[a] = targets[a] / extents[a];
            if (std::abs(scaleFactors[a] - 1.0f) > 0.01f)
                needsScale = true;
        }
    }

    if (needsScale)
    {
        // Scale per-axis without centering — the 3DS model's baked-in
        // offset is intentional (matches BlenderDMX behavior).
        for (size_t i = 0; i < rawVerts.size(); i += 3)
        {
            rawVerts[i + 0] *= scaleFactors[0];
            rawVerts[i + 1] *= scaleFactors[1];
            rawVerts[i + 2] *= scaleFactors[2];
        }
        fprintf(stderr, "TdsLoader: '%s' scaled per-axis (%.3f, %.3f, %.3f) "
                "mesh(%.3f,%.3f,%.3f) → target(%.3f,%.3f,%.3f)\n",
                debugName.c_str(), scaleFactors[0], scaleFactors[1], scaleFactors[2],
                extents[0], extents[1], extents[2],
                targets[0], targets[1], targets[2]);
    }

    // Build PosNormalVertex array with smooth (area-weighted) vertex normals.
    std::vector<PosNormalVertex> vertices(numRawVerts);
    for (size_t i = 0; i < numRawVerts; i++)
    {
        vertices[i].x = rawVerts[i * 3 + 0];
        vertices[i].y = rawVerts[i * 3 + 1];
        vertices[i].z = rawVerts[i * 3 + 2];
        vertices[i].nx = 0.0f;
        vertices[i].ny = 0.0f;
        vertices[i].nz = 0.0f;
    }

    // Accumulate face normals (area-weighted by cross product magnitude)
    std::vector<uint32_t> indices;
    indices.reserve(numTris * 3);
    for (size_t t = 0; t < numTris; t++)
    {
        uint32_t i0 = rawFaces[t * 3 + 0];
        uint32_t i1 = rawFaces[t * 3 + 1];
        uint32_t i2 = rawFaces[t * 3 + 2];
        if (i0 >= numRawVerts || i1 >= numRawVerts || i2 >= numRawVerts)
            continue;

        float e1x = vertices[i1].x - vertices[i0].x;
        float e1y = vertices[i1].y - vertices[i0].y;
        float e1z = vertices[i1].z - vertices[i0].z;
        float e2x = vertices[i2].x - vertices[i0].x;
        float e2y = vertices[i2].y - vertices[i0].y;
        float e2z = vertices[i2].z - vertices[i0].z;
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;

        vertices[i0].nx += nx; vertices[i0].ny += ny; vertices[i0].nz += nz;
        vertices[i1].nx += nx; vertices[i1].ny += ny; vertices[i1].nz += nz;
        vertices[i2].nx += nx; vertices[i2].ny += ny; vertices[i2].nz += nz;

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
    }

    // Normalize accumulated normals
    for (auto &v : vertices)
    {
        float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (len > 1e-8f) { v.nx /= len; v.ny /= len; v.nz /= len; }
    }

    if (vertices.empty() || indices.empty())
    {
        fprintf(stderr, "TdsLoader: '%s' produced no valid triangles\n", debugName.c_str());
        return result;
    }

    result.numVertices = static_cast<uint32_t>(vertices.size());
    result.numIndices = static_cast<uint32_t>(indices.size());

    const bgfx::Memory *vbMem = bgfx::copy(vertices.data(),
                                             result.numVertices * sizeof(PosNormalVertex));
    result.vbh = bgfx::createVertexBuffer(vbMem, s_posNormalLayout);

    // Use 32-bit index buffer for large meshes (>65535 vertices)
    bool use32bit = (result.numVertices > 65535);
    if (use32bit)
    {
        const bgfx::Memory *ibMem = bgfx::copy(indices.data(),
                                                 result.numIndices * sizeof(uint32_t));
        result.ibh = bgfx::createIndexBuffer(ibMem, BGFX_BUFFER_INDEX32);
    }
    else
    {
        std::vector<uint16_t> indices16(indices.begin(), indices.end());
        const bgfx::Memory *ibMem = bgfx::copy(indices16.data(),
                                                 result.numIndices * sizeof(uint16_t));
        result.ibh = bgfx::createIndexBuffer(ibMem);
    }

    fprintf(stderr, "TdsLoader: '%s' loaded %u verts, %u indices (bbox max %.2fm)\n",
            debugName.c_str(), result.numVertices, result.numIndices, bboxMax);

    return result;
}

} // namespace qlcrender
