/*
  Q Light Controller Plus
  gdtfkinematics_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef GDTFKINEMATICS_TEST_H
#define GDTFKINEMATICS_TEST_H

#include <QObject>

class GDTFKinematics_Test : public QObject
{
    Q_OBJECT

private slots:
    // --- buildGDTFKinematics tests ---
    void movingHead_standardIdentityRotation();
    void movingHead_wellAuthoredRotation();
    void movingHead_identityVsWellAuthored_equivalence();
    void movingMirror_identityRotation();
    void movingMirror_wellAuthoredRotation();
    void tiltOnly();
    void panOnly();
    void fixedFixture();
    void multiBeam_ledBar();
    void invertedPhysicalRange();
    void sixteenBitChannels();
    void missingDmxChannel();

    // --- synthesizeGDTFFromQXF tests ---
    void synthesize_movingHead();
    void synthesize_mirrorScanner();
    void synthesize_panOnly();
    void synthesize_fixed();

    // --- Integration / round-trip tests ---
    void pipeline_qxfToKinematics_matchesFactory();
    void pipeline_forwardInverseRoundTrip();
    void pipeline_multiBeamForwardAll();
    void pipeline_channelMapRoundTrip();

    // --- Beam origin vs scene graph consistency tests ---
    void intermediateNode_beforeFirstAxis();
    void intermediateNode_betweenAxes();
    void intermediateNode_beforeBeam();
    void beamOrigin_matchesSceneGraphWalk();
    void beamOrigin_matchesSceneGraphWalk_withIntermediates();

    // --- rigmath v1.1 API adoption tests ---
    void forwardWorldAll_matchesForwardWorldPerBeam();
    void beamHitPlaneZ_matchesManualFloorClip();
    void beamHitPlaneZ_rejectsPointingAwayAndParallel();
};

#endif // GDTFKINEMATICS_TEST_H
