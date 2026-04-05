#include "orbitcamera.h"
#include <bx/math.h>
#include <cmath>

namespace qlcrender {

OrbitCamera::OrbitCamera()
    : m_yaw(45.0f)
    , m_pitch(30.0f)
    , m_distance(10.0f)
    , m_targetX(0.0f)
    , m_targetY(0.0f)
    , m_targetZ(1.0f)  // default look at 1m above floor
{
}

void OrbitCamera::setOrbit(float yaw, float pitch, float distance)
{
    m_yaw = yaw;
    m_pitch = bx::clamp(pitch, -89.0f, 89.0f);
    m_distance = bx::clamp(distance, 0.5f, 200.0f);
}

void OrbitCamera::setTarget(float x, float y, float z)
{
    m_targetX = x;
    m_targetY = y;
    m_targetZ = z;
}

void OrbitCamera::pan(float dx, float dy)
{
    // Pan in camera-local XY plane
    float yawRad = bx::toRad(m_yaw);

    // Camera right vector (in XY plane, Z-up)
    float rx = -sinf(yawRad);
    float ry = cosf(yawRad);

    // Camera up is Z (simplified for Z-up)
    m_targetX += rx * dx;
    m_targetY += ry * dx;
    m_targetZ += dy;
}

void OrbitCamera::viewMatrix(float out[16]) const
{
    float yawRad = bx::toRad(m_yaw);
    float pitchRad = bx::toRad(m_pitch);

    // Compute eye position on sphere around target (Z-up)
    float cosPitch = cosf(pitchRad);
    float eyeX = m_targetX + m_distance * cosPitch * cosf(yawRad);
    float eyeY = m_targetY + m_distance * cosPitch * sinf(yawRad);
    float eyeZ = m_targetZ + m_distance * sinf(pitchRad);

    const bx::Vec3 eye = { eyeX, eyeY, eyeZ };
    const bx::Vec3 at  = { m_targetX, m_targetY, m_targetZ };
    const bx::Vec3 up  = { 0.0f, 0.0f, 1.0f };  // Z-up

    bx::mtxLookAt(out, eye, at, up, bx::Handedness::Right);
}

void OrbitCamera::projMatrix(float out[16], float aspectRatio, bool homogeneousDepth,
                             float fovDeg, float nearPlane, float farPlane) const
{
    bx::mtxProj(out, fovDeg, aspectRatio, nearPlane, farPlane,
                homogeneousDepth, bx::Handedness::Right);
}

} // namespace qlcrender
