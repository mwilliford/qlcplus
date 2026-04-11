/*
  Q Light Controller Plus
  raypick.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef RAYPICK_H
#define RAYPICK_H

#include <cstdint>
#include <cmath>
#include <cfloat>
#include <vector>
#include "rendertypes.h"

namespace qlcrender {

struct Ray
{
    float origin[3];
    float direction[3];  // normalized
};

struct AABB
{
    float min[3] = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    float max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    bool valid() const { return min[0] <= max[0]; }

    void expandToInclude(float x, float y, float z)
    {
        if (x < min[0]) min[0] = x;
        if (y < min[1]) min[1] = y;
        if (z < min[2]) min[2] = z;
        if (x > max[0]) max[0] = x;
        if (y > max[1]) max[1] = y;
        if (z > max[2]) max[2] = z;
    }

    void expandToInclude(const AABB &other)
    {
        if (!other.valid()) return;
        expandToInclude(other.min[0], other.min[1], other.min[2]);
        expandToInclude(other.max[0], other.max[1], other.max[2]);
    }
};

/**
 * @brief Unproject a screen-space point into a world-space ray.
 *
 * @param mouseX, mouseY  Screen coordinates (pixels, origin top-left)
 * @param viewportW, viewportH  Viewport size in pixels
 * @param view  4x4 view matrix (column-major)
 * @param proj  4x4 projection matrix (column-major)
 * @return Ray from camera position through the mouse point
 */
Ray screenToRay(float mouseX, float mouseY,
                uint32_t viewportW, uint32_t viewportH,
                const float view[16], const float proj[16]);

/**
 * @brief Test ray vs axis-aligned bounding box.
 *
 * @param ray  The ray to test
 * @param aabb  The bounding box
 * @param outT  If hit, the distance along the ray (nearest intersection)
 * @return true if the ray intersects the AABB
 */
bool rayIntersectsAABB(const Ray &ray, const AABB &aabb, float &outT);

/**
 * @brief Intersect a ray with a horizontal plane at fixed Z.
 *
 * @param r         The ray to test
 * @param planeZ    Z value of the plane (normal = +Z)
 * @param out       If hit, the intersection point (x, y, planeZ)
 * @param outT      Optional: the ray t value at the intersection
 * @return true if the ray hits the plane in front of the camera within a
 *         reasonable distance (rejects parallel rays, behind-camera hits,
 *         and horizon skims beyond 200 m)
 */
bool rayIntersectsPlaneZ(const Ray &r, float planeZ, float out[3], float *outT = nullptr);

/**
 * @brief Compute the world-space AABB of a fixture from its transform.
 *
 * Uses the fixture's scene graph local AABB (if available) or a default
 * unit cube, transformed by the fixture's world transform.
 *
 * @param fixture  The render fixture with its 4x4 transform
 * @param localAABB  The fixture's local-space AABB (pre-transform)
 * @return The world-space AABB
 */
AABB transformAABB(const float transform[16], const AABB &localAABB);

struct HitResult
{
    uint32_t fixtureId;
    float distance;
};

/**
 * @brief Pick the nearest fixture hit by a screen-space click.
 *
 * @param mouseX, mouseY  Screen coordinates
 * @param viewportW, viewportH  Viewport size
 * @param view, proj  Camera matrices (column-major)
 * @param fixtures  The fixtures to test against
 * @param localAABBs  Per-fixture local AABBs (indexed same as fixtures)
 * @param outHit  If hit, the result
 * @return true if any fixture was hit
 */
bool pickFixture(float mouseX, float mouseY,
                 uint32_t viewportW, uint32_t viewportH,
                 const float view[16], const float proj[16],
                 const std::vector<RenderFixture> &fixtures,
                 const std::vector<AABB> &localAABBs,
                 HitResult &outHit);

} // namespace qlcrender

#endif // RAYPICK_H
