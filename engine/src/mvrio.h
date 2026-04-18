/*
  Q Light Controller Plus
  mvrio.h

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

#ifndef MVRIO_H
#define MVRIO_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <rigmath/rigid_transform.hpp>

class Doc;
class Fixture;

// Forward-declare the libMVRgdtf struct so callers don't need its header.
namespace VectorworksMVR {
    struct STransformMatrix;
    class ISceneObj;
    class IMediaRessourceVectorInterface;
}

/**
 * @brief MVR (My Virtual Rig) import/export for the current Doc.
 *
 * MVR is an industry-standard zip container (GDTF ecosystem) that holds a
 * rig description — fixtures, positions, focus points, optional truss/venue
 * geometry — along with the `.gdtf` files they reference. MvrIO is the
 * engine-layer bridge between libMVRgdtf and QLC+'s Doc / Fixture /
 * SpatialModel.
 *
 * MVR-1 scope: read-only import. Export, `.bhx`, and QXF→GDTF synthesis are
 * separate phases — see `docs/PLAN-mvr.md`.
 */
class MvrIO : public QObject
{
    Q_OBJECT
public:
    explicit MvrIO(Doc *doc, QObject *parent = nullptr);

    /**
     * Import fixtures, focus points, and static geometry from an MVR file.
     *
     * Behaviour:
     *  - Every GDTF attached to the MVR is parsed directly from memory into
     *    the Doc's *project* fixture-def cache (`Doc::projectFixtureDefCache`).
     *    The user's global GDTF library is never written to — MVR content is
     *    treated as self-contained, matching commercial consoles.
     *  - Each Fixture node is created as a QLC+ Fixture with universe+address
     *    from the MVR addresses and a RigidTransform written to the
     *    SpatialModel committed layer.
     *  - Focus points are added to the SpatialModel; fixture↔focus-point
     *    assignments are resolved in a second pass.
     *  - Fixtures whose referenced GDTF failed to parse (or wasn't embedded)
     *    are skipped with a warning — import is not aborted.
     *
     * Import is additive: existing workspace state is untouched.
     *
     * @param mvrPath Absolute path to the `.mvr` file.
     * @return true on success (even if some fixtures were skipped). False on
     *         unrecoverable errors (file not readable, MVR version too new).
     *         On failure, `lastError()` describes the problem.
     */
    bool importMvr(const QString &mvrPath);

    /**
     * Export the current Doc's fixture rig to an MVR file.
     *
     * Behaviour:
     *  - Produces a single layer ("Rig") with one `<Fixture>` entry per
     *    patched Fixture in the Doc.
     *  - Each fixture's transform comes from `SpatialModel::committedTransform`
     *    when available; fixtures with no committed transform get the
     *    identity at the world origin.
     *  - GDTFs are embedded directly in the archive — from raw bytes stored
     *    on the `QLCFixtureDef` (MVR-imported defs) when present, otherwise
     *    read from the def's `definitionSourceFile()` if it points at an
     *    on-disk `.gdtf`. Each GDTF is embedded at most once per export.
     *  - Fixtures backed by a QXF-only def (no GDTF source) are skipped
     *    with a warning — proper synthesis is deferred to MVR-3.
     *  - Fixtures without a stored `mvrUuid()` get a freshly generated one
     *    written back to the Fixture so subsequent round-trips are stable.
     *
     * @return true on success; on failure `lastError()` describes the problem.
     */
    bool exportMvr(const QString &mvrPath);

    /** Names of fixtures that were skipped during the last export because
     *  no embeddable GDTF was available (QXF-only defs pending MVR-3). */
    QStringList skippedOnExport() const { return m_skippedOnExport; }

    /** Number of fixtures written by the most recent export. */
    int exportedFixtureCount() const { return m_exportedFixtures; }

    /** Number of GDTF archives embedded by the most recent export. */
    int exportedGdtfCount() const { return m_exportedGdtfs; }

    /** Human-readable error from the most recent import/export, or empty. */
    QString lastError() const { return m_lastError; }

    /** Number of fixtures successfully created by the most recent import. */
    int importedFixtureCount() const { return m_importedFixtures; }

    /** Number of focus points successfully created by the most recent import. */
    int importedFocusPointCount() const { return m_importedFocusPoints; }

    /** GDTF filenames that were referenced by the MVR but could not be
     *  resolved to a QLCFixtureDef (archive missing them + local cache
     *  didn't have them). Populated during importMvr(). */
    QStringList missingGdtfs() const { return m_missingGdtfs; }

    // ---- Coordinate conversion (public for unit testing) ----

    /**
     * Convert an MVR `STransformMatrix` to a rigmath RigidTransform.
     *
     * MVR convention (GDTF-aligned):
     *   - Z-up, right-handed.
     *   - The u/v/w vectors are the fixture's local X/Y/Z axes expressed in
     *     world coordinates (columns of the rotation matrix).
     *   - Translation (ox, oy, oz) is in MILLIMETERS per MVR spec.
     *
     * rigmath convention:
     *   - Z-up, right-handed.
     *   - Row-major 3x3 rotation stored in `rot[9]`.
     *   - Translation in METERS in `pos[3]`.
     *
     * So the axis basis vectors map directly (no axis swaps) and the
     * translation is divided by 1000.
     */
    static rigmath::RigidTransform convertMvrMatrix(
        const VectorworksMVR::STransformMatrix &src);

    /**
     * Inverse of convertMvrMatrix — rigmath → MVR. Used by MVR-2 (export).
     * Exposed here so both directions ship with their unit tests together.
     */
    static void convertToMvrMatrix(const rigmath::RigidTransform &src,
                                    VectorworksMVR::STransformMatrix &out);

signals:
    /** Emitted as each fixture finishes importing (for UI progress). */
    void importProgress(int done, int total);

    /** Emitted when an import finishes (success or failure). */
    void importFinished(bool success, int fixtureCount);

private:
    Doc *m_doc;

    // Populated during importMvr()
    QString m_lastError;
    int m_importedFixtures = 0;
    int m_importedFocusPoints = 0;
    QStringList m_missingGdtfs;

    // Populated during exportMvr()
    int m_exportedFixtures = 0;
    int m_exportedGdtfs = 0;
    QStringList m_skippedOnExport;

    // Internal helpers — implemented in mvrio.cpp
    struct ImportState;
    bool importMvrInternal(const QString &mvrPath, ImportState &state);
    void walkContainer(void *containerSceneObj, ImportState &state);
    bool importFixture(void *fixtureSceneObj, ImportState &state);
    bool importFocusPoint(void *fpSceneObj, ImportState &state);

    /** Parse every GDTF that libMVRgdtf extracted for this archive into the
     *  Doc's project fixture-def cache, keyed by GDTFSpec filename so the
     *  scene walk can resolve `<Fixture GDTFSpec="...">` nodes. Results are
     *  written into `state.defsByGdtfFile`. The user's global gdtf-cache is
     *  not touched. Must be called while the IMediaRessourceVectorInterface
     *  is still alive (libMVRgdtf deletes the export dir on close). */
    void parseEmbeddedGdtfs(ImportState &state);

    /** Embed the GDTF for a single fixture into the MVR archive, skipping
     *  any filename already present in `alreadyEmbedded`. Returns the
     *  filename used inside the archive, or empty string if nothing was
     *  embedded (QXF-only fixtures or unreadable source). */
    QString embedGdtfForFixture(VectorworksMVR::IMediaRessourceVectorInterface *mvr,
                                Fixture *fxi,
                                QSet<QString> &alreadyEmbedded);

    /** Write a single fixture into the given layer. Returns true on success.
     *  `gdtfFilename` is the archive-relative filename that the fixture's
     *  `<GDTFSpec>` should reference (empty if the fixture had no embeddable
     *  GDTF — export still writes the record so the rig is preserved). */
    bool writeFixture(VectorworksMVR::IMediaRessourceVectorInterface *mvr,
                      VectorworksMVR::ISceneObj *layer,
                      Fixture *fxi,
                      const QString &gdtfFilename);
};

#endif // MVRIO_H
