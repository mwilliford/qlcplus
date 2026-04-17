/*
  Q Light Controller Plus
  fixturescenegraph.h

  Copyright (C) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef FIXTURESCENEGRAPH_H
#define FIXTURESCENEGRAPH_H

#include "meshloader.h"
#include "raypick.h"
#include <vector>

namespace qlcrender {

/**
 * A node in a fixture's scene graph (kinematic chain).
 *
 * Each node has a local transform relative to its parent, an optional
 * mesh to render, and zero or more children. The tree mirrors the GDTF
 * geometry hierarchy (e.g., Base -> Yoke -> Head -> Beam).
 */
struct SceneNode
{
    float localTransform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    const LoadedMesh *mesh = nullptr;  // null = grouping node (no draw call)
    std::vector<SceneNode> children;

    // R2: DOF articulation — set during buildSceneNode() for GeometryAxis nodes.
    // dofIndex identifies which entry in RenderFixture::dofAngles drives this joint.
    // dofAxis is the rotation axis in this node's LOCAL space (unit vector).
    int dofIndex = -1;
    float dofAxis[3] = {0.0f, 0.0f, 0.0f};

    // R4: Beam node tagging — set during buildSceneNode() for Lamp/Laser nodes.
    bool isBeamNode = false;
    float beamAngle = 0.0f;  // degrees
};

/**
 * Complete scene graph for one fixture definition.
 *
 * Shared by all fixtures of the same type -- each fixture instance
 * provides its own root transform (from SpatialModel) which is multiplied
 * by the scene graph's internal transforms during rendering.
 */
struct FixtureSceneGraph
{
    SceneNode root;
    bool valid = false;

    /** Local-space AABB encompassing all geometry nodes. */
    AABB localAABB;
};

/** Default local-space AABB for fixtures without GDTF (unit cube centered at origin). */
inline AABB defaultFixtureAABB()
{
    AABB box;
    box.min[0] = -0.15f; box.min[1] = -0.15f; box.min[2] = -0.15f;
    box.max[0] =  0.15f; box.max[1] =  0.15f; box.max[2] =  0.15f;
    return box;
}

/**
 * Compute the local-space AABB of a scene graph by walking all nodes
 * and accumulating their mesh bounding boxes through transforms.
 *
 * This uses a simplified approach: it transforms each node's position
 * (translation from the local transform) into root space and expands
 * the AABB by a per-node padding. For accurate bounds, mesh vertex
 * bounds would be needed, but this is sufficient for picking.
 */
AABB computeSceneGraphAABB(const SceneNode &root);

} // namespace qlcrender

#endif // FIXTURESCENEGRAPH_H
