/*
  Q Light Controller Plus
  spatialcontroller.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef SPATIALCONTROLLER_H
#define SPATIALCONTROLLER_H

#include <QObject>
#include <QVariantList>
#include <cstdint>
#include <functional>
#include <vector>

class SpatialView;
class SpatialModel;
class Doc;

/**
 * @brief QML-facing controller for the Spatial View side panel.
 *
 * Bridges QML property bindings to SpatialView (camera, selection) and
 * SpatialModel (fixture transforms). Exposed to QML via context property.
 */
class SpatialController : public QObject
{
    Q_OBJECT

    // Selection
    Q_PROPERTY(int selectedFixtureId READ selectedFixtureId WRITE setSelectedFixtureId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedFixtureName READ selectedFixtureName NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

    // Position (meters, 2 decimal places in QML)
    Q_PROPERTY(double posX READ posX WRITE setPosX NOTIFY transformChanged)
    Q_PROPERTY(double posY READ posY WRITE setPosY NOTIFY transformChanged)
    Q_PROPERTY(double posZ READ posZ WRITE setPosZ NOTIFY transformChanged)

    // Rotation (degrees)
    Q_PROPERTY(double rotPitch READ rotPitch WRITE setRotPitch NOTIFY transformChanged)
    Q_PROPERTY(double rotYaw READ rotYaw WRITE setRotYaw NOTIFY transformChanged)
    Q_PROPERTY(double rotRoll READ rotRoll WRITE setRotRoll NOTIFY transformChanged)

    // Mode
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)

    // Snap
    Q_PROPERTY(bool gridSnap READ gridSnap WRITE setGridSnap NOTIFY gridSnapChanged)
    Q_PROPERTY(double gridSize READ gridSize WRITE setGridSize NOTIFY gridSizeChanged)

    // Axis mode (0=World, 1=Local)
    Q_PROPERTY(int axisMode READ axisMode WRITE setAxisMode NOTIFY axisModeChanged)

    // Gizmo mode (0=Translate, 1=Rotate)
    Q_PROPERTY(int gizmoMode READ gizmoMode WRITE setGizmoMode NOTIFY gizmoModeChanged)

    // Calibration state
    Q_PROPERTY(int obsCount READ observationCount NOTIFY calibrationChanged)
    Q_PROPERTY(bool hasSolverResult READ hasSolverResult NOTIFY calibrationChanged)
    Q_PROPERTY(bool solverConverged READ solverConverged NOTIFY calibrationChanged)
    Q_PROPERTY(double solverRms READ solverRms NOTIFY calibrationChanged)

public:
    explicit SpatialController(Doc *doc, SpatialView *view, QObject *parent = nullptr);

    enum Mode { Layout = 0, Calibrate = 1, Focus = 2, Live = 3 };
    Q_ENUM(Mode)

    // --- Selection ---
    int selectedFixtureId() const { return m_selectedFixtureId; }
    void setSelectedFixtureId(int id);
    QString selectedFixtureName() const;
    bool hasSelection() const { return m_selectedFixtureId >= 0; }
    int selectionCount() const { return m_selectionCount; }

    // --- Position ---
    double posX() const;
    double posY() const;
    double posZ() const;
    void setPosX(double v);
    void setPosY(double v);
    void setPosZ(double v);

    // --- Rotation ---
    double rotPitch() const;
    double rotYaw() const;
    double rotRoll() const;
    void setRotPitch(double v);
    void setRotYaw(double v);
    void setRotRoll(double v);

    // --- Mode ---
    int mode() const { return m_mode; }
    void setMode(int m);

    // --- Snap ---
    bool gridSnap() const { return m_gridSnap; }
    void setGridSnap(bool v);
    double gridSize() const { return m_gridSize; }
    void setGridSize(double v);

    // --- Axis mode ---
    int axisMode() const { return m_axisMode; }
    void setAxisMode(int m);

    // --- Gizmo mode ---
    int gizmoMode() const { return m_gizmoMode; }
    void setGizmoMode(int m);

    // --- Align (Q_INVOKABLE for QML button clicks) ---
    Q_INVOKABLE void alignSelection(const QString &axis);

    // --- Truss ---
    Q_INVOKABLE void addDefaultTruss();
    Q_INVOKABLE void removeTruss(const QString &id);

    // --- Camera (Q_INVOKABLE for QML button clicks) ---
    Q_INVOKABLE void setCameraPreset(const QString &preset);

    // --- Calibration (Q_INVOKABLE for QML in Calibrate mode) ---

    /** Run the Ceres solver on current observations. Returns true if converged. */
    Q_INVOKABLE bool runSolve();

    /** Accept solver-derived transforms for all fixtures (promote to committed). */
    Q_INVOKABLE void acceptSolverResults();

    /** Dismiss solver-derived transforms (clear solverDerived layer). */
    Q_INVOKABLE void dismissSolverResults();

    /** Add a position observation for a fixture. axis: 0=X, 1=Y, 2=Z */
    Q_INVOKABLE int addPositionObs(int fixtureId, int axis, double value, double certainty);

    /** Add a rotation observation for a fixture. axis: 3=RX, 4=RY, 5=RZ */
    Q_INVOKABLE int addRotationObs(int fixtureId, int axis, double valueDeg, double certainty);

    /** Add a distance observation between two fixtures. */
    Q_INVOKABLE int addDistanceObs(int fixtureIdA, int fixtureIdB, double distance, double certainty);

    /** Remove an observation by ID. */
    Q_INVOKABLE void removeObs(int obsId);

    /** Clear all observations. */
    Q_INVOKABLE void clearAllObs();

    /** Get observation count. */
    Q_INVOKABLE int observationCount() const;

    /** Lock a fixture in the solver (exclude from optimization). */
    Q_INVOKABLE void lockFixtureInSolver(int fixtureId);

    /** Set a height constraint for a fixture (Z axis). */
    Q_INVOKABLE void setHeightConstraint(int fixtureId, double heightM, double certainty);

    /** Get all observations as a QML-friendly list of maps. */
    Q_INVOKABLE QVariantList observationsList() const;

    /** Get per-fixture solver results (uncertainty, quality). */
    Q_INVOKABLE QVariantList solverFixtureResults() const;

    /** Called by SpatialView when selection changes via mouse click. */
    void notifySelectionChanged(int fixtureId, int count = 1);

    /** Set a callback to retrieve all selected fixture IDs. */
    void setSelectedIdsCallback(std::function<std::vector<int32_t>()> cb) { m_selectedIdsCallback = std::move(cb); }

    // --- Calibration read-only properties ---
    bool hasSolverResult() const;
    bool solverConverged() const;
    double solverRms() const;

signals:
    void selectionChanged();
    void transformChanged();
    void modeChanged();
    void gridSnapChanged();
    void gridSizeChanged();
    void axisModeChanged();
    void gizmoModeChanged();
    void calibrationChanged();

private:
    void updateTransformFromModel();
    QString fixtureName(const QString &fixtureId) const;

    Doc *m_doc;
    SpatialView *m_view;
    int m_selectedFixtureId = -1;
    int m_selectionCount = 0;
    int m_mode = Layout;
    bool m_gridSnap = true;
    double m_gridSize = 0.5;  // 0.5m grid
    int m_axisMode = 0;       // 0=World, 1=Local
    std::function<std::vector<int32_t>()> m_selectedIdsCallback;
    int m_gizmoMode = 0;      // 0=Translate, 1=Rotate

    // Cached transform values (avoid querying model every frame)
    double m_posX = 0, m_posY = 0, m_posZ = 0;
    double m_rotPitch = 0, m_rotYaw = 0, m_rotRoll = 0;
};

#endif // SPATIALCONTROLLER_H
