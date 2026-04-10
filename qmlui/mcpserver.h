/*
  Q Light Controller Plus
  mcpserver.h

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

#ifndef MCPSERVER_H
#define MCPSERVER_H

#include <QAbstractHttpServer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <functional>

class QWindow;
class QQuickWindow;
class Doc;

/**
 * Minimal HTTP server for the MCP (Model Context Protocol).
 *
 * Subclasses QAbstractHttpServer directly instead of using QHttpServer
 * because QHttpServer's router has a static initialization order crash
 * when Qt3D modules are also loaded (the defaultConverters QHash gets
 * corrupted by framework load ordering on macOS).
 */
class McpHttpServer : public QAbstractHttpServer
{
    Q_OBJECT
public:
    using Handler = std::function<QByteArray(const QHttpServerRequest &)>;
    explicit McpHttpServer(QObject *parent = nullptr);

    void setPostHandler(Handler handler);
    void setGetHandler(Handler handler);
    void setDeleteHandler(Handler handler);

protected:
    bool handleRequest(const QHttpServerRequest &request,
                       QHttpServerResponder &responder) override;
    void missingHandler(const QHttpServerRequest &request,
                        QHttpServerResponder &responder) override;

private:
    Handler m_postHandler;
    Handler m_getHandler;
    Handler m_deleteHandler;
};

class McpServer : public QObject
{
    Q_OBJECT
public:
    explicit McpServer(QQuickWindow *window, Doc *doc, quint16 port = 9876, QObject *parent = nullptr);
    ~McpServer();

    bool start();
    void stop();

    struct ToolDef {
        QString name;
        QString description;
        QJsonObject inputSchema;
        std::function<QJsonObject(const QJsonObject &args)> handler;
    };

    void registerTool(const ToolDef &tool);

signals:
    void started(quint16 port);
    void error(const QString &message);

private:
    void registerBuiltinTools();

    // JSON-RPC dispatch
    QByteArray handlePost(const QHttpServerRequest &request);
    QJsonObject dispatch(const QJsonObject &request, const QString &sessionId);
    QJsonObject handleInitialize(const QJsonObject &params);
    QJsonObject handleToolsList(const QJsonObject &params);
    QJsonObject handleToolsCall(const QJsonObject &params);
    QJsonObject handlePing();

    // Built-in tools
    QJsonObject toolScreenshot(const QJsonObject &args);
    QJsonObject toolClick(const QJsonObject &args);
    QJsonObject toolTypeText(const QJsonObject &args);
    QJsonObject toolFindElement(const QJsonObject &args);
    QJsonObject toolGetDmxValues(const QJsonObject &args);
    QJsonObject toolListFixtures(const QJsonObject &args);
    QJsonObject toolListFunctions(const QJsonObject &args);
    QJsonObject toolShowSpatialView(const QJsonObject &args);
    QJsonObject toolSelectFixture(const QJsonObject &args);
    QJsonObject toolGetFixtureScreenPositions(const QJsonObject &args);
    QJsonObject toolSetCamera(const QJsonObject &args);
    QJsonObject toolDrag(const QJsonObject &args);
    QJsonObject toolSetGizmoMode(const QJsonObject &args);
    QJsonObject toolAlignSelection(const QJsonObject &args);
    QJsonObject toolAddTruss(const QJsonObject &args);
    QJsonObject toolSetSpatialMode(const QJsonObject &args);
    QJsonObject toolCalibrateAddObs(const QJsonObject &args);
    QJsonObject toolCalibrateRunSolve(const QJsonObject &args);

    // Helpers
    QJsonObject makeResult(int id, const QJsonObject &result);
    QJsonObject makeError(int id, int code, const QString &message);
    QWindow *resolveWindow(const QJsonObject &args, QString &outName);

    McpHttpServer *m_httpServer = nullptr;
    QQuickWindow *m_window = nullptr;
    Doc *m_doc = nullptr;
    quint16 m_port;

    QMap<QString, QJsonObject> m_sessions;
    QVector<ToolDef> m_tools;
};

#endif // MCPSERVER_H
