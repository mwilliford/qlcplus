/*
  Q Light Controller Plus
  calibratecontroller.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef CALIBRATECONTROLLER_H
#define CALIBRATECONTROLLER_H

#include <QObject>
#include <QVariantList>

class Doc;
class SpatialModel;

/**
 * @brief QML-facing controller for the Calibrate mode panel.
 *
 * Manages observation CRUD, solver execution, accept/dismiss workflow,
 * and per-fixture uncertainty display. Exposed to QML as "calibrateController".
 */
class CalibrateController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int obsCount READ observationCount NOTIFY changed)
    Q_PROPERTY(bool hasSolverResult READ hasSolverResult NOTIFY changed)
    Q_PROPERTY(bool solverConverged READ solverConverged NOTIFY changed)
    Q_PROPERTY(double solverRms READ solverRms NOTIFY changed)

public:
    explicit CalibrateController(Doc *doc, QObject *parent = nullptr);

    // --- Read-only properties ---
    int observationCount() const;
    bool hasSolverResult() const;
    bool solverConverged() const;
    double solverRms() const;

    // --- Observation CRUD ---
    Q_INVOKABLE int addPositionObs(int fixtureId, int axis, double value, double certainty);
    Q_INVOKABLE int addRotationObs(int fixtureId, int axis, double valueDeg, double certainty);
    Q_INVOKABLE int addDistanceObs(int fixtureIdA, int fixtureIdB, double distance, double certainty);
    Q_INVOKABLE void removeObs(int obsId);
    Q_INVOKABLE void clearAllObs();

    // --- Solver ---
    Q_INVOKABLE bool runSolve();
    Q_INVOKABLE void acceptSolverResults();
    Q_INVOKABLE void dismissSolverResults();
    Q_INVOKABLE void acceptFixtureResult(int fixtureId);
    Q_INVOKABLE void dismissFixtureResult(int fixtureId);
    Q_INVOKABLE int addAimObs(int fixtureId, double targetX, double targetY, double targetZ, double certainty);

    // --- Constraints ---
    Q_INVOKABLE void lockFixtureInSolver(int fixtureId);
    Q_INVOKABLE void setHeightConstraint(int fixtureId, double heightM, double certainty);

    // --- Tolerances (layout priors for the solver) ---
    // dof: 0=tx, 1=ty, 2=tz (meters), 3=rx, 4=ry, 5=rz (degrees)

    /** Get the current tolerance for a fixture DOF (default if not set). */
    Q_INVOKABLE double getTolerance(int fixtureId, int dof) const;

    /** Set the tolerance for a fixture DOF. */
    Q_INVOKABLE void setTolerance(int fixtureId, int dof, double value);

    /** Reset all 6 DOF tolerances for a fixture to defaults. */
    Q_INVOKABLE void resetTolerances(int fixtureId);

    /** Reset tolerances for all currently selected fixtures. */
    Q_INVOKABLE void resetTolerancesForSelection(const QVariantList &fixtureIds);

    /** Set the same tolerance on multiple fixtures (for multi-select edits). */
    Q_INVOKABLE void setToleranceForFixtures(const QVariantList &fixtureIds,
                                              int dof, double value);

    // --- Data for QML lists ---
    Q_INVOKABLE QVariantList observationsList() const;
    Q_INVOKABLE QVariantList solverFixtureResults() const;

signals:
    void changed();

private:
    QString fixtureName(const QString &fixtureId) const;

    Doc *m_doc;
};

#endif // CALIBRATECONTROLLER_H
