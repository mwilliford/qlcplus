/*
  Q Light Controller Plus
  agentconnection.h

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

#ifndef AGENTCONNECTION_H
#define AGENTCONNECTION_H

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QAbstractSocket>
#include <QVector3D>
#include <QSet>
#include <functional>

#include "agentcontext.h"
#include "agentauthmanager.h"

class QWebSocket;
class Doc;

class AgentConnection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ stateInt NOTIFY stateChanged)
    Q_PROPERTY(QObject* authManager READ authManagerObj CONSTANT)

public:
    enum State
    {
        Disconnected,
        Authenticating,  // Auth manager is exchanging tokens
        Connecting,
        WaitingForSync,
        Connected
    };
    Q_ENUM(State)

    AgentConnection(Doc *doc, QObject *parent = nullptr);
    ~AgentConnection();

    State state() const;
    int stateInt() const { return static_cast<int>(state()); }

    Q_INVOKABLE void setAuthToken(const QString &token);
    Q_INVOKABLE QJsonArray getSessionList() const;
    Q_INVOKABLE void removeSession(const QString &sessionId);
    QString authToken() const;

    void setServerUrl(const QUrl &url);
    QUrl serverUrl() const;

    /** Request a specific graph version in workspace_sync (e.g. "v2"). Empty = server default. */
    void setGraphVersion(const QString &version) { m_graphVersion = version; }
    QString graphVersion() const { return m_graphVersion; }

    /** Access the auth manager for login/logout/config. */
    AgentAuthManager *authManager() { return &m_authManager; }
    QObject *authManagerObj() { return &m_authManager; }

    /** Set a callback to serialize Virtual Console data (UI layer bridge) */
    void setVirtualConsoleSerializer(std::function<QJsonObject()> serializer);

    /** Callback type for VC commands (create/modify/delete widgets).
     *  Parameters: command name, params JSON, result JSON (output).
     *  Returns true on success. */
    using VCCommandHandler = std::function<bool(const QString &command,
                                                const QJsonObject &params,
                                                QJsonObject &result)>;
    /** Set a callback to handle Virtual Console commands (UI layer bridge) */
    void setVCCommandHandler(VCCommandHandler handler);

    /** Send a calibration observation from the UI (e.g., crossing, position drag).
     *  Fire-and-forget — server responds with calibration_state_update. */
    void sendObservation(const QJsonObject &observation);

public slots:
    void connectToServer();
    void disconnectFromServer();
    void sendChatMessage(const QString &text);

    void sendSessionResume(const QString &sessionId);
    void sendCancel();
    void sendCompact();

signals:
    void stateChanged(AgentConnection::State state);
    void chatTokenReceived(const QString &text);
    void chatStreamEnded();
    void commandExecuting(const QString &commandType);
    void errorOccurred(const QString &message);
    void simpleDeskRequested(uint channel, uchar value);
    void simpleDeskResetChannelRequested(uint channel);
    void simpleDeskResetUniverseRequested(int universe);
    void sessionCreated(const QString &sessionId);
    void sessionHistoryReceived(const QString &sessionId, const QJsonArray &messages, bool expired);
    void sessionMetadataUpdated(const QString &sessionId);
    void sessionStatusReceived(int estimatedTokens, int contextLimit);
    void compactingStarted();
    void compactingFinished();

private slots:
    void onAuthAuthenticated(const QString &accessToken);
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &message);
    void onWsError(QAbstractSocket::SocketError error);
    void onReconnectTimer();

    // Doc signal handlers for workspace_delta
    void onDocLoading();
    void onDocLoaded();
    void onFunctionAdded(quint32 id);
    void onFunctionRemoved(quint32 id);
    void onFunctionChanged(quint32 id);
    void onFixtureAdded(quint32 id);
    void onFixtureRemoved(quint32 id);
    void onFixtureChanged(quint32 id);
    void onFixtureGroupAdded(quint32 id);
    void onFixtureGroupRemoved(quint32 id);
    void onPaletteAdded(quint32 id);
    void onPaletteRemoved(quint32 id);
    void onModeChanged();
    void onGrandMasterValueChanged(uchar value);
    void onBlackoutChanged(bool state);

    // SpatialModel signal handlers (spatial transform deltas — meters/radians)
    void onSpatialTransformChanged(const QString &fixtureId);
    void onSpatialDebounceTimeout();

private:
    void setState(State state);
    void sendJson(const QJsonObject &obj);
    void sendCommandResult(const QString &requestId, bool success,
                           const QJsonObject &extra = QJsonObject());
    void resetReconnect();

    // Workspace sync serialization
    QJsonObject buildWorkspaceSync();
    QJsonArray serializeFixtures();
    QJsonArray serializeFixtureDefs();
    QJsonArray serializeFunctions();
    QJsonArray serializeFixtureGroups();
    QJsonArray serializeChannelGroups();
    QJsonArray serializePalettes();
    QJsonObject serializeStageLayout();
    QJsonObject serializeSpatialModel();
    QJsonObject serializeGrandMaster();
    QJsonObject serializeInputOutputMap();

    // Command handlers
    void handleCreateScene(const QJsonObject &msg);
    void handleCreateChaser(const QJsonObject &msg);
    void handleCreateSequence(const QJsonObject &msg);
    void handleCreateEfx(const QJsonObject &msg);
    void handleCreateCollection(const QJsonObject &msg);
    void handleStartFunction(const QJsonObject &msg);
    void handleStopFunction(const QJsonObject &msg);
    void handleStopAll(const QJsonObject &msg);
    void handleCreateFixture(const QJsonObject &msg);
    void handleDeleteFixture(const QJsonObject &msg);
    void handleSearchFixtureLibrary(const QJsonObject &msg);
    void handleDeleteFunction(const QJsonObject &msg);
    void handleModifyFunction(const QJsonObject &msg);
    void handleSetSimpleDesk(const QJsonObject &msg);
    void handleSetGrandMaster(const QJsonObject &msg);
    void handleSetBlackout(const QJsonObject &msg);
    void handleUpdateAgentNote(const QJsonObject &msg);
    void handleCreateFixtureGroup(const QJsonObject &msg);
    void handleModifyFixtureGroup(const QJsonObject &msg);
    void handleDeleteFixtureGroup(const QJsonObject &msg);
    void handleSessionCreated(const QJsonObject &msg);
    void handleSessionHistory(const QJsonObject &msg);
    void handleUpdateSessionMetadata(const QJsonObject &msg);
    void handleGetDmxValues(const QJsonObject &msg);
    void handleResetSimpleDesk(const QJsonObject &msg);
    void handleGetRunningFunctions(const QJsonObject &msg);
    void handleCreateScript(const QJsonObject &msg);
    void handleCreatePalette(const QJsonObject &msg);
    void handleModifyPalette(const QJsonObject &msg);
    void handleDeletePalette(const QJsonObject &msg);
    void handleSetFixturePosition(const QJsonObject &msg);
    void handleSetFixtureTransform(const QJsonObject &msg);
    void handleCalibrationStateUpdate(const QJsonObject &msg);

    // Virtual Console command handlers (delegated to UI layer via callback)
    void handleVCCommand(const QJsonObject &msg, const QString &command);

    // Serialization helpers
    QJsonObject serializeAgentContext(const AgentContext &ctx);

    // Delta helpers
    void sendDelta(const QJsonArray &changes);
    QJsonObject serializeFunction(quint32 id);
    QJsonObject serializeFixture(quint32 id);
    QJsonObject serializeFixtureGroup(quint32 id);

    void connectDocSignals();
    void disconnectDocSignals();
    void proceedWithConnection();

private:
    Doc *m_doc;
    QWebSocket *m_webSocket;
    State m_state;
    QUrl m_serverUrl;
    QString m_graphVersion;     // optional: request specific graph version in workspace_sync

    QTimer m_reconnectTimer;
    int m_reconnectDelay;       // milliseconds
    bool m_wantConnection;      // user wants to be connected
    bool m_lastErrorWasAuth;    // last WS error was 403 (expired JWT)

    std::function<QJsonObject()> m_vcSerializer;
    VCCommandHandler m_vcCommandHandler;

    AgentAuthManager m_authManager;

    // Spatial transform delta debounce (meters/radians)
    QTimer m_spatialDebounce;
    QSet<QString> m_dirtySpatialTransforms;
    bool m_suppressLayoutDelta;  // true during agent command handling (avoid echo)
};

#endif // AGENTCONNECTION_H
