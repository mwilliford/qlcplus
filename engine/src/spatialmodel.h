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
#include <QVector3D>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <optional>

#include <rigmath/rigid_transform.hpp>

class MonitorProperties;

/**
 * @brief Engine-layer model holding spatial state for all fixtures and venue planes.
 *
 * SpatialModel is the canonical source of fixture transforms (position + rotation).
 * Transforms use rigmath conventions: meters, Z-up center-stage origin, axis-angle radians.
 *
 * Each fixture has three transform layers:
 *  - committed: user-approved transform (persisted in XML)
 *  - agentDerived: agent proposal (shown as green ghost in 3D)
 *  - solverDerived: solver result (shown as cyan ghost in 3D, updated by auto_solve)
 *
 * A fixture with committed=nullopt is "new" (never positioned by the user).
 * agentDerived/solverDerived are ephemeral and not persisted.
 */
class SpatialModel : public QObject
{
    Q_OBJECT

public:
    explicit SpatialModel(QObject *parent = nullptr);

    /** Transform layer — replaces the old Source enum. */
    enum Layer { Committed, AgentDerived, SolverDerived };

    // --- Three-layer fixture transforms ---

    /** Write a transform to a specific layer.
     *  Writing to Committed clears agentDerived and solverDerived (user decision overrides). */
    void setFixtureTransform(const QString &id, const rigmath::RigidTransform &t,
                             Layer layer = Committed);

    /** Explicit layer accessors — return nullopt if no transform on that layer. */
    std::optional<rigmath::RigidTransform> committedTransform(const QString &id) const;
    std::optional<rigmath::RigidTransform> agentDerivedTransform(const QString &id) const;
    std::optional<rigmath::RigidTransform> solverDerivedTransform(const QString &id) const;

    /** Best available for rendering: committed > agentDerived > solverDerived > identity.
     *  Used by 2D view and as fallback. Prefer explicit layer accessors. */
    rigmath::RigidTransform renderTransform(const QString &id) const;

    /** Backward-compat alias for renderTransform(). */
    rigmath::RigidTransform fixtureTransform(const QString &id) const;

    /** Write 4x4 column-major matrix for rendering (OpenGL/bgfx ready).
     *  Uses renderTransform(). */
    void fixtureMatrix4x4(const QString &id, double out[16]) const;

    /** True if fixture has no committed transform (new/unpositioned). */
    bool isNew(const QString &id) const;

    /** Promote a proposal layer to committed.
     *  Copies the source layer's value to committed, then clears both proposal layers. */
    void promoteTransform(const QString &id, Layer sourceLayer);

    /** Clear a specific proposal layer (AgentDerived or SolverDerived). */
    void clearTransformLayer(const QString &id, Layer layer);

    void removeFixture(const QString &id);
    QStringList fixtureIds() const;
    bool hasFixture(const QString &id) const;

    // --- Convenience accessors for 2D view (mm + Euler degrees) ---
    // Reads use renderTransform(). Writes go to committed layer.

    /** Get position in mm (Z-up, center-stage origin) from renderTransform(). */
    QVector3D fixturePositionMm(const QString &id) const;

    /** Get rotation in Euler degrees (ZYX convention) from renderTransform(). */
    QVector3D fixtureRotationDeg(const QString &id) const;

    /** Set position from mm. Writes to committed, preserves existing rotation. */
    void setFixturePositionMm(const QString &id, const QVector3D &posMm);

    /** Set rotation from Euler degrees. Writes to committed, preserves existing position. */
    void setFixtureRotationDeg(const QString &id, const QVector3D &rotDeg);

    // --- Named planes (future: replace gridSize as room model) ---

    struct Plane
    {
        QString name;
        double normal[3];
        double distance;  // from origin, in meters
    };

    void setPlanes(const QList<Plane> &planes);
    QList<Plane> planes() const;

    // --- Truss/pipe elements (linear snap geometry) ---

    struct Truss
    {
        QString id;
        QString name;
        double start[3] = {0, 0, 0};  // meters
        double end[3]   = {0, 0, 0};  // meters
    };

    void addTruss(const Truss &truss);
    void removeTruss(const QString &id);
    QList<Truss> trusses() const;

    /** Project a point onto the nearest truss line. Returns true if within snapDistance. */
    bool snapToTruss(double x, double y, double z,
                     double &outX, double &outY, double &outZ,
                     double snapDistance = 0.5) const;

    // --- Ephemeral solver visualization (from server, NOT persisted) ---

    struct FixtureViz
    {
        double ellipsoidAxes[3] = {0, 0, 0};  // a, b, c semi-axes in cm (sorted a >= b >= c)
        double ellipsoidRot[9] = {1,0,0, 0,1,0, 0,0,1};  // 3x3 row-major eigenvector rotation
        QString quality;  // good / moderate / poor / unconstrained
    };

    /** Apply a calibration_state_update message from the server.
     *  Writes solver transforms to solverDerived layer and updates ephemeral viz data. */
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
    /** Emitted when a single fixture transform changes (any layer). */
    void fixtureTransformChanged(const QString &id);

    /** Emitted when the set of planes changes. */
    void planesChanged();

    /** Emitted when trusses are added/removed. */
    void trussesChanged();

    /** Emitted when ephemeral solver viz data changes (ellipsoids, quality, rms). */
    void solverVizChanged();

private:
    struct FixtureEntry
    {
        std::optional<rigmath::RigidTransform> committed;
        std::optional<rigmath::RigidTransform> agentDerived;
        std::optional<rigmath::RigidTransform> solverDerived;
        FixtureViz viz;
    };

    QMap<QString, FixtureEntry> m_fixtures;
    QList<Plane> m_planes;
    QList<Truss> m_trusses;

    // Ephemeral solver state (not persisted)
    bool m_hasSolverViz = false;
    double m_rmsResidual = 0.0;
    bool m_converged = false;
    QStringList m_poorlyConstrained;
};

#endif // SPATIALMODEL_H
