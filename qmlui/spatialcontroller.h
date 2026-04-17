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
#include <QSet>
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

    /** Get all currently selected fixture IDs (from the renderer selection). */
    Q_INVOKABLE QVariantList selectedFixtureIds() const;

    // --- Truss ---
    Q_INVOKABLE void addDefaultTruss();
    Q_INVOKABLE void removeTruss(const QString &id);

    // --- Camera (Q_INVOKABLE for QML button clicks) ---
    Q_INVOKABLE void setCameraPreset(const QString &preset);

    /** Called by SpatialView when selection changes via mouse click. */
    void notifySelectionChanged(int fixtureId, int count = 1);

    /** Set a callback to retrieve all selected fixture IDs. */
    void setSelectedIdsCallback(std::function<std::vector<int32_t>()> cb) { m_selectedIdsCallback = std::move(cb); }

    // --- Focus mode aim ---
    /** Set the world-space aim point for Focus mode. Computes IK for each
     *  selected moving-head fixture and emits focusDmxWrite signals. */
    Q_INVOKABLE void setFocusAim(double wx, double wy, double wz);

    /** Clear the aim and release any DMX channels we were holding. */
    Q_INVOKABLE void clearFocusAim();

    bool focusAimValid() const { return m_focusAimValid; }
    void getFocusAim(double *x, double *y, double *z) const;

    // --- Focus points (persistent named aim targets) ---

    /** Create a new focus point at the given world position. Returns the
     *  generated id. Name is auto-generated if empty (e.g., "Point 1"). */
    Q_INVOKABLE QString createFocusPoint(double wx, double wy, double wz,
                                          const QString &name = QString());

    /** Delete a focus point. Also unassigns any selected-focus-point state. */
    Q_INVOKABLE void deleteFocusPoint(const QString &id);

    /** Move an existing focus point to a new world position. */
    Q_INVOKABLE void moveFocusPoint(const QString &id, double wx, double wy, double wz);

    /** Rename an existing focus point. */
    Q_INVOKABLE void renameFocusPoint(const QString &id, const QString &name);

    /** Assign a fixture to track a focus point. Returns true if newly assigned. */
    Q_INVOKABLE bool assignFixtureToFocusPoint(const QString &fpId, int fixtureId);

    /** Unassign a fixture from a focus point. Returns true if it was assigned. */
    Q_INVOKABLE bool unassignFixtureFromFocusPoint(const QString &fpId, int fixtureId);

    /** Aim all fixtures assigned to the given focus point at its position.
     *  Internally calls setFocusAim() after temporarily routing the
     *  assigned fixtures as the "selected" set for IK. */
    Q_INVOKABLE void aimAtFocusPoint(const QString &id);

    /** Currently selected focus point id (empty if none). */
    QString selectedFocusPointId() const { return m_selectedFocusPointId; }
    Q_INVOKABLE void setSelectedFocusPointId(const QString &id);

signals:
    void selectionChanged();
    void transformChanged();
    void modeChanged();
    void gridSnapChanged();
    void gridSizeChanged();
    void axisModeChanged();
    void gizmoModeChanged();

    /** Emitted for each DMX byte Focus mode wants to write. Wired in
     *  spatialviewwindow.cpp to SimpleDesk::setAbsoluteChannelValue. */
    void focusDmxWrite(uint absChannel, uchar value);

    /** Emitted for each DMX channel Focus mode is releasing on exit/clear.
     *  Wired to SimpleDesk::resetAbsoluteChannel. */
    void focusDmxReset(uint absChannel);

    /** Fired when the aim point changes or clears — SpatialView uses this
     *  to update the renderer's focus aim marker. */
    void focusAimChanged();

    /** Emitted when the selected focus point changes. */
    void selectedFocusPointChanged();

private:
    void updateTransformFromModel();

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

    // --- Focus mode state ---
    bool m_focusAimValid = false;
    double m_focusAim[3] = {0, 0, 0};
    QSet<uint> m_focusControlledChannels;

    // Focus points
    QString m_selectedFocusPointId;
    int m_nextFocusPointIdNum = 0;  // for auto-generating unique ids
};

#endif // SPATIALCONTROLLER_H
