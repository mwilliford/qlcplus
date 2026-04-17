/*
  Q Light Controller Plus
  mvrio.cpp

  Copyright (c) Marcus Williford

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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

#include "Include/VectorworksMVR.h"

#include "mvrio.h"
#include "doc.h"
#include "fixture.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "spatialmodel.h"

using namespace VectorworksMVR;
using VectorworksMVR::STransformMatrix;

// ---------------------------------------------------------------------------
// ImportState — tracks per-import bookkeeping (focus-point UUIDs etc.).
// ---------------------------------------------------------------------------
struct MvrIO::ImportState
{
    // The live MVR interface — needed for GetFirstChild/GetNextObject calls
    // during tree traversal. Owned by importMvrInternal; borrowed here.
    VectorworksMVR::IMediaRessourceVectorInterface *mvr = nullptr;

    // MVR focus-point UUID → SpatialModel focus-point id.
    // Populated as focus points are imported, used to resolve the
    // fixture→focus-point link in a second pass.
    QHash<QString, QString> focusIdByUuid;

    // Fixture id → MVR focus-point UUID, for the second pass.
    QHash<quint32, QString> pendingFocusByFixture;
};

// ---------------------------------------------------------------------------
// MvrUUID → QString helper (canonical 8-4-4-4-12 hex form).
// ---------------------------------------------------------------------------
static QString uuidToString(const MvrUUID &u)
{
    return QStringLiteral("%1-%2-%3-%4")
        .arg(u.a, 8, 16, QChar('0'))
        .arg(u.b, 8, 16, QChar('0'))
        .arg(u.c, 8, 16, QChar('0'))
        .arg(u.d, 8, 16, QChar('0'));
}

// ---------------------------------------------------------------------------
// Coordinate conversion (static, unit-testable)
// ---------------------------------------------------------------------------

rigmath::RigidTransform MvrIO::convertMvrMatrix(const STransformMatrix &src)
{
    rigmath::RigidTransform t;

    // MVR: u,v,w are the world-space basis vectors of the fixture's local
    // X/Y/Z axes (i.e. the columns of the rotation matrix).
    // rigmath stores rot[9] row-major: rot[row*3 + col].
    //
    //   rot(0,0)=ux  rot(0,1)=vx  rot(0,2)=wx
    //   rot(1,0)=uy  rot(1,1)=vy  rot(1,2)=wy
    //   rot(2,0)=uz  rot(2,1)=vz  rot(2,2)=wz
    t.rot[0] = src.ux; t.rot[1] = src.vx; t.rot[2] = src.wx;
    t.rot[3] = src.uy; t.rot[4] = src.vy; t.rot[5] = src.wy;
    t.rot[6] = src.uz; t.rot[7] = src.vz; t.rot[8] = src.wz;

    // MVR translation is in millimeters per DIN SPEC 15801; rigmath is meters.
    t.pos[0] = src.ox / 1000.0;
    t.pos[1] = src.oy / 1000.0;
    t.pos[2] = src.oz / 1000.0;

    return t;
}

void MvrIO::convertToMvrMatrix(const rigmath::RigidTransform &src,
                                STransformMatrix &out)
{
    // Inverse of convertMvrMatrix: write columns from rigmath's rows.
    out.ux = src.rot[0]; out.uy = src.rot[3]; out.uz = src.rot[6];
    out.vx = src.rot[1]; out.vy = src.rot[4]; out.vz = src.rot[7];
    out.wx = src.rot[2]; out.wy = src.rot[5]; out.wz = src.rot[8];

    out.ox = src.pos[0] * 1000.0;
    out.oy = src.pos[1] * 1000.0;
    out.oz = src.pos[2] * 1000.0;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MvrIO::MvrIO(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
}

// ---------------------------------------------------------------------------
// Public import entry point
// ---------------------------------------------------------------------------

bool MvrIO::importMvr(const QString &mvrPath)
{
    m_lastError.clear();
    m_importedFixtures = 0;
    m_importedFocusPoints = 0;
    m_missingGdtfs.clear();

    if (m_doc == nullptr)
    {
        m_lastError = QStringLiteral("MvrIO has no Doc attached");
        emit importFinished(false, 0);
        return false;
    }

    QFileInfo fi(mvrPath);
    if (!fi.exists() || !fi.isReadable())
    {
        m_lastError = QStringLiteral("MVR file not readable: %1").arg(mvrPath);
        emit importFinished(false, 0);
        return false;
    }

    ImportState state;
    const bool ok = importMvrInternal(mvrPath, state);

    emit importFinished(ok, m_importedFixtures);
    return ok;
}

bool MvrIO::importMvrInternal(const QString &mvrPath, ImportState &state)
{
    // 1. Open the archive
    IMediaRessourceVectorInterfacePtr mvr;
    VCOMError err = mvr.Query(IID_MediaRessourceVectorInterface);
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("Failed to create MVR interface (err=%1)").arg(err);
        return false;
    }

    err = mvr->OpenForRead(mvrPath.toUtf8().constData());
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("OpenForRead failed for %1 (err=%2)")
            .arg(mvrPath).arg(err);
        return false;
    }

    // 2. Version check — refuse newer MAJOR versions than we support
    Sint32 fileMaj = 0, fileMin = 0, libMaj = 0, libMin = 0;
    mvr->GetMVRVersion(fileMaj, fileMin);
    mvr->GetLatestMVRSupoortedVersion(libMaj, libMin);
    qDebug() << "[MvrIO] Import" << mvrPath
             << "file MVR" << fileMaj << "." << fileMin
             << "lib supports up to" << libMaj << "." << libMin;
    if (fileMaj > libMaj)
    {
        m_lastError = QStringLiteral("MVR version %1.%2 is newer than supported (%3.%4)")
            .arg(fileMaj).arg(fileMin).arg(libMaj).arg(libMin);
        return false;
    }

    // 3. Copy extracted GDTFs into the user's gdtf-cache and reload
    const QString cacheDir = ingestExtractedGdtfs(mvrPath);
    if (!cacheDir.isEmpty())
        m_doc->fixtureDefCache()->loadGDTFCache(cacheDir);

    // 4. Walk each top-level layer's children
    state.mvr = mvr;
    ISceneObj *layer = nullptr;
    err = mvr->GetFirstLayer(&layer);
    while (err == kVCOMError_NoError && layer != nullptr)
    {
        walkContainer(layer, state);

        ISceneObj *next = nullptr;
        err = mvr->GetNextObject(layer, &next);
        layer->Release();
        layer = (err == kVCOMError_NoError) ? next : nullptr;
    }

    // 5. Second pass: resolve fixture→focus-point assignments
    for (auto it = state.pendingFocusByFixture.constBegin();
         it != state.pendingFocusByFixture.constEnd(); ++it)
    {
        const quint32 fixtureId = it.key();
        const QString &fpUuid = it.value();
        const auto fpIt = state.focusIdByUuid.constFind(fpUuid);
        if (fpIt == state.focusIdByUuid.constEnd())
            continue;
        m_doc->spatialModel()->assignFixtureToFocusPoint(
            fpIt.value(), QString::number(fixtureId));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Tree walk
// ---------------------------------------------------------------------------

void MvrIO::walkContainer(void *containerSceneObj, ImportState &state)
{
    auto *container = static_cast<ISceneObj *>(containerSceneObj);
    if (container == nullptr || state.mvr == nullptr)
        return;

    ISceneObj *child = nullptr;
    VCOMError err = state.mvr->GetFirstChild(container, &child);
    while (err == kVCOMError_NoError && child != nullptr)
    {
        ESceneObjType type = ESceneObjType::SceneObj;
        child->GetType(type);
        switch (type)
        {
        case ESceneObjType::Fixture:
            importFixture(child, state);
            break;
        case ESceneObjType::FocusPoint:
            importFocusPoint(child, state);
            break;
        case ESceneObjType::Group:
        case ESceneObjType::Layer:
            walkContainer(child, state);
            break;
        case ESceneObjType::Truss:
        case ESceneObjType::Support:
        case ESceneObjType::SceneObj:
        case ESceneObjType::VideoScreen:
        case ESceneObjType::Projector:
        default:
            // Static geometry + AV objects are out of MVR-1 scope.
            break;
        }

        ISceneObj *next = nullptr;
        err = state.mvr->GetNextObject(child, &next);
        child->Release();
        child = (err == kVCOMError_NoError) ? next : nullptr;
    }
}

// ---------------------------------------------------------------------------
// Fixture import
// ---------------------------------------------------------------------------

bool MvrIO::importFixture(void *fixtureSceneObj, ImportState &state)
{
    auto *mvrFxi = static_cast<ISceneObj *>(fixtureSceneObj);
    if (mvrFxi == nullptr)
        return false;

    // GDTF identity
    const QString gdtfFile = QString::fromUtf8(mvrFxi->GetGdtfName());
    const QString gdtfMode = QString::fromUtf8(mvrFxi->GetGdtfMode());
    QString fxiName = QString::fromUtf8(mvrFxi->GetName());
    if (fxiName.isEmpty())
        fxiName = gdtfFile.isEmpty() ? QStringLiteral("MVR Fixture") : gdtfFile;

    if (gdtfFile.isEmpty())
    {
        qWarning() << "[MvrIO] Fixture" << fxiName
                   << "has no GDTFSpec; skipping.";
        return false;
    }

    // Resolve against the fixture-def cache (populated from the MVR-extracted
    // gdtf-cache directory earlier in importMvrInternal).
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDefByGdtfFile(gdtfFile);
    if (def == nullptr)
    {
        qWarning() << "[MvrIO] Cannot resolve GDTF" << gdtfFile
                   << "for fixture" << fxiName << "- skipping.";
        if (!m_missingGdtfs.contains(gdtfFile))
            m_missingGdtfs.append(gdtfFile);
        return false;
    }

    QLCFixtureMode *mode = nullptr;
    if (!gdtfMode.isEmpty())
        mode = def->mode(gdtfMode);
    if (mode == nullptr && !def->modes().isEmpty())
        mode = def->modes().first();
    if (mode == nullptr)
    {
        qWarning() << "[MvrIO] Fixture" << fxiName
                   << "has no usable mode (requested:" << gdtfMode << ") - skipping.";
        return false;
    }

    // Address: MVR absolute is 1-based; QLC+ universe+address are 0-based.
    size_t addrCount = 0;
    mvrFxi->GetAdressCount(addrCount);
    quint32 universe = 0;
    quint32 address = 0;
    if (addrCount > 0)
    {
        SDmxAdress a;
        if (mvrFxi->GetAdressAt(0, a) == kVCOMError_NoError)
        {
            const size_t abs0 = (a.fAbsuluteAdress > 0)
                ? (a.fAbsuluteAdress - 1) : 0;
            universe = static_cast<quint32>(abs0 / 512);
            address  = static_cast<quint32>(abs0 % 512);
        }
    }

    // Build the Fixture and hand ownership to the Doc.
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName(fxiName);
    fxi->setFixtureDefinition(def, mode);
    fxi->setUniverse(universe);
    fxi->setAddress(address);

    if (!m_doc->addFixture(fxi))
    {
        qWarning() << "[MvrIO] Doc::addFixture failed for" << fxiName;
        delete fxi;
        return false;
    }

    // Write spatial transform to committed layer.
    STransformMatrix mat = {};
    if (mvrFxi->GetTransfromMatrix(mat) == kVCOMError_NoError)
    {
        const rigmath::RigidTransform t = convertMvrMatrix(mat);
        m_doc->spatialModel()->setFixtureTransform(
            QString::number(fxi->id()), t, SpatialModel::Committed);
    }

    // Defer focus-point linkage to the second pass.
    ISceneObj *fp = nullptr;
    if (mvrFxi->GetFocusPoint(&fp) == kVCOMError_NoError && fp != nullptr)
    {
        MvrUUID fpGuid;
        if (fp->GetGuid(fpGuid) == kVCOMError_NoError)
            state.pendingFocusByFixture.insert(fxi->id(), uuidToString(fpGuid));
        fp->Release();
    }

    m_importedFixtures++;
    emit importProgress(m_importedFixtures, -1);
    return true;
}

// ---------------------------------------------------------------------------
// Focus point import
// ---------------------------------------------------------------------------

bool MvrIO::importFocusPoint(void *fpSceneObj, ImportState &state)
{
    auto *obj = static_cast<ISceneObj *>(fpSceneObj);
    if (obj == nullptr)
        return false;

    MvrUUID guid;
    if (obj->GetGuid(guid) != kVCOMError_NoError)
        return false;

    STransformMatrix mat = {};
    obj->GetTransfromMatrix(mat);
    const rigmath::RigidTransform t = convertMvrMatrix(mat);

    SpatialModel::FocusPoint fp;
    fp.id = uuidToString(guid);
    fp.name = QString::fromUtf8(obj->GetName());
    if (fp.name.isEmpty())
        fp.name = QStringLiteral("Focus Point");
    fp.position[0] = t.pos[0];
    fp.position[1] = t.pos[1];
    fp.position[2] = t.pos[2];

    m_doc->spatialModel()->addFocusPoint(fp);
    state.focusIdByUuid.insert(fp.id, fp.id);
    m_importedFocusPoints++;
    return true;
}

// ---------------------------------------------------------------------------
// GDTF extraction: copy what libMVRgdtf pre-extracted into our cache
// ---------------------------------------------------------------------------

// Path where libMVRgdtf unpacks MVR-attached files on OpenForRead.
// Must mirror SceneDataExchange::SceneDataExchange() + FilingWrapper.cpp's
// GetFolderAppDataPath(). Files live here only while the interface is alive —
// the SceneDataExchange destructor deletes them (see DeleteOnDisk loop).
static QString mvrExchangeExportDir()
{
#if defined(Q_OS_MAC)
    return QDir::homePath()
           + QStringLiteral("/Library/Application Support/mvrexchange/MVR_Export");
#elif defined(Q_OS_WIN)
    // libMVRgdtf uses CSIDL_LOCAL_APPDATA on Windows.
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/MVR_Export");
#else
    // Linux: libMVRgdtf uses $HOME directly with no XDG prefix.
    return QDir::homePath() + QStringLiteral("/MVR_Export");
#endif
}

QString MvrIO::ingestExtractedGdtfs(const QString & /*mvrPath*/)
{
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/gdtf-cache");
    QDir().mkpath(cacheDir);

    const QDir exportDir(mvrExchangeExportDir());
    if (!exportDir.exists())
    {
        qWarning() << "[MvrIO] libMVRgdtf export dir missing:"
                   << exportDir.absolutePath();
        return cacheDir;
    }

    const QStringList gdtfs = exportDir.entryList(
        QStringList() << QStringLiteral("*.gdtf"), QDir::Files);
    for (const QString &f : gdtfs)
    {
        const QString src = exportDir.absoluteFilePath(f);
        const QString dst = cacheDir + QChar('/') + f;
        QFile::remove(dst);
        if (!QFile::copy(src, dst))
            qWarning() << "[MvrIO] Failed to copy" << src << "to" << dst;
        else
            qDebug() << "[MvrIO] Ingested" << f << "into gdtf-cache";
    }
    return cacheDir;
}
