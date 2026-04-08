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
#include <vector>

namespace qlcrender {

/**
 * A node in a fixture's scene graph (kinematic chain).
 *
 * Each node has a local transform relative to its parent, an optional
 * mesh to render, and zero or more children. The tree mirrors the GDTF
 * geometry hierarchy (e.g., Base → Yoke → Head → Beam).
 *
 * For SV-1, all transforms are static (from GDTF file).
 * In Phase T4, axis nodes will have pan/tilt rotation injected from DMX.
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
};

/**
 * Complete scene graph for one fixture definition.
 *
 * Shared by all fixtures of the same type — each fixture instance
 * provides its own root transform (from SpatialModel) which is multiplied
 * by the scene graph's internal transforms during rendering.
 */
struct FixtureSceneGraph
{
    SceneNode root;
    bool valid = false;
};

} // namespace qlcrender

#endif // FIXTURESCENEGRAPH_H
