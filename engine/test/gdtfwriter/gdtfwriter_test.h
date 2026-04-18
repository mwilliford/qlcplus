/*
  Q Light Controller Plus - Unit test
  gdtfwriter_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef GDTFWRITER_TEST_H
#define GDTFWRITER_TEST_H

#include <QObject>

class GDTFWriter_Test : public QObject
{
    Q_OBJECT

private slots:
    // Input validation
    void writeSynthetic_nullDef_returnsEmptyWithError();
    void writeSynthetic_nullMode_returnsEmptyWithError();

    // Archive shape
    void writeSynthetic_producesValidZipBytes();
    void writeSynthetic_bytesRoundTripThroughFromBuffer();

    // Geometry tree
    void writeSynthetic_panOnlyFixture_buildsBaseYokeLamp();
    void writeSynthetic_panTiltMovingHead_buildsBaseYokeHeadLamp();
    void writeSynthetic_mirrorScanner_usesScannerPrimitive();
    void writeSynthetic_fixedPar_buildsBaseLampOnly();

    // DMX channels
    void writeSynthetic_emitsOneChannelPerQxfSlot();
    void writeSynthetic_collapsesMsbLsbPairIntoSingleGdtfChannel();
    void writeSynthetic_panChannelWiresPanAttribute();
    void writeSynthetic_dimmerChannelWiresDimmerAttribute();

    // Deterministic UUID
    void writeSynthetic_sameInputsProduceSameUuid();
    void writeSynthetic_differentInputsProduceDifferentUuids();
};

#endif // GDTFWRITER_TEST_H
