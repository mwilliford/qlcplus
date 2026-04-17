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

namespace qlcrender {

/**
 * Stateless factory for GDTF primitive meshes.
 * Generates procedural meshes at the requested GDTF dimensions.
 * Dimensions are baked into vertex data — no external scaling needed.
 *
 * GDTF dimension mapping: Length→X, Width→Y, Height→Z.
 */
class PrimitiveGen
{
public:
    /**
     * Generate a primitive mesh at the specified GDTF dimensions.
     * @param primitiveType GDTF primitive type enum (from GDTFGeometryData)
     * @param length GDTF Model Length in meters (X extent)
     * @param width  GDTF Model Width in meters (Y extent)
     * @param height GDTF Model Height in meters (Z extent)
     * @return LoadedMesh with GPU buffers; caller owns the handles.
     */
    static LoadedMesh generate(int primitiveType,
                               float length, float width, float height);
};

} // namespace qlcrender

#endif // PRIMITIVEGEN_H
