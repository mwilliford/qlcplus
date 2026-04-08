/*
  Q Light Controller Plus
  gizmo.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef GIZMO_H
#define GIZMO_H

#include "raypick.h"
#include <cstdint>

namespace qlcrender {

/** Which gizmo axis is hovered or being dragged. */
enum class GizmoAxis { None, X, Y, Z };

/**
 * @brief Translate gizmo — three arrow handles for axis-constrained movement.
 *
 * Renders at a given world position. Each arrow is a cylinder + cone along
 * one axis. Colors: X=red, Y=green, Z=blue. Highlighted when hovered.
 *
 * The gizmo auto-scales based on camera distance so it maintains a constant
 * screen size regardless of zoom level.
 */
class TranslateGizmo
{
public:
    TranslateGizmo() = default;

    /** Set the world-space position of the gizmo (center of selected fixture). */
    void setPosition(float x, float y, float z);
    void getPosition(float out[3]) const;

    /** Set camera distance for auto-scaling. */
    void setCameraDistance(float dist) { m_cameraDistance = dist; }

    /** Current scale factor (based on camera distance). */
    float scale() const;

    /** Length of each arrow shaft (before scaling). */
    static constexpr float kShaftLength = 1.0f;
    /** Radius of each arrow shaft. */
    static constexpr float kShaftRadius = 0.03f;
    /** Length of each arrowhead cone. */
    static constexpr float kHeadLength = 0.2f;
    /** Radius of each arrowhead cone. */
    static constexpr float kHeadRadius = 0.08f;
    /** Hit-test padding (makes arrows easier to click). */
    static constexpr float kHitPadding = 0.06f;

    /**
     * @brief Hit-test the gizmo arrows against a screen-space ray.
     *
     * Tests ray against each arrow's AABB (with padding) and returns the
     * closest hit axis.
     *
     * @param ray  World-space ray from camera
     * @return The hit axis, or GizmoAxis::None
     */
    GizmoAxis hitTest(const Ray &ray) const;

    /** Get the world-space direction vector for an axis. */
    static void axisDirection(GizmoAxis axis, float out[3]);

    /** Set/get the currently active (dragging) axis. */
    void setActiveAxis(GizmoAxis axis) { m_activeAxis = axis; }
    GizmoAxis activeAxis() const { return m_activeAxis; }

    /** Set/get the hovered axis (for highlight rendering). */
    void setHoveredAxis(GizmoAxis axis) { m_hoveredAxis = axis; }
    GizmoAxis hoveredAxis() const { return m_hoveredAxis; }

    /**
     * @brief Project mouse movement onto the active axis.
     *
     * Given the drag start point and current mouse position, compute
     * the world-space displacement along the active axis.
     *
     * @param ray  Current mouse ray
     * @param dragStartRay  Ray at drag start
     * @param outDelta  World-space displacement along active axis
     * @return true if projection succeeded
     */
    bool projectDrag(const Ray &ray, const Ray &dragStartRay,
                     const float dragStartPos[3], float outDelta[3]) const;

private:
    float m_position[3] = {0, 0, 0};
    float m_cameraDistance = 10.0f;
    GizmoAxis m_activeAxis = GizmoAxis::None;
    GizmoAxis m_hoveredAxis = GizmoAxis::None;
};

} // namespace qlcrender

#endif // GIZMO_H
