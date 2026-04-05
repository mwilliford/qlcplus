#include "meshloader.h"

#include <bgfx/bgfx.h>
#include <bx/readerwriter.h>
#include <bx/file.h>
#include <bx/math.h>
#include <cstdio>
#include <cstring>

// Chunk IDs matching geometryc output
#define BGFX_CHUNK_MAGIC_VB  BX_MAKEFOURCC('V', 'B', ' ', 0x1)
#define BGFX_CHUNK_MAGIC_VBC BX_MAKEFOURCC('V', 'B', 'C', 0x0)
#define BGFX_CHUNK_MAGIC_IB  BX_MAKEFOURCC('I', 'B', ' ', 0x0)
#define BGFX_CHUNK_MAGIC_IBC BX_MAKEFOURCC('I', 'B', 'C', 0x1)
#define BGFX_CHUNK_MAGIC_PRI BX_MAKEFOURCC('P', 'R', 'I', 0x0)

namespace bgfx
{
    // Internal bgfx function to read vertex layout from binary
    int32_t read(bx::ReaderI* _reader, bgfx::VertexLayout& _layout, bx::Error* _err);
}

namespace qlcrender {

void LoadedMesh::destroy()
{
    if (bgfx::isValid(vbh))
        bgfx::destroy(vbh);
    if (bgfx::isValid(ibh))
        bgfx::destroy(ibh);
    vbh = BGFX_INVALID_HANDLE;
    ibh = BGFX_INVALID_HANDLE;
    numVertices = 0;
    numIndices = 0;
}

MeshLoader::~MeshLoader()
{
    shutdown();
}

void MeshLoader::shutdown()
{
    for (auto &pair : m_cache)
        pair.second.destroy();
    m_cache.clear();
}

const LoadedMesh *MeshLoader::getMesh(const std::string &filePath)
{
    // Return cached
    auto it = m_cache.find(filePath);
    if (it != m_cache.end())
        return it->second.isValid() ? &it->second : nullptr;

    // Load from file
    bx::FileReader reader;
    bx::Error err;
    if (!bx::open(&reader, filePath.c_str(), &err))
    {
        fprintf(stderr, "[MeshLoader] Failed to open: %s\n", filePath.c_str());
        return nullptr;
    }

    LoadedMesh mesh;

    uint32_t chunk;
    while (4 == bx::read(&reader, chunk, &err) && err.isOk())
    {
        switch (chunk)
        {
        case BGFX_CHUNK_MAGIC_VB:
        {
            // Skip bounding volumes:
            //   Sphere: Vec3 center (3f) + float radius (1f) = 4 floats = 16 bytes
            //   Aabb:   Vec3 min (3f) + Vec3 max (3f) = 6 floats = 24 bytes
            //   Obb:    float mtx[16] = 16 floats = 64 bytes
            //   Total: 26 floats = 104 bytes
            float skip[4 + 6 + 16];
            bx::read(&reader, skip, sizeof(skip), &err);

            bgfx::read(&reader, mesh.layout, &err);
            uint16_t stride = mesh.layout.getStride();

            uint16_t numVertices;
            bx::read(&reader, numVertices, &err);
            mesh.numVertices = numVertices;

            const bgfx::Memory *mem = bgfx::alloc(numVertices * stride);
            bx::read(&reader, mem->data, mem->size, &err);
            mesh.vbh = bgfx::createVertexBuffer(mem, mesh.layout);
            break;
        }

        case BGFX_CHUNK_MAGIC_IB:
        {
            uint32_t numIndices;
            bx::read(&reader, numIndices, &err);
            mesh.numIndices = numIndices;

            const bgfx::Memory *mem = bgfx::alloc(numIndices * 2);
            bx::read(&reader, mem->data, mem->size, &err);
            mesh.ibh = bgfx::createIndexBuffer(mem);
            break;
        }

        case BGFX_CHUNK_MAGIC_VBC:
        {
            // Compressed vertex buffer — skip for now, our meshes aren't compressed
            fprintf(stderr, "[MeshLoader] Compressed VB not supported: %s\n", filePath.c_str());
            bx::close(&reader);
            return nullptr;
        }

        case BGFX_CHUNK_MAGIC_IBC:
        {
            // Compressed index buffer — skip
            fprintf(stderr, "[MeshLoader] Compressed IB not supported: %s\n", filePath.c_str());
            bx::close(&reader);
            return nullptr;
        }

        case BGFX_CHUNK_MAGIC_PRI:
        {
            // Primitive info — skip material/name data
            uint16_t len;
            bx::read(&reader, len, &err);
            // Skip material name
            char skipBuf[256];
            if (len > 0 && len < sizeof(skipBuf))
                bx::read(&reader, skipBuf, len, &err);

            uint16_t num;
            bx::read(&reader, num, &err);

            for (uint16_t i = 0; i < num; i++)
            {
                // Skip primitive name + indices
                uint16_t plen;
                bx::read(&reader, plen, &err);
                if (plen > 0 && plen < sizeof(skipBuf))
                    bx::read(&reader, skipBuf, plen, &err);
                uint32_t startIndex, numPrimIndices, startVertex, numPrimVertices;
                float sphere[4], aabb[6], obb[15];
                bx::read(&reader, startIndex, &err);
                bx::read(&reader, numPrimIndices, &err);
                bx::read(&reader, startVertex, &err);
                bx::read(&reader, numPrimVertices, &err);
                bx::read(&reader, sphere, sizeof(sphere), &err);
                bx::read(&reader, aabb, sizeof(aabb), &err);
                bx::read(&reader, obb, sizeof(obb), &err);
            }
            break;
        }

        default:
            // Unknown chunk — try to continue
            break;
        }
    }

    bx::close(&reader);

    if (!mesh.isValid())
    {
        fprintf(stderr, "[MeshLoader] Invalid mesh data: %s\n", filePath.c_str());
        mesh.destroy();
        return nullptr;
    }

    fprintf(stderr, "[MeshLoader] Loaded: %s (%u verts, %u indices)\n",
            filePath.c_str(), mesh.numVertices, mesh.numIndices);

    auto result = m_cache.emplace(filePath, mesh);
    return &result.first->second;
}

// QLC+ QLCFixtureDef::FixtureType enum values (from qlcfixturedef.h)
// We duplicate the values here to avoid depending on engine headers
const char *MeshLoader::meshFileForFixtureType(int fixtureType)
{
    // QLCFixtureDef::FixtureType:
    //   ColorChanger=0, Dimmer=1, Effect=2, Fan=3, Flower=4, Hazer=5,
    //   Laser=6, LEDBarBeams=7, LEDBarPixels=8, MovingHead=9, Other=10,
    //   Scanner=11, Smoke=12, Strobe=13
    switch (fixtureType)
    {
    case 0:  // ColorChanger
    case 1:  // Dimmer
        return "par.bin";
    case 9:  // MovingHead
        return "moving_head.bin";
    case 11: // Scanner
        return "scanner.bin";
    case 13: // Strobe
        return "strobe.bin";
    case 5:  // Hazer
        return "hazer.bin";
    case 12: // Smoke
        return "smoke.bin";
    default:
        return nullptr;  // No mesh for this type
    }
}

} // namespace qlcrender
