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
#include <cmath>
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

// ---------------------------------------------------------------------------
// Build a column-major 4x4 transform from a tinygltf Node's TRS or matrix.
// ---------------------------------------------------------------------------
static void computeNodeTransform(const tinygltf::Node &node, float out[16])
{
    if (node.matrix.size() == 16)
    {
        // glTF stores matrices in column-major order — copy directly
        for (int i = 0; i < 16; i++)
            out[i] = static_cast<float>(node.matrix[i]);
        return;
    }

    // Compose T * R * S from individual components
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    if (node.translation.size() == 3)
    {
        tx = static_cast<float>(node.translation[0]);
        ty = static_cast<float>(node.translation[1]);
        tz = static_cast<float>(node.translation[2]);
    }

    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
    if (node.rotation.size() == 4)
    {
        qx = static_cast<float>(node.rotation[0]);
        qy = static_cast<float>(node.rotation[1]);
        qz = static_cast<float>(node.rotation[2]);
        qw = static_cast<float>(node.rotation[3]);
    }

    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
    if (node.scale.size() == 3)
    {
        sx = static_cast<float>(node.scale[0]);
        sy = static_cast<float>(node.scale[1]);
        sz = static_cast<float>(node.scale[2]);
    }

    // Quaternion to 3x3 rotation matrix (column-major)
    float x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
    float xx = qx * x2, xy = qx * y2, xz = qx * z2;
    float yy = qy * y2, yz = qy * z2, zz = qz * z2;
    float wx = qw * x2, wy = qw * y2, wz = qw * z2;

    // Column 0 (X axis) * sx
    out[0]  = (1.0f - (yy + zz)) * sx;
    out[1]  = (xy + wz) * sx;
    out[2]  = (xz - wy) * sx;
    out[3]  = 0.0f;

    // Column 1 (Y axis) * sy
    out[4]  = (xy - wz) * sy;
    out[5]  = (1.0f - (xx + zz)) * sy;
    out[6]  = (yz + wx) * sy;
    out[7]  = 0.0f;

    // Column 2 (Z axis) * sz
    out[8]  = (xz + wy) * sz;
    out[9]  = (yz - wx) * sz;
    out[10] = (1.0f - (xx + yy)) * sz;
    out[11] = 0.0f;

    // Column 3 (translation)
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
    out[15] = 1.0f;
}

// ---------------------------------------------------------------------------
// 4x4 column-major matrix multiply: result = A * B
// ---------------------------------------------------------------------------
static void mtx4Mul(float result[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            tmp[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    for (int i = 0; i < 16; i++)
        result[i] = tmp[i];
}

// ---------------------------------------------------------------------------
// Transform a position by a 4x4 column-major matrix (homogeneous, w=1)
// ---------------------------------------------------------------------------
static void transformPos(float &ox, float &oy, float &oz,
                         float ix, float iy, float iz, const float m[16])
{
    ox = m[0] * ix + m[4] * iy + m[8]  * iz + m[12];
    oy = m[1] * ix + m[5] * iy + m[9]  * iz + m[13];
    oz = m[2] * ix + m[6] * iy + m[10] * iz + m[14];
}

// ---------------------------------------------------------------------------
// Transform a normal by the upper-left 3x3 of a 4x4, then normalize.
// ---------------------------------------------------------------------------
static void transformNormal(float &ox, float &oy, float &oz,
                            float ix, float iy, float iz, const float m[16])
{
    float nx = m[0] * ix + m[4] * iy + m[8]  * iz;
    float ny = m[1] * ix + m[5] * iy + m[9]  * iz;
    float nz = m[2] * ix + m[6] * iy + m[10] * iz;
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-7f) { nx /= len; ny /= len; nz /= len; }
    ox = nx; oy = ny; oz = nz;
}

// ---------------------------------------------------------------------------
// Recursively collect transformed vertices from the glTF node tree.
// ---------------------------------------------------------------------------
static void collectNodeVertices(const tinygltf::Model &model, int nodeIndex,
                                const float parentMtx[16],
                                std::vector<PosNormalVertex> &vertices,
                                std::vector<uint16_t> &indices)
{
    const auto &node = model.nodes[nodeIndex];

    // Compute this node's world transform
    float localMtx[16];
    computeNodeTransform(node, localMtx);

    float worldMtx[16];
    mtx4Mul(worldMtx, parentMtx, localMtx);

    // If this node references a mesh, extract and transform its vertices
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
    {
        const auto &mesh = model.meshes[node.mesh];
        for (const auto &prim : mesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
                prim.mode != -1)  // -1 = default (triangles)
                continue;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
                continue;
            const auto &posAccessor = model.accessors[posIt->second];
            const auto &posView = model.bufferViews[posAccessor.bufferView];
            const auto &posBuf = model.buffers[posView.buffer];
            const float *posData = reinterpret_cast<const float *>(
                posBuf.data.data() + posView.byteOffset + posAccessor.byteOffset);
            size_t posStride = posView.byteStride ? posView.byteStride / sizeof(float) : 3;

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

            for (size_t v = 0; v < posAccessor.count; v++)
            {
                PosNormalVertex vert;
                float px = posData[v * posStride + 0];
                float py = posData[v * posStride + 1];
                float pz = posData[v * posStride + 2];
                transformPos(vert.x, vert.y, vert.z, px, py, pz, worldMtx);

                if (normData)
                {
                    float inx = normData[v * normStride + 0];
                    float iny = normData[v * normStride + 1];
                    float inz = normData[v * normStride + 2];
                    transformNormal(vert.nx, vert.ny, vert.nz, inx, iny, inz, worldMtx);
                }
                else
                {
                    vert.nx = 0.0f;
                    vert.ny = 1.0f;
                    vert.nz = 0.0f;
                }
                vertices.push_back(vert);
            }

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
                for (size_t i = 0; i < posAccessor.count; i++)
                    indices.push_back(static_cast<uint16_t>(baseVertex + i));
            }
        }
    }

    // Recurse into children
    for (int childIdx : node.children)
    {
        if (childIdx >= 0 && childIdx < static_cast<int>(model.nodes.size()))
            collectNodeVertices(model, childIdx, worldMtx, vertices, indices);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
LoadedMesh GltfLoader::loadFromMemory(const unsigned char *data, size_t length,
                                       const std::string &debugName,
                                       float targetLength,
                                       float targetWidth,
                                       float targetHeight)
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

    // Walk the node tree, applying node transforms to vertices
    std::vector<PosNormalVertex> vertices;
    std::vector<uint16_t> indices;

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    // Determine scene root nodes
    std::vector<int> rootNodes;
    if (!model.scenes.empty())
    {
        int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
        rootNodes = model.scenes[sceneIdx].nodes;
    }
    else
    {
        // No scenes defined — use all top-level nodes
        for (int i = 0; i < static_cast<int>(model.nodes.size()); i++)
            rootNodes.push_back(i);
    }

    for (int rootIdx : rootNodes)
    {
        if (rootIdx >= 0 && rootIdx < static_cast<int>(model.nodes.size()))
            collectNodeVertices(model, rootIdx, identity, vertices, indices);
    }

    if (vertices.empty() || indices.empty())
    {
        fprintf(stderr, "GltfLoader: '%s' produced no geometry\n", debugName.c_str());
        return result;
    }

    // Scale to GDTF target dimensions if provided.
    // This handles both unit correction and axis remapping — some GDTF
    // models are authored with a different axis convention than L→X, W→Y,
    // H→Z, and the dimension scaling implicitly reorients the mesh.
    if (targetLength > 0.001f && targetWidth > 0.001f && targetHeight > 0.001f)
    {
        // Compute bounding box
        float minX = vertices[0].x, maxX = vertices[0].x;
        float minY = vertices[0].y, maxY = vertices[0].y;
        float minZ = vertices[0].z, maxZ = vertices[0].z;
        for (const auto &v : vertices)
        {
            if (v.x < minX) minX = v.x; if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y; if (v.y > maxY) maxY = v.y;
            if (v.z < minZ) minZ = v.z; if (v.z > maxZ) maxZ = v.z;
        }

        float extX = maxX - minX;
        float extY = maxY - minY;
        float extZ = maxZ - minZ;

        // GDTF: Length→X, Width→Y, Height→Z
        float scaleX = (extX > 1e-6f) ? targetLength / extX : 1.0f;
        float scaleY = (extY > 1e-6f) ? targetWidth  / extY : 1.0f;
        float scaleZ = (extZ > 1e-6f) ? targetHeight / extZ : 1.0f;

        for (auto &v : vertices)
        {
            v.x *= scaleX;
            v.y *= scaleY;
            v.z *= scaleZ;

            // Adjust normals for non-uniform scale: multiply by inverse scale, renormalize
            float nx = v.nx / scaleX;
            float ny = v.ny / scaleY;
            float nz = v.nz / scaleZ;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-7f) { nx /= len; ny /= len; nz /= len; }
            v.nx = nx; v.ny = ny; v.nz = nz;
        }

        fprintf(stderr, "GltfLoader: '%s' scaled to GDTF dims (%.3f x %.3f x %.3f) "
                "from mesh extent (%.3f x %.3f x %.3f)\n",
                debugName.c_str(), targetLength, targetWidth, targetHeight,
                extX, extY, extZ);
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
