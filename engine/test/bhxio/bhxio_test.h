/*
  Q Light Controller Plus - Unit test
  bhxio_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef BHXIO_TEST_H
#define BHXIO_TEST_H

#include <QObject>

class BhxIO_Test : public QObject
{
    Q_OBJECT

private slots:
    void saveEmptyWorkspace_producesOpenableBhx();
    void saveAndReopen_preservesFixturesFunctionsAndSpatial();
    void saveAndReopen_preservesConsoleXmlBytes();
    void bhx_isValidMvrRig();
    void openMissingFile_returnsErrorWithoutCrash();
    void openNonBhxZip_returnsErrorWithoutCrash();
};

#endif // BHXIO_TEST_H
