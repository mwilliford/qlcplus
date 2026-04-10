/*
  Q Light Controller Plus - Unit test
  calibrationmodel_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef CALIBRATIONMODEL_TEST_H
#define CALIBRATIONMODEL_TEST_H

#include <QObject>

class CalibrationModel_Test : public QObject
{
    Q_OBJECT

private slots:
    // Observation CRUD
    void addAndRemoveObservation();
    void addAllObservationTypes();
    void clearObservations();
    void convenienceBuilders();

    // Constraints
    void setAndClearConstraints();
    void lockFixture();

    // Solver integration
    void solveWithAimObservations();
    void solveReportsConvergence();
    void solveUpdatesCovariance();

    // XML persistence
    void saveAndLoadObservations();
    void saveAndLoadConstraints();
    void saveAndLoadAllTypes();

    // Clear
    void clearAll();
};

#endif // CALIBRATIONMODEL_TEST_H
