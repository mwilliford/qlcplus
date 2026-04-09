#pragma once

#include <cstdint>
#include <set>
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

    // --- Gizmo mode ---
    void setGizmoMode(int mode) { m_gizmoMode = mode; }  // 0=Translate, 1=Rotate
    int gizmoMode() const { return m_gizmoMode; }

    // --- Selection ---
    virtual void setSelectedFixture(int32_t fixtureId) {
        m_selectedIds.clear();
        if (fixtureId >= 0) m_selectedIds.insert(fixtureId);
    }
    void addSelectedFixture(int32_t fixtureId) { if (fixtureId >= 0) m_selectedIds.insert(fixtureId); }
    void removeSelectedFixture(int32_t fixtureId) { m_selectedIds.erase(fixtureId); }
    void clearSelection() { m_selectedIds.clear(); }
    bool isSelected(int32_t fixtureId) const { return m_selectedIds.count(fixtureId) > 0; }
    const std::set<int32_t> &selectedIds() const { return m_selectedIds; }
    /** Primary selected fixture (for gizmo placement). -1 if empty. */
    int32_t selectedFixture() const { return m_selectedIds.empty() ? -1 : *m_selectedIds.begin(); }

    /** Pick fixture at screen coordinates. Returns fixture ID or -1. */
    virtual int32_t hitTest(float mouseX, float mouseY,
                            uint32_t viewportW, uint32_t viewportH) { return -1; }

    /** Test if mouse hits a gizmo axis. Returns the axis or None. */
    virtual GizmoAxis gizmoHitTest(float mouseX, float mouseY,
                                    uint32_t viewportW, uint32_t viewportH) { return GizmoAxis::None; }

    /**
     * Project a world position to screen coordinates (logical pixels).
     * Returns false if the point is behind the camera.
     */
    virtual bool worldToScreen(const float worldPos[3],
                                float &outScreenX, float &outScreenY,
                                bool &outVisible) { return false; }

    // --- Trusses ---
    virtual void setTrusses(const std::vector<RenderTruss>& trusses) { m_trusses = trusses; }

    // --- Calibration overlays (Phase C, no-op default) ---
    virtual void setCalibrationOverlays(const std::vector<RenderEllipsoid>& ellipsoids) { (void)ellipsoids; }
    virtual void setObservationLines(const std::vector<RenderLine>& lines) { (void)lines; }
    virtual void setNamedPlanes(const std::vector<RenderPlane>& planes) { (void)planes; }

protected:
    std::set<int32_t> m_selectedIds;
    int m_gizmoMode = 0;
    std::vector<RenderTruss> m_trusses;
};

/** Factory: create a bgfx-based SpatialRenderer. Caller owns the pointer. */
SpatialRenderer *createBgfxRenderer();

} // namespace qlcrender
