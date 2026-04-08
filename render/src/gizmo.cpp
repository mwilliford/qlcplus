/*
  Q Light Controller Plus
  gizmo.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "gizmo.h"
#include <cmath>
#include <algorithm>
#include <cfloat>

namespace qlcrender {

void TranslateGizmo::setPosition(float x, float y, float z)
{
    m_position[0] = x;
    m_position[1] = y;
    m_position[2] = z;
}

void TranslateGizmo::getPosition(float out[3]) const
{
    out[0] = m_position[0];
    out[1] = m_position[1];
    out[2] = m_position[2];
}

float TranslateGizmo::scale() const
{
    // Keep gizmo at ~10% of screen height regardless of zoom
    return m_cameraDistance * 0.12f;
}

void TranslateGizmo::axisDirection(GizmoAxis axis, float out[3])
{
    out[0] = out[1] = out[2] = 0.0f;
    switch (axis)
    {
    case GizmoAxis::X: out[0] = 1.0f; break;
    case GizmoAxis::Y: out[1] = 1.0f; break;
    case GizmoAxis::Z: out[2] = 1.0f; break;
    default: break;
    }
}

GizmoAxis TranslateGizmo::hitTest(const Ray &ray) const
{
    float s = scale();
    float bestT = FLT_MAX;
    GizmoAxis bestAxis = GizmoAxis::None;

    // Test each axis arrow as an AABB
    GizmoAxis axes[] = { GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z };
    for (GizmoAxis axis : axes)
    {
        float dir[3];
        axisDirection(axis, dir);

        // Build AABB for this arrow: from gizmo center to tip along the axis
        float pad = (kShaftRadius + kHitPadding) * s;
        float len = (kShaftLength + kHeadLength) * s;

        AABB box;
        // Start point (center with padding)
        box.expandToInclude(m_position[0] - pad, m_position[1] - pad, m_position[2] - pad);
        box.expandToInclude(m_position[0] + pad, m_position[1] + pad, m_position[2] + pad);

        // Now extend along the axis direction only
        // Reset to axis-aligned extent
        box.min[0] = m_position[0] - pad;
        box.min[1] = m_position[1] - pad;
        box.min[2] = m_position[2] - pad;
        box.max[0] = m_position[0] + pad;
        box.max[1] = m_position[1] + pad;
        box.max[2] = m_position[2] + pad;

        // Extend along the axis
        int ai = (axis == GizmoAxis::X) ? 0 : (axis == GizmoAxis::Y) ? 1 : 2;
        box.max[ai] = m_position[ai] + len;
        // Small negative extent for the shaft base
        box.min[ai] = m_position[ai] - pad;

        float t;
        if (rayIntersectsAABB(ray, box, t) && t < bestT && t > 0.0f)
        {
            bestT = t;
            bestAxis = axis;
        }
    }

    return bestAxis;
}

// Helper: closest point on a ray to a point
static float closestTOnRay(const Ray &ray, const float point[3])
{
    float d[3] = {
        point[0] - ray.origin[0],
        point[1] - ray.origin[1],
        point[2] - ray.origin[2]
    };
    return d[0]*ray.direction[0] + d[1]*ray.direction[1] + d[2]*ray.direction[2];
}

// Helper: project a ray onto an axis line, return the parameter along the axis
static float rayAxisProjection(const Ray &ray, const float axisOrigin[3], const float axisDir[3])
{
    // Find the point on the axis line closest to the ray.
    // Using the formula for closest points between two lines.
    float w[3] = {
        ray.origin[0] - axisOrigin[0],
        ray.origin[1] - axisOrigin[1],
        ray.origin[2] - axisOrigin[2]
    };

    float a = ray.direction[0]*ray.direction[0] + ray.direction[1]*ray.direction[1] + ray.direction[2]*ray.direction[2]; // always 1 (normalized)
    float b = ray.direction[0]*axisDir[0] + ray.direction[1]*axisDir[1] + ray.direction[2]*axisDir[2];
    float c = axisDir[0]*axisDir[0] + axisDir[1]*axisDir[1] + axisDir[2]*axisDir[2]; // always 1
    float d_val = ray.direction[0]*w[0] + ray.direction[1]*w[1] + ray.direction[2]*w[2];
    float e = axisDir[0]*w[0] + axisDir[1]*w[1] + axisDir[2]*w[2];

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-9f)
        return 0.0f;  // parallel

    // Parameter along the axis line
    float tAxis = (a * e - b * d_val) / denom;
    return tAxis;
}

bool TranslateGizmo::projectDrag(const Ray &ray, const Ray &dragStartRay,
                                  const float dragStartPos[3], float outDelta[3]) const
{
    if (m_activeAxis == GizmoAxis::None)
        return false;

    float axisDir[3];
    axisDirection(m_activeAxis, axisDir);

    // Project current ray onto the axis
    float tNow = rayAxisProjection(ray, dragStartPos, axisDir);
    float tStart = rayAxisProjection(dragStartRay, dragStartPos, axisDir);
    float delta = tNow - tStart;

    outDelta[0] = axisDir[0] * delta;
    outDelta[1] = axisDir[1] * delta;
    outDelta[2] = axisDir[2] * delta;

    return true;
}

} // namespace qlcrender
