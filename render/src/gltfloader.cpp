/*
  Q Light Controller Plus
  gltfloader.cpp

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

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include "gltfloader.h"
#include <bgfx/bgfx.h>
#include <cstdio>
#include <vector>

namespace qlcrender {

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

LoadedMesh GltfLoader::loadFromMemory(const unsigned char *data, size_t length,
                                       const std::string &debugName)
{
    LoadedMesh result;
    result.vbh = BGFX_INVALID_HANDLE;
    result.ibh = BGFX_INVALID_HANDLE;
    result.numVertices = 0;
    result.numIndices = 0;

    ensureLayout();
    result.layout = s_posNormalLayout;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromMemory(&model, &err, &warn, data, length);
    if (!ok)
    {
        fprintf(stderr, "GltfLoader: failed to load '%s': %s\n",
                debugName.c_str(), err.c_str());
        return result;
    }

    if (!warn.empty())
        fprintf(stderr, "GltfLoader: warnings for '%s': %s\n",
                debugName.c_str(), warn.c_str());

    if (model.meshes.empty())
    {
        fprintf(stderr, "GltfLoader: '%s' has no meshes\n", debugName.c_str());
        return result;
    }

    // Collect all primitives from all meshes into a single vertex/index buffer
    std::vector<PosNormalVertex> vertices;
    std::vector<uint16_t> indices;

    for (const auto &mesh : model.meshes)
    {
        for (const auto &prim : mesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
                prim.mode != -1)  // -1 = default (triangles)
                continue;

            // Find POSITION accessor
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
                continue;
            const auto &posAccessor = model.accessors[posIt->second];
            const auto &posView = model.bufferViews[posAccessor.bufferView];
            const auto &posBuf = model.buffers[posView.buffer];
            const float *posData = reinterpret_cast<const float *>(
                posBuf.data.data() + posView.byteOffset + posAccessor.byteOffset);
            size_t posStride = posView.byteStride ? posView.byteStride / sizeof(float) : 3;

            // Find NORMAL accessor (optional)
            const float *normData = nullptr;
            size_t normStride = 3;
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end())
            {
                const auto &normAccessor = model.accessors[normIt->second];
                const auto &normView = model.bufferViews[normAccessor.bufferView];
                const auto &normBuf = model.buffers[normView.buffer];
                normData = reinterpret_cast<const float *>(
                    normBuf.data.data() + normView.byteOffset + normAccessor.byteOffset);
                normStride = normView.byteStride ? normView.byteStride / sizeof(float) : 3;
            }

            uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

            // Vertices
            for (size_t v = 0; v < posAccessor.count; v++)
            {
                PosNormalVertex vert;
                vert.x = posData[v * posStride + 0];
                vert.y = posData[v * posStride + 1];
                vert.z = posData[v * posStride + 2];
                if (normData)
                {
                    vert.nx = normData[v * normStride + 0];
                    vert.ny = normData[v * normStride + 1];
                    vert.nz = normData[v * normStride + 2];
                }
                else
                {
                    vert.nx = 0.0f;
                    vert.ny = 1.0f;
                    vert.nz = 0.0f;
                }
                vertices.push_back(vert);
            }

            // Indices
            if (prim.indices >= 0)
            {
                const auto &idxAccessor = model.accessors[prim.indices];
                const auto &idxView = model.bufferViews[idxAccessor.bufferView];
                const auto &idxBuf = model.buffers[idxView.buffer];
                const uint8_t *idxBase = idxBuf.data.data() + idxView.byteOffset + idxAccessor.byteOffset;

                for (size_t i = 0; i < idxAccessor.count; i++)
                {
                    uint32_t idx = 0;
                    switch (idxAccessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        idx = reinterpret_cast<const uint16_t *>(idxBase)[i];
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        idx = reinterpret_cast<const uint32_t *>(idxBase)[i];
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        idx = idxBase[i];
                        break;
                    default:
                        break;
                    }
                    indices.push_back(static_cast<uint16_t>(baseVertex + idx));
                }
            }
            else
            {
                // Non-indexed: generate sequential indices
                for (size_t i = 0; i < posAccessor.count; i++)
                    indices.push_back(static_cast<uint16_t>(baseVertex + i));
            }
        }
    }

    if (vertices.empty() || indices.empty())
    {
        fprintf(stderr, "GltfLoader: '%s' produced no geometry\n", debugName.c_str());
        return result;
    }

    result.numVertices = static_cast<uint32_t>(vertices.size());
    result.numIndices = static_cast<uint32_t>(indices.size());

    const bgfx::Memory *vbMem = bgfx::copy(vertices.data(),
                                             result.numVertices * sizeof(PosNormalVertex));
    result.vbh = bgfx::createVertexBuffer(vbMem, s_posNormalLayout);

    const bgfx::Memory *ibMem = bgfx::copy(indices.data(),
                                             result.numIndices * sizeof(uint16_t));
    result.ibh = bgfx::createIndexBuffer(ibMem);

    fprintf(stderr, "GltfLoader: '%s' loaded %u verts, %u indices\n",
            debugName.c_str(), result.numVertices, result.numIndices);

    return result;
}

} // namespace qlcrender
