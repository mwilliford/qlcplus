/*
  Q Light Controller Plus
  calibrationmodel.cpp

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
#include <cmath>

#include "calibrationmodel.h"
#include "spatialmodel.h"
#include "doc.h"
#include "fixture.h"
#include "qlcchannel.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

using namespace rigmath::solver;

#define KXMLQLCCalibration          "Calibration"
#define KXMLQLCCalibrationObs       "Observation"
#define KXMLQLCCalibrationConstraint "Constraint"
#define KXMLQLCAttrType             "Type"
#define KXMLQLCAttrID               "ID"
#define KXMLQLCAttrFixture          "Fixture"
#define KXMLQLCAttrFixtureA         "FixtureA"
#define KXMLQLCAttrFixtureB         "FixtureB"
#define KXMLQLCAttrFixtures         "Fixtures"
#define KXMLQLCAttrDMX              "DMX"
#define KXMLQLCAttrAxis             "Axis"
#define KXMLQLCAttrValue            "Value"
#define KXMLQLCAttrCertainty        "Certainty"
#define KXMLQLCAttrTargetX          "TX"
#define KXMLQLCAttrTargetY          "TY"
#define KXMLQLCAttrTargetZ          "TZ"
#define KXMLQLCAttrElevation        "Elevation"
#define KXMLQLCAttrAzimuth          "Azimuth"
#define KXMLQLCAttrHasElevation     "HasEl"
#define KXMLQLCAttrHasAzimuth       "HasAz"
#define KXMLQLCAttrDistance          "Distance"
#define KXMLQLCAttrDOF              "DOF"

CalibrationModel::CalibrationModel(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Observation management
// ---------------------------------------------------------------------------

int CalibrationModel::addObservation(const Observation &obs)
{
    int id = m_nextObsId++;

    // Stamp the ID into the observation
    Observation stamped = obs;
    std::visit([id](auto &o) { o.id = id; }, stamped);

    m_observations[id] = stamped;
    emit observationsChanged();
    return id;
}

void CalibrationModel::removeObservation(int id)
{
    if (m_observations.remove(id))
    {
        // Solver result is now stale
        m_hasResult = false;
        m_lastResult = rigmath::solver::SolveResult();
        emit observationsChanged();
    }
}

void CalibrationModel::clearObservations()
{
    m_observations.clear();
    m_nextObsId = 0;
    m_hasResult = false;
    m_lastResult = rigmath::solver::SolveResult();
    emit observationsChanged();
}

// ---------------------------------------------------------------------------
// Convenience builders
// ---------------------------------------------------------------------------

int CalibrationModel::addAimObservation(const QString &fixture,
                                         const std::vector<double> &dmxNormalized,
                                         double targetX, double targetY, double targetZ,
                                         double certainty)
{
    AimObs obs;
    obs.fixture = fixture;
    obs.dmxNormalized = dmxNormalized;
    obs.target[0] = targetX;
    obs.target[1] = targetY;
    obs.target[2] = targetZ;
    obs.certainty = certainty;
    return addObservation(obs);
}

int CalibrationModel::addPositionObservation(const QString &fixture,
                                              int axis, double value,
                                              double certainty)
{
    PositionObs obs;
    obs.fixture = fixture;
    obs.axis = axis;
    obs.value = value;
    obs.certainty = certainty;
    return addObservation(obs);
}

int CalibrationModel::addRotationObservation(const QString &fixture,
                                              int axis, double valueDeg,
                                              double certainty)
{
    RotationObs obs;
    obs.fixture = fixture;
    obs.axis = axis;
    obs.valueDeg = valueDeg;
    obs.certainty = certainty;
    return addObservation(obs);
}

int CalibrationModel::addBeamDirectionObservation(const QString &fixture,
                                                    const std::vector<double> &dmxNormalized,
                                                    double elevationDeg, double azimuthDeg,
                                                    bool hasElevation, bool hasAzimuth,
                                                    double certainty)
{
    BeamDirectionObs obs;
    obs.fixture = fixture;
    obs.dmxNormalized = dmxNormalized;
    obs.hasElevation = hasElevation;
    obs.hasAzimuth = hasAzimuth;
    obs.elevationDeg = elevationDeg;
    obs.azimuthDeg = azimuthDeg;
    obs.certainty = certainty;
    return addObservation(obs);
}

int CalibrationModel::addCrossingObservation(const QStringList &fixtures,
                                              const std::vector<std::vector<double>> &dmxValues,
                                              int axis, double value,
                                              double certainty)
{
    CrossingObs obs;
    obs.fixtures = fixtures;
    obs.dmxValues = dmxValues;
    obs.axis = axis;
    obs.value = value;
    obs.certainty = certainty;
    return addObservation(obs);
}

int CalibrationModel::addDistanceObservation(const QString &fixtureA,
                                              const QString &fixtureB,
                                              double distance,
                                              double certainty)
{
    DistanceObs obs;
    obs.fixtureA = fixtureA;
    obs.fixtureB = fixtureB;
    obs.distance = distance;
    obs.certainty = certainty;
    return addObservation(obs);
}

// ---------------------------------------------------------------------------
// Constraints
// ---------------------------------------------------------------------------

void CalibrationModel::setConstraint(const QString &fixture, int dof,
                                      double value, double certainty)
{
    FixtureConstraint c;
    c.dof = dof;
    c.value = value;
    c.certainty = certainty;

    // Replace existing constraint for this DOF, or append
    QList<FixtureConstraint> &list = m_constraints[fixture];
    for (int i = 0; i < list.size(); i++)
    {
        if (list[i].dof == dof)
        {
            list[i] = c;
            return;
        }
    }
    list.append(c);
}

void CalibrationModel::lockFixture(const QString &fixture)
{
    // Lock all 6 DOFs
    for (int dof = 0; dof < 6; dof++)
        setConstraint(fixture, dof, 0.0, 1.0);
}

void CalibrationModel::clearConstraints(const QString &fixture)
{
    m_constraints.remove(fixture);
}

// ---------------------------------------------------------------------------
// Build fixture spec from QLC+ fixture data
// ---------------------------------------------------------------------------

bool CalibrationModel::buildFixtureSpec(quint32 fixtureId,
                                         std::vector<ChannelSpec> &channels,
                                         KinematicsType &kinType) const
{
    if (!m_doc)
        return false;

    Fixture *fixture = m_doc->fixture(fixtureId);
    if (!fixture)
        return false;

    const QLCFixtureMode *mode = fixture->fixtureMode();
    if (!mode)
        return false;

    const QLCPhysical phy = mode->physical();
    double panRange = phy.focusPanMax();
    double tiltRange = phy.focusTiltMax();

    // Detect kinematics type from channel groups
    bool hasPan = false, hasTilt = false;
    for (int i = 0; i < (int)mode->channels().size(); i++)
    {
        const QLCChannel *ch = mode->channel(i);
        if (!ch)
            continue;
        if (ch->group() == QLCChannel::Pan && ch->controlByte() == QLCChannel::MSB)
            hasPan = true;
        if (ch->group() == QLCChannel::Tilt && ch->controlByte() == QLCChannel::MSB)
            hasTilt = true;
    }

    channels.clear();

    if (hasPan && hasTilt)
    {
        kinType = KinematicsType::MovingHead;
        channels.push_back({"pan", panRange > 0 ? panRange : 540.0});
        channels.push_back({"tilt", tiltRange > 0 ? tiltRange : 270.0});
    }
    else if (hasPan)
    {
        kinType = KinematicsType::PanOnly;
        channels.push_back({"pan", panRange > 0 ? panRange : 540.0});
    }
    else
    {
        kinType = KinematicsType::Fixed;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

bool CalibrationModel::solve()
{
    if (!m_doc || !m_spatialModel)
    {
        qWarning() << "[CalibrationModel] Cannot solve: doc or spatialModel not set";
        return false;
    }

    if (m_observations.isEmpty())
    {
        qWarning() << "[CalibrationModel] Cannot solve: no observations";
        return false;
    }

    CalibrationProblem prob;

    // Collect all fixture IDs referenced in observations
    QSet<QString> referencedFixtures;
    for (const Observation &obs : m_observations)
    {
        std::visit([&referencedFixtures](const auto &o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, AimObs>)
                referencedFixtures.insert(o.fixture);
            else if constexpr (std::is_same_v<T, CrossingObs>)
                for (const auto &f : o.fixtures) referencedFixtures.insert(f);
            else if constexpr (std::is_same_v<T, PositionObs>)
                referencedFixtures.insert(o.fixture);
            else if constexpr (std::is_same_v<T, RotationObs>)
                referencedFixtures.insert(o.fixture);
            else if constexpr (std::is_same_v<T, BeamDirectionObs>)
                referencedFixtures.insert(o.fixture);
            else if constexpr (std::is_same_v<T, DistanceObs>)
            {
                referencedFixtures.insert(o.fixtureA);
                referencedFixtures.insert(o.fixtureB);
            }
        }, obs);
    }

    // Register fixtures with the solver
    for (const QString &fid : referencedFixtures)
    {
        quint32 qfid = fid.toUInt();
        std::vector<ChannelSpec> channels;
        KinematicsType kinType;

        if (!buildFixtureSpec(qfid, channels, kinType))
        {
            qWarning() << "[CalibrationModel] Cannot build spec for fixture" << fid;
            continue;
        }

        // Initial pose from SpatialModel's committed transform (or identity)
        rigmath::RigidTransform initialPose = m_spatialModel->renderTransform(fid);
        std::string sid = fid.toStdString();
        prob.addFixture(sid, initialPose, channels, kinType);
    }

    // Add constraints
    for (auto it = m_constraints.constBegin(); it != m_constraints.constEnd(); ++it)
    {
        std::string sid = it.key().toStdString();
        bool allLocked = true;
        for (const FixtureConstraint &c : it.value())
        {
            DOFConstraint dc;
            dc.value = c.value;
            dc.certainty = c.certainty;
            prob.setConstraint(sid, c.dof, dc);
            if (c.certainty < 1.0)
                allLocked = false;
        }
        // If all 6 DOFs are locked with certainty 1.0, use lockFixture for efficiency
        if (it.value().size() == 6 && allLocked)
            prob.lockFixture(sid);
    }

    // Add observations to the solver
    for (const Observation &obs : m_observations)
    {
        std::visit([&prob](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, AimObs>)
            {
                prob.addAimObservation(o.fixture.toStdString(),
                                       o.dmxNormalized, o.target, o.certainty);
            }
            else if constexpr (std::is_same_v<T, CrossingObs>)
            {
                std::vector<std::string> fids;
                for (const auto &f : o.fixtures) fids.push_back(f.toStdString());
                prob.addCrossingObservation(fids, o.dmxValues,
                                            o.axis, o.value, o.certainty);
            }
            else if constexpr (std::is_same_v<T, PositionObs>)
            {
                prob.addPositionObservation(o.fixture.toStdString(),
                                            o.axis, o.value, o.certainty);
            }
            else if constexpr (std::is_same_v<T, RotationObs>)
            {
                prob.addRotationObservation(o.fixture.toStdString(),
                                            o.axis, o.valueDeg, o.certainty);
            }
            else if constexpr (std::is_same_v<T, BeamDirectionObs>)
            {
                prob.addBeamDirectionObservation(
                    o.fixture.toStdString(), o.dmxNormalized,
                    o.elevationDeg, o.azimuthDeg,
                    o.hasElevation, o.hasAzimuth, o.certainty);
            }
            else if constexpr (std::is_same_v<T, DistanceObs>)
            {
                prob.addDistanceObservation(o.fixtureA.toStdString(),
                                            o.fixtureB.toStdString(),
                                            o.distance, o.certainty);
            }
        }, obs);
    }

    // Solve
    m_lastResult = prob.solve();
    m_hasResult = true;

    qDebug() << "[CalibrationModel] Solve complete:"
             << (m_lastResult.converged ? "converged" : "did not converge")
             << "rms=" << m_lastResult.rms_residual
             << "fixtures=" << m_lastResult.poses.size();

    // Apply results to SpatialModel
    if (m_spatialModel)
    {
        for (const auto &[sid, pose] : m_lastResult.poses)
        {
            if (pose.size() != 6)
                continue;

            QString fid = QString::fromStdString(sid);
            rigmath::RigidTransform t = rigmath::RigidTransform::from_pose(
                pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
            m_spatialModel->setFixtureTransform(fid, t, SpatialModel::SolverDerived);
        }

        // Update viz from covariance state
        const rigmath::SolveState &covState = m_lastResult.covariance_state;
        for (const auto &[sid, pose] : m_lastResult.poses)
        {
            QString fid = QString::fromStdString(sid);
            if (covState.layout.fixture_offset.find(sid) == covState.layout.fixture_offset.end())
                continue;

            rigmath::EllipsoidAxes ellipsoid = rigmath::error_ellipsoid(covState, sid);
            rigmath::PositionUncertainty unc = rigmath::fixture_uncertainty(covState, sid);

            SpatialModel::FixtureViz viz;
            viz.ellipsoidAxes[0] = ellipsoid.a;
            viz.ellipsoidAxes[1] = ellipsoid.b;
            viz.ellipsoidAxes[2] = ellipsoid.c;
            for (int i = 0; i < 9; i++)
                viz.ellipsoidRot[i] = ellipsoid.rot[i];
            viz.quality = QString::fromStdString(unc.quality());

            m_spatialModel->setFixtureViz(fid, viz);
        }

        m_spatialModel->setSolverState(m_lastResult.rms_residual, m_lastResult.converged);
    }

    emit solveCompleted(m_lastResult.converged);
    return m_lastResult.converged;
}

// ---------------------------------------------------------------------------
// Convenience wrappers around rigmath C++ analysis
// ---------------------------------------------------------------------------

rigmath::PositionUncertainty CalibrationModel::fixtureUncertainty(const QString &fixtureId) const
{
    if (!m_hasResult)
        return {};
    std::string id = fixtureId.toStdString();
    const auto &layout = m_lastResult.covariance_state.layout;
    if (layout.fixture_offset.find(id) == layout.fixture_offset.end())
        return {};
    return rigmath::fixture_uncertainty(m_lastResult.covariance_state, id);
}

rigmath::EllipsoidAxes CalibrationModel::errorEllipsoid(const QString &fixtureId) const
{
    if (!m_hasResult)
        return {};
    std::string id = fixtureId.toStdString();
    const auto &layout = m_lastResult.covariance_state.layout;
    if (layout.fixture_offset.find(id) == layout.fixture_offset.end())
        return {};
    return rigmath::error_ellipsoid(m_lastResult.covariance_state, id);
}

rigmath::PositionUncertainty CalibrationModel::differentialUncertainty(
    const QString &fixtureA, const QString &fixtureB) const
{
    if (!m_hasResult)
        return {};
    std::string a = fixtureA.toStdString();
    std::string b = fixtureB.toStdString();
    const auto &layout = m_lastResult.covariance_state.layout;
    if (layout.fixture_offset.find(a) == layout.fixture_offset.end())
        return {};
    if (layout.fixture_offset.find(b) == layout.fixture_offset.end())
        return {};
    return rigmath::differential_uncertainty(m_lastResult.covariance_state, a, b);
}

QStringList CalibrationModel::poorlyConstrained(double thresholdCm) const
{
    if (!m_hasResult)
        return {};
    auto params = rigmath::poorly_constrained(m_lastResult.covariance_state, thresholdCm);
    QStringList result;
    for (const auto &p : params)
        result.append(QString::fromStdString(p));
    return result;
}

// ---------------------------------------------------------------------------
// XML Persistence
// ---------------------------------------------------------------------------

static QString obsTypeToString(const CalibrationModel::Observation &obs)
{
    return std::visit([](const auto &o) -> QString {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, CalibrationModel::AimObs>) return "Aim";
        else if constexpr (std::is_same_v<T, CalibrationModel::CrossingObs>) return "Crossing";
        else if constexpr (std::is_same_v<T, CalibrationModel::PositionObs>) return "Position";
        else if constexpr (std::is_same_v<T, CalibrationModel::RotationObs>) return "Rotation";
        else if constexpr (std::is_same_v<T, CalibrationModel::BeamDirectionObs>) return "BeamDirection";
        else if constexpr (std::is_same_v<T, CalibrationModel::DistanceObs>) return "Distance";
        else return "Unknown";
    }, obs);
}

static QString dmxToString(const std::vector<double> &dmx)
{
    QStringList parts;
    for (double v : dmx)
        parts.append(QString::number(v, 'g', 10));
    return parts.join(",");
}

static std::vector<double> stringToDmx(const QString &s)
{
    std::vector<double> result;
    if (s.isEmpty())
        return result;
    for (const QString &part : s.split(","))
        result.push_back(part.toDouble());
    return result;
}

void CalibrationModel::saveXML(QXmlStreamWriter &writer) const
{
    if (m_observations.isEmpty() && m_constraints.isEmpty())
        return;

    writer.writeStartElement(KXMLQLCCalibration);

    for (auto it = m_observations.constBegin(); it != m_observations.constEnd(); ++it)
    {
        const Observation &obs = it.value();
        writer.writeStartElement(KXMLQLCCalibrationObs);
        writer.writeAttribute(KXMLQLCAttrID, QString::number(it.key()));
        writer.writeAttribute(KXMLQLCAttrType, obsTypeToString(obs));

        std::visit([&writer](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, AimObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrDMX, dmxToString(o.dmxNormalized));
                writer.writeAttribute(KXMLQLCAttrTargetX, QString::number(o.target[0], 'g', 10));
                writer.writeAttribute(KXMLQLCAttrTargetY, QString::number(o.target[1], 'g', 10));
                writer.writeAttribute(KXMLQLCAttrTargetZ, QString::number(o.target[2], 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, CrossingObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixtures, o.fixtures.join(","));
                // DMX values: semicolon-separated per fixture
                QStringList dmxParts;
                for (const auto &dmx : o.dmxValues)
                    dmxParts.append(dmxToString(dmx));
                writer.writeAttribute(KXMLQLCAttrDMX, dmxParts.join(";"));
                writer.writeAttribute(KXMLQLCAttrAxis, QString::number(o.axis));
                writer.writeAttribute(KXMLQLCAttrValue, QString::number(o.value, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, PositionObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrAxis, QString::number(o.axis));
                writer.writeAttribute(KXMLQLCAttrValue, QString::number(o.value, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, RotationObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrAxis, QString::number(o.axis));
                writer.writeAttribute(KXMLQLCAttrValue, QString::number(o.valueDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, BeamDirectionObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrDMX, dmxToString(o.dmxNormalized));
                writer.writeAttribute(KXMLQLCAttrHasElevation, o.hasElevation ? "1" : "0");
                writer.writeAttribute(KXMLQLCAttrHasAzimuth, o.hasAzimuth ? "1" : "0");
                writer.writeAttribute(KXMLQLCAttrElevation, QString::number(o.elevationDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrAzimuth, QString::number(o.azimuthDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, DistanceObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixtureA, o.fixtureA);
                writer.writeAttribute(KXMLQLCAttrFixtureB, o.fixtureB);
                writer.writeAttribute(KXMLQLCAttrDistance, QString::number(o.distance, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(o.certainty, 'g', 10));
            }
        }, obs);

        writer.writeEndElement();
    }

    // Save constraints
    for (auto it = m_constraints.constBegin(); it != m_constraints.constEnd(); ++it)
    {
        for (const FixtureConstraint &c : it.value())
        {
            writer.writeStartElement(KXMLQLCCalibrationConstraint);
            writer.writeAttribute(KXMLQLCAttrFixture, it.key());
            writer.writeAttribute(KXMLQLCAttrDOF, QString::number(c.dof));
            writer.writeAttribute(KXMLQLCAttrValue, QString::number(c.value, 'g', 10));
            writer.writeAttribute(KXMLQLCAttrCertainty, QString::number(c.certainty, 'g', 10));
            writer.writeEndElement();
        }
    }

    writer.writeEndElement();
}

bool CalibrationModel::loadXML(QXmlStreamReader &reader)
{
    if (reader.name() != QLatin1String(KXMLQLCCalibration))
        return false;

    while (reader.readNextStartElement())
    {
        if (reader.name() == QLatin1String(KXMLQLCCalibrationObs))
        {
            QXmlStreamAttributes attrs = reader.attributes();
            int id = attrs.value(KXMLQLCAttrID).toInt();
            QString type = attrs.value(KXMLQLCAttrType).toString();

            if (type == "Aim")
            {
                AimObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.dmxNormalized = stringToDmx(attrs.value(KXMLQLCAttrDMX).toString());
                obs.target[0] = attrs.value(KXMLQLCAttrTargetX).toDouble();
                obs.target[1] = attrs.value(KXMLQLCAttrTargetY).toDouble();
                obs.target[2] = attrs.value(KXMLQLCAttrTargetZ).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }
            else if (type == "Crossing")
            {
                CrossingObs obs;
                obs.id = id;
                obs.fixtures = attrs.value(KXMLQLCAttrFixtures).toString().split(",");
                QString dmxStr = attrs.value(KXMLQLCAttrDMX).toString();
                for (const QString &part : dmxStr.split(";"))
                    obs.dmxValues.push_back(stringToDmx(part));
                obs.axis = attrs.value(KXMLQLCAttrAxis).toInt();
                obs.value = attrs.value(KXMLQLCAttrValue).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }
            else if (type == "Position")
            {
                PositionObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.axis = attrs.value(KXMLQLCAttrAxis).toInt();
                obs.value = attrs.value(KXMLQLCAttrValue).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }
            else if (type == "Rotation")
            {
                RotationObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.axis = attrs.value(KXMLQLCAttrAxis).toInt();
                obs.valueDeg = attrs.value(KXMLQLCAttrValue).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }
            else if (type == "BeamDirection")
            {
                BeamDirectionObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.dmxNormalized = stringToDmx(attrs.value(KXMLQLCAttrDMX).toString());
                obs.hasElevation = attrs.value(KXMLQLCAttrHasElevation).toString() == "1";
                obs.hasAzimuth = attrs.value(KXMLQLCAttrHasAzimuth).toString() == "1";
                obs.elevationDeg = attrs.value(KXMLQLCAttrElevation).toDouble();
                obs.azimuthDeg = attrs.value(KXMLQLCAttrAzimuth).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }
            else if (type == "Distance")
            {
                DistanceObs obs;
                obs.id = id;
                obs.fixtureA = attrs.value(KXMLQLCAttrFixtureA).toString();
                obs.fixtureB = attrs.value(KXMLQLCAttrFixtureB).toString();
                obs.distance = attrs.value(KXMLQLCAttrDistance).toDouble();
                obs.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
                m_observations[id] = obs;
            }

            if (id >= m_nextObsId)
                m_nextObsId = id + 1;

            reader.skipCurrentElement();
        }
        else if (reader.name() == QLatin1String(KXMLQLCCalibrationConstraint))
        {
            QXmlStreamAttributes attrs = reader.attributes();
            QString fixture = attrs.value(KXMLQLCAttrFixture).toString();
            FixtureConstraint c;
            c.dof = attrs.value(KXMLQLCAttrDOF).toInt();
            c.value = attrs.value(KXMLQLCAttrValue).toDouble();
            c.certainty = attrs.value(KXMLQLCAttrCertainty).toDouble();
            m_constraints[fixture].append(c);
            reader.skipCurrentElement();
        }
        else
        {
            reader.skipCurrentElement();
        }
    }

    if (!m_observations.isEmpty())
        emit observationsChanged();

    return true;
}

void CalibrationModel::clearSolverResult()
{
    m_hasResult = false;
    m_lastResult = SolveResult();
}

void CalibrationModel::clear()
{
    m_observations.clear();
    m_constraints.clear();
    m_nextObsId = 0;
    m_hasResult = false;
    m_lastResult = SolveResult();
}
