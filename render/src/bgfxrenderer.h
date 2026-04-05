#pragma once

#include "spatialrenderer.h"
#include "orbitcamera.h"
#include "meshgen.h"
#include "meshloader.h"
#include <bgfx/bgfx.h>

namespace qlcrender {

/**
 * @brief bgfx-based implementation of SpatialRenderer.
 *
 * Renders into a native window handle using Metal (macOS), D3D (Windows),
 * or OpenGL/Vulkan (Linux). Single-threaded mode.
 */
class BgfxRenderer : public SpatialRenderer
{
public:
    BgfxRenderer();
    ~BgfxRenderer() override;

    // --- Lifecycle ---
    bool init(void* nativeWindowHandle, uint32_t width, uint32_t height) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void frame() override;

    // --- Camera ---
    void setCameraOrbit(float yaw, float pitch, float distance) override;
    void setCameraTarget(float x, float y, float z) override;
    void panCamera(float dx, float dy) override;

    // --- Scene data ---
    void setFixtures(const std::vector<RenderFixture>& fixtures) override;

    OrbitCamera& camera() { return m_camera; }

    void setMeshBasePath(const std::string &path) override { m_meshBasePath = path; }

private:
    void renderGrid();
    void renderFixtures();

    bool m_initialized = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    OrbitCamera m_camera;
    std::vector<RenderFixture> m_fixtures;

    // Cube mesh (fallback for fixtures without a 3D model)
    bgfx::VertexBufferHandle m_cubeVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_cubeIbh = BGFX_INVALID_HANDLE;

    // Fixture mesh loader + cache
    MeshLoader m_meshLoader;
    std::string m_meshBasePath;

    // Shader programs
    bgfx::ProgramHandle m_colorProgram = BGFX_INVALID_HANDLE;

    // Uniforms
    bgfx::UniformHandle m_u_color = BGFX_INVALID_HANDLE;
};

} // namespace qlcrender
