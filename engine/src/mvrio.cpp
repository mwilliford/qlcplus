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

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

#include "Include/VectorworksMVR.h"

#include "mvrio.h"
#include "doc.h"
#include "fixture.h"
#include "gdtfparser.h"
#include "gdtfwriter.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "spatialmodel.h"

using namespace VectorworksMVR;
using VectorworksMVR::STransformMatrix;

// ---------------------------------------------------------------------------
// ImportState — tracks per-import bookkeeping.
// ---------------------------------------------------------------------------
struct MvrIO::ImportState
{
    // The live MVR interface — needed for GetFirstChild/GetNextObject calls
    // during tree traversal. Owned by importMvrInternal; borrowed here.
    VectorworksMVR::IMediaRessourceVectorInterface *mvr = nullptr;

    // GDTFSpec filename (as it appears in the MVR's <Fixture> nodes) →
    // QLCFixtureDef* that lives in Doc::projectFixtureDefCache(). Used to
    // resolve fixture definitions without consulting the user's global cache.
    QHash<QString, QLCFixtureDef*> defsByGdtfFile;

    // MVR focus-point UUID → SpatialModel focus-point id.
    // Populated as focus points are imported, used to resolve the
    // fixture→focus-point link in a second pass.
    QHash<QString, QString> focusIdByUuid;

    // Fixture id → MVR focus-point UUID, for the second pass.
    QHash<quint32, QString> pendingFocusByFixture;
};

// ---------------------------------------------------------------------------
// MvrUUID ⇄ QString helpers (canonical 32 hex chars, optionally dashed).
// ---------------------------------------------------------------------------
static QString uuidToString(const MvrUUID &u)
{
    return QStringLiteral("%1-%2-%3-%4")
        .arg(u.a, 8, 16, QChar('0'))
        .arg(u.b, 8, 16, QChar('0'))
        .arg(u.c, 8, 16, QChar('0'))
        .arg(u.d, 8, 16, QChar('0'));
}

// Parse a UUID string (with or without dashes / braces) into an MvrUUID.
// Accepts both the `8-8-8-8` form we emit and the dashed `8-4-4-4-12` form
// that QUuid produces by default. Returns zero UUID on parse failure.
static MvrUUID uuidFromString(const QString &s)
{
    MvrUUID out;
    static const QRegularExpression stripRe(QStringLiteral("[\\{\\}\\-]"));
    QString hex = s;
    hex.remove(stripRe);
    if (hex.size() != 32)
        return out;
    bool ok[4] = { true, true, true, true };
    out.a = QStringView(hex).mid(0, 8).toUInt(&ok[0], 16);
    out.b = QStringView(hex).mid(8, 8).toUInt(&ok[1], 16);
    out.c = QStringView(hex).mid(16, 8).toUInt(&ok[2], 16);
    out.d = QStringView(hex).mid(24, 8).toUInt(&ok[3], 16);
    if (!ok[0] || !ok[1] || !ok[2] || !ok[3])
        return MvrUUID();
    return out;
}

// Make a fresh random UUID string in the canonical 8-8-8-8 form used above.
static QString newRandomUuidString()
{
    const QByteArray raw = QUuid::createUuid().toRfc4122();
    MvrUUID u;
    u.a = static_cast<Uint32>((uchar(raw[0]) << 24) | (uchar(raw[1]) << 16)
                              | (uchar(raw[2]) << 8)  |  uchar(raw[3]));
    u.b = static_cast<Uint32>((uchar(raw[4]) << 24) | (uchar(raw[5]) << 16)
                              | (uchar(raw[6]) << 8)  |  uchar(raw[7]));
    u.c = static_cast<Uint32>((uchar(raw[8]) << 24) | (uchar(raw[9]) << 16)
                              | (uchar(raw[10]) << 8) |  uchar(raw[11]));
    u.d = static_cast<Uint32>((uchar(raw[12]) << 24) | (uchar(raw[13]) << 16)
                              | (uchar(raw[14]) << 8) |  uchar(raw[15]));
    return uuidToString(u);
}

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
    // 1. Open the archive. libMVRgdtf unpacks attached .gdtf / .3ds / .png
    //    files into mvrExchangeExportDir() as a side-effect; they vanish when
    //    the interface goes out of scope.
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

    // 2. Version check — refuse newer MAJOR versions than we support.
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

    // 3. Parse every embedded GDTF into the Doc's *project* fixture-def cache.
    //    Must happen before mvr goes out of scope (libMVRgdtf deletes the
    //    export dir on destruction). The user's global gdtf-cache is left
    //    untouched — MVR content is treated as self-contained.
    parseEmbeddedGdtfs(state);

    // 4. Walk each top-level layer's children.
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

    // 5. Second pass: resolve fixture→focus-point assignments.
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
// Embedded GDTF parsing — project-scoped, no writes to gdtf-cache
// ---------------------------------------------------------------------------

void MvrIO::parseEmbeddedGdtfs(ImportState &state)
{
    QLCFixtureDefCache *projectCache =
        (m_doc != nullptr) ? m_doc->projectFixtureDefCache() : nullptr;
    if (projectCache == nullptr)
    {
        qWarning() << "[MvrIO] Doc has no project fixture-def cache; "
                      "cannot import embedded GDTFs.";
        return;
    }

    const QDir exportDir(mvrExchangeExportDir());
    if (!exportDir.exists())
    {
        qWarning() << "[MvrIO] libMVRgdtf export dir missing:"
                   << exportDir.absolutePath();
        return;
    }

    const QStringList gdtfs = exportDir.entryList(
        QStringList() << QStringLiteral("*.gdtf"), QDir::Files);
    for (const QString &filename : gdtfs)
    {
        const QString path = exportDir.absoluteFilePath(filename);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
        {
            qWarning() << "[MvrIO] Failed to read embedded GDTF" << path
                       << ":" << f.errorString();
            continue;
        }
        const QByteArray bytes = f.readAll();
        f.close();

        QLCFixtureDef *def = new QLCFixtureDef();
        GDTFParser parser;
        if (!parser.loadGDTFFromBuffer(bytes, def))
        {
            qWarning() << "[MvrIO] Failed to parse embedded GDTF" << filename;
            delete def;
            continue;
        }

        def->setIsUser(true);
        def->setDefinitionSourceFile(filename);
        def->setLoaded(true);
        def->setGdtfGeometryData(parser.takeGeometryData());

        // Stash the raw bytes so MVR-2 export can re-embed without needing
        // the original file on disk (the MVR's export dir is transient).
        def->setRawGdtfBytes(bytes);

        if (!projectCache->addFixtureDef(def))
        {
            // Duplicate (manufacturer, model) within the same MVR — reuse
            // the first one and drop this copy. `addFixtureDef` does NOT
            // delete on failure; the caller owns the pointer.
            const QString mfg = def->manufacturer();
            const QString model = def->model();
            delete def;
            def = projectCache->fixtureDef(mfg, model);
            if (def == nullptr)
            {
                qWarning() << "[MvrIO] addFixtureDef refused" << filename
                           << "but cache has no" << mfg << "/" << model;
                continue;
            }
        }

        // Insert under both the full filename (with .gdtf) and the bare
        // GDTFSpec name (without extension). MVR spec says GDTFSpec omits
        // the extension, but some exporters include it — handle both.
        state.defsByGdtfFile.insert(filename, def);
        const QString bareKey = filename.endsWith(QStringLiteral(".gdtf"), Qt::CaseInsensitive)
            ? filename.chopped(5) : filename;
        if (bareKey != filename)
            state.defsByGdtfFile.insert(bareKey, def);
        qDebug() << "[MvrIO] Parsed embedded GDTF" << filename
                 << "→" << def->manufacturer() << "/" << def->model();
    }
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

    // Resolve against the per-import parsed-defs map (populated from the MVR's
    // embedded GDTFs in parseEmbeddedGdtfs). No fallthrough to the user's
    // global cache: MVR content is self-contained by design.
    QLCFixtureDef *def = state.defsByGdtfFile.value(gdtfFile, nullptr);
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

    // Preserve the MVR UUID so re-exports keep a stable identity.
    MvrUUID fxiGuid;
    if (mvrFxi->GetGuid(fxiGuid) == kVCOMError_NoError && !fxiGuid.isEmpty())
        fxi->setMvrUuid(uuidToString(fxiGuid));

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
// Export
// ---------------------------------------------------------------------------

// Pick the archive-relative filename used for a def's embedded GDTF.
// Preference order: the basename of `definitionSourceFile()` if it looks
// like a .gdtf, otherwise "<manufacturer>@<model>.gdtf".
static QString gdtfArchiveNameForDef(const QLCFixtureDef *def)
{
    if (def == nullptr)
        return QString();
    const QString src = def->definitionSourceFile();
    if (!src.isEmpty())
    {
        const QString base = QFileInfo(src).fileName();
        if (base.endsWith(QStringLiteral(".gdtf"), Qt::CaseInsensitive))
            return base;
    }
    const QString safeMfg = def->manufacturer().isEmpty()
        ? QStringLiteral("Unknown") : def->manufacturer();
    const QString safeModel = def->model().isEmpty()
        ? QStringLiteral("Unknown") : def->model();
    // Keep the name filesystem-safe; MVR spec doesn't restrict characters but
    // some consumers mishandle slashes/colons. Replace with underscores.
    QString composed = QStringLiteral("%1@%2.gdtf").arg(safeMfg, safeModel);
    static const QRegularExpression badChars(QStringLiteral("[\\\\/:*?\"<>|]"));
    composed.replace(badChars, QStringLiteral("_"));
    return composed;
}

bool MvrIO::exportMvr(const QString &mvrPath)
{
    m_lastError.clear();
    m_exportedFixtures = 0;
    m_exportedGdtfs = 0;
    m_skippedOnExport.clear();

    if (m_doc == nullptr)
    {
        m_lastError = QStringLiteral("MvrIO has no Doc attached");
        return false;
    }

    IMediaRessourceVectorInterfacePtr mvr;
    VCOMError err = mvr.Query(IID_MediaRessourceVectorInterface);
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("Failed to create MVR interface (err=%1)").arg(err);
        return false;
    }

    err = mvr->OpenForWrite(mvrPath.toUtf8().constData());
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("OpenForWrite failed for %1 (err=%2)")
            .arg(mvrPath).arg(err);
        return false;
    }

    mvr->AddProviderAndProviderVersion("bunnyhole", "0.1");

    // --- Embed all referenced GDTFs (deduplicated) ---
    QSet<QString> embedded;
    QHash<quint32, QString> gdtfFilenameByFixtureId;
    const QList<Fixture*> fixtures = m_doc->fixtures();
    for (Fixture *fxi : fixtures)
    {
        const QString filename = embedGdtfForFixture(mvr, fxi, embedded);
        gdtfFilenameByFixtureId.insert(fxi->id(), filename);
    }

    // --- Create a single top-level layer and write each fixture ---
    ISceneObjPtr layer;
    err = mvr->CreateLayerObject(MvrUUID(0, 0, 0, 1), "Rig", &layer);
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("CreateLayerObject failed (err=%1)").arg(err);
        mvr->Close();
        return false;
    }

    for (Fixture *fxi : fixtures)
    {
        const QString gdtfName = gdtfFilenameByFixtureId.value(fxi->id());
        if (!writeFixture(mvr, layer, fxi, gdtfName))
        {
            qWarning() << "[MvrIO] Failed to write fixture" << fxi->name();
            continue;
        }
        m_exportedFixtures++;
    }

    err = mvr->Close();
    if (err != kVCOMError_NoError)
    {
        m_lastError = QStringLiteral("Close failed (err=%1)").arg(err);
        return false;
    }

    qDebug() << "[MvrIO] Exported" << mvrPath
             << "fixtures=" << m_exportedFixtures
             << "gdtfs=" << m_exportedGdtfs
             << "skipped=" << m_skippedOnExport;
    return true;
}

QString MvrIO::embedGdtfForFixture(IMediaRessourceVectorInterface *mvr,
                                   Fixture *fxi,
                                   QSet<QString> &alreadyEmbedded)
{
    if (mvr == nullptr || fxi == nullptr || fxi->fixtureDef() == nullptr)
        return QString();

    QLCFixtureDef *def = fxi->fixtureDef();
    const QString filename = gdtfArchiveNameForDef(def);
    if (filename.isEmpty())
    {
        m_skippedOnExport.append(fxi->name());
        return QString();
    }

    if (alreadyEmbedded.contains(filename))
        return filename;

    // 1. Prefer raw bytes stashed on the def (MVR-imported path).
    QByteArray bytes = def->rawGdtfBytes();

    // 2. Fall back to reading from the on-disk source file.
    if (bytes.isEmpty())
    {
        const QString src = def->definitionSourceFile();
        if (!src.isEmpty()
            && src.endsWith(QStringLiteral(".gdtf"), Qt::CaseInsensitive))
        {
            QFile f(src);
            if (f.open(QIODevice::ReadOnly))
            {
                bytes = f.readAll();
                f.close();
            }
        }
    }

    // 3. MVR-3a: QXF-only fixture — synthesize a minimal GDTF in memory.
    if (bytes.isEmpty())
    {
        const QLCFixtureMode *mode = fxi->fixtureMode();
        if (mode != nullptr)
        {
            QString synthErr;
            bytes = GDTFWriter::writeSynthetic(def, mode, &synthErr);
            if (bytes.isEmpty())
            {
                m_skippedOnExport.append(fxi->name());
                qWarning() << "[MvrIO] GDTF synthesis failed for fixture"
                           << fxi->name() << ":" << synthErr;
                return QString();
            }
        }
        else
        {
            m_skippedOnExport.append(fxi->name());
            qWarning() << "[MvrIO] Fixture has no mode — cannot synthesize GDTF"
                       << fxi->name();
            return QString();
        }
    }

    const VCOMError err = mvr->AddBufferToMvrFile(
        filename.toUtf8().constData(),
        bytes.data(),
        static_cast<size_t>(bytes.size()));
    if (err != kVCOMError_NoError)
    {
        qWarning() << "[MvrIO] AddBufferToMvrFile failed for"
                   << filename << "err=" << err;
        return QString();
    }

    alreadyEmbedded.insert(filename);
    m_exportedGdtfs++;
    return filename;
}

bool MvrIO::writeFixture(IMediaRessourceVectorInterface *mvr,
                         ISceneObj *layer,
                         Fixture *fxi,
                         const QString &gdtfFilename)
{
    if (mvr == nullptr || layer == nullptr || fxi == nullptr)
        return false;

    // --- Transform: committed spatial > identity ---
    rigmath::RigidTransform xform;
    auto committed = m_doc->spatialModel()->committedTransform(
        QString::number(fxi->id()));
    if (committed.has_value())
        xform = committed.value();

    STransformMatrix mat = {};
    convertToMvrMatrix(xform, mat);

    // --- UUID: stored on the Fixture if round-tripped, else fresh ---
    QString uuidStr = fxi->mvrUuid();
    if (uuidStr.isEmpty())
    {
        uuidStr = newRandomUuidString();
        fxi->setMvrUuid(uuidStr);
    }
    MvrUUID uuid = uuidFromString(uuidStr);
    if (uuid.isEmpty())
    {
        // Corrupt stored UUID — regenerate and overwrite.
        uuidStr = newRandomUuidString();
        fxi->setMvrUuid(uuidStr);
        uuid = uuidFromString(uuidStr);
    }

    ISceneObjPtr mvrFxi;
    const QByteArray nameUtf8 = fxi->name().toUtf8();
    VCOMError err = mvr->CreateFixture(
        uuid, mat, nameUtf8.constData(), layer, &mvrFxi);
    if (err != kVCOMError_NoError)
    {
        qWarning() << "[MvrIO] CreateFixture failed for"
                   << fxi->name() << "err=" << err;
        return false;
    }

    // --- GDTF identity ---
    if (!gdtfFilename.isEmpty())
    {
        mvrFxi->SetGdtfName(gdtfFilename.toUtf8().constData());
        if (fxi->fixtureMode() != nullptr)
            mvrFxi->SetGdtfMode(fxi->fixtureMode()->name().toUtf8().constData());
    }

    // --- Address: MVR absolute is 1-based (universe * 512 + offset + 1) ---
    const size_t absolute = static_cast<size_t>(fxi->universe()) * 512u
                          + static_cast<size_t>(fxi->address()) + 1u;
    mvrFxi->AddAdress(absolute, 0);

    return true;
}
