/*
  Q Light Controller Plus - Unit test
  mcpserver_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "mcpserver_test.h"
#include "spatialview_stubs.h"

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Pull in mcpserver with private access for processRequest
#define protected public
#define private public
#include "mcpserver.h"
#undef private
#undef protected

#include "doc.h"
#include "fixture.h"
#include "qlcfixturedef.h"
#include "scene.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QJsonObject McpServer_Test::call(const QString &method, const QJsonObject &params)
{
    QJsonObject req{
        {"jsonrpc", "2.0"},
        {"id", m_nextId++},
        {"method", method},
        {"params", params}
    };
    QByteArray body = QJsonDocument(req).toJson(QJsonDocument::Compact);
    QByteArray resp = m_server->processRequest(body, m_sessionId);
    return QJsonDocument::fromJson(resp).object();
}

QJsonObject McpServer_Test::toolCall(const QString &toolName, const QJsonObject &args)
{
    return call("tools/call", QJsonObject{{"name", toolName}, {"arguments", args}});
}

QString McpServer_Test::contentText(const QJsonObject &response)
{
    return response.value("result").toObject()
                   .value("content").toArray()
                   .at(0).toObject()
                   .value("text").toString();
}

bool McpServer_Test::isToolError(const QJsonObject &response)
{
    return response.value("result").toObject().value("isError").toBool();
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

void McpServer_Test::initTestCase()
{
    m_doc = new Doc(this);
    m_server = new McpServer(nullptr, m_doc, 0 /* port 0 = don't bind */, this);

    // Initialize and capture session ID
    QJsonObject initReq{
        {"jsonrpc", "2.0"},
        {"id", m_nextId++},
        {"method", "initialize"},
        {"params", QJsonObject{}}
    };
    QByteArray initBody = QJsonDocument(initReq).toJson(QJsonDocument::Compact);
    QByteArray initResp = m_server->processRequest(initBody, {});
    QJsonObject initObj  = QJsonDocument::fromJson(initResp).object();
    m_sessionId = initObj.value("result").toObject().value("_sessionId").toString();
    QVERIFY(!m_sessionId.isEmpty());
}

void McpServer_Test::cleanupTestCase()
{
    delete m_server;
    delete m_doc;
}

void McpServer_Test::cleanup()
{
    m_doc->clearContents();
    McpTestStubs::reset();
}

// ---------------------------------------------------------------------------
// Protocol
// ---------------------------------------------------------------------------

void McpServer_Test::initializeReturnsProtocolVersion()
{
    QJsonObject req{
        {"jsonrpc", "2.0"},
        {"id", 99},
        {"method", "initialize"},
        {"params", QJsonObject{}}
    };
    QByteArray body = QJsonDocument(req).toJson(QJsonDocument::Compact);
    QJsonObject resp = QJsonDocument::fromJson(m_server->processRequest(body, {})).object();

    QJsonObject result = resp.value("result").toObject();
    QCOMPARE(result.value("protocolVersion").toString(), QString("2025-03-26"));
    QCOMPARE(result.value("serverInfo").toObject().value("name").toString(),
             QString("qlcplus-mcp"));
    QVERIFY(!result.value("_sessionId").toString().isEmpty());
}

void McpServer_Test::pingReturnsEmptyResult()
{
    QJsonObject resp = call("ping");
    QVERIFY(resp.contains("result"));
    QVERIFY(!resp.contains("error"));
}

void McpServer_Test::toolsListReturnsAllTools()
{
    QJsonObject resp = call("tools/list");
    QJsonArray tools = resp.value("result").toObject().value("tools").toArray();
    QVERIFY(tools.size() >= 27);

    // Spot-check a few tool names
    QStringList names;
    for (const auto &t : tools)
        names << t.toObject().value("name").toString();
    QVERIFY(names.contains("screenshot"));
    QVERIFY(names.contains("create_focus_point"));
    QVERIFY(names.contains("calibrate_run_solve"));
}

void McpServer_Test::unknownMethodReturnsError()
{
    QJsonObject resp = call("bogus/method");
    QVERIFY(resp.contains("error"));
    QCOMPARE(resp.value("error").toObject().value("code").toInt(), -32601);
}

void McpServer_Test::missingSessionReturnsError()
{
    // No sessionId at all on a non-initialize request
    QJsonObject req{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "tools/list"},
        {"params", QJsonObject{}}
    };
    QByteArray body = QJsonDocument(req).toJson(QJsonDocument::Compact);
    // Pass a non-empty but unknown session ID
    QByteArray resp = m_server->processRequest(body, "nonexistent-session-id");
    QJsonObject obj = QJsonDocument::fromJson(resp).object();
    QVERIFY(obj.contains("error"));
    QCOMPARE(obj.value("error").toObject().value("code").toInt(), -32600);
}

void McpServer_Test::invalidSessionReturnsError()
{
    QJsonObject resp = QJsonDocument::fromJson(
        m_server->processRequest(
            QJsonDocument(QJsonObject{
                {"jsonrpc","2.0"},{"id",1},{"method","tools/list"},{"params",QJsonObject{}}
            }).toJson(QJsonDocument::Compact),
            "invalid-uuid-1234"
        )
    ).object();
    QVERIFY(resp.contains("error"));
    QCOMPARE(resp.value("error").toObject().value("code").toInt(), -32600);
}

void McpServer_Test::notificationReturnsEmpty()
{
    // Notifications have no "id" field — server returns empty body
    QJsonObject notif{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"},
        {"params", QJsonObject{}}
    };
    QByteArray body = QJsonDocument(notif).toJson(QJsonDocument::Compact);
    QByteArray resp = m_server->processRequest(body, m_sessionId);
    QVERIFY(resp.isEmpty());
}

// ---------------------------------------------------------------------------
// Validation: required params
// ---------------------------------------------------------------------------

void McpServer_Test::clickMissingX()
{
    auto resp = toolCall("click", {{"y", 100}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("x"));
}

void McpServer_Test::clickMissingY()
{
    auto resp = toolCall("click", {{"x", 100}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("y"));
}

void McpServer_Test::dragMissingX1()
{
    auto resp = toolCall("drag", {{"y1",0},{"x2",10},{"y2",10}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("x1"));
}

void McpServer_Test::dragMissingY1()
{
    auto resp = toolCall("drag", {{"x1",0},{"x2",10},{"y2",10}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("y1"));
}

void McpServer_Test::selectFixtureMissingId()
{
    auto resp = toolCall("select_fixture", {});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("id"));
}

void McpServer_Test::createFocusPointMissingX()
{
    auto resp = toolCall("create_focus_point", {{"y",0.0},{"z",1.0}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("x"));
}

void McpServer_Test::createFocusPointMissingZ()
{
    auto resp = toolCall("create_focus_point", {{"x",0.0},{"y",0.0}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("z"));
}

void McpServer_Test::deleteFocusPointMissingId()
{
    auto resp = toolCall("delete_focus_point", {});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("id"));
}

void McpServer_Test::deleteFocusPointEmptyId()
{
    auto resp = toolCall("delete_focus_point", {{"id", ""}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("empty"));
}

void McpServer_Test::moveFocusPointMissingId()
{
    auto resp = toolCall("move_focus_point", {{"x",1.0},{"y",0.0},{"z",0.0}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("id"));
}

void McpServer_Test::renameFocusPointMissingName()
{
    auto resp = toolCall("rename_focus_point", {{"id","fp0"}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("name"));
}

void McpServer_Test::assignFixtureMissingFixtureId()
{
    auto resp = toolCall("assign_fixture_to_focus_point", {{"id","fp0"}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("fixtureId"));
}

void McpServer_Test::aimAtFocusPointEmptyId()
{
    auto resp = toolCall("aim_at_focus_point", {{"id",""}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("empty"));
}

// ---------------------------------------------------------------------------
// Validation: enum bounds
// ---------------------------------------------------------------------------

void McpServer_Test::setGizmoModeInvalid()
{
    auto resp = toolCall("set_gizmo_mode", {{"mode", 5}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("0") || contentText(resp).contains("1"));
}

void McpServer_Test::alignSelectionInvalidAxis()
{
    auto resp = toolCall("align_selection", {{"axis", "W"}});
    QVERIFY(isToolError(resp));
}

void McpServer_Test::setSpatialModeInvalid()
{
    auto resp = toolCall("set_spatial_mode", {{"mode", 4}});
    QVERIFY(isToolError(resp));
}

// ---------------------------------------------------------------------------
// list_fixtures
// ---------------------------------------------------------------------------

void McpServer_Test::listFixturesEmpty()
{
    auto resp = toolCall("list_fixtures");
    QVERIFY(!isToolError(resp));
    QString text = contentText(resp);
    QJsonArray arr = QJsonDocument::fromJson(text.toUtf8()).array();
    QCOMPARE(arr.size(), 0);
}

void McpServer_Test::listFixturesWithFixture()
{
    Fixture *fix = new Fixture(m_doc);
    fix->setName("Test Spot");
    fix->setAddress(0);
    fix->setUniverse(0);
    fix->setChannels(16);
    m_doc->addFixture(fix);

    auto resp = toolCall("list_fixtures");
    QVERIFY(!isToolError(resp));
    QJsonArray arr = QJsonDocument::fromJson(contentText(resp).toUtf8()).array();
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr.at(0).toObject().value("name").toString(), QString("Test Spot"));
    QCOMPARE(arr.at(0).toObject().value("channels").toInt(), 16);
}

// ---------------------------------------------------------------------------
// list_functions
// ---------------------------------------------------------------------------

void McpServer_Test::listFunctionsEmpty()
{
    auto resp = toolCall("list_functions");
    QVERIFY(!isToolError(resp));
    QJsonArray arr = QJsonDocument::fromJson(contentText(resp).toUtf8()).array();
    QCOMPARE(arr.size(), 0);
}

void McpServer_Test::listFunctionsWithScene()
{
    Scene *scene = new Scene(m_doc);
    scene->setName("Test Scene");
    m_doc->addFunction(scene);

    auto resp = toolCall("list_functions");
    QVERIFY(!isToolError(resp));
    QJsonArray arr = QJsonDocument::fromJson(contentText(resp).toUtf8()).array();
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr.at(0).toObject().value("name").toString(), QString("Test Scene"));
    QCOMPARE(arr.at(0).toObject().value("type").toString(), QString("Scene"));
}

// ---------------------------------------------------------------------------
// get_dmx_values
// ---------------------------------------------------------------------------

void McpServer_Test::getDmxValuesInvalidUniverse()
{
    auto resp = toolCall("get_dmx_values", {{"universe", 99}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("99"));
}

void McpServer_Test::getDmxValuesEmpty()
{
    auto resp = toolCall("get_dmx_values", {{"universe", 0}});
    QVERIFY(!isToolError(resp));
    // Should return universe 0 with empty nonZeroValues
    QString text = contentText(resp);
    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value("universe").toInt(), 0);
    QCOMPARE(obj.value("nonZeroValues").toObject().size(), 0);
}

// ---------------------------------------------------------------------------
// screenshot
// ---------------------------------------------------------------------------

void McpServer_Test::screenshotSpatialViewNotOpen()
{
    // grabSpatialViewWindow stub returns null QImage → should get "not open" error
    auto resp = toolCall("screenshot", {{"window", "3d"}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("not open") || contentText(resp).contains("open"));
}

void McpServer_Test::screenshotNoMainWindow()
{
    // m_window is nullptr in our test server → "No window available"
    auto resp = toolCall("screenshot", {{"window", "main"}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("window") || contentText(resp).contains("available"));
}

// ---------------------------------------------------------------------------
// click
// ---------------------------------------------------------------------------

void McpServer_Test::clickNoWindow()
{
    // resolveWindow("main") returns m_window which is nullptr → not found error
    auto resp = toolCall("click", {{"x", 100}, {"y", 200}});
    // null m_window → resolveWindow returns null → error
    QVERIFY(isToolError(resp));
}

// ---------------------------------------------------------------------------
// select_fixture
// ---------------------------------------------------------------------------

void McpServer_Test::selectFixtureSuccess()
{
    auto resp = toolCall("select_fixture", {{"id", 3}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSelectedFixtureId, 3);
    QCOMPARE(McpTestStubs::lastSelectAdd, false);
}

void McpServer_Test::selectFixtureDeselect()
{
    auto resp = toolCall("select_fixture", {{"id", -1}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSelectedFixtureId, -1);
}

void McpServer_Test::selectFixtureAdd()
{
    auto resp = toolCall("select_fixture", {{"id", 2}, {"add", true}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSelectedFixtureId, 2);
    QCOMPARE(McpTestStubs::lastSelectAdd, true);
}

// ---------------------------------------------------------------------------
// set_gizmo_mode
// ---------------------------------------------------------------------------

void McpServer_Test::setGizmoModeTranslate()
{
    auto resp = toolCall("set_gizmo_mode", {{"mode", 0}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastGizmoMode, 0);
    QVERIFY(contentText(resp).contains("Translate"));
}

void McpServer_Test::setGizmoModeRotate()
{
    auto resp = toolCall("set_gizmo_mode", {{"mode", 1}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastGizmoMode, 1);
    QVERIFY(contentText(resp).contains("Rotate"));
}

// ---------------------------------------------------------------------------
// align_selection
// ---------------------------------------------------------------------------

void McpServer_Test::alignSelectionX()
{
    auto resp = toolCall("align_selection", {{"axis", "X"}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastAlignAxis, QString("X"));
}

void McpServer_Test::alignSelectionY()
{
    auto resp = toolCall("align_selection", {{"axis", "Y"}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastAlignAxis, QString("Y"));
}

void McpServer_Test::alignSelectionZ()
{
    auto resp = toolCall("align_selection", {{"axis", "Z"}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastAlignAxis, QString("Z"));
}

// ---------------------------------------------------------------------------
// set_spatial_mode
// ---------------------------------------------------------------------------

void McpServer_Test::setSpatialModeLayout()
{
    auto resp = toolCall("set_spatial_mode", {{"mode", 0}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSpatialMode, 0);
    QVERIFY(contentText(resp).contains("Layout"));
}

void McpServer_Test::setSpatialModeCalibrate()
{
    auto resp = toolCall("set_spatial_mode", {{"mode", 1}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSpatialMode, 1);
    QVERIFY(contentText(resp).contains("Calibrate"));
}

void McpServer_Test::setSpatialModeFocus()
{
    auto resp = toolCall("set_spatial_mode", {{"mode", 2}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSpatialMode, 2);
    QVERIFY(contentText(resp).contains("Focus"));
}

void McpServer_Test::setSpatialModeLive()
{
    auto resp = toolCall("set_spatial_mode", {{"mode", 3}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastSpatialMode, 3);
    QVERIFY(contentText(resp).contains("Live"));
}

// ---------------------------------------------------------------------------
// set_camera
// ---------------------------------------------------------------------------

void McpServer_Test::setCameraPresetFoh()
{
    auto resp = toolCall("set_camera", {{"preset", "foh"}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastCameraYaw,   -90.0f);
    QCOMPARE(McpTestStubs::lastCameraPitch,  30.0f);
}

void McpServer_Test::setCameraPresetTop()
{
    auto resp = toolCall("set_camera", {{"preset", "top"}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastCameraPitch, 89.0f);
}

void McpServer_Test::setCameraExplicit()
{
    auto resp = toolCall("set_camera", {{"yaw", 45.0}, {"pitch", 15.0}, {"distance", 8.0}});
    QVERIFY(!isToolError(resp));
    QCOMPARE(McpTestStubs::lastCameraYaw,      45.0f);
    QCOMPARE(McpTestStubs::lastCameraPitch,    15.0f);
    QCOMPARE(McpTestStubs::lastCameraDistance,  8.0f);
}

// ---------------------------------------------------------------------------
// Focus points
// ---------------------------------------------------------------------------

void McpServer_Test::createFocusPointSuccess()
{
    McpTestStubs::nextFocusPointId = "fp2";
    auto resp = toolCall("create_focus_point", {{"x",1.0},{"y",0.0},{"z",2.0},{"name","Stage L"}});
    QVERIFY(!isToolError(resp));
    QVERIFY(contentText(resp).contains("fp2"));
}

void McpServer_Test::createFocusPointUnavailable()
{
    McpTestStubs::nextFocusPointId = "";  // stub signals unavailable
    auto resp = toolCall("create_focus_point", {{"x",0.0},{"y",0.0},{"z",1.0}});
    QVERIFY(isToolError(resp));
}

void McpServer_Test::listFocusPointsEmpty()
{
    McpTestStubs::focusPointsList = QJsonArray{};
    auto resp = toolCall("list_focus_points");
    QVERIFY(!isToolError(resp));
    QJsonObject result = resp.value("result").toObject();
    QCOMPARE(result.value("points").toArray().size(), 0);
}

void McpServer_Test::listFocusPointsWithPoints()
{
    McpTestStubs::focusPointsList = QJsonArray{
        QJsonObject{{"id","fp0"},{"name","DS Center"},{"x",0.0},{"y",0.0},{"z",0.9}}
    };
    auto resp = toolCall("list_focus_points");
    QVERIFY(!isToolError(resp));
    QJsonObject result = resp.value("result").toObject();
    QCOMPARE(result.value("points").toArray().size(), 1);
}

void McpServer_Test::deleteFocusPointSuccess()
{
    McpTestStubs::focusPointOpSucceeds = true;
    auto resp = toolCall("delete_focus_point", {{"id","fp0"}});
    QVERIFY(!isToolError(resp));
}

void McpServer_Test::deleteFocusPointNotFound()
{
    McpTestStubs::focusPointOpSucceeds = false;
    auto resp = toolCall("delete_focus_point", {{"id","fp99"}});
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("fp99"));
}

void McpServer_Test::moveFocusPointSuccess()
{
    auto resp = toolCall("move_focus_point", {{"id","fp0"},{"x",2.0},{"y",0.0},{"z",1.5}});
    QVERIFY(!isToolError(resp));
}

void McpServer_Test::moveFocusPointNotFound()
{
    McpTestStubs::focusPointOpSucceeds = false;
    auto resp = toolCall("move_focus_point", {{"id","fp99"},{"x",0.0},{"y",0.0},{"z",0.0}});
    QVERIFY(isToolError(resp));
}

void McpServer_Test::renameFocusPointSuccess()
{
    auto resp = toolCall("rename_focus_point", {{"id","fp0"},{"name","New Name"}});
    QVERIFY(!isToolError(resp));
    QVERIFY(contentText(resp).contains("New Name"));
}

void McpServer_Test::assignFixtureSuccess()
{
    auto resp = toolCall("assign_fixture_to_focus_point", {{"id","fp0"},{"fixtureId",1}});
    QVERIFY(!isToolError(resp));
}

void McpServer_Test::unassignFixtureSuccess()
{
    auto resp = toolCall("unassign_fixture_from_focus_point", {{"id","fp0"},{"fixtureId",1}});
    QVERIFY(!isToolError(resp));
}

void McpServer_Test::aimAtFocusPointSuccess()
{
    auto resp = toolCall("aim_at_focus_point", {{"id","fp0"}});
    QVERIFY(!isToolError(resp));
}

void McpServer_Test::aimAtFocusPointNotFound()
{
    McpTestStubs::focusPointOpSucceeds = false;
    auto resp = toolCall("aim_at_focus_point", {{"id","fp99"}});
    QVERIFY(isToolError(resp));
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

void McpServer_Test::calibrateAddObsHeight()
{
    auto resp = toolCall("calibrate_add_obs",
                         {{"type","height"},{"fixtureId",0},{"value",3.5}});
    QVERIFY(!isToolError(resp));
    QVERIFY(contentText(resp).contains("height") || contentText(resp).contains("Z="));
}

void McpServer_Test::calibrateAddObsDistance()
{
    auto resp = toolCall("calibrate_add_obs",
                         {{"type","distance"},{"fixtureIdA",0},{"fixtureIdB",1},{"value",4.2}});
    QVERIFY(!isToolError(resp));
    QVERIFY(contentText(resp).contains("distance") || contentText(resp).contains("4.2"));
}

void McpServer_Test::calibrateAddObsUnknownType()
{
    auto resp = toolCall("calibrate_add_obs", {{"type","bogus"},{"value",1.0}});
    QVERIFY(isToolError(resp));
}

void McpServer_Test::calibrateRunSolveNoObservations()
{
    // Fresh doc has no observations → expect error
    auto resp = toolCall("calibrate_run_solve");
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("observation") || contentText(resp).contains("Add"));
}

// ---------------------------------------------------------------------------
// Unknown tool
// ---------------------------------------------------------------------------

void McpServer_Test::unknownToolReturnsError()
{
    auto resp = toolCall("does_not_exist");
    QVERIFY(isToolError(resp));
    QVERIFY(contentText(resp).contains("does_not_exist") ||
            contentText(resp).contains("Unknown"));
}

QTEST_MAIN(McpServer_Test)
