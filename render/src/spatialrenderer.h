#pragma once

#include <cstdint>
#include <vector>
#include "rendertypes.h"
#include "raypick.h"
#include "gizmo.h"

namespace qlcrender {

/**
 * @brief Abstract interface for spatial 3D rendering.
 *
 * The renderer draws fixtures, calibration overlays, and a reference grid
 * into a native window handle. Platform-specific implementations (BgfxRenderer)
 * provide the actual rendering backend.
 */
class SpatialRenderer
{
public:
    virtual ~SpatialRenderer() = default;

    // --- Lifecycle ---
    virtual bool init(void* nativeWindowHandle, uint32_t width, uint32_t height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void frame() = 0;

    // --- Camera ---
    virtual void setCameraOrbit(float yaw, float pitch, float distance) = 0;
    virtual void setCameraTarget(float x, float y, float z) = 0;
    virtual void panCamera(float dx, float dy) = 0;

    // --- Scene data (set once, updated on change) ---
    virtual void setFixtures(const std::vector<RenderFixture>& fixtures) = 0;
    virtual void setMeshBasePath(const std::string& path) { (void)path; }

    // --- Selection ---
    virtual void setSelectedFixture(int32_t fixtureId) { m_selectedFixtureId = fixtureId; }
    int32_t selectedFixture() const { return m_selectedFixtureId; }

    /** Pick fixture at screen coordinates. Returns fixture ID or -1. */
    virtual int32_t hitTest(float mouseX, float mouseY,
                            uint32_t viewportW, uint32_t viewportH) { return -1; }

    /** Test if mouse hits a gizmo axis. Returns the axis or None. */
    virtual GizmoAxis gizmoHitTest(float mouseX, float mouseY,
                                    uint32_t viewportW, uint32_t viewportH) { return GizmoAxis::None; }

    // --- Calibration overlays (Phase C, no-op default) ---
    virtual void setCalibrationOverlays(const std::vector<RenderEllipsoid>& ellipsoids) { (void)ellipsoids; }
    virtual void setObservationLines(const std::vector<RenderLine>& lines) { (void)lines; }
    virtual void setNamedPlanes(const std::vector<RenderPlane>& planes) { (void)planes; }

protected:
    int32_t m_selectedFixtureId = -1;
};

/** Factory: create a bgfx-based SpatialRenderer. Caller owns the pointer. */
SpatialRenderer *createBgfxRenderer();

} // namespace qlcrender
