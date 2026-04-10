/*
  Q Light Controller Plus
  calibrationmodel.h

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

#ifndef CALIBRATIONMODEL_H
#define CALIBRATIONMODEL_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <variant>
#include <vector>

#include <rigmath/covariance.hpp>
#include <rigmath/rigid_transform.hpp>
#include <rigmath/solver/CalibrationProblem.h>

class Doc;
class SpatialModel;

/**
 * @brief Engine-layer model for calibration: observations, constraints, and local solver.
 *
 * Owns the calibration observation data and runs the Ceres-based solver client-side
 * via rigmath::solver::CalibrationProblem. Results feed into SpatialModel's solverDerived
 * layer and uncertainty visualization.
 *
 * Observations are persisted in XML within the workspace file.
 * The solver runs on-demand (not automatically) via solve().
 */
class CalibrationModel : public QObject
{
    Q_OBJECT

public:
    explicit CalibrationModel(QObject *parent = nullptr);

    void setDoc(Doc *doc) { m_doc = doc; }
    void setSpatialModel(SpatialModel *sm) { m_spatialModel = sm; }

    // -----------------------------------------------------------------------
    // Observation types (mirrors rigmath::solver observation records)
    // -----------------------------------------------------------------------

    struct AimObs {
        int id = -1;
        QString fixture;
        std::vector<double> dmxNormalized;
        double target[3] = {0, 0, 0};
        double certainty = 0.95;
    };

    struct CrossingObs {
        int id = -1;
        QStringList fixtures;
        std::vector<std::vector<double>> dmxValues;
        int axis = 2;        // 0=x, 1=y, 2=z
        double value = 0.0;
        double certainty = 0.85;
    };

    struct PositionObs {
        int id = -1;
        QString fixture;
        int axis = 2;  // 0=x, 1=y, 2=z
        double value = 0.0;
        double certainty = 0.95;
    };

    struct RotationObs {
        int id = -1;
        QString fixture;
        int axis = 5;  // 0=rx, 1=ry, 2=rz (maps to pose index 3,4,5)
        double valueDeg = 0.0;
        double certainty = 0.95;
    };

    struct BeamDirectionObs {
        int id = -1;
        QString fixture;
        std::vector<double> dmxNormalized;
        bool hasElevation = false;
        bool hasAzimuth = false;
        double elevationDeg = 0.0;
        double azimuthDeg = 0.0;
        double certainty = 0.85;
    };

    struct DistanceObs {
        int id = -1;
        QString fixtureA;
        QString fixtureB;
        double distance = 0.0;
        double certainty = 0.90;
    };

    using Observation = std::variant<AimObs, CrossingObs, PositionObs,
                                     RotationObs, BeamDirectionObs, DistanceObs>;

    // -----------------------------------------------------------------------
    // Observation management
    // -----------------------------------------------------------------------

    /** Add an observation. Returns the assigned ID. */
    int addObservation(const Observation &obs);

    /** Remove an observation by ID. */
    void removeObservation(int id);

    /** Get all observations. */
    QList<Observation> observations() const { return m_observations.values(); }

    /** Get observation count. */
    int observationCount() const { return m_observations.size(); }

    /** Clear all observations. */
    void clearObservations();

    // -----------------------------------------------------------------------
    // Convenience builders (return the new observation's ID)
    // -----------------------------------------------------------------------

    int addAimObservation(const QString &fixture,
                          const std::vector<double> &dmxNormalized,
                          double targetX, double targetY, double targetZ,
                          double certainty = 0.95);

    int addPositionObservation(const QString &fixture,
                               int axis, double value,
                               double certainty = 0.95);

    int addRotationObservation(const QString &fixture,
                                int axis, double valueDeg,
                                double certainty = 0.95);

    int addBeamDirectionObservation(const QString &fixture,
                                     const std::vector<double> &dmxNormalized,
                                     double elevationDeg, double azimuthDeg,
                                     bool hasElevation, bool hasAzimuth,
                                     double certainty = 0.85);

    int addCrossingObservation(const QStringList &fixtures,
                                const std::vector<std::vector<double>> &dmxValues,
                                int axis, double value,
                                double certainty = 0.85);

    int addDistanceObservation(const QString &fixtureA,
                                const QString &fixtureB,
                                double distance,
                                double certainty = 0.90);

    // -----------------------------------------------------------------------
    // Per-fixture constraints
    // -----------------------------------------------------------------------

    struct FixtureConstraint {
        int dof = 0;  // 0=tx, 1=ty, 2=tz, 3=rx, 4=ry, 5=rz
        double value = 0.0;
        double certainty = 1.0;
    };

    void setConstraint(const QString &fixture, int dof,
                       double value, double certainty = 1.0);
    void lockFixture(const QString &fixture);
    void clearConstraints(const QString &fixture);

    // -----------------------------------------------------------------------
    // Solver
    // -----------------------------------------------------------------------

    /** Run the solver using current observations + fixture data from Doc.
     *  Results are written to SpatialModel's solverDerived layer.
     *  Returns true if solver converged. */
    bool solve();

    /** @return true if a valid solve result exists */
    bool hasSolveResult() const { return m_hasResult; }

    /** @return the last SolveResult */
    const rigmath::solver::SolveResult &lastResult() const { return m_lastResult; }

    /** Clear just the solver result (keeps observations and constraints). */
    void clearSolverResult();

    // --- Convenience wrappers around rigmath C++ analysis ---

    rigmath::PositionUncertainty fixtureUncertainty(const QString &fixtureId) const;
    rigmath::EllipsoidAxes errorEllipsoid(const QString &fixtureId) const;
    rigmath::PositionUncertainty differentialUncertainty(
        const QString &fixtureA, const QString &fixtureB) const;
    QStringList poorlyConstrained(double thresholdCm = 50.0) const;

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    bool loadXML(QXmlStreamReader &reader);
    void saveXML(QXmlStreamWriter &writer) const;

    /** Clear all state (observations, constraints, results). */
    void clear();

signals:
    void observationsChanged();
    void solveCompleted(bool converged);

private:
    /** Build ChannelSpec + KinematicsType for a QLC+ fixture ID. */
    bool buildFixtureSpec(quint32 fixtureId,
                          std::vector<rigmath::solver::ChannelSpec> &channels,
                          rigmath::solver::KinematicsType &kinType) const;

    Doc *m_doc = nullptr;
    SpatialModel *m_spatialModel = nullptr;

    QMap<int, Observation> m_observations;
    int m_nextObsId = 0;

    // Per-fixture constraints: fixture_id → list of DOF constraints
    QMap<QString, QList<FixtureConstraint>> m_constraints;

    // Last solve result
    bool m_hasResult = false;
    rigmath::solver::SolveResult m_lastResult;
};

#endif // CALIBRATIONMODEL_H
