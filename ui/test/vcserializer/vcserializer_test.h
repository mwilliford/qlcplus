/*
  Q Light Controller Plus - Unit test
  vcserializer_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef VCSERIALIZER_TEST_H
#define VCSERIALIZER_TEST_H

#include <QObject>

class Doc;

class VCSerializer_Test : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void serializeEmptyVC();
    void serializeButton();
    void serializeSliderLevel();
    void serializeSliderPlayback();
    void serializeFrameWithChildren();
    void serializeSoloFrame();
    void serializeCueList();
    void serializeSpeedDial();
    void serializeXYPad();
    void serializeMatrix();
    void serializeInputSources();
    void serializeFullLayout();

private:
    Doc *m_doc;
};

#endif // VCSERIALIZER_TEST_H
