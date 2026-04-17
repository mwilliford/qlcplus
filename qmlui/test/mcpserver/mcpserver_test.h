/*
  Q Light Controller Plus - Unit test
  mcpserver_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef MCPSERVER_TEST_H
#define MCPSERVER_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QString>

class Doc;
class McpServer;

class McpServer_Test : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // --- Protocol ---
    void initializeReturnsProtocolVersion();
    void pingReturnsEmptyResult();
    void toolsListReturnsAllTools();
    void unknownMethodReturnsError();
    void missingSessionReturnsError();
    void invalidSessionReturnsError();
    void notificationReturnsEmpty();

    // --- Validation: required params ---
    void clickMissingX();
    void clickMissingY();
    void dragMissingX1();
    void dragMissingY1();
    void selectFixtureMissingId();
    void createFocusPointMissingX();
    void createFocusPointMissingZ();
    void deleteFocusPointMissingId();
    void deleteFocusPointEmptyId();
    void moveFocusPointMissingId();
    void renameFocusPointMissingName();
    void assignFixtureMissingFixtureId();
    void aimAtFocusPointEmptyId();

    // --- Validation: enum bounds ---
    void setGizmoModeInvalid();
    void alignSelectionInvalidAxis();
    void setSpatialModeInvalid();

    // --- list_fixtures ---
    void listFixturesEmpty();
    void listFixturesWithFixture();

    // --- list_functions ---
    void listFunctionsEmpty();
    void listFunctionsWithScene();

    // --- get_dmx_values ---
    void getDmxValuesInvalidUniverse();
    void getDmxValuesEmpty();

    // --- screenshot ---
    void screenshotSpatialViewNotOpen();
    void screenshotNoMainWindow();

    // --- click ---
    void clickNoWindow();

    // --- select_fixture ---
    void selectFixtureSuccess();
    void selectFixtureDeselect();
    void selectFixtureAdd();

    // --- set_gizmo_mode ---
    void setGizmoModeTranslate();
    void setGizmoModeRotate();

    // --- align_selection ---
    void alignSelectionX();
    void alignSelectionY();
    void alignSelectionZ();

    // --- set_spatial_mode ---
    void setSpatialModeLayout();
    void setSpatialModeCalibrate();
    void setSpatialModeFocus();
    void setSpatialModeLive();

    // --- set_camera ---
    void setCameraPresetFoh();
    void setCameraPresetTop();
    void setCameraExplicit();

    // --- focus points ---
    void createFocusPointSuccess();
    void createFocusPointUnavailable();
    void listFocusPointsEmpty();
    void listFocusPointsWithPoints();
    void deleteFocusPointSuccess();
    void deleteFocusPointNotFound();
    void moveFocusPointSuccess();
    void moveFocusPointNotFound();
    void renameFocusPointSuccess();
    void assignFixtureSuccess();
    void unassignFixtureSuccess();
    void aimAtFocusPointSuccess();
    void aimAtFocusPointNotFound();

    // --- calibration ---
    void calibrateAddObsHeight();
    void calibrateAddObsDistance();
    void calibrateAddObsUnknownType();
    void calibrateRunSolveNoObservations();

    // --- unknown tool ---
    void unknownToolReturnsError();

private:
    // Send a JSON-RPC request as the initialized session. Returns parsed response.
    QJsonObject call(const QString &method, const QJsonObject &params = {});

    // Send a raw tool call. Returns the outer JSON-RPC response object.
    QJsonObject toolCall(const QString &toolName, const QJsonObject &args = {});

    // Extract content[0].text from a tool result.
    static QString contentText(const QJsonObject &response);

    // Returns true if the response has isError=true in result.
    static bool isToolError(const QJsonObject &response);

    Doc       *m_doc    = nullptr;
    McpServer *m_server = nullptr;
    QString    m_sessionId;
    int        m_nextId = 1;
};

#endif // MCPSERVER_TEST_H
