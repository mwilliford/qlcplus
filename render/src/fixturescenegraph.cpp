/*
  Q Light Controller Plus
  fixturescenegraph.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "fixturescenegraph.h"

namespace qlcrender {

static void accumulateAABB(const SceneNode &node, const float parentTransform[16], AABB &out)
{
    // Compute world transform: parent * local (column-major multiply)
    float world[16];
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            world[j * 4 + i] =
                parentTransform[0 * 4 + i] * node.localTransform[j * 4 + 0] +
                parentTransform[1 * 4 + i] * node.localTransform[j * 4 + 1] +
                parentTransform[2 * 4 + i] * node.localTransform[j * 4 + 2] +
                parentTransform[3 * 4 + i] * node.localTransform[j * 4 + 3];
        }
    }

    if (node.mesh && node.mesh->isValid())
    {
        // Node has geometry — expand AABB around this node's position
        // with padding for the mesh extent. Without vertex access,
        // use a conservative 0.15m (15cm) radius per mesh node.
        float pos[3] = { world[12], world[13], world[14] };
        const float pad = 0.15f;
        out.expandToInclude(pos[0] - pad, pos[1] - pad, pos[2] - pad);
        out.expandToInclude(pos[0] + pad, pos[1] + pad, pos[2] + pad);
    }
    else
    {
        // Grouping node — still include its position for bounding
        out.expandToInclude(world[12], world[13], world[14]);
    }

    for (const auto &child : node.children)
        accumulateAABB(child, world, out);
}

AABB computeSceneGraphAABB(const SceneNode &root)
{
    AABB result;

    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    accumulateAABB(root, identity, result);

    // Ensure a minimum size so tiny fixtures are still clickable
    if (result.valid())
    {
        for (int i = 0; i < 3; i++)
        {
            float extent = result.max[i] - result.min[i];
            if (extent < 0.1f)
            {
                float center = (result.max[i] + result.min[i]) * 0.5f;
                result.min[i] = center - 0.05f;
                result.max[i] = center + 0.05f;
            }
        }
    }

    return result;
}

} // namespace qlcrender
