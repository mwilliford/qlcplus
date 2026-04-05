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
#include <QString>

#include <rigmath/covariance.hpp>
#include <rigmath/rigid_transform.hpp>

/**
 * @brief Engine-layer model holding the current calibration solve state.
 *
 * Receives calibration_state_update messages from the server, deserializes
 * the SolveState, and provides query access via rigmath C++ functions.
 * Emits stateChanged() so the 3D renderer can update error ellipsoids.
 */
class CalibrationModel : public QObject
{
    Q_OBJECT

public:
    explicit CalibrationModel(QObject *parent = nullptr);

    /** @return true if a valid SolveState has been received */
    bool hasSolveState() const { return m_hasState; }

    /** @return the current SolveState (check hasSolveState() first) */
    const rigmath::SolveState &solveState() const { return m_state; }

    /** Update the solve state from a calibration_state_update JSON message.
     *  Parses the solveState object and emits stateChanged(). */
    void updateFromJson(const QJsonObject &solveStateObj);

    /** Clear the solve state (e.g., on disconnect or workspace change) */
    void clear();

    // --- Convenience wrappers around rigmath C++ analysis ---

    /** Per-fixture uncertainty. Returns zero uncertainty if fixture not in state. */
    rigmath::PositionUncertainty fixtureUncertainty(const QString &fixtureId) const;

    /** Error ellipsoid for 3D rendering. Returns zero axes if fixture not in state. */
    rigmath::EllipsoidAxes errorEllipsoid(const QString &fixtureId) const;

    /** Differential uncertainty between two fixtures. */
    rigmath::PositionUncertainty differentialUncertainty(
        const QString &fixtureA, const QString &fixtureB) const;

    /** Parameters exceeding uncertainty threshold. */
    QStringList poorlyConstrained(double thresholdCm = 50.0) const;

    /** Get the solved RigidTransform for a fixture. Returns identity if not solved. */
    rigmath::RigidTransform fixtureTransform(const QString &fixtureId) const;

    /** Get the 4x4 column-major matrix for a fixture (OpenGL/bgfx ready). */
    void fixtureMatrix4x4(const QString &fixtureId, double out[16]) const;

signals:
    /** Emitted after updateFromJson() successfully parses a new state. */
    void stateChanged();

private:
    bool m_hasState;
    rigmath::SolveState m_state;
};

#endif // CALIBRATIONMODEL_H
