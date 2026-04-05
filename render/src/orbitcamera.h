#pragma once

#include <cstdint>

namespace qlcrender {

/**
 * @brief Z-up orbit camera for the spatial 3D view.
 *
 * Orbits around a target point with yaw (azimuth), pitch (elevation),
 * and distance (zoom). Produces view and projection matrices compatible
 * with bgfx's coordinate conventions.
 */
class OrbitCamera
{
public:
    OrbitCamera();

    void setOrbit(float yaw, float pitch, float distance);
    void setTarget(float x, float y, float z);
    void pan(float dx, float dy);

    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    float distance() const { return m_distance; }

    /** Write the 4x4 view matrix (column-major, bgfx convention). */
    void viewMatrix(float out[16]) const;

    /** Write the 4x4 perspective projection matrix.
     *  @param homogeneousDepth true for OpenGL/Metal ([-1,1] NDC), false for D3D ([0,1]) */
    void projMatrix(float out[16], float aspectRatio, bool homogeneousDepth,
                    float fovDeg = 60.0f, float nearPlane = 0.1f, float farPlane = 1000.0f) const;

private:
    float m_yaw;       // degrees, azimuth around Z axis
    float m_pitch;     // degrees, elevation (clamped -89..89)
    float m_distance;  // zoom distance from target

    float m_targetX;
    float m_targetY;
    float m_targetZ;
};

} // namespace qlcrender
