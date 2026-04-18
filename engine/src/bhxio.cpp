/*
  Q Light Controller Plus
  bhxio.cpp

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
#include <QFile>
#include <QFileInfo>
#include <QMap>

// Qt private API. QZipReader/QZipWriter are Q_CORE_EXPORT'd and stable —
// the headers have been in Qt's private API since Qt 4 and libMVRgdtf itself
// ships a copy (src/Minizip/Source/miniz.c). We use Qt's rather than pulling
// miniz out of libMVRgdtf because libMVRgdtf doesn't expose a read-side
// content extractor (`GetAttachedFileCount`/`GetAttachedFileCountAt` return
// names only).
#include <private/qzipreader_p.h>

#include "bhxio.h"
#include "doc.h"
#include "mvrio.h"

// ---------------------------------------------------------------------------
// .bhx layout constants
// ---------------------------------------------------------------------------

static const QString kManifestPath     = QStringLiteral("manifest.json");
static const QString kProgrammingPath  = QStringLiteral("programming/functions.xml");
static const QString kConsolePath      = QStringLiteral("console/virtualconsole.xml");
static const QString kIoPath           = QStringLiteral("io/universes.xml");
static const QString kCalibrationPath  = QStringLiteral("calibration/spatial.xml");

// Bump when the .bhx layout changes in a way openers need to branch on.
static const QString kManifestVersion  = QStringLiteral("1");

static QByteArray buildManifestJson()
{
    // Minimal JSON by hand — avoids QJsonDocument pulling a runtime dep into
    // the engine for one line of metadata. If this grows, switch to QJson.
    return QByteArrayLiteral(
        "{\n"
        "  \"format\": \"bhx\",\n"
        "  \"formatVersion\": \"") + kManifestVersion.toUtf8() +
        QByteArrayLiteral("\",\n"
        "  \"producer\": \"bunnyhole\"\n"
        "}\n");
}

// ---------------------------------------------------------------------------
// BhxIO
// ---------------------------------------------------------------------------

BhxIO::BhxIO(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
}

bool BhxIO::saveBhx(const QString &path, const QByteArray &consoleXml)
{
    m_lastError.clear();

    if (m_doc == nullptr)
    {
        m_lastError = QStringLiteral("BhxIO has no Doc attached");
        return false;
    }

    // Build the extension streams. All four Doc-owned streams are produced
    // via Doc::save*XmlStream(); the console stream comes from the caller
    // because VirtualConsole lives in the UI layer.
    QMap<QString, QByteArray> extras;
    extras.insert(kManifestPath, buildManifestJson());
    extras.insert(kProgrammingPath, m_doc->saveProgrammingXmlStream());
    extras.insert(kIoPath, m_doc->saveIoXmlStream());
    extras.insert(kCalibrationPath, m_doc->saveCalibrationXmlStream());
    if (!consoleXml.isEmpty())
        extras.insert(kConsolePath, consoleXml);

    // The rig half (MVR-compatible) is written by MvrIO. Extension streams
    // ride alongside via AddBufferToMvrFile(path, ...).
    MvrIO mvrio(m_doc);
    if (!mvrio.exportMvrWithExtras(path, extras))
    {
        m_lastError = QStringLiteral("MVR export failed: %1").arg(mvrio.lastError());
        return false;
    }

    qDebug() << "[BhxIO] Saved" << path
             << "fixtures=" << mvrio.exportedFixtureCount()
             << "gdtfs=" << mvrio.exportedGdtfCount()
             << "hasConsole=" << !consoleXml.isEmpty();
    return true;
}

bool BhxIO::openBhx(const QString &path)
{
    m_lastError.clear();
    m_consoleXml.clear();

    if (m_doc == nullptr)
    {
        m_lastError = QStringLiteral("BhxIO has no Doc attached");
        return false;
    }

    if (!QFileInfo::exists(path))
    {
        m_lastError = QStringLiteral("File does not exist: %1").arg(path);
        return false;
    }

    QZipReader zr(path);
    if (!zr.isReadable())
    {
        m_lastError = QStringLiteral("Not a readable zip: %1").arg(path);
        return false;
    }

    const QByteArray programmingBytes = zr.fileData(kProgrammingPath);
    const QByteArray ioBytes          = zr.fileData(kIoPath);
    const QByteArray calibrationBytes = zr.fileData(kCalibrationPath);
    m_consoleXml                      = zr.fileData(kConsolePath);

    zr.close();

    if (programmingBytes.isEmpty())
    {
        m_lastError = QStringLiteral(
            "Missing %1 — not a bunnyhole workspace").arg(kProgrammingPath);
        return false;
    }

    // Fixtures/groups/palettes/functions/monitor-props/startup all live in
    // programming/functions.xml. io/calibration are optional.
    m_doc->beginMultiStreamLoad();

    if (!m_doc->loadXmlStream(programmingBytes, /*loadIO=*/false))
    {
        m_doc->endMultiStreamLoad();
        m_lastError = QStringLiteral("Failed to parse %1").arg(kProgrammingPath);
        return false;
    }

    if (!ioBytes.isEmpty())
    {
        if (!m_doc->loadXmlStream(ioBytes, /*loadIO=*/true))
            qWarning() << "[BhxIO] Failed to parse" << kIoPath
                       << "— continuing without IO config";
    }

    if (!calibrationBytes.isEmpty())
    {
        if (!m_doc->loadXmlStream(calibrationBytes, /*loadIO=*/false))
            qWarning() << "[BhxIO] Failed to parse" << kCalibrationPath
                       << "— continuing without calibration";
    }

    m_doc->endMultiStreamLoad();

    qDebug() << "[BhxIO] Opened" << path
             << "programmingBytes=" << programmingBytes.size()
             << "ioBytes=" << ioBytes.size()
             << "calibrationBytes=" << calibrationBytes.size()
             << "consoleBytes=" << m_consoleXml.size();
    return true;
}
