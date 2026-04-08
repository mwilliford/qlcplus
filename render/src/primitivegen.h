/*
  Q Light Controller Plus
  primitivegen.h

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

#ifndef PRIMITIVEGEN_H
#define PRIMITIVEGEN_H

#include "meshloader.h"
#include <unordered_map>

namespace qlcrender {

/**
 * Generates procedural meshes for GDTF PrimitiveType values.
 * Used as fallback when a GDTF fixture has no embedded 3D model.
 * All meshes use PosNormalVertex format for the lit shader.
 */
class PrimitiveGen
{
public:
    PrimitiveGen();
    ~PrimitiveGen();

    /** Initialize all primitive meshes. Call after bgfx::init(). */
    void init();

    /** Get a primitive mesh by GDTF primitive type enum value (from GDTFGeometryData). */
    const LoadedMesh *getPrimitive(int primitiveType) const;

    /** Destroy all meshes. Call before bgfx::shutdown(). */
    void shutdown();

private:
    void createCylinder(float radius, float height, int segments);
    std::unordered_map<int, LoadedMesh> m_meshes;
};

} // namespace qlcrender

#endif // PRIMITIVEGEN_H
