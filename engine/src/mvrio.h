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
#include <QString>
#include <QStringList>

#include <rigmath/rigid_transform.hpp>

class Doc;

// Forward-declare the libMVRgdtf struct so callers don't need its header.
namespace VectorworksMVR {
    struct STransformMatrix;
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
     *  - libMVRgdtf auto-extracts attached GDTF files next to the MVR (in
     *    a sibling `MVR_Export/` directory). We copy any `.gdtf` files into
     *    QLC+'s per-user GDTF cache and reload the fixture-def cache so the
     *    imported fixtures can be resolved.
     *  - Each Fixture node is created as a QLC+ Fixture with universe+address
     *    from the MVR addresses and a RigidTransform written to the
     *    SpatialModel committed layer.
     *  - Focus points are added to the SpatialModel; fixture↔focus-point
     *    assignments are resolved in a second pass.
     *  - Fixtures whose referenced GDTF is missing from the archive and not
     *    already in the local cache are skipped with a warning — import is
     *    not aborted.
     *
     * Import is additive: existing workspace state is untouched.
     *
     * @param mvrPath Absolute path to the `.mvr` file.
     * @return true on success (even if some fixtures were skipped). False on
     *         unrecoverable errors (file not readable, MVR version too new).
     *         On failure, `lastError()` describes the problem.
     */
    bool importMvr(const QString &mvrPath);

    /** Human-readable error from the most recent import, or empty string. */
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

    // Internal helpers — implemented in mvrio.cpp
    struct ImportState;
    bool importMvrInternal(const QString &mvrPath, ImportState &state);
    void walkContainer(void *containerSceneObj, ImportState &state);
    bool importFixture(void *fixtureSceneObj, ImportState &state);
    bool importFocusPoint(void *fpSceneObj, ImportState &state);

    /** Copy any MVR-extracted `.gdtf` files into the QLC+ GDTF cache dir
     *  and force a reload of the fixture-def cache. Returns the cache dir. */
    QString ingestExtractedGdtfs(const QString &mvrPath);
};

#endif // MVRIO_H
