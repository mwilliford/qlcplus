/*
  Q Light Controller Plus
  gltfloader.h

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

#ifndef GLTFLOADER_H
#define GLTFLOADER_H

#include "meshloader.h"
#include <string>

namespace qlcrender {

/**
 * Loads glTF/glb binary data into bgfx vertex/index buffers.
 * Extracts POSITION and NORMAL attributes → PosNormalVertex format.
 * Textures and materials are not loaded (deferred to Phase T7).
 */
class GltfLoader
{
public:
    /**
     * Load a glb binary from memory into a LoadedMesh.
     * Returns an invalid mesh on failure.
     */
    static LoadedMesh loadFromMemory(const unsigned char *data, size_t length,
                                     const std::string &debugName = "");
};

} // namespace qlcrender

#endif // GLTFLOADER_H
