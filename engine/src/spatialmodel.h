/*
  Q Light Controller Plus
  spatialmodel.h

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

#ifndef SPATIALMODEL_H
#define SPATIALMODEL_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <rigmath/rigid_transform.hpp>

class MonitorProperties;

/**
 * @brief Engine-layer model holding spatial state for all fixtures and venue planes.
 *
 * SpatialModel is the canonical source of fixture transforms (position + rotation).
 * Transforms use rigmath conventions: meters, Z-up center-stage origin, axis-angle radians.
 *
 * Data is client-owned and persisted in workspace XML. The server enriches with
 * ephemeral solver visualization (ellipsoids, quality) via calibration_state_update.
 *
 * Replaces CalibrationModel (server-only) + MonitorProperties position/rotation data.
 */
class SpatialModel : public QObject
{
    Q_OBJECT

public:
    explicit SpatialModel(QObject *parent = nullptr);

    /** Source of the transform data */
    enum Source { Manual, Solver };

    // --- Persisted fixture transforms (rigmath native: meters, Z-up, axis-angle) ---

    void setFixtureTransform(const QString &id, const rigmath::RigidTransform &t,
                             Source source = Manual);
    rigmath::RigidTransform fixtureTransform(const QString &id) const;

    /** Write 4x4 column-major matrix for rendering (OpenGL/bgfx ready). */
    void fixtureMatrix4x4(const QString &id, double out[16]) const;

    void removeFixture(const QString &id);
    QStringList fixtureIds() const;
    bool hasFixture(const QString &id) const;
    Source fixtureSource(const QString &id) const;

    // --- Named planes (future: replace gridSize as room model) ---

    struct Plane
    {
        QString name;
        double normal[3];
        double distance;  // from origin, in meters
    };

    void setPlanes(const QList<Plane> &planes);
    QList<Plane> planes() const;

    // --- Ephemeral solver visualization (from server, NOT persisted) ---

    struct FixtureViz
    {
        double ellipsoidAxes[3] = {0, 0, 0};  // a, b, c semi-axes in cm (sorted a >= b >= c)
        double ellipsoidRot[9] = {1,0,0, 0,1,0, 0,0,1};  // 3x3 row-major eigenvector rotation
        QString quality;  // good / moderate / poor / unconstrained
    };

    /** Apply a calibration_state_update message from the server.
     *  Updates transforms (source=Solver) and ephemeral viz data. */
    void applySolverVisualization(const QJsonObject &msg);

    FixtureViz fixtureViz(const QString &id) const;
    bool hasSolverViz() const { return m_hasSolverViz; }
    double rmsResidual() const { return m_rmsResidual; }
    bool converged() const { return m_converged; }
    QStringList poorlyConstrained() const { return m_poorlyConstrained; }
    void clearSolverViz();

    // --- Persistence ---

    bool loadXML(QXmlStreamReader &reader);
    void saveXML(QXmlStreamWriter &writer) const;

    /** Migrate positions from MonitorProperties (legacy workspace without <SpatialModel>). */
    void migrateFromMonitorProperties(MonitorProperties *monProps);

    /** Clear all data (e.g., on workspace change). */
    void clear();

signals:
    /** Emitted when a single fixture transform changes (local edit or solver update). */
    void fixtureTransformChanged(const QString &id);

    /** Emitted when the set of planes changes. */
    void planesChanged();

    /** Emitted when ephemeral solver viz data changes (ellipsoids, quality, rms). */
    void solverVizChanged();

private:
    struct FixtureEntry
    {
        rigmath::RigidTransform transform;
        Source source = Manual;
        FixtureViz viz;
    };

    QMap<QString, FixtureEntry> m_fixtures;
    QList<Plane> m_planes;

    // Ephemeral solver state (not persisted)
    bool m_hasSolverViz = false;
    double m_rmsResidual = 0.0;
    bool m_converged = false;
    QStringList m_poorlyConstrained;
};

#endif // SPATIALMODEL_H
