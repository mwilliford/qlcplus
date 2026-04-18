/*
  Q Light Controller Plus - Unit test
  mvrio_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef MVRIO_TEST_H
#define MVRIO_TEST_H

#include <QObject>

class MvrIO_Test : public QObject
{
    Q_OBJECT

private slots:
    // Coordinate conversion — MVR STransformMatrix <-> rigmath RigidTransform
    void convertMatrix_identity();
    void convertMatrix_translationMillimetersToMeters();
    void convertMatrix_pan90Degrees();
    void convertMatrix_tilt90Degrees();
    void convertMatrix_roundTripRigmathToMvrAndBack();

    // Import — real libMVRgdtf round-trip using a synthesized MVR
    void importEmptyMvr_succeedsWithZeroFixtures();
    void importMvr_missingGdtfIsSkippedNotAborted();

    // Project-scoped GDTF registry — the import routes embedded GDTFs into
    // Doc::projectFixtureDefCache() rather than the user's global cache.
    void importMvr_dedupsGdtfWithinSameFile();
    void importMvr_projectCacheIsolatedFromGlobalCache();
    void clearContents_purgesProjectCacheButNotGlobal();

    // Export — MVR-2
    void exportMvr_emptyDocProducesOpenableArchive();
    void exportMvr_roundTripPreservesFixturesAndPositions();
    void exportMvr_dedupsGdtfAcrossFixtures();
    void exportMvr_fallsBackToIdentityWithoutSpatialModel();
    void exportMvr_synthesizesGdtfForQxfOnlyFixtures();
    void exportMvr_preservesMvrUuidOnRoundTrip();
};

#endif // MVRIO_TEST_H
