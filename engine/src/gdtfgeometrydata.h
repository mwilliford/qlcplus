/*
  Q Light Controller Plus
  gdtfgeometrydata.h

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

#ifndef GDTFGEOMETRYDATA_H
#define GDTFGEOMETRYDATA_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

/** @addtogroup engine Engine
 * @{
 */

/**
 * Geometry node types matching GDTF EGdtfObjectType values for geometry nodes.
 * These are a simplified subset — only geometry-relevant types.
 */
enum GDTFGeometryType
{
    GeometryGeneral = 0,
    GeometryAxis,
    GeometryBeamFilter,
    GeometryColorFilter,
    GeometryGoboFilter,
    GeometryShaperFilter,
    GeometryLamp,
    GeometryReference,
    GeometryMediaServerCamera,
    GeometryMediaServerLayer,
    GeometryMediaServerMaster,
    GeometryDisplay,
    GeometryLaser,
    GeometryWiringObject,
    GeometryInventory,
    GeometryStructure,
    GeometrySupport,
    GeometryMagnet
};

/**
 * Primitive types matching GDTF EGdtfModel_PrimitiveType.
 * Used as fallback when the GDTF fixture has no 3D model file.
 */
enum GDTFPrimitiveType
{
    PrimitiveUndefined = 0,
    PrimitiveCube,
    PrimitiveCylinder,
    PrimitiveSphere,
    PrimitiveBase,
    PrimitiveYoke,
    PrimitiveHead,
    PrimitiveScanner,
    PrimitiveConventional,
    PrimitivePigtail,
    PrimitiveBase1_1,
    PrimitiveScanner1_1,
    PrimitiveConventional1_1
};

/**
 * A node in the GDTF geometry tree.
 *
 * Represents one element of a fixture's physical structure
 * (e.g., Base, Yoke, Head, Beam). Children are nested sub-geometries.
 *
 * The localTransform is a 4x4 column-major matrix defining this node's
 * position/orientation relative to its parent, in meters.
 */
struct GDTFGeometryNode
{
    QString name;
    GDTFGeometryType type = GeometryGeneral;
    float localTransform[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };  // identity

    // Model info
    QString meshRef;                        // glTF filename inside GDTF archive (empty if none)
    GDTFPrimitiveType primitiveType = PrimitiveUndefined;  // fallback if no mesh
    float modelLength = 0.0f;               // meters
    float modelWidth = 0.0f;                // meters
    float modelHeight = 0.0f;               // meters

    // Beam properties (only meaningful for GeometryLamp/GeometryLaser)
    float beamAngle = 0.0f;                 // degrees
    float fieldAngle = 0.0f;                // degrees
    float luminousIntensity = 0.0f;         // candela
    float colorTemperature = 0.0f;          // Kelvin

    QVector<GDTFGeometryNode> children;
};

/**
 * Complete geometry data extracted from a GDTF file.
 *
 * This struct is a plain data container with no libMVRgdtf types,
 * so it can be included by the render layer without linking libMVRgdtf.
 */
struct GDTFGeometryData
{
    GDTFGeometryNode root;

    /** Raw 3D model data extracted from the GDTF archive.
     *  Key = mesh reference name (e.g., "body.glb"), Value = raw glb bytes */
    QMap<QString, QByteArray> meshData;
};

/** @} */

#endif // GDTFGEOMETRYDATA_H
