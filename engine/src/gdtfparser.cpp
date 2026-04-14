/*
  Q Light Controller Plus
  gdtfparser.cpp

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

#include <QDebug>
#include <QFile>
#include <algorithm>

#include "Include/VectorworksMVR.h"

#include "gdtfparser.h"
#include "gdtfgeometrydata.h"
#include "qlcchannel.h"
#include "qlccapability.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

using namespace VectorworksMVR;
using namespace VectorworksMVR::GdtfDefines;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static GDTFGeometryType mapGeometryType(EGdtfObjectType type)
{
    switch (type)
    {
    case eGdtfGeometry:                  return GeometryGeneral;
    case eGdtfGeometryAxis:              return GeometryAxis;
    case eGdtfGeometryBeamFilter:        return GeometryBeamFilter;
    case eGdtfGeometryColorFilter:       return GeometryColorFilter;
    case eGdtfGeometryGoboFilter:        return GeometryGoboFilter;
    case eGdtfGeometryShaperFilter:      return GeometryShaperFilter;
    case eGdtfGeometryLamp:              return GeometryLamp;
    case eGdtfGeometryReference:         return GeometryReference;
    case eGdtfGeometryMediaServerCamera: return GeometryMediaServerCamera;
    case eGdtfGeometryMediaServerLayer:  return GeometryMediaServerLayer;
    case eGdtfGeometryMediaServerMaster: return GeometryMediaServerMaster;
    case eGdtfGeometryDisplay:           return GeometryDisplay;
    case eGdtfGeometryLaser:             return GeometryLaser;
    case eGdtfGeometryWiringObject:      return GeometryWiringObject;
    case eGdtfGeometryInventory:         return GeometryInventory;
    case eGdtfGeometryStructure:         return GeometryStructure;
    case eGdtfGeometrySupport:           return GeometrySupport;
    case eGdtfGeometryMagnet:            return GeometryMagnet;
    default:                             return GeometryGeneral;
    }
}

static GDTFPrimitiveType mapPrimitiveType(EGdtfModel_PrimitiveType type)
{
    switch (type)
    {
    case eGdtfModel_PrimitiveType_Cube:             return PrimitiveCube;
    case eGdtfModel_PrimitiveType_Cylinder:         return PrimitiveCylinder;
    case eGdtfModel_PrimitiveType_Sphere:           return PrimitiveSphere;
    case eGdtfModel_PrimitiveType_Base:             return PrimitiveBase;
    case eGdtfModel_PrimitiveType_Yoke:             return PrimitiveYoke;
    case eGdtfModel_PrimitiveType_Head:             return PrimitiveHead;
    case eGdtfModel_PrimitiveType_Scanner:          return PrimitiveScanner;
    case eGdtfModel_PrimitiveType_Conventional:     return PrimitiveConventional;
    case eGdtfModel_PrimitiveType_Pigtail:          return PrimitivePigtail;
    case eGdtfModel_PrimitiveType_Base1_1:          return PrimitiveBase1_1;
    case eGdtfModel_PrimitiveType_Scanner1_1:       return PrimitiveScanner1_1;
    case eGdtfModel_PrimitiveType_Conventional1_1:  return PrimitiveConventional1_1;
    default:                                        return PrimitiveUndefined;
    }
}

/**
 * Convert libMVRgdtf STransformMatrix to a 4x4 column-major float array.
 *
 * STransformMatrix has:
 *   ux,uy,uz  (X axis direction)
 *   vx,vy,vz  (Y axis direction)
 *   wx,wy,wz  (Z axis direction)
 *   ox,oy,oz  (origin/translation)
 *
 * GDTF uses millimeters for position; we convert to meters.
 */
static void convertTransform(const STransformMatrix &src, float dst[16])
{
    // Column 0 (X axis)
    dst[0]  = static_cast<float>(src.ux);
    dst[1]  = static_cast<float>(src.uy);
    dst[2]  = static_cast<float>(src.uz);
    dst[3]  = 0.0f;

    // Column 1 (Y axis)
    dst[4]  = static_cast<float>(src.vx);
    dst[5]  = static_cast<float>(src.vy);
    dst[6]  = static_cast<float>(src.vz);
    dst[7]  = 0.0f;

    // Column 2 (Z axis)
    dst[8]  = static_cast<float>(src.wx);
    dst[9]  = static_cast<float>(src.wy);
    dst[10] = static_cast<float>(src.wz);
    dst[11] = 0.0f;

    // Column 3 (translation — meters per DIN SPEC 15800)
    dst[12] = static_cast<float>(src.ox);
    dst[13] = static_cast<float>(src.oy);
    dst[14] = static_cast<float>(src.oz);
    dst[15] = 1.0f;
}

/**
 * Map a GDTF attribute name to QLC+ channel group.
 * GDTF uses standardized attribute names (e.g., "Pan", "Tilt", "Dimmer").
 */
static QLCChannel::Group attributeNameToGroup(const QString &attrName)
{
    if (attrName.startsWith(QStringLiteral("Pan")))
        return QLCChannel::Pan;
    if (attrName.startsWith(QStringLiteral("Tilt")))
        return QLCChannel::Tilt;
    if (attrName.startsWith(QStringLiteral("Dimmer")))
        return QLCChannel::Intensity;
    if (attrName.startsWith(QStringLiteral("Shutter")) ||
        attrName.startsWith(QStringLiteral("Strobe")))
        return QLCChannel::Shutter;
    if (attrName.startsWith(QStringLiteral("Color")) ||
        attrName.startsWith(QStringLiteral("ColorAdd_")) ||
        attrName.startsWith(QStringLiteral("ColorSub_")) ||
        attrName.startsWith(QStringLiteral("CTO")) ||
        attrName.startsWith(QStringLiteral("CTB")) ||
        attrName.startsWith(QStringLiteral("CTC")))
        return QLCChannel::Colour;
    if (attrName.startsWith(QStringLiteral("Gobo")))
        return QLCChannel::Gobo;
    if (attrName.startsWith(QStringLiteral("Prism")))
        return QLCChannel::Prism;
    if (attrName.startsWith(QStringLiteral("Focus")) ||
        attrName.startsWith(QStringLiteral("Zoom")) ||
        attrName.startsWith(QStringLiteral("Iris")) ||
        attrName.startsWith(QStringLiteral("Frost")))
        return QLCChannel::Beam;
    if (attrName.startsWith(QStringLiteral("Speed")) ||
        attrName.contains(QStringLiteral("Speed")))
        return QLCChannel::Speed;
    if (attrName.startsWith(QStringLiteral("Effects")) ||
        attrName.startsWith(QStringLiteral("Blade")) ||
        attrName.startsWith(QStringLiteral("Fog")) ||
        attrName.startsWith(QStringLiteral("Fan")))
        return QLCChannel::Effect;
    if (attrName.startsWith(QStringLiteral("Control")) ||
        attrName.startsWith(QStringLiteral("Lamp")) ||
        attrName.startsWith(QStringLiteral("Reset")))
        return QLCChannel::Maintenance;

    return QLCChannel::Nothing;
}

/**
 * Guess fixture type from GDTF geometry tree.
 */
static QLCFixtureDef::FixtureType guessFixtureType(const GDTFGeometryData &geo)
{
    // Walk the tree looking for characteristic geometry types
    std::function<bool(const GDTFGeometryNode &, GDTFGeometryType)> hasType;
    hasType = [&hasType](const GDTFGeometryNode &node, GDTFGeometryType type) -> bool
    {
        if (node.type == type) return true;
        for (const auto &child : node.children)
            if (hasType(child, type)) return true;
        return false;
    };

    if (hasType(geo.root, GeometryLaser))
        return QLCFixtureDef::Laser;
    // Axis nodes with a Lamp child suggest moving head or scanner
    bool hasAxis = hasType(geo.root, GeometryAxis);
    bool hasLamp = hasType(geo.root, GeometryLamp);
    if (hasAxis && hasLamp)
    {
        // Count axis nodes: 2 = moving head (pan+tilt), 1 = scanner (tilt only)
        int axisCount = 0;
        std::function<void(const GDTFGeometryNode &)> countAxes;
        countAxes = [&countAxes, &axisCount](const GDTFGeometryNode &node)
        {
            if (node.type == GeometryAxis) axisCount++;
            for (const auto &child : node.children)
                countAxes(child);
        };
        countAxes(geo.root);
        return axisCount >= 2 ? QLCFixtureDef::MovingHead : QLCFixtureDef::Scanner;
    }
    if (hasLamp)
        return QLCFixtureDef::ColorChanger;  // generic fixture with a lamp
    return QLCFixtureDef::Other;
}

// ---------------------------------------------------------------------------
// Geometry tree extraction
// ---------------------------------------------------------------------------

/** Extract model properties (mesh, primitive, dimensions) from a libMVRgdtf geometry. */
static void extractModelProperties(IGdtfGeometry *geom, GDTFGeometryNode &node,
                                    QMap<QString, QByteArray> &meshData)
{
    IGdtfModel *model = nullptr;
    if (geom->GetModel(&model) != kVCOMError_NoError || model == nullptr)
        return;

    // Dimensions — GDTF Model dimensions are in meters per DIN SPEC 15800
    double l = 0, w = 0, h = 0;
    model->GetLength(l);
    model->GetWidth(w);
    model->GetHeight(h);
    node.modelLength = static_cast<float>(l);
    node.modelWidth  = static_cast<float>(w);
    node.modelHeight = static_cast<float>(h);

    // Primitive type
    EGdtfModel_PrimitiveType primType;
    if (model->GetPrimitiveType(primType) == kVCOMError_NoError)
        node.primitiveType = mapPrimitiveType(primType);

    // glTF model data (preferred)
    void *buffer = nullptr;
    size_t bufLen = 0;
    if (model->GetBufferGLTF(&buffer, bufLen) == kVCOMError_NoError &&
        buffer != nullptr && bufLen > 0)
    {
        QString meshName = QString::fromUtf8(model->GetGeometryFileName());
        if (meshName.isEmpty())
            meshName = node.name + QStringLiteral(".glb");
        node.meshRef = meshName;
        if (!meshData.contains(meshName))
            meshData.insert(meshName, QByteArray(static_cast<const char *>(buffer), bufLen));
    }

    // 3DS model data (fallback — most GDTF fixtures on gdtf-share only have .3ds)
    // Use file path approach only — GetBuffer3DS() has known bugs in libMVRgdtf.
    if (node.meshRef.isEmpty())
    {
        QString filePath = QString::fromUtf8(model->GetGeometryFile_3DS_FullPath());
        if (!filePath.isEmpty())
        {
            QFile f(filePath);
            if (f.open(QIODevice::ReadOnly))
            {
                QByteArray data = f.readAll();
                if (!data.isEmpty())
                {
                    QString meshName = QString::fromUtf8(model->GetGeometryFileName());
                    if (meshName.isEmpty())
                        meshName = node.name + QStringLiteral(".3ds");
                    else if (!meshName.endsWith(QStringLiteral(".3ds"), Qt::CaseInsensitive))
                        meshName += QStringLiteral(".3ds");
                    node.meshRef = meshName;
                    if (!meshData.contains(meshName))
                        meshData.insert(meshName, data);
                }
            }
        }
    }

    model->Release();
}

/** Extract beam properties (angle, flux, color temp) from a Lamp/Laser/Beam geometry. */
static void extractBeamProperties(IGdtfGeometry *geom, GDTFGeometryNode &node)
{
    double val = 0;
    if (geom->GetBeamAngle(val) == kVCOMError_NoError)
        node.beamAngle = static_cast<float>(val);
    if (geom->GetFieldAngle(val) == kVCOMError_NoError)
        node.fieldAngle = static_cast<float>(val);
    if (geom->GetLuminousIntensity(val) == kVCOMError_NoError)
        node.luminousIntensity = static_cast<float>(val);
    if (geom->GetColorTemperature(val) == kVCOMError_NoError)
        node.colorTemperature = static_cast<float>(val);
}

static void extractGeometryNode(IGdtfGeometry *geom, GDTFGeometryNode &node,
                                QMap<QString, QByteArray> &meshData)
{
    node.name = QString::fromUtf8(geom->GetName());

    // Type
    EGdtfObjectType objType;
    if (geom->GetGeometryType(objType) == kVCOMError_NoError)
        node.type = mapGeometryType(objType);

    // Transform
    STransformMatrix mat;
    if (geom->GetTransformMatrix(mat) == kVCOMError_NoError)
        convertTransform(mat, node.localTransform);

    // Model (mesh, primitive, dimensions)
    extractModelProperties(geom, node, meshData);

    // Beam properties (for Lamp/Laser geometry types)
    if (node.type == GeometryLamp || node.type == GeometryLaser)
        extractBeamProperties(geom, node);

    // GeometryReference: resolve the referenced geometry and copy its
    // model + beam properties into this node. The reference node keeps
    // its own localTransform (position offset) and name, but gains the
    // target's visual and beam data so it renders correctly.
    if (node.type == GeometryReference)
    {
        IGdtfGeometry *refedGeom = nullptr;
        if (geom->GetGeometryReference(&refedGeom) == kVCOMError_NoError && refedGeom)
        {
            // Copy model from referenced geometry if this node has none
            if (node.meshRef.isEmpty() && node.primitiveType == PrimitiveUndefined)
                extractModelProperties(refedGeom, node, meshData);

            // Copy beam properties from referenced geometry
            EGdtfObjectType refType;
            if (refedGeom->GetGeometryType(refType) == kVCOMError_NoError)
            {
                if (refType == eGdtfGeometryLamp ||
                    refType == eGdtfGeometryLaser)
                {
                    extractBeamProperties(refedGeom, node);
                }
            }

            // Recursively extract children of the referenced geometry
            size_t refChildCount = 0;
            refedGeom->GetInternalGeometryCount(refChildCount);
            for (size_t i = 0; i < refChildCount; i++)
            {
                IGdtfGeometry *refChild = nullptr;
                if (refedGeom->GetInternalGeometryAt(i, &refChild) == kVCOMError_NoError &&
                    refChild != nullptr)
                {
                    node.children.append(GDTFGeometryNode());
                    extractGeometryNode(refChild, node.children.last(), meshData);
                    refChild->Release();
                }
            }

            refedGeom->Release();
        }
    }

    // Children (of the node itself, not the referenced geometry)
    size_t childCount = 0;
    geom->GetInternalGeometryCount(childCount);
    for (size_t i = 0; i < childCount; i++)
    {
        IGdtfGeometry *childGeom = nullptr;
        if (geom->GetInternalGeometryAt(i, &childGeom) == kVCOMError_NoError &&
            childGeom != nullptr)
        {
            node.children.append(GDTFGeometryNode());
            extractGeometryNode(childGeom, node.children.last(), meshData);
            childGeom->Release();
        }
    }
}

// ---------------------------------------------------------------------------
// GDTFParser
// ---------------------------------------------------------------------------

GDTFParser::GDTFParser() = default;
GDTFParser::~GDTFParser() = default;

bool GDTFParser::loadGDTF(const QString &path, QLCFixtureDef *fixtureDef)
{
    if (fixtureDef == nullptr)
    {
        m_lastError = QStringLiteral("fixtureDef is null");
        return false;
    }

    // Create GDTF fixture interface via libMVRgdtf's COM factory
    IGdtfFixturePtr gdtf;
    VCOMError err = gdtf.Query(IID_IGdtfFixture);
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("Failed to create IGdtfFixture interface (err=%1)").arg(err);
        return false;
    }

    // Parse the .gdtf file
    err = gdtf->ReadFromFile(path.toUtf8().constData());
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("Failed to read GDTF file: %1 (err=%2)").arg(path).arg(err);
        return false;
    }

    // --- Basic info ---
    fixtureDef->setManufacturer(QString::fromUtf8(gdtf->GetManufacturer()));
    fixtureDef->setModel(QString::fromUtf8(gdtf->GetName()));

    QString desc = QString::fromUtf8(gdtf->GetFixtureTypeDescription());
    if (!desc.isEmpty())
        fixtureDef->setAuthor(desc);

    // --- Extract geometry tree ---
    m_geometryData = std::make_unique<GDTFGeometryData>();

    size_t geomCount = 0;
    gdtf->GetGeometryCount(geomCount);
    if (geomCount > 0)
    {
        // GDTF fixtures typically have a single root geometry.
        // If multiple, wrap them under the root node.
        if (geomCount == 1)
        {
            IGdtfGeometry *rootGeom = nullptr;
            if (gdtf->GetGeometryAt(0, &rootGeom) == kVCOMError_NoError && rootGeom)
            {
                extractGeometryNode(rootGeom, m_geometryData->root, m_geometryData->meshData);
                rootGeom->Release();
            }
        }
        else
        {
            m_geometryData->root.name = QStringLiteral("Root");
            for (size_t i = 0; i < geomCount; i++)
            {
                IGdtfGeometry *geom = nullptr;
                if (gdtf->GetGeometryAt(i, &geom) == kVCOMError_NoError && geom)
                {
                    m_geometryData->root.children.append(GDTFGeometryNode());
                    extractGeometryNode(geom, m_geometryData->root.children.last(),
                                        m_geometryData->meshData);
                    geom->Release();
                }
            }
        }
    }

    // --- Guess fixture type from geometry ---
    fixtureDef->setType(guessFixtureType(*m_geometryData));

    // --- DMX modes and channels ---
    size_t modeCount = 0;
    gdtf->GetDmxModeCount(modeCount);
    for (size_t m = 0; m < modeCount; m++)
    {
        IGdtfDmxMode *gdtfMode = nullptr;
        if (gdtf->GetDmxModeAt(m, &gdtfMode) != kVCOMError_NoError || !gdtfMode)
            continue;

        QLCFixtureMode *mode = new QLCFixtureMode(fixtureDef);
        mode->setName(QString::fromUtf8(gdtfMode->GetName()));

        // Walk DMX channels for this mode — collect (offset, channel) pairs first
        struct ChanEntry { int offset; QLCChannel *chan; };
        QVector<ChanEntry> chanEntries;

        GDTFDmxModeInfo modeInfo;
        modeInfo.modeName = QString::fromUtf8(gdtfMode->GetName());

        // Track per-mode physical ranges for Pan/Tilt
        double panPhysicalRange = 0.0;
        double tiltPhysicalRange = 0.0;

        size_t chanCount = 0;
        gdtfMode->GetDmxChannelCount(chanCount);
        for (size_t c = 0; c < chanCount; c++)
        {
            IGdtfDmxChannel *gdtfChan = nullptr;
            if (gdtfMode->GetDmxChannelAt(c, &gdtfChan) != kVCOMError_NoError || !gdtfChan)
                continue;

            Sint32 coarse = -1;
            gdtfChan->GetCoarse(coarse);
            if (coarse < 0)
            {
                gdtfChan->Release();
                continue;
            }

            // Get geometry reference for this channel (for Phase 3 kinematics)
            QString geoRef;
            {
                IGdtfGeometry *geo = nullptr;
                if (gdtfChan->GetGeometry(&geo) == kVCOMError_NoError && geo)
                {
                    geoRef = QString::fromUtf8(geo->GetName());
                    geo->Release();
                }
            }

            // Determine channel name and group from the first logical channel's attribute.
            // Also descend into channel functions for PhysicalStart/PhysicalEnd.
            QString chanName = QString::fromUtf8(gdtfChan->GetName());
            QLCChannel::Group group = QLCChannel::Nothing;
            QString attrName;
            double physFrom = 0.0, physTo = 0.0;
            bool hasPhysical = false;

            size_t logCount = 0;
            gdtfChan->GetLogicalChannelCount(logCount);
            if (logCount > 0)
            {
                IGdtfDmxLogicalChannel *logChan = nullptr;
                if (gdtfChan->GetLogicalChannelAt(0, &logChan) == kVCOMError_NoError && logChan)
                {
                    IGdtfAttribute *attr = nullptr;
                    if (logChan->GetAttribute(&attr) == kVCOMError_NoError && attr)
                    {
                        attrName = QString::fromUtf8(attr->GetName());
                        if (chanName.isEmpty())
                            chanName = QString::fromUtf8(attr->GetPrettyName());
                        if (chanName.isEmpty())
                            chanName = attrName;
                        group = attributeNameToGroup(attrName);
                        attr->Release();
                    }

                    // Descend into channel functions for physical range data.
                    // The first function typically carries the main physical range
                    // (e.g., Pan 0→540°). Additional functions are sub-ranges
                    // (e.g., Pan fine trim) which we skip.
                    size_t funcCount = 0;
                    logChan->GetDmxFunctionCount(funcCount);
                    if (funcCount > 0)
                    {
                        IGdtfDmxChannelFunction *chanFunc = nullptr;
                        if (logChan->GetDmxFunctionAt(0, &chanFunc) == kVCOMError_NoError && chanFunc)
                        {
                            chanFunc->GetPhysicalStart(physFrom);
                            chanFunc->GetPhysicalEnd(physTo);
                            hasPhysical = true;
                            chanFunc->Release();
                        }
                    }

                    logChan->Release();
                }
            }

            if (chanName.isEmpty())
                chanName = QStringLiteral("Channel %1").arg(coarse + 1);

            // Create or reuse the coarse QLC channel
            QLCChannel *qlcChan = nullptr;
            for (QLCChannel *existing : fixtureDef->channels())
            {
                if (existing->name() == chanName && existing->group() == group &&
                    existing->controlByte() == QLCChannel::MSB)
                {
                    qlcChan = existing;
                    break;
                }
            }
            if (!qlcChan)
            {
                qlcChan = new QLCChannel();
                qlcChan->setName(chanName);
                qlcChan->setGroup(group);
                qlcChan->setControlByte(QLCChannel::MSB);
                qlcChan->addCapability(new QLCCapability(0, 255, chanName));
                fixtureDef->addChannel(qlcChan);
            }
            chanEntries.append({coarse, qlcChan});

            // Fine channel
            Sint32 fine = -1;
            gdtfChan->GetFine(fine);
            if (fine >= 0)
            {
                QString fineName = chanName + QStringLiteral(" Fine");
                QLCChannel *fineChan = nullptr;
                for (QLCChannel *existing : fixtureDef->channels())
                {
                    if (existing->name() == fineName)
                    {
                        fineChan = existing;
                        break;
                    }
                }
                if (!fineChan)
                {
                    fineChan = new QLCChannel();
                    fineChan->setName(fineName);
                    fineChan->setGroup(group);
                    fineChan->setControlByte(QLCChannel::LSB);
                    fineChan->addCapability(new QLCCapability(0, 255, fineName));
                    fixtureDef->addChannel(fineChan);
                }
                chanEntries.append({fine, fineChan});
            }

            // Collect DMX channel metadata for Phase 3 (GDTF-native kinematics)
            if (!attrName.isEmpty())
            {
                GDTFDmxChannelInfo info;
                info.attributeName = attrName;
                info.coarseOffset = coarse;
                info.fineOffset = fine;
                info.physicalFrom = physFrom;
                info.physicalTo = physTo;
                info.geometryRef = geoRef;
                modeInfo.channels.append(info);

                // Track physical ranges for Pan/Tilt to populate QLCPhysical
                if (hasPhysical)
                {
                    double range = std::abs(physTo - physFrom);
                    if (attrName.startsWith(QStringLiteral("Pan")) && range > panPhysicalRange)
                        panPhysicalRange = range;
                    if (attrName.startsWith(QStringLiteral("Tilt")) && range > tiltPhysicalRange)
                        tiltPhysicalRange = range;
                }
            }

            gdtfChan->Release();
        }

        // Sort by DMX offset and insert into mode sequentially
        std::sort(chanEntries.begin(), chanEntries.end(),
                  [](const ChanEntry &a, const ChanEntry &b) { return a.offset < b.offset; });
        for (const auto &entry : chanEntries)
            mode->insertChannel(entry.chan, mode->channels().size());

        // Physical properties
        QLCPhysical physical;
        // Try to get beam info from the geometry tree's lamp node
        std::function<const GDTFGeometryNode *(const GDTFGeometryNode &)> findLamp;
        findLamp = [&findLamp](const GDTFGeometryNode &node) -> const GDTFGeometryNode *
        {
            if (node.type == GeometryLamp) return &node;
            for (const auto &child : node.children)
            {
                auto *result = findLamp(child);
                if (result) return result;
            }
            return nullptr;
        };
        if (auto *lamp = findLamp(m_geometryData->root))
        {
            physical.setLensDegreesMin(lamp->beamAngle);
            physical.setLensDegreesMax(lamp->fieldAngle);
            physical.setBulbLumens(static_cast<int>(lamp->luminousIntensity));
            physical.setBulbColourTemperature(static_cast<int>(lamp->colorTemperature));
        }

        // Set pan/tilt ranges from GDTF PhysicalStart/PhysicalEnd.
        // This populates the legacy QLCPhysical scalars so the existing
        // pipeline works (Phase 1 stopgap). Phase 3 bypasses QLCPhysical
        // entirely and builds KinematicChain + ChannelMap from the raw
        // signed pair stored in GDTFDmxModeInfo.
        if (panPhysicalRange > 0)
            physical.setFocusPanMax(static_cast<int>(panPhysicalRange));
        if (tiltPhysicalRange > 0)
            physical.setFocusTiltMax(static_cast<int>(tiltPhysicalRange));

        mode->setPhysical(physical);

        // Store DMX mode metadata for Phase 3 (GDTF-native kinematics)
        m_geometryData->dmxModes.append(modeInfo);

        fixtureDef->addMode(mode);
        gdtfMode->Release();
    }

    // Set overall physical from first mode
    if (!fixtureDef->modes().isEmpty())
        fixtureDef->setPhysical(fixtureDef->modes().first()->physical());

    // Log parsing errors from libMVRgdtf
    size_t errCount = 0;
    gdtf->GetParsingErrorCount(errCount);
    if (errCount > 0)
    {
        qWarning() << "GDTF parsing warnings for" << path << ":" << errCount << "issues";
    }

    qDebug() << "GDTF loaded:" << fixtureDef->manufacturer() << fixtureDef->model()
             << "modes:" << fixtureDef->modes().size()
             << "channels:" << fixtureDef->channels().size()
             << "geometry nodes:" << m_geometryData->root.children.size();

    return true;
}

GDTFGeometryData *GDTFParser::takeGeometryData()
{
    return m_geometryData.release();
}

QString GDTFParser::lastError() const
{
    return m_lastError;
}
