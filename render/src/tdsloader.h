/*
  Q Light Controller Plus
  tdsloader.h

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

#ifndef TDSLOADER_H
#define TDSLOADER_H

#include "meshloader.h"
#include <string>

namespace qlcrender {

/**
 * Loads .3ds (3D Studio) binary data into bgfx vertex/index buffers.
 * Extracts POSITION from vertex lists and computes flat normals from faces.
 * Same PosNormalVertex output format as GltfLoader.
 *
 * 3DS is the most common model format in GDTF fixtures on gdtf-share.
 */
class TdsLoader
{
public:
    /**
     * Load a .3ds binary from memory into a LoadedMesh.
     * Returns an invalid mesh on failure.
     */
    static LoadedMesh loadFromMemory(const unsigned char *data, size_t length,
                                     const std::string &debugName = "");
};

} // namespace qlcrender

#endif // TDSLOADER_H
