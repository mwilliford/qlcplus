#pragma once

#include "spatialrenderer.h"
#include "orbitcamera.h"
#include "meshgen.h"
#include "meshloader.h"
#include "primitivegen.h"
#include "fixturescenegraph.h"
#include "gizmo.h"
#include <bgfx/bgfx.h>

namespace qlcrender {

/**
 * @brief bgfx-based implementation of SpatialRenderer.
 *
 * Renders into a native window handle using Metal (macOS), D3D (Windows),
 * or OpenGL/Vulkan (Linux). Single-threaded mode.
 */
/**
 * @brief bgfx callback handler — captures screenshots from the GPU.
 */
class BgfxCallback : public bgfx::CallbackI
{
public:
    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char*) override {}
    void traceVargs(const char*, uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}

    void screenShot(const char *filePath, uint32_t width, uint32_t height,
                    uint32_t pitch, bgfx::TextureFormat::Enum format,
                    const void *data, uint32_t size, bool yflip) override;

    /** Check if a screenshot is ready and take the data. */
    bool takeScreenshot(std::vector<uint8_t> &outPng, uint32_t &outW, uint32_t &outH);

    /** Request a screenshot on the next frame. */
    void requestScreenshot() { m_requested = true; }
    bool isRequested() const { return m_requested; }

private:
    bool m_requested = false;
    bool m_ready = false;
    std::vector<uint8_t> m_pngData;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

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
    void setCalibrationOverlays(const std::vector<RenderEllipsoid>& ellipsoids) override;

    // --- Selection ---
    int32_t hitTest(float mouseX, float mouseY,
                    uint32_t viewportW, uint32_t viewportH) override;
    GizmoAxis gizmoHitTest(float mouseX, float mouseY,
                            uint32_t viewportW, uint32_t viewportH) override;
    bool worldToScreen(const float worldPos[3],
                        float &outScreenX, float &outScreenY,
                        bool &outVisible) override;

    OrbitCamera& camera() { return m_camera; }
    TranslateGizmo& gizmo() { return m_gizmo; }
    BgfxCallback& callback() { return m_callback; }
    const std::vector<RenderFixture>& fixtures() const { return m_fixtures; }

    void setMeshBasePath(const std::string &path) override { m_meshBasePath = path; }

private:
    void renderGrid();
    void renderFixtures();
    void renderGizmo();
    void renderSceneGraph(const SceneNode &node, const float parentTransform[16],
                          const float color[4]);

    bool m_initialized = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    OrbitCamera m_camera;
    std::vector<RenderFixture> m_fixtures;

    void renderEllipsoids();

    // Cube mesh (fallback for fixtures without a 3D model)
    bgfx::VertexBufferHandle m_cubeVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_cubeIbh = BGFX_INVALID_HANDLE;

    // Sphere mesh (for ellipsoid overlays)
    bgfx::VertexBufferHandle m_sphereVbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_sphereIbh = BGFX_INVALID_HANDLE;

    // Fixture mesh loader + cache
    MeshLoader m_meshLoader;
    std::string m_meshBasePath;

    // GDTF primitive mesh generator
    PrimitiveGen m_primitiveGen;

    // Per-fixture local AABBs (indexed same as m_fixtures)
    std::vector<AABB> m_localAABBs;

    // Translate gizmo
    TranslateGizmo m_gizmo;

    // Screenshot callback
    BgfxCallback m_callback;

    // Calibration overlays
    std::vector<RenderEllipsoid> m_ellipsoids;

    // Shader programs
    bgfx::ProgramHandle m_colorProgram = BGFX_INVALID_HANDLE;  // vertex-color (grid/lines)
    bgfx::ProgramHandle m_litProgram = BGFX_INVALID_HANDLE;    // lit (fixture meshes)

    // Uniforms
    bgfx::UniformHandle m_u_color = BGFX_INVALID_HANDLE;
};

} // namespace qlcrender
