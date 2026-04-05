#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace qlcrender {

/**
 * @brief A loaded bgfx mesh with vertex/index buffers ready to render.
 */
struct LoadedMesh
{
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout;
    uint32_t numVertices = 0;
    uint32_t numIndices = 0;

    bool isValid() const { return bgfx::isValid(vbh) && bgfx::isValid(ibh); }
    void destroy();
};

/**
 * @brief Loads and caches bgfx binary mesh files (.bin from geometryc).
 *
 * Meshes are loaded once and shared. Call getMesh() with a file path
 * to get a cached mesh handle. Call shutdown() to release all GPU resources.
 */
class MeshLoader
{
public:
    MeshLoader() = default;
    ~MeshLoader();

    /** Load a bgfx .bin mesh file. Returns cached mesh if already loaded. */
    const LoadedMesh *getMesh(const std::string &filePath);

    /** Release all loaded meshes. */
    void shutdown();

    /** Map QLC+ fixture type to mesh filename (e.g. "scanner.bin"). */
    static const char *meshFileForFixtureType(int fixtureType);

private:
    std::unordered_map<std::string, LoadedMesh> m_cache;
};

} // namespace qlcrender
