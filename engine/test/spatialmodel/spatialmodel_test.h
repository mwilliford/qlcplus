/*
  Q Light Controller Plus - Unit test
  spatialmodel_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef SPATIALMODEL_TEST_H
#define SPATIALMODEL_TEST_H

#include <QObject>

class SpatialModel_Test : public QObject
{
    Q_OBJECT

private slots:
    // Transform basics
    void setAndGetTransform();
    void fixtureMatrix4x4();
    void removeFixture();
    void sourceTracking();
    void identityForUnknown();

    // Named planes
    void setAndGetPlanes();

    // XML round-trip
    void saveAndLoadXML();
    void saveSkipsEmpty();
    void loadPreservesSource();

    // Legacy migration
    void migrateFromMonitorProperties();
    void migratePositionConversion();

    // Solver visualization
    void applySolverVisualization();
    void clearSolverViz();

    // Signals
    void transformChangedSignal();
    void solverVizChangedSignal();

    // Coordinate conversion round-trip
    void mmDegreesRoundTrip();
    void mmDegreesRoundTripWithRotation();
    void mmPositionPreservesRotation();
    void degRotationPreservesPosition();

    // Clear
    void clearAll();
};

#endif // SPATIALMODEL_TEST_H
