/*
  Q Light Controller Plus
  gdtfparser.h

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

#ifndef GDTFPARSER_H
#define GDTFPARSER_H

#include <QString>
#include <memory>

#include "qlcfixturedef.h"

class GDTFGeometryData;

/** @addtogroup engine Engine
 * @{
 */

#define KExtGDTFFixture QStringLiteral(".gdtf")

/**
 * Parser for GDTF (.gdtf) fixture definition files.
 *
 * Uses libMVRgdtf to parse the GDTF archive, then:
 * 1. Populates a QLCFixtureDef with channels, modes, and physical data
 * 2. Extracts geometry tree + embedded 3D models into GDTFGeometryData
 *
 * Follows the same pattern as AvolitesD4Parser.
 */
class GDTFParser final
{
public:
    GDTFParser();
    ~GDTFParser();

    /**
     * Parse a .gdtf file and populate a QLCFixtureDef.
     *
     * @param path Full path to the .gdtf file
     * @param fixtureDef The fixture definition to populate (must not be NULL)
     * @return true if successful
     */
    bool loadGDTF(const QString &path, QLCFixtureDef *fixtureDef);

    /**
     * Take ownership of the extracted geometry data.
     * Returns nullptr if loadGDTF() hasn't been called or failed.
     * After calling this, the parser no longer owns the data.
     */
    GDTFGeometryData *takeGeometryData();

    /** Get the last error message */
    QString lastError() const;

private:
    QString m_lastError;
    std::unique_ptr<GDTFGeometryData> m_geometryData;
};

/** @} */

#endif // GDTFPARSER_H
