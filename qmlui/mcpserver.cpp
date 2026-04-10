/*
  Q Light Controller Plus
  mcpserver.cpp

  Copyright (c) QLC+ contributors

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

#include "mcpserver.h"

#include <QtCore/QBuffer>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTest>
#include <QUuid>

#include "doc.h"
#include "spatialviewwindow.h"
#include "fixture.h"
#include "function.h"
#include "qlcfixturedef.h"
#include "universe.h"
#include "inputoutputmap.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>

// ---------------------------------------------------------------------------
// McpHttpServer — minimal QAbstractHttpServer subclass
// Bypasses QHttpServer's router which crashes when Qt3D modules are loaded
// (static initialization order fiasco with defaultConverters QHash).
// ---------------------------------------------------------------------------

McpHttpServer::McpHttpServer(QObject *parent)
    : QAbstractHttpServer(parent)
{
}

void McpHttpServer::setPostHandler(Handler handler) { m_postHandler = std::move(handler); }
void McpHttpServer::setGetHandler(Handler handler) { m_getHandler = std::move(handler); }
void McpHttpServer::setDeleteHandler(Handler handler) { m_deleteHandler = std::move(handler); }

bool McpHttpServer::handleRequest(const QHttpServerRequest &request,
                                   QHttpServerResponder &responder)
{
    if (request.url().path() != "/mcp")
        return false;

    auto method = request.method();
    QByteArray responseBody;

    if (method == QHttpServerRequest::Method::Post && m_postHandler) {
        responseBody = m_postHandler(request);
        if (responseBody.isEmpty()) {
            responder.sendResponse(QHttpServerResponse(
                QHttpServerResponse::StatusCode::Accepted));
        } else {
            responder.sendResponse(QHttpServerResponse(
                "application/json", responseBody));
        }
        return true;
    }
    if (method == QHttpServerRequest::Method::Get) {
        responder.sendResponse(QHttpServerResponse(
            QHttpServerResponse::StatusCode::MethodNotAllowed));
        return true;
    }
    if (method == QHttpServerRequest::Method::Delete && m_deleteHandler) {
        m_deleteHandler(request);
        responder.sendResponse(QHttpServerResponse(
            QHttpServerResponse::StatusCode::Ok));
        return true;
    }

    return false;
}

void McpHttpServer::missingHandler(const QHttpServerRequest &,
                                    QHttpServerResponder &responder)
{
    responder.sendResponse(QHttpServerResponse(
        QHttpServerResponse::StatusCode::NotFound));
}

// ---------------------------------------------------------------------------
// McpServer
// ---------------------------------------------------------------------------

McpServer::McpServer(QQuickWindow *window, Doc *doc, quint16 port, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_doc(doc)
    , m_port(port)
{
    registerBuiltinTools();
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::start()
{
    m_httpServer = new McpHttpServer(this);

    m_httpServer->setPostHandler([this](const QHttpServerRequest &req) {
        return handlePost(req);
    });

    m_httpServer->setDeleteHandler([this](const QHttpServerRequest &req) {
        auto headers = req.headers();
        if (headers.contains("Mcp-Session-Id")) {
            QString sid = QString::fromUtf8(headers.value("Mcp-Session-Id"));
            m_sessions.remove(sid);
            qInfo() << "McpServer: session deleted:" << sid;
        }
        return QByteArray();
    });

    auto *tcpServer = new QTcpServer(m_httpServer);
    if (!tcpServer->listen(QHostAddress::LocalHost, m_port)) {
        qWarning() << "McpServer: failed to listen on port" << m_port;
        emit error(QString("Failed to listen on port %1").arg(m_port));
        return false;
    }
    m_port = tcpServer->serverPort();
    m_httpServer->bind(tcpServer);

    qInfo() << "McpServer: listening on http://localhost:" << m_port << "/mcp";
    emit started(m_port);
    return true;
}

void McpServer::stop()
{
}

void McpServer::registerTool(const ToolDef &tool)
{
    m_tools.append(tool);
}

QByteArray McpServer::handlePost(const QHttpServerRequest &request)
{
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QJsonObject err = makeError(0, -32700, "Parse error: " + parseError.errorString());
        return QJsonDocument(err).toJson(QJsonDocument::Compact);
    }

    // Extract session ID from header
    QString sessionId;
    auto headers = request.headers();
    if (headers.contains("Mcp-Session-Id")) {
        sessionId = QString::fromUtf8(headers.value("Mcp-Session-Id"));
    }

    QJsonObject requestObj = jsonDoc.object();
    QString method = requestObj.value("method").toString();

    // Handle notifications (no id) — return empty (triggers 202)
    if (!requestObj.contains("id")) {
        if (method == "notifications/initialized") {
            qInfo() << "McpServer: client initialized";
        }
        return QByteArray();
    }

    // Validate session for post-init requests
    if (!sessionId.isEmpty() && !m_sessions.contains(sessionId) && method != "initialize") {
        QJsonObject err = makeError(requestObj.value("id").toInt(), -32600, "Invalid session");
        return QJsonDocument(err).toJson(QJsonDocument::Compact);
    }

    // Dispatch
    QJsonObject response = dispatch(requestObj, sessionId);

    // For initialize, create session (note: can't set response headers from here,
    // but the session ID is returned in the JSON body for now)
    if (method == "initialize" && response.contains("result")) {
        QString newSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_sessions.insert(newSessionId, QJsonObject{});
        // Embed session ID in result for client to extract
        QJsonObject result = response["result"].toObject();
        result["_sessionId"] = newSessionId;
        response["result"] = result;
        qInfo() << "McpServer: session created:" << newSessionId;
    }

    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QJsonObject McpServer::dispatch(const QJsonObject &request, const QString &sessionId)
{
    Q_UNUSED(sessionId);
    int id = request.value("id").toInt();
    QString method = request.value("method").toString();
    QJsonObject params = request.value("params").toObject();

    if (method == "initialize")
        return makeResult(id, handleInitialize(params));
    else if (method == "ping")
        return makeResult(id, handlePing());
    else if (method == "tools/list")
        return makeResult(id, handleToolsList(params));
    else if (method == "tools/call")
        return makeResult(id, handleToolsCall(params));
    else
        return makeError(id, -32601, "Method not found: " + method);
}

QJsonObject McpServer::handleInitialize(const QJsonObject &params)
{
    Q_UNUSED(params);
    return QJsonObject{
        {"protocolVersion", "2025-03-26"},
        {"capabilities", QJsonObject{
            {"tools", QJsonObject{{"listChanged", true}}}
        }},
        {"serverInfo", QJsonObject{
            {"name", "qlcplus-mcp"},
            {"version", "0.1.0"}
        }}
    };
}

QJsonObject McpServer::handlePing()
{
    return QJsonObject{};
}

QJsonObject McpServer::handleToolsList(const QJsonObject &params)
{
    Q_UNUSED(params);
    QJsonArray toolsArray;
    for (const auto &tool : m_tools) {
        toolsArray.append(QJsonObject{
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema}
        });
    }
    return QJsonObject{{"tools", toolsArray}};
}

QJsonObject McpServer::handleToolsCall(const QJsonObject &params)
{
    QString name = params.value("name").toString();
    QJsonObject args = params.value("arguments").toObject();

    for (const auto &tool : m_tools) {
        if (tool.name == name) {
            return tool.handler(args);
        }
    }

    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", "Unknown tool: " + name}
        }}},
        {"isError", true}
    };
}

// ---------------------------------------------------------------------------
// Built-in Tools
// ---------------------------------------------------------------------------

void McpServer::registerBuiltinTools()
{
    // --- Visual & Input ---

    registerTool({
        "screenshot",
        "Capture a screenshot of the QLC+ application window. Returns a base64-encoded PNG image.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"window", QJsonObject{
                    {"type", "string"},
                    {"enum", QJsonArray{"main", "3d"}},
                    {"default", "main"},
                    {"description", "Which window to capture (main QML or 3D spatial view)"}
                }}
            }},
        },
        [this](const QJsonObject &args) { return toolScreenshot(args); }
    });

    registerTool({
        "click",
        "Click at (x, y) coordinates in a QLC+ window. Coordinates are in logical pixels (not retina/device pixels).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"x", QJsonObject{{"type", "number"}, {"description", "X coordinate (logical pixels)"}}},
                {"y", QJsonObject{{"type", "number"}, {"description", "Y coordinate (logical pixels)"}}},
                {"button", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"left", "right", "middle"}}, {"default", "left"}}},
                {"window", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"main", "3d"}}, {"default", "main"}, {"description", "Target window"}}},
            }},
            {"required", QJsonArray{"x", "y"}}
        },
        [this](const QJsonObject &args) { return toolClick(args); }
    });

    registerTool({
        "type_text",
        "Type text or press a key combination in the QLC+ window.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"text", QJsonObject{{"type", "string"}, {"description", "Text to type"}}},
                {"key", QJsonObject{{"type", "string"}, {"description", "Key to press, e.g. Return, Escape, Tab, Up, Down"}}},
            }},
        },
        [this](const QJsonObject &args) { return toolTypeText(args); }
    });

    registerTool({
        "find_element",
        "Find QML UI elements by objectName. Returns position, size, visibility, and requested properties.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"objectName", QJsonObject{{"type", "string"}, {"description", "objectName to search for"}}},
                {"properties", QJsonObject{
                    {"type", "array"},
                    {"items", QJsonObject{{"type", "string"}}},
                    {"description", "Property names to include in result"}
                }},
            }},
        },
        [this](const QJsonObject &args) { return toolFindElement(args); }
    });

    // --- App State ---

    registerTool({
        "get_dmx_values",
        "Read current DMX channel values for a universe.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"universe", QJsonObject{{"type", "integer"}, {"default", 0}, {"description", "Universe index (0-based)"}}},
                {"startChannel", QJsonObject{{"type", "integer"}, {"default", 1}, {"description", "First channel (1-based)"}}},
                {"count", QJsonObject{{"type", "integer"}, {"default", 512}, {"description", "Number of channels to read"}}},
            }},
        },
        [this](const QJsonObject &args) { return toolGetDmxValues(args); }
    });

    registerTool({
        "list_fixtures",
        "List all fixtures in the current workspace with their properties.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        },
        [this](const QJsonObject &args) { return toolListFixtures(args); }
    });

    registerTool({
        "list_functions",
        "List all functions (scenes, chasers, etc.) in the current workspace.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        },
        [this](const QJsonObject &args) { return toolListFunctions(args); }
    });

    registerTool({
        "show_spatial_view",
        "Open the 3D Spatial View window. Programmatic — does not require clicking a UI button.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        },
        [this](const QJsonObject &args) { return toolShowSpatialView(args); }
    });

    registerTool({
        "select_fixture",
        "Select a fixture by ID in the Spatial View. id=-1 to deselect all. add=true for Shift+click (multi-select).",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"id", QJsonObject{{"type", "integer"}, {"description", "Fixture ID to select, or -1 to deselect all"}}},
                {"add", QJsonObject{{"type", "boolean"}, {"default", false}, {"description", "Add to selection (Shift+click) instead of replacing"}}},
            }},
            {"required", QJsonArray{"id"}}
        },
        [this](const QJsonObject &args) { return toolSelectFixture(args); }
    });

    registerTool({
        "get_fixture_screen_positions",
        "Get screen positions of all fixtures projected through the current camera. Returns logical pixel coordinates in the Spatial View viewport. Camera-independent — always returns current positions.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        },
        [this](const QJsonObject &args) { return toolGetFixtureScreenPositions(args); }
    });

    registerTool({
        "set_camera",
        "Set the Spatial View camera. Use preset names or explicit yaw/pitch/distance values.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"preset", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"foh", "top", "front", "side"}},
                    {"description", "Camera preset name"}}},
                {"yaw", QJsonObject{{"type", "number"}, {"description", "Yaw angle in degrees"}}},
                {"pitch", QJsonObject{{"type", "number"}, {"description", "Pitch angle in degrees"}}},
                {"distance", QJsonObject{{"type", "number"}, {"description", "Camera distance in meters"}}},
            }},
        },
        [this](const QJsonObject &args) { return toolSetCamera(args); }
    });

    registerTool({
        "drag",
        "Simulate a mouse drag in the Spatial View viewport. Coordinates are in logical pixels. Use for gizmo drag testing.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"x1", QJsonObject{{"type", "number"}, {"description", "Start X (logical pixels)"}}},
                {"y1", QJsonObject{{"type", "number"}, {"description", "Start Y (logical pixels)"}}},
                {"x2", QJsonObject{{"type", "number"}, {"description", "End X (logical pixels)"}}},
                {"y2", QJsonObject{{"type", "number"}, {"description", "End Y (logical pixels)"}}},
                {"steps", QJsonObject{{"type", "integer"}, {"default", 10}, {"description", "Number of intermediate move events"}}},
                {"window", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"main", "3d"}}, {"default", "3d"}}},
            }},
            {"required", QJsonArray{"x1", "y1", "x2", "y2"}}
        },
        [this](const QJsonObject &args) { return toolDrag(args); }
    });

    registerTool({
        "set_gizmo_mode",
        "Switch gizmo tool: 0=Translate (Move), 1=Rotate. Keyboard shortcuts: W=Move, E=Rotate.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"mode", QJsonObject{{"type", "integer"}, {"enum", QJsonArray{0, 1}},
                    {"description", "0=Translate, 1=Rotate"}}},
            }},
            {"required", QJsonArray{"mode"}}
        },
        [this](const QJsonObject &args) { return toolSetGizmoMode(args); }
    });

    registerTool({
        "align_selection",
        "Align all selected fixtures on an axis. Sets all to the primary fixture's coordinate. Requires 2+ fixtures selected.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"axis", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"X", "Y", "Z"}},
                    {"description", "Axis to align on"}}},
            }},
            {"required", QJsonArray{"axis"}}
        },
        [this](const QJsonObject &args) { return toolAlignSelection(args); }
    });

    registerTool({
        "add_truss",
        "Add a truss/pipe to the 3D scene. Fixtures snap to trusses when dragged nearby. Specify start and end points in meters.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"x1", QJsonObject{{"type", "number"}, {"default", -3.0}, {"description", "Start X (m)"}}},
                {"y1", QJsonObject{{"type", "number"}, {"default", 0.0}, {"description", "Start Y (m)"}}},
                {"z1", QJsonObject{{"type", "number"}, {"default", 3.0}, {"description", "Start Z (m)"}}},
                {"x2", QJsonObject{{"type", "number"}, {"default", 3.0}, {"description", "End X (m)"}}},
                {"y2", QJsonObject{{"type", "number"}, {"default", 0.0}, {"description", "End Y (m)"}}},
                {"z2", QJsonObject{{"type", "number"}, {"default", 3.0}, {"description", "End Z (m)"}}},
                {"name", QJsonObject{{"type", "string"}, {"description", "Truss name"}}},
            }},
        },
        [this](const QJsonObject &args) { return toolAddTruss(args); }
    });

    registerTool({
        "set_spatial_mode",
        "Set the Spatial View panel mode. 0=Layout, 1=Calibrate, 2=Focus, 3=Live.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"mode", QJsonObject{{"type", "integer"}, {"description", "Mode index: 0=Layout 1=Calibrate 2=Focus 3=Live"}}},
            }},
            {"required", QJsonArray{"mode"}}
        },
        [this](const QJsonObject &args) { return toolSetSpatialMode(args); }
    });
}

// Find a window by title substring from the application's window list.
// Used to locate the Spatial View popup (title: "QLC+ Spatial View").
static QWindow *findWindowByTitle(const QString &titleSubstr)
{
    for (QWindow *w : QGuiApplication::topLevelWindows())
    {
        // Match by exact title and QWidgetWindow type to avoid matching
        // terminal windows that happen to contain the same text
        if (w->isVisible()
            && w->title() == titleSubstr
            && QString::fromUtf8(w->metaObject()->className()) == "QWidgetWindow")
            return w;
    }
    return nullptr;
}

// Find the embedded SpatialView QWindow (child of the QWidgetWindow).
// createWindowContainer makes it a child window, so it appears in allWindows but not topLevelWindows.
static QWindow *findSpatialViewWindow()
{
    for (QWindow *w : QGuiApplication::allWindows())
    {
        // SpatialView is a QWindow with parent (embedded), class name "SpatialView"
        if (w->parent() && QString::fromUtf8(w->metaObject()->className()) == "SpatialView"
            && w->isVisible())
            return w;
    }
    return nullptr;
}

// Resolve which window to target based on the "window" arg.
// Returns the QWindow* and a human-readable name.
QWindow *McpServer::resolveWindow(const QJsonObject &args, QString &outName)
{
    QString which = args.value("window").toString("main");

    if (which == "3d" || which == "spatial")
    {
        outName = "spatial";
        // For clicks: use the embedded SpatialView QWindow (receives mouse events)
        // For screenshots: use the QWidgetWindow (captures the whole window including QML panel)
        // The caller differentiates by checking the "forClick" flag.
        // Default: try embedded SpatialView first, fall back to QWidgetWindow.
        QWindow *embedded = findSpatialViewWindow();
        if (embedded)
            return embedded;
        return findWindowByTitle("QLC+ Spatial View");
    }

    outName = "main";
    return m_window;
}

QJsonObject McpServer::toolScreenshot(const QJsonObject &args)
{
    QString which = args.value("window").toString("main");
    QImage image;

    if (which == "3d" || which == "spatial")
    {
        // grabSpatialViewWindow() uses QWidget::grab() — works off-screen,
        // no z-order dependency, captures viewport + QML panel together.
        image = grabSpatialViewWindow();
        if (image.isNull()) {
            return QJsonObject{
                {"content", QJsonArray{QJsonObject{{"type", "text"},
                    {"text", "Spatial View not open. Call show_spatial_view first."}}}},
                {"isError", true}
            };
        }
    }
    else
    {
        if (!m_window) {
            return QJsonObject{
                {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No window available"}}}},
                {"isError", true}
            };
        }
        image = m_window->grabWindow();
        if (image.isNull()) {
            return QJsonObject{
                {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Screenshot failed"}}}},
                {"isError", true}
            };
        }
    }

    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    buffer.close();

    QString base64 = QString::fromLatin1(pngData.toBase64());

    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "image"},
            {"data", base64},
            {"mimeType", "image/png"}
        }}}
    };
}

QJsonObject McpServer::toolClick(const QJsonObject &args)
{
    QString windowName;
    QWindow *targetWindow = resolveWindow(args, windowName);

    if (!targetWindow) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"},
                {"text", QString("Window '%1' not found. Is it open?").arg(windowName)}}}},
            {"isError", true}
        };
    }

    int x = args.value("x").toInt();
    int y = args.value("y").toInt();
    QString button = args.value("button").toString("left");
    QPoint pos(x, y);

    Qt::MouseButton btn = Qt::LeftButton;
    if (button == "right") btn = Qt::RightButton;
    else if (button == "middle") btn = Qt::MiddleButton;

    qDebug() << "[MCP click]" << windowName << "window:" << targetWindow
             << "pos:" << pos << "btn:" << btn
             << "windowSize:" << targetWindow->width() << "x" << targetWindow->height()
             << "dpr:" << targetWindow->devicePixelRatio();

    QTest::mouseClick(targetWindow, btn, Qt::NoModifier, pos);

    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", QString("Clicked at (%1, %2) on %3 window (size: %4x%5, dpr: %6)")
                .arg(x).arg(y).arg(windowName)
                .arg(targetWindow->width()).arg(targetWindow->height())
                .arg(targetWindow->devicePixelRatio())}
        }}}
    };
}

QJsonObject McpServer::toolTypeText(const QJsonObject &args)
{
    if (!m_window) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No window available"}}}},
            {"isError", true}
        };
    }

    QString text = args.value("text").toString();
    QString key = args.value("key").toString();

    if (!key.isEmpty()) {
        static const QMap<QString, Qt::Key> keyMap = {
            {"Return", Qt::Key_Return}, {"Enter", Qt::Key_Enter},
            {"Escape", Qt::Key_Escape}, {"Tab", Qt::Key_Tab},
            {"Backspace", Qt::Key_Backspace}, {"Delete", Qt::Key_Delete},
            {"Space", Qt::Key_Space},
            {"Up", Qt::Key_Up}, {"Down", Qt::Key_Down},
            {"Left", Qt::Key_Left}, {"Right", Qt::Key_Right},
        };
        Qt::Key qtKey = keyMap.value(key, Qt::Key_unknown);
        if (qtKey != Qt::Key_unknown) {
            QTest::keyClick(m_window, qtKey);
        }
    }
    if (!text.isEmpty()) {
        for (const QChar &ch : text) {
            QTest::keyClick(m_window, ch.toLatin1());
        }
    }

    QString desc;
    if (!key.isEmpty()) desc += "Pressed key: " + key + ". ";
    if (!text.isEmpty()) desc += "Typed: " + text;

    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", desc.trimmed()}
        }}}
    };
}

QJsonObject McpServer::toolFindElement(const QJsonObject &args)
{
    if (!m_window) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No window available"}}}},
            {"isError", true}
        };
    }

    QString objectName = args.value("objectName").toString();
    QJsonArray propsToInclude;
    if (args.contains("properties"))
        propsToInclude = args.value("properties").toArray();

    QJsonArray results;

    QQuickItem *root = m_window->contentItem();
    if (!root) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "[]"}}}},
        };
    }

    QList<QQuickItem*> matches;
    if (!objectName.isEmpty()) {
        matches = root->findChildren<QQuickItem*>(objectName);
    }

    for (auto *item : matches) {
        QPointF scenePos = item->mapToScene(QPointF(0, 0));
        QJsonObject entry{
            {"objectName", item->objectName()},
            {"typeName", QString::fromUtf8(item->metaObject()->className())},
            {"x", scenePos.x()},
            {"y", scenePos.y()},
            {"width", item->width()},
            {"height", item->height()},
            {"visible", item->isVisible()},
            {"enabled", item->isEnabled()},
        };

        QJsonObject props;
        for (const auto &propVal : propsToInclude) {
            QString propName = propVal.toString();
            QVariant val = item->property(propName.toUtf8().constData());
            if (val.isValid()) {
                props.insert(propName, QJsonValue::fromVariant(val));
            }
        }
        if (!props.isEmpty())
            entry.insert("properties", props);

        results.append(entry);
    }

    QString resultText = QJsonDocument(results).toJson(QJsonDocument::Indented);
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", resultText}
        }}}
    };
}

QJsonObject McpServer::toolGetDmxValues(const QJsonObject &args)
{
    if (!m_doc) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No document loaded"}}}},
            {"isError", true}
        };
    }

    int universeIdx = args.value("universe").toInt(0);
    int startCh = args.value("startChannel").toInt(1);
    int count = args.value("count").toInt(512);

    InputOutputMap *ioMap = m_doc->inputOutputMap();
    if (!ioMap) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No I/O map available"}}}},
            {"isError", true}
        };
    }

    QList<Universe*> universes = ioMap->universes();
    if (universeIdx < 0 || universeIdx >= universes.size()) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"},
                {"text", QString("Universe %1 not found (have %2)").arg(universeIdx).arg(universes.size())}}}},
            {"isError", true}
        };
    }

    const QByteArray preGM = universes.at(universeIdx)->preGMValues();

    QJsonObject values;
    int endCh = qMin(startCh + count - 1, 512);
    for (int ch = startCh; ch <= endCh; ch++) {
        int idx = ch - 1; // 0-based index
        if (idx >= 0 && idx < preGM.size()) {
            uchar val = static_cast<uchar>(preGM.at(idx));
            if (val > 0)
                values.insert(QString::number(ch), static_cast<int>(val));
        }
    }

    QJsonObject result;
    result["universe"] = universeIdx;
    result["startChannel"] = startCh;
    result["count"] = count;
    result["nonZeroValues"] = values;

    QJsonObject content;
    content["type"] = QString("text");
    content["text"] = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
    QJsonObject ret;
    ret["content"] = QJsonArray{content};
    return ret;
}

QJsonObject McpServer::toolListFixtures(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_doc) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No document loaded"}}}},
            {"isError", true}
        };
    }

    QJsonArray fixtures;
    for (Fixture *fix : m_doc->fixtures()) {
        if (!fix) continue;
        QJsonObject fObj;
        fObj["id"] = static_cast<int>(fix->id());
        fObj["name"] = fix->name();
        fObj["manufacturer"] = fix->fixtureDef() ? fix->fixtureDef()->manufacturer() : QString("Generic");
        fObj["model"] = fix->fixtureDef() ? fix->fixtureDef()->model() : QString("Unknown");
        fObj["universe"] = static_cast<int>(fix->universe());
        fObj["address"] = static_cast<int>(fix->address());
        fObj["channels"] = static_cast<int>(fix->channels());
        fixtures.append(fObj);
    }

    QJsonObject content;
    content["type"] = QString("text");
    content["text"] = QString::fromUtf8(QJsonDocument(fixtures).toJson(QJsonDocument::Indented));
    QJsonObject ret;
    ret["content"] = QJsonArray{content};
    return ret;
}

QJsonObject McpServer::toolListFunctions(const QJsonObject &args)
{
    Q_UNUSED(args);
    if (!m_doc) {
        return QJsonObject{
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "No document loaded"}}}},
            {"isError", true}
        };
    }

    QJsonArray functions;
    for (Function *func : m_doc->functions()) {
        if (!func) continue;
        QJsonObject fObj;
        fObj["id"] = static_cast<int>(func->id());
        fObj["name"] = func->name();
        fObj["type"] = func->typeString();
        fObj["running"] = func->isRunning();
        functions.append(fObj);
    }

    QJsonObject content;
    content["type"] = QString("text");
    content["text"] = QString::fromUtf8(QJsonDocument(functions).toJson(QJsonDocument::Indented));
    QJsonObject ret;
    ret["content"] = QJsonArray{content};
    return ret;
}

QJsonObject McpServer::toolShowSpatialView(const QJsonObject &args)
{
    Q_UNUSED(args);
    QTimer::singleShot(0, this, [this]() {
        showSpatialViewWindow(m_doc);
    });
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"}, {"text", "Spatial View opened"}
        }}}
    };
}

QJsonObject McpServer::toolSelectFixture(const QJsonObject &args)
{
    int id = args.value("id").toInt(-1);
    bool add = args.value("add").toBool(false);

    if (add && id >= 0)
        spatialViewAddSelectedFixture(id);
    else
        spatialViewSelectFixture(id);

    QString msg = (id >= 0) ? QString("Selected fixture %1%2").arg(id).arg(add ? " (added)" : "")
                            : "Deselected all";
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", msg}}}}
    };
}

QJsonObject McpServer::toolGetFixtureScreenPositions(const QJsonObject &args)
{
    Q_UNUSED(args);
    QJsonArray positions = spatialViewGetFixtureScreenPositions();
    QString text = QString::fromUtf8(QJsonDocument(positions).toJson(QJsonDocument::Indented));
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}
    };
}

QJsonObject McpServer::toolSetCamera(const QJsonObject &args)
{
    QString preset = args.value("preset").toString();
    float yaw, pitch, distance;

    if (preset == "foh")          { yaw = -90.0f; pitch = 30.0f;  distance = 10.0f; }
    else if (preset == "top")     { yaw = -90.0f; pitch = 89.0f;  distance = 10.0f; }
    else if (preset == "front")   { yaw = -90.0f; pitch = 0.0f;   distance = 10.0f; }
    else if (preset == "side")    { yaw = 0.0f;   pitch = 0.0f;   distance = 10.0f; }
    else
    {
        yaw = float(args.value("yaw").toDouble(-90.0));
        pitch = float(args.value("pitch").toDouble(30.0));
        distance = float(args.value("distance").toDouble(10.0));
    }

    spatialViewSetCamera(yaw, pitch, distance);
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"},
            {"text", QString("Camera: yaw=%1 pitch=%2 dist=%3").arg(yaw).arg(pitch).arg(distance)}
        }}}
    };
}

QJsonObject McpServer::toolDrag(const QJsonObject &args)
{
    float x1 = float(args.value("x1").toDouble());
    float y1 = float(args.value("y1").toDouble());
    float x2 = float(args.value("x2").toDouble());
    float y2 = float(args.value("y2").toDouble());
    int steps = args.value("steps").toInt(10);

    spatialViewDrag(x1, y1, x2, y2, steps);
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"},
            {"text", QString("Dragged from (%1,%2) to (%3,%4) in %5 steps")
                .arg(x1).arg(y1).arg(x2).arg(y2).arg(steps)}
        }}}
    };
}

QJsonObject McpServer::toolSetGizmoMode(const QJsonObject &args)
{
    int mode = args.value("mode").toInt(0);
    spatialViewSetGizmoMode(mode);
    QString name = (mode == 0) ? "Translate" : "Rotate";
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Gizmo mode: " + name}}}}
    };
}

QJsonObject McpServer::toolAlignSelection(const QJsonObject &args)
{
    QString axis = args.value("axis").toString("X");
    spatialViewAlignSelection(axis);
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Aligned selection on " + axis}}}}
    };
}

QJsonObject McpServer::toolAddTruss(const QJsonObject &args)
{
    double x1 = args.value("x1").toDouble(-3.0);
    double y1 = args.value("y1").toDouble(0.0);
    double z1 = args.value("z1").toDouble(3.0);
    double x2 = args.value("x2").toDouble(3.0);
    double y2 = args.value("y2").toDouble(0.0);
    double z2 = args.value("z2").toDouble(3.0);
    QString name = args.value("name").toString();
    spatialViewAddTruss(name, x1, y1, z1, x2, y2, z2);
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"},
            {"text", QString("Added truss from (%1,%2,%3) to (%4,%5,%6)")
                .arg(x1).arg(y1).arg(z1).arg(x2).arg(y2).arg(z2)}
        }}}
    };
}

QJsonObject McpServer::toolSetSpatialMode(const QJsonObject &args)
{
    int mode = args.value("mode").toInt(0);
    spatialViewSetMode(mode);
    QStringList modeNames = {"Layout", "Calibrate", "Focus", "Live"};
    QString name = (mode >= 0 && mode < modeNames.size()) ? modeNames[mode] : "Unknown";
    return QJsonObject{
        {"content", QJsonArray{QJsonObject{{"type", "text"},
            {"text", QString("Spatial View mode set to %1 (%2)").arg(mode).arg(name)}
        }}}
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QJsonObject McpServer::makeResult(int id, const QJsonObject &result)
{
    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

QJsonObject McpServer::makeError(int id, int code, const QString &message)
{
    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", QJsonObject{
            {"code", code},
            {"message", message}
        }}
    };
}
