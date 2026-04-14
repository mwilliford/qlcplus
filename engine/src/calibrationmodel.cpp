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
#include "gdtfgeometrydata.h"
#include "gdtfkinematics.h"
#include "spatialmodel.h"
#include "doc.h"
#include "fixture.h"
#include "qlcchannel.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

using namespace rigmath::solver;

#define KXMLQLCCalibration          "Calibration"
#define KXMLQLCAttrSigma            "Sigma"
#define KXMLQLCCalibrationTolerance "Tolerance"
#define KXMLQLCAttrTx               "Tx"
#define KXMLQLCAttrTy               "Ty"
#define KXMLQLCAttrTz               "Tz"
#define KXMLQLCAttrRx               "Rx"
#define KXMLQLCAttrRy               "Ry"
#define KXMLQLCAttrRz               "Rz"
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
                                         double sigma)
{
    AimObs obs;
    obs.fixture = fixture;
    obs.dmxNormalized = dmxNormalized;
    obs.target[0] = targetX;
    obs.target[1] = targetY;
    obs.target[2] = targetZ;
    obs.sigma = sigma;
    return addObservation(obs);
}

int CalibrationModel::addPositionObservation(const QString &fixture,
                                              int axis, double value,
                                              double sigma)
{
    PositionObs obs;
    obs.fixture = fixture;
    obs.axis = axis;
    obs.value = value;
    obs.sigma = sigma;
    return addObservation(obs);
}

int CalibrationModel::addRotationObservation(const QString &fixture,
                                              int axis, double valueDeg,
                                              double sigma)
{
    RotationObs obs;
    obs.fixture = fixture;
    obs.axis = axis;
    obs.valueDeg = valueDeg;
    obs.sigma = sigma;
    return addObservation(obs);
}

int CalibrationModel::addBeamDirectionObservation(const QString &fixture,
                                                    const std::vector<double> &dmxNormalized,
                                                    double elevationDeg, double azimuthDeg,
                                                    bool hasElevation, bool hasAzimuth,
                                                    double sigma)
{
    BeamDirectionObs obs;
    obs.fixture = fixture;
    obs.dmxNormalized = dmxNormalized;
    obs.hasElevation = hasElevation;
    obs.hasAzimuth = hasAzimuth;
    obs.elevationDeg = elevationDeg;
    obs.azimuthDeg = azimuthDeg;
    obs.sigma = sigma;
    return addObservation(obs);
}

int CalibrationModel::addCrossingObservation(const QStringList &fixtures,
                                              const std::vector<std::vector<double>> &dmxValues,
                                              int axis, double value,
                                              double sigma)
{
    CrossingObs obs;
    obs.fixtures = fixtures;
    obs.dmxValues = dmxValues;
    obs.axis = axis;
    obs.value = value;
    obs.sigma = sigma;
    return addObservation(obs);
}

int CalibrationModel::addDistanceObservation(const QString &fixtureA,
                                              const QString &fixtureB,
                                              double distance,
                                              double sigma)
{
    DistanceObs obs;
    obs.fixtureA = fixtureA;
    obs.fixtureB = fixtureB;
    obs.distance = distance;
    obs.sigma = sigma;
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
// Tolerances (layout priors for the solver)
// ---------------------------------------------------------------------------

double CalibrationModel::getTolerance(const QString &fixture, int dof) const
{
    if (dof < 0 || dof >= 6)
        return -1.0;

    auto it = m_tolerances.find(fixture);
    if (it == m_tolerances.end())
    {
        // Not explicitly set — return default
        return (dof < 3) ? kDefaultPosTolerance : kDefaultRotTolerance;
    }

    double v = it.value()[dof];
    if (v < 0)
        return (dof < 3) ? kDefaultPosTolerance : kDefaultRotTolerance;
    return v;
}

void CalibrationModel::setTolerance(const QString &fixture, int dof, double value)
{
    if (dof < 0 || dof >= 6)
        return;

    auto it = m_tolerances.find(fixture);
    if (it == m_tolerances.end())
    {
        ToleranceArray arr;
        for (int i = 0; i < 6; i++)
            arr[i] = -1.0;
        arr[dof] = value;
        m_tolerances.insert(fixture, arr);
    }
    else
    {
        it.value()[dof] = value;
    }

    emit tolerancesChanged(fixture);
}

void CalibrationModel::resetTolerances(const QString &fixture)
{
    m_tolerances.remove(fixture);
    emit tolerancesChanged(fixture);
}

// ---------------------------------------------------------------------------
// Build KinematicChain from QLC+ fixture data
// ---------------------------------------------------------------------------

std::unique_ptr<rigmath::KinematicChain>
CalibrationModel::buildKinematicChain(quint32 fixtureId) const
{
    if (!m_doc)
        return nullptr;

    Fixture *fixture = m_doc->fixture(fixtureId);
    if (!fixture)
        return nullptr;

    const QLCFixtureMode *mode = fixture->fixtureMode();
    if (!mode)
        return nullptr;

    // Get GDTF geometry data (real for GDTF fixtures, synthesize for QXF)
    const QLCFixtureDef *def = fixture->fixtureDef();
    const GDTFGeometryData *geoData = def ? def->gdtfGeometryData() : nullptr;

    GDTFGeometryData synthesized;
    GDTFDmxModeInfo modeInfo;

    if (geoData && !geoData->dmxModes.isEmpty())
    {
        // GDTF fixture — find matching mode
        QString modeName = mode->name();
        for (const auto &mi : geoData->dmxModes)
        {
            if (mi.modeName == modeName)
            {
                modeInfo = mi;
                break;
            }
        }
        if (modeInfo.channels.isEmpty() && !geoData->dmxModes.isEmpty())
            modeInfo = geoData->dmxModes.first();
    }
    else
    {
        // QXF fixture — synthesize approximate GDTF data
        const QLCPhysical phy = mode->physical();
        double panRange  = phy.focusPanMax()  > 0 ? phy.focusPanMax()  : 540.0;
        double tiltRange = phy.focusTiltMax() > 0 ? phy.focusTiltMax() : 270.0;
        bool isMirror = phy.focusType().compare(
            QStringLiteral("Mirror"), Qt::CaseInsensitive) == 0;

        bool hasPan = false, hasTilt = false;
        quint32 panMSB = QLCChannel::invalid(), panLSB = QLCChannel::invalid();
        quint32 tiltMSB = QLCChannel::invalid(), tiltLSB = QLCChannel::invalid();
        panMSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::MSB);
        panLSB  = mode->channelNumber(QLCChannel::Pan,  QLCChannel::LSB);
        tiltMSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
        tiltLSB = mode->channelNumber(QLCChannel::Tilt, QLCChannel::LSB);
        hasPan  = (panMSB != QLCChannel::invalid());
        hasTilt = (tiltMSB != QLCChannel::invalid());

        synthesizeGDTFFromQXF(
            hasPan, hasTilt, panRange, tiltRange, isMirror,
            panMSB != QLCChannel::invalid() ? static_cast<int>(panMSB) : -1,
            panLSB != QLCChannel::invalid() ? static_cast<int>(panLSB) : -1,
            tiltMSB != QLCChannel::invalid() ? static_cast<int>(tiltMSB) : -1,
            tiltLSB != QLCChannel::invalid() ? static_cast<int>(tiltLSB) : -1,
            phy.lensDegreesMin(), phy.lensDegreesMax(),
            synthesized, modeInfo);
        geoData = &synthesized;
    }

    GDTFKinematicsResult result = buildGDTFKinematics(geoData->rootForMode(mode->name()), modeInfo);
    if (result.chain)
        return std::make_unique<rigmath::KinematicChain>(*result.chain);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

// Sigma storage conventions for observation types:
// - Position, Distance, Aim, Crossing: meters
// - Rotation, BeamDirection: degrees (converted to radians for the solver)

bool CalibrationModel::solve()
{
    if (!m_doc || !m_spatialModel)
    {
        qWarning() << "[CalibrationModel] Cannot solve: doc or spatialModel not set";
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

    // Also include all fixtures with committed transforms — so layout priors
    // apply even if the user hasn't added explicit observations.
    for (const QString &fid : m_spatialModel->fixtureIds())
    {
        if (m_spatialModel->committedTransform(fid).has_value())
            referencedFixtures.insert(fid);
    }

    if (referencedFixtures.isEmpty())
    {
        qWarning() << "[CalibrationModel] Cannot solve: no fixtures to solve for";
        return false;
    }

    // Build KinematicChains and register fixtures with the solver.
    // Chains must outlive the CalibrationProblem (solver holds raw pointers).
    std::vector<std::unique_ptr<rigmath::KinematicChain>> chains;
    for (const QString &fid : referencedFixtures)
    {
        quint32 qfid = fid.toUInt();
        auto chain = buildKinematicChain(qfid);
        if (!chain)
        {
            qWarning() << "[CalibrationModel] Cannot build chain for fixture" << fid;
            continue;
        }

        rigmath::RigidTransform initialPose = m_spatialModel->renderTransform(fid);
        std::string sid = fid.toStdString();
        prob.addFixture(sid, initialPose, chain.get());
        chains.push_back(std::move(chain));
    }

    // Auto-add layout priors (soft constraints from committed positions + tolerances)
    // These anchor the solver so it doesn't drift from user-placed positions.
    // User tolerance → sigma directly: tolerance 0.5m means σ=0.5m.
    // Rotation tolerances are degrees and must be converted to radians for the solver.
    static constexpr double kDegToRad = M_PI / 180.0;
    for (const QString &fid : referencedFixtures)
    {
        auto committed = m_spatialModel->committedTransform(fid);
        if (!committed.has_value())
            continue;

        std::string sid = fid.toStdString();
        const rigmath::RigidTransform &t = committed.value();

        // Extract pose values: tx, ty, tz (meters), rx, ry, rz (axis-angle radians)
        double ax, ay, az;
        t.get_axis_angle(ax, ay, az);
        double poseVals[6] = {t.pos[0], t.pos[1], t.pos[2], ax, ay, az};

        for (int dof = 0; dof < 6; dof++)
        {
            double tol = getTolerance(fid, dof);
            if (tol <= 0.0)
                continue;  // no prior for this DOF

            // Position DOFs (0-2) use tolerance in meters directly.
            // Rotation DOFs (3-5) store tolerance in degrees → convert to radians.
            double sigma = (dof < 3) ? tol : (tol * kDegToRad);

            DOFConstraint dc;
            dc.value = poseVals[dof];
            dc.sigma = sigma;
            prob.setConstraint(sid, dof, dc);
        }
    }

    // Explicit constraints override auto-priors for the same DOF
    // Legacy FixtureConstraint still uses certainty → pass through to DOFConstraint
    // which in v0.8.0 treats certainty as inv_sigma directly.
    for (auto it = m_constraints.constBegin(); it != m_constraints.constEnd(); ++it)
    {
        std::string sid = it.key().toStdString();
        bool allLocked = true;
        for (const FixtureConstraint &c : it.value())
        {
            DOFConstraint dc;
            dc.value = c.value;
            dc.certainty = c.certainty;  // legacy path
            prob.setConstraint(sid, c.dof, dc);
            if (c.certainty < 1.0)
                allLocked = false;
        }
        // If all 6 DOFs are locked with certainty 1.0, use lockFixture for efficiency
        if (it.value().size() == 6 && allLocked)
            prob.lockFixture(sid);
    }

    // Add observations to the solver using sigma-based API.
    // Observation sigmas are stored in meters (position/distance/aim/crossing)
    // or degrees (rotation/beam direction). Rotation degrees are converted to
    // radians for the solver.
    for (const Observation &obs : m_observations)
    {
        std::visit([&prob](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            if constexpr (std::is_same_v<T, AimObs>)
            {
                prob.addAimObservationSigma(o.fixture.toStdString(),
                                            o.dmxNormalized, o.target, o.sigma);
            }
            else if constexpr (std::is_same_v<T, CrossingObs>)
            {
                std::vector<std::string> fids;
                for (const auto &f : o.fixtures) fids.push_back(f.toStdString());
                prob.addCrossingObservationSigma(fids, o.dmxValues,
                                                  o.axis, o.value, o.sigma);
            }
            else if constexpr (std::is_same_v<T, PositionObs>)
            {
                prob.addPositionObservationSigma(o.fixture.toStdString(),
                                                  o.axis, o.value, o.sigma);
            }
            else if constexpr (std::is_same_v<T, RotationObs>)
            {
                // Rotation sigma is stored in degrees, solver wants radians
                double sigma_rad = o.sigma * (M_PI / 180.0);
                prob.addRotationObservationSigma(o.fixture.toStdString(),
                                                  o.axis, o.valueDeg, sigma_rad);
            }
            else if constexpr (std::is_same_v<T, BeamDirectionObs>)
            {
                // BeamDirection sigma is degrees; solver wants radians.
                double sigma_rad = o.sigma * (M_PI / 180.0);
                prob.addBeamDirectionObservationSigma(
                    o.fixture.toStdString(), o.dmxNormalized,
                    o.elevationDeg, o.azimuthDeg,
                    o.hasElevation, o.hasAzimuth, sigma_rad);
            }
            else if constexpr (std::is_same_v<T, DistanceObs>)
            {
                prob.addDistanceObservationSigma(o.fixtureA.toStdString(),
                                                  o.fixtureB.toStdString(),
                                                  o.distance, o.sigma);
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
    if (m_observations.isEmpty() && m_constraints.isEmpty() && m_tolerances.isEmpty())
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
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
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
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, PositionObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrAxis, QString::number(o.axis));
                writer.writeAttribute(KXMLQLCAttrValue, QString::number(o.value, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, RotationObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrAxis, QString::number(o.axis));
                writer.writeAttribute(KXMLQLCAttrValue, QString::number(o.valueDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, BeamDirectionObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixture, o.fixture);
                writer.writeAttribute(KXMLQLCAttrDMX, dmxToString(o.dmxNormalized));
                writer.writeAttribute(KXMLQLCAttrHasElevation, o.hasElevation ? "1" : "0");
                writer.writeAttribute(KXMLQLCAttrHasAzimuth, o.hasAzimuth ? "1" : "0");
                writer.writeAttribute(KXMLQLCAttrElevation, QString::number(o.elevationDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrAzimuth, QString::number(o.azimuthDeg, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
            }
            else if constexpr (std::is_same_v<T, DistanceObs>)
            {
                writer.writeAttribute(KXMLQLCAttrFixtureA, o.fixtureA);
                writer.writeAttribute(KXMLQLCAttrFixtureB, o.fixtureB);
                writer.writeAttribute(KXMLQLCAttrDistance, QString::number(o.distance, 'g', 10));
                writer.writeAttribute(KXMLQLCAttrSigma, QString::number(o.sigma, 'g', 10));
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

    // Save tolerances
    for (auto it = m_tolerances.constBegin(); it != m_tolerances.constEnd(); ++it)
    {
        writer.writeStartElement(KXMLQLCCalibrationTolerance);
        writer.writeAttribute(KXMLQLCAttrFixture, it.key());
        writer.writeAttribute(KXMLQLCAttrTx, QString::number(it.value()[0], 'g', 10));
        writer.writeAttribute(KXMLQLCAttrTy, QString::number(it.value()[1], 'g', 10));
        writer.writeAttribute(KXMLQLCAttrTz, QString::number(it.value()[2], 'g', 10));
        writer.writeAttribute(KXMLQLCAttrRx, QString::number(it.value()[3], 'g', 10));
        writer.writeAttribute(KXMLQLCAttrRy, QString::number(it.value()[4], 'g', 10));
        writer.writeAttribute(KXMLQLCAttrRz, QString::number(it.value()[5], 'g', 10));
        writer.writeEndElement();
    }

    writer.writeEndElement();
}

// Read sigma from XML with backwards-compat fallback to legacy certainty field.
// Old files stored Certainty = 0.90/0.95; new files store Sigma in meters.
static double readObsSigma(const QXmlStreamAttributes &attrs,
                            double defaultSigma)
{
    if (attrs.hasAttribute(KXMLQLCAttrSigma))
        return attrs.value(KXMLQLCAttrSigma).toDouble();
    if (attrs.hasAttribute(KXMLQLCAttrCertainty))
    {
        // Legacy: certainty ∈ [0,1] is reinterpreted as inv_sigma in v0.8.0,
        // so an old "certainty=0.95" value implies σ ≈ 1/0.95 ≈ 1.05m.
        double c = attrs.value(KXMLQLCAttrCertainty).toDouble();
        if (c > 0)
            return 1.0 / c;
    }
    return defaultSigma;
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
                obs.sigma = readObsSigma(attrs, kDefaultAimSigma);
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
                obs.sigma = readObsSigma(attrs, kDefaultCrossingSigma);
                m_observations[id] = obs;
            }
            else if (type == "Position")
            {
                PositionObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.axis = attrs.value(KXMLQLCAttrAxis).toInt();
                obs.value = attrs.value(KXMLQLCAttrValue).toDouble();
                obs.sigma = readObsSigma(attrs, kDefaultHeightSigma);
                m_observations[id] = obs;
            }
            else if (type == "Rotation")
            {
                RotationObs obs;
                obs.id = id;
                obs.fixture = attrs.value(KXMLQLCAttrFixture).toString();
                obs.axis = attrs.value(KXMLQLCAttrAxis).toInt();
                obs.valueDeg = attrs.value(KXMLQLCAttrValue).toDouble();
                obs.sigma = readObsSigma(attrs, kDefaultRotationSigma);
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
                obs.sigma = readObsSigma(attrs, kDefaultBeamDirSigma);
                m_observations[id] = obs;
            }
            else if (type == "Distance")
            {
                DistanceObs obs;
                obs.id = id;
                obs.fixtureA = attrs.value(KXMLQLCAttrFixtureA).toString();
                obs.fixtureB = attrs.value(KXMLQLCAttrFixtureB).toString();
                obs.distance = attrs.value(KXMLQLCAttrDistance).toDouble();
                obs.sigma = readObsSigma(attrs, kDefaultDistanceSigma);
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
        else if (reader.name() == QLatin1String(KXMLQLCCalibrationTolerance))
        {
            QXmlStreamAttributes attrs = reader.attributes();
            QString fixture = attrs.value(KXMLQLCAttrFixture).toString();
            ToleranceArray arr;
            arr[0] = attrs.value(KXMLQLCAttrTx).toDouble();
            arr[1] = attrs.value(KXMLQLCAttrTy).toDouble();
            arr[2] = attrs.value(KXMLQLCAttrTz).toDouble();
            arr[3] = attrs.value(KXMLQLCAttrRx).toDouble();
            arr[4] = attrs.value(KXMLQLCAttrRy).toDouble();
            arr[5] = attrs.value(KXMLQLCAttrRz).toDouble();
            m_tolerances.insert(fixture, arr);
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
    m_tolerances.clear();
    m_nextObsId = 0;
    m_hasResult = false;
    m_lastResult = SolveResult();
}
