/*
  Q Light Controller Plus
  bhxio.h

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

#ifndef BHXIO_H
#define BHXIO_H

#include <QByteArray>
#include <QObject>
#include <QString>

class Doc;

/** @addtogroup engine Engine
 * @{
 */

/**
 * @brief `.bhx` save/open — the native Bunnyhole workspace format.
 *
 * `.bhx` is an MVR-compatible zip container:
 *
 *     myshow.bhx
 *     ├── manifest.json                       ← version metadata
 *     ├── GeneralSceneDescription.xml         ← MVR rig description (root)
 *     ├── *.gdtf                              ← embedded fixture archives
 *     ├── programming/functions.xml           ← QLC+-native: fixtures,
 *     │                                         functions, groups, palettes,
 *     │                                         monitor props, startup fn
 *     ├── console/virtualconsole.xml          ← VC layout (from the UI layer)
 *     ├── io/universes.xml                    ← InputOutputMap
 *     └── calibration/spatial.xml             ← SpatialModel + CalibrationModel
 *
 * The rig half (root + `*.gdtf`) is a spec-valid MVR — any MVR-aware host
 * (grandMA3, BlenderDMX, Capture, Vectorworks) can read it and the extension
 * files in sibling folders are ignored per the MVR spec.
 *
 * On re-open, we do NOT reload fixtures via the MVR rig half — instead we
 * read them from `programming/functions.xml`, which preserves QLC+ Fixture
 * IDs and any other QLC+-native detail that the MVR rig format doesn't
 * capture. The MVR rig is third-party interop only.
 *
 * Virtual Console XML is owned by the UI layer (ui/qmlui), not by Doc.
 * Save: caller passes the serialized VC bytes to saveBhx().
 * Open: BhxIO exposes the extracted VC bytes via consoleXml() for the UI
 * layer to parse itself.
 */
class BhxIO final : public QObject
{
    Q_OBJECT
public:
    explicit BhxIO(Doc *doc, QObject *parent = nullptr);

    /**
     * Save the current Doc (and caller-supplied VC XML) to @p path.
     *
     * @param path Absolute path to the `.bhx` file to write.
     * @param consoleXml Serialized `<VirtualConsole>` XML from the UI layer.
     *                   May be empty if the caller has no VC (headless tests).
     * @return true on success; on failure lastError() describes the problem.
     */
    bool saveBhx(const QString &path, const QByteArray &consoleXml = QByteArray());

    /**
     * Open a `.bhx` archive into the Doc.
     *
     * Semantics:
     *  - Caller is responsible for `Doc::clearContents()` beforehand — this
     *    function is additive, matching the `MvrIO::importMvr` contract.
     *  - Extension streams are loaded in order: programming, io, calibration.
     *    The MVR rig half is ignored (programming/functions.xml is the
     *    authoritative fixture source).
     *  - VC XML is extracted to consoleXml() for the UI layer to parse.
     *  - beginMultiStreamLoad/endMultiStreamLoad bracket the engine loads so
     *    `loading()`/`loaded()` signals and `postLoad()` fire exactly once.
     *
     * @return true on success; on failure lastError() describes the problem.
     */
    bool openBhx(const QString &path);

    /**
     * VC XML extracted by the most recent openBhx() call. Empty if the
     * archive contained no `console/virtualconsole.xml`.
     */
    QByteArray consoleXml() const { return m_consoleXml; }

    /** Human-readable error from the most recent save/open, or empty. */
    QString lastError() const { return m_lastError; }

private:
    Doc *m_doc;
    QString m_lastError;
    QByteArray m_consoleXml;
};

/** @} */

#endif // BHXIO_H
