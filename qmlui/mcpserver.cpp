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

    // Must run on GUI thread
    QTimer::singleShot(0, this, [this]() {
        showSpatialViewWindow(m_doc);
    });

    return QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", "Spatial View opened"}
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
