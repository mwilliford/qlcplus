/*
  Q Light Controller Plus
  calibratecontroller.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "calibratecontroller.h"
#include "calibrationmodel.h"
#include "spatialmodel.h"
#include "doc.h"
#include "fixture.h"
#include "fixturekinematics.h"
#include "inputoutputmap.h"
#include "universe.h"
#include "fixture.h"

#include <QVariantMap>

CalibrateController::CalibrateController(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    connect(cm, &CalibrationModel::observationsChanged, this, &CalibrateController::changed);
    connect(cm, &CalibrationModel::solveCompleted, this, &CalibrateController::changed);
    connect(cm, &CalibrationModel::tolerancesChanged, this, &CalibrateController::changed);
}

// ---------------------------------------------------------------------------
// Read-only properties
// ---------------------------------------------------------------------------

int CalibrateController::observationCount() const
{
    return m_doc->calibrationModel()->observationCount();
}

bool CalibrateController::hasSolverResult() const
{
    return m_doc->calibrationModel()->hasSolveResult();
}

bool CalibrateController::solverConverged() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return false;
    return m_doc->calibrationModel()->lastResult().converged;
}

double CalibrateController::solverRms() const
{
    if (!m_doc->calibrationModel()->hasSolveResult())
        return 0.0;
    return m_doc->calibrationModel()->lastResult().rms_residual;
}

// Forward decl — definition is below addAimObs/addCrossingObs.
static std::vector<double> captureFixtureNormalizedDmx(Doc *doc, int fixtureId);

// ---------------------------------------------------------------------------
// Observation CRUD
// ---------------------------------------------------------------------------

int CalibrateController::addPositionObs(int fixtureId, int axis, double value, double sigma)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addPositionObservation(QString::number(fixtureId), axis, value, sigma);
    emit changed();
    return id;
}

int CalibrateController::addRotationObs(int fixtureId, int axis, double valueDeg, double sigma)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addRotationObservation(QString::number(fixtureId), axis, valueDeg, sigma);
    emit changed();
    return id;
}

int CalibrateController::addDistanceObs(int fixtureIdA, int fixtureIdB, double distance, double sigma)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addDistanceObservation(QString::number(fixtureIdA),
                                         QString::number(fixtureIdB),
                                         distance, sigma);
    emit changed();
    return id;
}

void CalibrateController::removeObs(int obsId)
{
    m_doc->calibrationModel()->removeObservation(obsId);
    emit changed();
}

void CalibrateController::clearAllObs()
{
    m_doc->calibrationModel()->clearObservations();
    m_doc->spatialModel()->clearSolverViz();
    emit changed();
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

bool CalibrateController::runSolve()
{
    CalibrationModel *cm = m_doc->calibrationModel();
    bool result = cm->solve();
    emit changed();
    return result;
}

void CalibrateController::acceptSolverResults()
{
    SpatialModel *sm = m_doc->spatialModel();
    for (const QString &id : sm->fixtureIds())
    {
        if (sm->solverDerivedTransform(id).has_value())
            sm->promoteTransform(id, SpatialModel::SolverDerived);
    }
    m_doc->calibrationModel()->clearSolverResult();
    emit changed();
}

void CalibrateController::dismissSolverResults()
{
    m_doc->spatialModel()->clearSolverViz();
    m_doc->calibrationModel()->clearSolverResult();
    emit changed();
}

void CalibrateController::acceptFixtureResult(int fixtureId)
{
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(fixtureId);
    if (sm->solverDerivedTransform(id).has_value())
    {
        sm->promoteTransform(id, SpatialModel::SolverDerived);
        emit changed();
    }
}

void CalibrateController::dismissFixtureResult(int fixtureId)
{
    SpatialModel *sm = m_doc->spatialModel();
    QString id = QString::number(fixtureId);
    sm->clearTransformLayer(id, SpatialModel::SolverDerived);
    SpatialModel::FixtureViz emptyViz;
    sm->setFixtureViz(id, emptyViz);
    emit changed();
}

int CalibrateController::addAimObs(int fixtureId, double targetX, double targetY,
                                    double targetZ, double sigma)
{
    // AimFactor's operator() reads dmxNormalized via forward_chain, which
    // dereferences dof_values[joint.dof_index] for each joint in the chain.
    // Passing an empty vector is UB — it crashes or returns garbage depending
    // on memory state. Always capture the live DMX at observation time.
    std::vector<double> dmx = captureFixtureNormalizedDmx(m_doc, fixtureId);
    if (dmx.empty())
    {
        qWarning() << "[Calibrate] addAimObs: fixture" << fixtureId
                   << "has no DOFs to capture — observation skipped";
        return -1;
    }
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addAimObservation(QString::number(fixtureId), dmx,
                                    targetX, targetY, targetZ, sigma);
    emit changed();
    return id;
}

// Capture normalized DMX (0..1 per DOF) for a fixture at the moment of the
// observation. The crossing solver needs these — unlike AimObs, it doesn't
// read from SpatialModel but directly uses these DMX values to compute beam
// directions through the fixture's kinematic chain.
static std::vector<double> captureFixtureNormalizedDmx(Doc *doc, int fixtureId)
{
    std::vector<double> out;
    if (!doc) return out;
    Fixture *fxi = doc->fixture(quint32(fixtureId));
    if (!fxi) return out;

    FixtureKinematics fk = buildFixtureKinematics(fxi);
    if (!fk.chain || fk.dmxAddresses.empty()) return out;

    InputOutputMap *ioMap = doc->inputOutputMap();
    if (!ioMap) return out;

    // Read live universe bytes and normalize each DOF's MSB to 0..1.
    const quint32 uni = fxi->universe();
    if (int(uni) >= ioMap->universesCount()) return out;

    // dmxAddresses holds absolute addresses (universe * 512 + rel); collect
    // one normalized value per DOF by reading the MSB channel per DOF.
    // fk.chain->dof_count() tells us how many DOFs; dmxAddresses is in
    // MSB-first order (2 per DOF when a fine channel exists, 1 otherwise).
    const int dofCount = fk.dofCount();
    out.reserve(dofCount);

    // Get the post-GM universe snapshot (what we're actually outputting).
    QList<Universe *> universes = ioMap->claimUniverses();
    QByteArray snapshot;
    if (int(uni) < universes.size() && universes[uni])
    {
        const QByteArray *pg = universes[uni]->postGMValues();
        if (pg) snapshot = *pg;
    }
    ioMap->releaseUniverses(false);

    if (snapshot.isEmpty())
    {
        // Fall back to zeros if no snapshot.
        out.assign(size_t(dofCount), 0.5);
        return out;
    }

    // fk.dmxAddresses typically holds MSB-then-LSB pairs per DOF. Walk in
    // strides that match: if fine channels exist, they follow MSBs; otherwise
    // just 1 entry per DOF. ChannelMap handles the detail for us via
    // dmxSnapshotToDofs, but we need raw 0..1 not angles. Simplest: pull
    // the MSB channel index for each DOF from channelMap.
    // For safety: iterate the addresses in groups determined by dofCount.
    // This assumes MSB ordering — correct for current rigmath ChannelMap.
    int stride = (fk.dmxAddresses.size() >= size_t(dofCount * 2)) ? 2 : 1;
    for (int d = 0; d < dofCount; ++d)
    {
        const size_t idx = size_t(d) * stride;
        if (idx >= fk.dmxAddresses.size()) { out.push_back(0.5); continue; }
        const quint32 absAddr = fk.dmxAddresses[idx];
        const int rel = int(absAddr & 0x1FF);
        if (rel >= 0 && rel < snapshot.size())
            out.push_back(double(uchar(snapshot.at(rel))) / 255.0);
        else
            out.push_back(0.5);
    }
    return out;
}

int CalibrateController::addCrossingObs(const QVariantList &fixtureIds,
                                         int axis, double value, double sigma)
{
    if (fixtureIds.size() < 2) return -1;
    QStringList fixtures;
    std::vector<std::vector<double>> dmxPerFixture;
    for (const QVariant &v : fixtureIds)
    {
        const int fid = v.toInt();
        fixtures << QString::number(fid);
        std::vector<double> n = captureFixtureNormalizedDmx(m_doc, fid);
        if (n.empty())
        {
            qWarning() << "[Calibrate] addCrossingObs: fixture" << fid
                       << "has no DOFs to capture — skipping observation";
            return -1;
        }
        dmxPerFixture.push_back(std::move(n));
    }
    CalibrationModel *cm = m_doc->calibrationModel();
    int id = cm->addCrossingObservation(fixtures, dmxPerFixture,
                                         axis, value, sigma);
    qDebug() << "[Calibrate] crossing obs added:" << fixtures.size() << "fixtures, axis" << axis << "value" << value;
    emit changed();
    return id;
}

// ---------------------------------------------------------------------------
// Constraints
// ---------------------------------------------------------------------------

void CalibrateController::lockFixtureInSolver(int fixtureId)
{
    m_doc->calibrationModel()->lockFixture(QString::number(fixtureId));
}

void CalibrateController::setHeightConstraint(int fixtureId, double heightM, double certainty)
{
    m_doc->calibrationModel()->setConstraint(
        QString::number(fixtureId), 2 /* tz */, heightM, certainty);
}

// ---------------------------------------------------------------------------
// Tolerances (layout priors)
// ---------------------------------------------------------------------------

double CalibrateController::getTolerance(int fixtureId, int dof) const
{
    return m_doc->calibrationModel()->getTolerance(QString::number(fixtureId), dof);
}

void CalibrateController::setTolerance(int fixtureId, int dof, double value)
{
    m_doc->calibrationModel()->setTolerance(QString::number(fixtureId), dof, value);
    emit changed();
}

void CalibrateController::resetTolerances(int fixtureId)
{
    m_doc->calibrationModel()->resetTolerances(QString::number(fixtureId));
    emit changed();
}

void CalibrateController::resetTolerancesForSelection(const QVariantList &fixtureIds)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    for (const QVariant &v : fixtureIds)
        cm->resetTolerances(QString::number(v.toInt()));
    emit changed();
}

void CalibrateController::setToleranceForFixtures(const QVariantList &fixtureIds,
                                                   int dof, double value)
{
    CalibrationModel *cm = m_doc->calibrationModel();
    for (const QVariant &v : fixtureIds)
        cm->setTolerance(QString::number(v.toInt()), dof, value);
    emit changed();
}

// ---------------------------------------------------------------------------
// Data for QML lists
// ---------------------------------------------------------------------------

QString CalibrateController::fixtureName(const QString &fixtureId) const
{
    Fixture *fxi = m_doc->fixture(fixtureId.toUInt());
    return fxi ? fxi->name() : QString("Fixture %1").arg(fixtureId);
}

QVariantList CalibrateController::observationsList() const
{
    static const char *axisLabels[] = {"X", "Y", "Height"};
    static const char *rotLabels[] = {"Pitch", "Yaw", "Roll"};

    QVariantList list;
    CalibrationModel *cm = m_doc->calibrationModel();

    for (const CalibrationModel::Observation &obs : cm->observations())
    {
        QVariantMap map;
        std::visit([&](const auto &o) {
            using T = std::decay_t<decltype(o)>;

            map["id"] = o.id;
            map["sigma"] = o.sigma;

            if constexpr (std::is_same_v<T, CalibrationModel::AimObs>)
            {
                map["type"] = "Aim";
                map["fixture"] = o.fixture;
                map["description"] = QString("[%1] aim → (%2, %3, %4) ±%5cm")
                    .arg(fixtureName(o.fixture))
                    .arg(o.target[0], 0, 'f', 2)
                    .arg(o.target[1], 0, 'f', 2)
                    .arg(o.target[2], 0, 'f', 2)
                    .arg(o.sigma * 100.0, 0, 'f', 0);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::CrossingObs>)
            {
                map["type"] = "Crossing";
                QStringList names;
                for (const auto &f : o.fixtures)
                    names.append(fixtureName(f));
                map["description"] = QString("%1 crossing @ %2=%3 ±%4cm")
                    .arg(names.join(" x "))
                    .arg(o.axis < 3 ? axisLabels[o.axis] : "?")
                    .arg(o.value, 0, 'f', 2)
                    .arg(o.sigma * 100.0, 0, 'f', 0);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::PositionObs>)
            {
                map["type"] = "Position";
                map["fixture"] = o.fixture;
                map["axis"] = o.axis;
                map["value"] = o.value;
                map["description"] = QString("[%1] %2 = %3m ±%4cm")
                    .arg(fixtureName(o.fixture))
                    .arg(o.axis >= 0 && o.axis < 3 ? axisLabels[o.axis] : "?")
                    .arg(o.value, 0, 'f', 2)
                    .arg(o.sigma * 100.0, 0, 'f', 0);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::RotationObs>)
            {
                map["type"] = "Rotation";
                map["fixture"] = o.fixture;
                map["axis"] = o.axis;
                map["value"] = o.valueDeg;
                int ri = o.axis >= 3 ? o.axis - 3 : o.axis;
                map["description"] = QString("[%1] %2 = %3° ±%4°")
                    .arg(fixtureName(o.fixture))
                    .arg(ri >= 0 && ri < 3 ? rotLabels[ri] : "?")
                    .arg(o.valueDeg, 0, 'f', 1)
                    .arg(o.sigma, 0, 'f', 1);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::BeamDirectionObs>)
            {
                map["type"] = "BeamDirection";
                map["fixture"] = o.fixture;
                QStringList parts;
                if (o.hasElevation)
                    parts.append(QString("el=%1°").arg(o.elevationDeg, 0, 'f', 0));
                if (o.hasAzimuth)
                    parts.append(QString("az=%1°").arg(o.azimuthDeg, 0, 'f', 0));
                map["description"] = QString("[%1] beam %2 ±%3°")
                    .arg(fixtureName(o.fixture))
                    .arg(parts.join(", "))
                    .arg(o.sigma, 0, 'f', 1);
            }
            else if constexpr (std::is_same_v<T, CalibrationModel::DistanceObs>)
            {
                map["type"] = "Distance";
                map["fixtureA"] = o.fixtureA;
                map["fixtureB"] = o.fixtureB;
                map["distance"] = o.distance;
                map["description"] = QString("[%1] ↔ [%2] = %3m ±%4cm")
                    .arg(fixtureName(o.fixtureA))
                    .arg(fixtureName(o.fixtureB))
                    .arg(o.distance, 0, 'f', 2)
                    .arg(o.sigma * 100.0, 0, 'f', 0);
            }
        }, obs);

        list.append(map);
    }
    return list;
}

QVariantList CalibrateController::solverFixtureResults() const
{
    QVariantList list;
    CalibrationModel *cm = m_doc->calibrationModel();
    if (!cm->hasSolveResult())
        return list;

    const auto &result = cm->lastResult();
    for (const auto &[sid, pose] : result.poses)
    {
        QString fid = QString::fromStdString(sid);
        auto unc = cm->fixtureUncertainty(fid);

        QVariantMap map;
        map["fixtureId"] = fid;
        map["fixtureName"] = fixtureName(fid);
        map["quality"] = QString::fromStdString(unc.quality());
        map["xCm"] = unc.x_cm;
        map["yCm"] = unc.y_cm;
        map["zCm"] = unc.z_cm;
        list.append(map);
    }
    return list;
}
