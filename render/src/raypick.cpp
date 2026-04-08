/*
  Q Light Controller Plus
  raypick.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "raypick.h"
#include <cmath>
#include <algorithm>

namespace qlcrender {

// ---------------------------------------------------------------------------
// 4x4 matrix helpers (column-major)
// ---------------------------------------------------------------------------

static void mtxMul4x4(float out[16], const float a[16], const float b[16])
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            out[j * 4 + i] =
                a[0 * 4 + i] * b[j * 4 + 0] +
                a[1 * 4 + i] * b[j * 4 + 1] +
                a[2 * 4 + i] * b[j * 4 + 2] +
                a[3 * 4 + i] * b[j * 4 + 3];
        }
    }
}

static bool mtxInverse4x4(float out[16], const float m[16])
{
    // Compute inverse of a 4x4 column-major matrix using cofactors.
    float inv[16];

    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
           + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
           - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
           + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
            - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::abs(det) < 1e-12f)
        return false;

    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
           - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
           + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
           - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
            + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
           + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
           - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
            + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
            - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];

    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
           - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
           + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
            - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
            + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * invDet;

    return true;
}

// Transform a point by a 4x4 column-major matrix (w=1, perspective divide)
static void transformPoint(float out[3], const float m[16], const float p[3])
{
    float w = m[3]*p[0] + m[7]*p[1] + m[11]*p[2] + m[15];
    if (std::abs(w) < 1e-12f) w = 1e-12f;
    out[0] = (m[0]*p[0] + m[4]*p[1] + m[8]*p[2]  + m[12]) / w;
    out[1] = (m[1]*p[0] + m[5]*p[1] + m[9]*p[2]  + m[13]) / w;
    out[2] = (m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14]) / w;
}

static void normalize3(float v[3])
{
    float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 1e-12f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Ray screenToRay(float mouseX, float mouseY,
                uint32_t viewportW, uint32_t viewportH,
                const float view[16], const float proj[16])
{
    // Convert screen coords to NDC [-1, 1]
    float ndcX = (2.0f * mouseX / float(viewportW)) - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY / float(viewportH));  // flip Y

    // Compute inverse(proj * view)
    float vp[16], vpInv[16];
    mtxMul4x4(vp, proj, view);
    mtxInverse4x4(vpInv, vp);

    // Near point in NDC → world
    float nearNDC[3] = { ndcX, ndcY, -1.0f };  // near plane
    float farNDC[3]  = { ndcX, ndcY,  1.0f };  // far plane

    float nearWorld[3], farWorld[3];
    transformPoint(nearWorld, vpInv, nearNDC);
    transformPoint(farWorld, vpInv, farNDC);

    Ray ray;
    ray.origin[0] = nearWorld[0];
    ray.origin[1] = nearWorld[1];
    ray.origin[2] = nearWorld[2];

    ray.direction[0] = farWorld[0] - nearWorld[0];
    ray.direction[1] = farWorld[1] - nearWorld[1];
    ray.direction[2] = farWorld[2] - nearWorld[2];
    normalize3(ray.direction);

    return ray;
}

bool rayIntersectsAABB(const Ray &ray, const AABB &aabb, float &outT)
{
    // Slab method (Kay/Kajiya)
    float tmin = -FLT_MAX;
    float tmax = FLT_MAX;

    for (int i = 0; i < 3; i++)
    {
        if (std::abs(ray.direction[i]) < 1e-9f)
        {
            // Ray parallel to slab — miss if origin outside
            if (ray.origin[i] < aabb.min[i] || ray.origin[i] > aabb.max[i])
                return false;
        }
        else
        {
            float invD = 1.0f / ray.direction[i];
            float t1 = (aabb.min[i] - ray.origin[i]) * invD;
            float t2 = (aabb.max[i] - ray.origin[i]) * invD;

            if (t1 > t2) std::swap(t1, t2);

            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);

            if (tmin > tmax)
                return false;
        }
    }

    if (tmax < 0.0f)
        return false;  // AABB is behind the ray

    outT = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}

AABB transformAABB(const float transform[16], const AABB &localAABB)
{
    if (!localAABB.valid())
        return localAABB;

    // Transform all 8 corners and compute enclosing AABB
    AABB result;
    for (int i = 0; i < 8; i++)
    {
        float corner[3] = {
            (i & 1) ? localAABB.max[0] : localAABB.min[0],
            (i & 2) ? localAABB.max[1] : localAABB.min[1],
            (i & 4) ? localAABB.max[2] : localAABB.min[2],
        };
        float world[3];
        // Apply 4x4 column-major transform (w=1, no perspective)
        world[0] = transform[0]*corner[0] + transform[4]*corner[1] + transform[8]*corner[2]  + transform[12];
        world[1] = transform[1]*corner[0] + transform[5]*corner[1] + transform[9]*corner[2]  + transform[13];
        world[2] = transform[2]*corner[0] + transform[6]*corner[1] + transform[10]*corner[2] + transform[14];
        result.expandToInclude(world[0], world[1], world[2]);
    }
    return result;
}

bool pickFixture(float mouseX, float mouseY,
                 uint32_t viewportW, uint32_t viewportH,
                 const float view[16], const float proj[16],
                 const std::vector<RenderFixture> &fixtures,
                 const std::vector<AABB> &localAABBs,
                 HitResult &outHit)
{
    Ray ray = screenToRay(mouseX, mouseY, viewportW, viewportH, view, proj);

    float bestT = FLT_MAX;
    bool hit = false;

    for (size_t i = 0; i < fixtures.size() && i < localAABBs.size(); i++)
    {
        // Only test primary (solid) fixtures, skip ghosts (alpha < 1)
        if (fixtures[i].color[3] < 0.99f)
            continue;

        AABB worldAABB = transformAABB(fixtures[i].transform, localAABBs[i]);
        float t;
        if (rayIntersectsAABB(ray, worldAABB, t) && t < bestT)
        {
            bestT = t;
            outHit.fixtureId = fixtures[i].id;
            outHit.distance = t;
            hit = true;
        }
    }

    return hit;
}

} // namespace qlcrender
