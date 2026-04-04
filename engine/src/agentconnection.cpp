/*
  Q Light Controller Plus
  agentconnection.cpp

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

#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QSettings>
#include <QDebug>

#include "agentconnection.h"
#include "agentsession.h"
#include "monitorproperties.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlccapability.h"
#include "inputoutputmap.h"
#include "universe.h"
#include "scriptwrapper.h"
#include "inputpatch.h"
#include "outputpatch.h"
#include "qlcinputprofile.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "grandmaster.h"
#include "mastertimer.h"
#include "scenevalue.h"
#include "chaserstep.h"
#include "collection.h"
#include "efxfixture.h"
#include "function.h"
#include "fixture.h"
#include "sequence.h"
#include "chaser.h"
#include "scene.h"
#include "channelsgroup.h"
#include "fixturegroup.h"
#include "rgbalgorithm.h"
#include "rgbmatrix.h"
#include "qlcpalette.h"
#include "efx.h"
#include "doc.h"

#define AGENT_PROTOCOL_VERSION "0.1"
#define AGENT_CLIENT_VERSION   "4.14.3"
#define RECONNECT_MAX_DELAY    30000

/*****************************************************************************
 * Initialization
 *****************************************************************************/

AgentConnection::AgentConnection(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_webSocket(nullptr)
    , m_state(Disconnected)
    , m_serverUrl(QUrl(qEnvironmentVariable("AGENT_SERVER_URL",
          QSettings().value("agent/url", "ws://localhost:8000/ws/agent").toString())))
    , m_reconnectDelay(1000)
    , m_wantConnection(false)
    , m_lastErrorWasAuth(false)
    , m_authManager(this)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &AgentConnection::onReconnectTimer);

    // Always listen for file load/clear — disconnect on workspace change
    connect(m_doc, &Doc::loading,
            this, &AgentConnection::onDocLoading);
    connect(m_doc, &Doc::loaded,
            this, &AgentConnection::onDocLoaded);

    // Auth manager signals
    connect(&m_authManager, &AgentAuthManager::authenticated,
            this, &AgentConnection::onAuthAuthenticated);

    // Fetch server config on startup (non-blocking, fail-safe)
    m_authManager.fetchServerConfig(m_serverUrl);
}

AgentConnection::~AgentConnection()
{
    m_wantConnection = false;
    m_reconnectTimer.stop();
    if (m_webSocket)
    {
        // Disconnect all signals before closing to prevent
        // onWsDisconnected() from firing during destruction
        // (m_doc may already be destroyed)
        m_webSocket->disconnect(this);
        m_webSocket->close();
        delete m_webSocket;
    }
}

/*****************************************************************************
 * State
 *****************************************************************************/

AgentConnection::State AgentConnection::state() const
{
    return m_state;
}

void AgentConnection::setState(State state)
{
    if (m_state != state)
    {
        m_state = state;
        emit stateChanged(state);
    }
}

void AgentConnection::setAuthToken(const QString &token)
{
    Q_UNUSED(token)
    // Deprecated — auth tokens are managed by AgentAuthManager.
    // Static tokens should be set via AGENT_API_TOKEN env var.
}

QString AgentConnection::authToken() const
{
    return m_authManager.accessToken();
}

QJsonArray AgentConnection::getSessionList() const
{
    QJsonArray result;
    const QList<AgentSession> &sessions = m_doc->sessions();
    // Build list newest-first
    for (int i = sessions.size() - 1; i >= 0; i--)
    {
        const AgentSession &s = sessions[i];
        QJsonObject obj;
        obj["sessionId"] = s.sessionId;
        obj["title"] = s.title;
        obj["createdAt"] = s.createdAt.toString(Qt::ISODate);
        result.append(obj);
    }
    return result;
}

void AgentConnection::removeSession(const QString &sessionId)
{
    m_doc->removeSession(sessionId);
}

void AgentConnection::setServerUrl(const QUrl &url)
{
    m_serverUrl = url;
}

QUrl AgentConnection::serverUrl() const
{
    return m_serverUrl;
}

void AgentConnection::setVirtualConsoleSerializer(std::function<QJsonObject()> serializer)
{
    m_vcSerializer = serializer;
}

void AgentConnection::setVCCommandHandler(VCCommandHandler handler)
{
    m_vcCommandHandler = handler;
}

/*****************************************************************************
 * Connection
 *****************************************************************************/

void AgentConnection::connectToServer()
{
    m_wantConnection = true;
    m_reconnectTimer.stop();
    resetReconnect();

    // If already authenticated, just connect
    if (m_authManager.authState() == AgentAuthManager::Authenticated)
    {
        proceedWithConnection();
        return;
    }

    setState(Authenticating);

    // Re-fetch config then authenticate. Config fetch is fast and non-blocking;
    // if it fails, authenticate() still works with whatever config we have.
    connect(&m_authManager, &AgentAuthManager::configFetched,
            this, [this]() {
        // Disconnect this one-shot lambda
        disconnect(&m_authManager, &AgentAuthManager::configFetched, this, nullptr);
        if (m_wantConnection)
            m_authManager.authenticate();
    });
    m_authManager.fetchServerConfig(m_serverUrl);
}

void AgentConnection::onAuthAuthenticated(const QString &accessToken)
{
    Q_UNUSED(accessToken)
    if (m_wantConnection)
        proceedWithConnection();
}

void AgentConnection::proceedWithConnection()
{
    if (m_webSocket)
    {
        m_webSocket->close();
        delete m_webSocket;
    }

    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_webSocket, &QWebSocket::connected,
            this, &AgentConnection::onWsConnected);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &AgentConnection::onWsDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &AgentConnection::onWsTextMessage);
    connect(m_webSocket, &QWebSocket::errorOccurred,
            this, &AgentConnection::onWsError);

    QNetworkRequest request(m_serverUrl);
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + m_authManager.accessToken().toUtf8());
    request.setRawHeader("X-Client-Version", AGENT_CLIENT_VERSION);
    request.setRawHeader("X-Protocol-Version", AGENT_PROTOCOL_VERSION);

    setState(Connecting);
    m_webSocket->open(request);
}

void AgentConnection::disconnectFromServer()
{
    m_wantConnection = false;
    m_reconnectTimer.stop();
    disconnectDocSignals();

    // Clean up any in-progress auth
    disconnect(&m_authManager, &AgentAuthManager::configFetched, this, nullptr);
    m_authManager.stopCallbackServer();
    if (m_authManager.authState() == AgentAuthManager::Authenticating)
        m_authManager.clearAccessToken();

    if (m_webSocket)
        m_webSocket->close();

    setState(Disconnected);
}

void AgentConnection::sendChatMessage(const QString &text)
{
    if (m_state != Connected)
        return;

    QJsonObject msg;
    msg["type"] = "chat_user";
    msg["text"] = text;
    sendJson(msg);
}

void AgentConnection::sendCancel()
{
    if (m_state != Connected)
        return;

    QJsonObject msg;
    msg["type"] = "cancel";
    sendJson(msg);
}

void AgentConnection::sendCompact()
{
    if (m_state != Connected)
        return;

    QJsonObject msg;
    msg["type"] = "compact";
    sendJson(msg);
}

/*****************************************************************************
 * WebSocket event handlers
 *****************************************************************************/

void AgentConnection::onWsConnected()
{
    qDebug() << "[AgentConnection] WebSocket connected";
    setState(WaitingForSync);
    resetReconnect();

    QJsonObject sync = buildWorkspaceSync();
    sendJson(sync);
}

void AgentConnection::onWsDisconnected()
{
    qDebug() << "[AgentConnection] WebSocket disconnected";
    disconnectDocSignals();
    setState(Disconnected);

    if (m_wantConnection)
    {
        // If the last error was auth-related (403), clear the stale JWT and
        // re-authenticate instead of blindly reconnecting with the same token.
        if (m_lastErrorWasAuth)
        {
            m_lastErrorWasAuth = false;
            qDebug() << "[AgentConnection] Auth failure — refreshing token before reconnect";
            m_authManager.clearAccessToken();
            setState(Authenticating);
            m_authManager.authenticate();
            return;
        }

        qDebug() << "[AgentConnection] Reconnecting in" << m_reconnectDelay << "ms";
        m_reconnectTimer.start(m_reconnectDelay);
        // Exponential backoff
        m_reconnectDelay = qMin(m_reconnectDelay * 2, RECONNECT_MAX_DELAY);
    }
}

void AgentConnection::onWsError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (m_webSocket)
    {
        QString errStr = m_webSocket->errorString();
        qWarning() << "[AgentConnection] WebSocket error:" << errStr;

        // Detect auth failure (HTTP 403 during WebSocket handshake)
        m_lastErrorWasAuth = errStr.contains("403");

        // Don't show auth errors to user — we handle them transparently
        if (!m_lastErrorWasAuth)
            emit errorOccurred(errStr);
    }
}

void AgentConnection::onReconnectTimer()
{
    if (m_wantConnection)
        connectToServer();
}

void AgentConnection::resetReconnect()
{
    m_reconnectDelay = 1000;
}

/*****************************************************************************
 * Message dispatch
 *****************************************************************************/

void AgentConnection::onWsTextMessage(const QString &message)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        qWarning() << "[AgentConnection] JSON parse error:" << error.errorString();
        return;
    }

    QJsonObject msg = doc.object();
    QString type = msg["type"].toString();

    if (type == "sync_ack")
    {
        QString greeting = msg["greeting"].toString();
        qDebug() << "[AgentConnection] Sync acknowledged:" << greeting;
        connectDocSignals();
        setState(Connected);
        if (!greeting.isEmpty())
            emit chatTokenReceived(greeting + "\n");
        emit chatStreamEnded();
    }
    else if (type == "chat_agent")
    {
        emit chatTokenReceived(msg["text"].toString());
    }
    else if (type == "chat_agent_end")
    {
        emit chatStreamEnded();
    }
    else if (type == "create_scene")
    {
        emit commandExecuting("create_scene");
        handleCreateScene(msg);
    }
    else if (type == "start_function")
    {
        emit commandExecuting("start_function");
        handleStartFunction(msg);
    }
    else if (type == "stop_function")
    {
        emit commandExecuting("stop_function");
        handleStopFunction(msg);
    }
    else if (type == "stop_all")
    {
        emit commandExecuting("stop_all");
        handleStopAll(msg);
    }
    else if (type == "create_fixture")
    {
        emit commandExecuting("create_fixture");
        handleCreateFixture(msg);
    }
    else if (type == "delete_fixture")
    {
        emit commandExecuting("delete_fixture");
        handleDeleteFixture(msg);
    }
    else if (type == "search_fixture_library")
    {
        handleSearchFixtureLibrary(msg);
    }
    else if (type == "delete_function")
    {
        emit commandExecuting("delete_function");
        handleDeleteFunction(msg);
    }
    else if (type == "create_chaser")
    {
        emit commandExecuting("create_chaser");
        handleCreateChaser(msg);
    }
    else if (type == "create_sequence")
    {
        emit commandExecuting("create_sequence");
        handleCreateSequence(msg);
    }
    else if (type == "create_efx")
    {
        emit commandExecuting("create_efx");
        handleCreateEfx(msg);
    }
    else if (type == "create_collection")
    {
        emit commandExecuting("create_collection");
        handleCreateCollection(msg);
    }
    else if (type == "create_script")
    {
        emit commandExecuting("create_script");
        handleCreateScript(msg);
    }
    else if (type == "modify_function")
    {
        emit commandExecuting("modify_function");
        handleModifyFunction(msg);
    }
    else if (type == "set_simple_desk")
    {
        emit commandExecuting("set_simple_desk");
        handleSetSimpleDesk(msg);
    }
    else if (type == "set_grand_master")
    {
        emit commandExecuting("set_grand_master");
        handleSetGrandMaster(msg);
    }
    else if (type == "set_blackout")
    {
        emit commandExecuting("set_blackout");
        handleSetBlackout(msg);
    }
    else if (type == "update_agent_note")
    {
        emit commandExecuting("update_agent_note");
        handleUpdateAgentNote(msg);
    }
    else if (type == "create_fixture_group")
    {
        emit commandExecuting("create_fixture_group");
        handleCreateFixtureGroup(msg);
    }
    else if (type == "modify_fixture_group")
    {
        emit commandExecuting("modify_fixture_group");
        handleModifyFixtureGroup(msg);
    }
    else if (type == "delete_fixture_group")
    {
        emit commandExecuting("delete_fixture_group");
        handleDeleteFixtureGroup(msg);
    }
    else if (type == "create_palette")
    {
        emit commandExecuting("create_palette");
        handleCreatePalette(msg);
    }
    else if (type == "modify_palette")
    {
        emit commandExecuting("modify_palette");
        handleModifyPalette(msg);
    }
    else if (type == "delete_palette")
    {
        emit commandExecuting("delete_palette");
        handleDeletePalette(msg);
    }
    else if (type == "create_widget" || type == "modify_widget" ||
             type == "delete_widget" || type == "set_widget_input" ||
             type == "clear_widget_input")
    {
        emit commandExecuting(type);
        handleVCCommand(msg, type);
    }
    else if (type == "session_created")
    {
        handleSessionCreated(msg);
    }
    else if (type == "session_history")
    {
        handleSessionHistory(msg);
    }
    else if (type == "update_session_metadata")
    {
        handleUpdateSessionMetadata(msg);
    }
    else if (type == "session_status")
    {
        int tokens = msg["estimatedTokens"].toInt();
        int limit = msg["contextLimit"].toInt();
        emit sessionStatusReceived(tokens, limit);
    }
    else if (type == "get_dmx_values")
    {
        handleGetDmxValues(msg);
    }
    else if (type == "reset_simple_desk")
    {
        emit commandExecuting("reset_simple_desk");
        handleResetSimpleDesk(msg);
    }
    else if (type == "get_running_functions")
    {
        handleGetRunningFunctions(msg);
    }
    else if (type == "set_fixture_position")
    {
        emit commandExecuting("set_fixture_position");
        handleSetFixturePosition(msg);
    }
    else if (type == "compacting")
    {
        emit compactingStarted();
    }
    else if (type == "compact_done")
    {
        emit compactingFinished();
    }
    else if (type == "error")
    {
        QString code = msg["code"].toString();
        QString message = msg["message"].toString();
        qWarning() << "[AgentConnection] Server error:" << code << message;
        emit errorOccurred(QString("%1: %2").arg(code, message));
    }
    else
    {
        qDebug() << "[AgentConnection] Unhandled message type:" << type;
    }
}

/*****************************************************************************
 * Command handlers
 *****************************************************************************/

void AgentConnection::handleCreateScene(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    QJsonArray values = msg["values"].toArray();


    Scene *scene = new Scene(m_doc);
    scene->setName(name);

    for (const QJsonValue &v : values)
    {
        QJsonObject sv = v.toObject();
        quint32 fxi = sv["fixtureId"].toInt();
        quint32 ch = sv["channel"].toInt();
        uchar val = (uchar)sv["value"].toInt();
        scene->setValue(fxi, ch, val);
    }

    bool ok = m_doc->addFunction(scene);


    if (ok)
    {
        if (msg["autoStart"].toBool())
            scene->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)scene->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add scene to document";
        sendCommandResult(requestId, false, extra);
        delete scene;
    }
}

void AgentConnection::handleStartFunction(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 fid = msg["functionId"].toInt();

    Function *fn = m_doc->function(fid);
    if (fn == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Function %1 not found").arg(fid);
        sendCommandResult(requestId, false, extra);
        return;
    }

    fn->start(m_doc->masterTimer(), FunctionParent::master());
    sendCommandResult(requestId, true);
}

void AgentConnection::handleStopFunction(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 fid = msg["functionId"].toInt();

    Function *fn = m_doc->function(fid);
    if (fn == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Function %1 not found").arg(fid);
        sendCommandResult(requestId, false, extra);
        return;
    }

    fn->stop(FunctionParent::master());
    sendCommandResult(requestId, true);
}

void AgentConnection::handleStopAll(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();

    for (Function *fn : m_doc->functions())
    {
        if (fn->isRunning())
            fn->stop(FunctionParent::master());
    }

    sendCommandResult(requestId, true);
}

void AgentConnection::handleCreateFixture(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    QString manufacturer = msg["manufacturer"].toString();
    QString model = msg["model"].toString();
    QString modeName = msg["mode"].toString();
    quint32 universe = (quint32)msg["universe"].toInt();
    quint32 dmxAddress = (quint32)msg["address"].toInt();  // 1-based from agent
    quint32 address = dmxAddress - 1;                       // convert to 0-based for engine

    // Look up fixture def from cache
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef(manufacturer, model);
    if (def == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Fixture definition not found: %1 %2").arg(manufacturer, model);
        sendCommandResult(requestId, false, extra);
        return;
    }

    // Look up mode
    QLCFixtureMode *mode = def->mode(modeName);
    if (mode == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Mode '%1' not found for %2 %3").arg(modeName, manufacturer, model);
        sendCommandResult(requestId, false, extra);
        return;
    }

    // Check address overlap with existing fixtures
    quint32 channels = mode->channels().size();
    foreach (Fixture *existing, m_doc->fixtures())
    {
        if (existing->universe() == universe)
        {
            quint32 exStart = existing->address();
            quint32 exEnd = exStart + existing->channels();
            quint32 newEnd = address + channels;
            if (address < exEnd && newEnd > exStart)
            {
                QJsonObject extra;
                extra["error"] = QString("ADDRESS_OVERLAP: range %1-%2 overlaps fixture '%3' (ID %4) at %5-%6")
                    .arg(dmxAddress).arg(dmxAddress + channels - 1)
                    .arg(existing->name()).arg(existing->id())
                    .arg(exStart + 1).arg(exEnd);
                sendCommandResult(requestId, false, extra);
                return;
            }
        }
    }

    // Create fixture
    Fixture *fixture = new Fixture(m_doc);
    fixture->setName(name);
    fixture->setFixtureDefinition(def, mode);
    fixture->setUniverse(universe);
    fixture->setAddress(address);

    bool ok = m_doc->addFixture(fixture);
    // fixtureAdded signal → onFixtureAdded → fixture_added delta auto-sent

    if (ok)
    {
        QJsonObject extra;
        extra["fixtureId"] = (int)fixture->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add fixture to document";
        sendCommandResult(requestId, false, extra);
        delete fixture;
    }
}

void AgentConnection::handleDeleteFixture(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 fixtureId = (quint32)msg["fixtureId"].toInt();

    Fixture *fxi = m_doc->fixture(fixtureId);
    if (fxi == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Fixture ID %1 does not exist").arg(fixtureId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    // Reset DMX universe address space before deletion
    QList<Universe*> ua = m_doc->inputOutputMap()->claimUniverses();
    int universe = fxi->universe();
    if (universe < ua.count())
        ua[universe]->reset(fxi->address(), fxi->channels());
    m_doc->inputOutputMap()->releaseUniverses(false);

    bool ok = m_doc->deleteFixture(fixtureId);
    // fixtureRemoved signal → onFixtureRemoved → fixture_removed delta auto-sent

    if (ok)
    {
        QJsonObject extra;
        extra["fixtureId"] = (int)fixtureId;
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = QString("Failed to delete fixture %1").arg(fixtureId);
        sendCommandResult(requestId, false, extra);
    }
}

void AgentConnection::handleSearchFixtureLibrary(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString query = msg["query"].toString().toLower();

    QJsonArray results;
    const int MAX_RESULTS = 20;

    QStringList manufacturers = m_doc->fixtureDefCache()->manufacturers();
    for (const QString &mfr : manufacturers)
    {
        if (results.count() >= MAX_RESULTS)
            break;

        QStringList models = m_doc->fixtureDefCache()->models(mfr);
        for (const QString &model : models)
        {
            if (results.count() >= MAX_RESULTS)
                break;

            // Case-insensitive substring match on manufacturer + model
            if (mfr.toLower().contains(query) || model.toLower().contains(query))
            {
                QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef(mfr, model);
                QJsonObject entry;
                entry["manufacturer"] = mfr;
                entry["model"] = model;
                if (def != nullptr)
                {
                    entry["type"] = QLCFixtureDef::typeToString(def->type());
                    QJsonArray modes;
                    for (QLCFixtureMode *mode : def->modes())
                        modes.append(mode->name());
                    entry["modes"] = modes;
                }
                results.append(entry);
            }
        }
    }

    QJsonObject extra;
    extra["results"] = results;
    sendCommandResult(requestId, true, extra);
}

void AgentConnection::handleDeleteFunction(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 functionId = (quint32)msg["functionId"].toInt();

    Function *fn = m_doc->function(functionId);
    if (fn == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Function ID %1 does not exist").arg(functionId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    // Stop the function if it's running before deleting
    if (fn->isRunning())
        fn->stop(FunctionParent::master());

    bool ok = m_doc->deleteFunction(functionId);

    if (ok)
    {
        QJsonObject extra;
        extra["functionId"] = (int)functionId;
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = QString("Failed to delete function %1").arg(functionId);
        sendCommandResult(requestId, false, extra);
    }
}

void AgentConnection::handleModifyFunction(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 functionId = (quint32)msg["functionId"].toInt();
    QJsonObject changes = msg["changes"].toObject();

    Function *fn = m_doc->function(functionId);
    if (fn == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Function ID %1 does not exist").arg(functionId);
        sendCommandResult(requestId, false, extra);
        return;
    }


    // --- Common properties (all function types) ---
    if (changes.contains("name"))
        fn->setName(changes["name"].toString());

    if (changes.contains("path"))
        fn->setPath(changes["path"].toString());

    if (changes.contains("runOrder"))
    {
        QString ro = changes["runOrder"].toString();
        if (ro == "Loop") fn->setRunOrder(Function::Loop);
        else if (ro == "SingleShot") fn->setRunOrder(Function::SingleShot);
        else if (ro == "PingPong") fn->setRunOrder(Function::PingPong);
        else if (ro == "Random") fn->setRunOrder(Function::Random);
    }

    if (changes.contains("direction"))
    {
        QString dir = changes["direction"].toString();
        if (dir == "Forward") fn->setDirection(Function::Forward);
        else if (dir == "Backward") fn->setDirection(Function::Backward);
    }

    if (changes.contains("tempoType"))
    {
        QString tt = changes["tempoType"].toString();
        if (tt == "Time") fn->setTempoType(Function::Time);
        else if (tt == "Beats") fn->setTempoType(Function::Beats);
    }

    if (changes.contains("fadeIn"))
        fn->setFadeInSpeed(changes["fadeIn"].toInt());

    if (changes.contains("fadeOut"))
        fn->setFadeOutSpeed(changes["fadeOut"].toInt());

    if (changes.contains("duration"))
        fn->setDuration(changes["duration"].toInt());

    // --- Scene-specific ---
    if (fn->type() == Function::SceneType)
    {
        Scene *scene = qobject_cast<Scene*>(fn);

        // setValues — replace ALL channel values (clear first, then set)
        if (changes.contains("setValues"))
        {
            // Clear all existing values
            QList<SceneValue> existing = scene->values();
            for (const SceneValue &sv : existing)
                scene->unsetValue(sv.fxi, sv.channel);

            QJsonArray vals = changes["setValues"].toArray();
            for (const QJsonValue &v : vals)
            {
                QJsonObject sv = v.toObject();
                quint32 fxi = sv["fixtureId"].toInt();
                quint32 ch = sv["channel"].toInt();
                uchar val = (uchar)sv["value"].toInt();
                scene->setValue(fxi, ch, val);
            }
        }

        // addValues — add or update channel values (preserves existing)
        if (changes.contains("addValues"))
        {
            QJsonArray vals = changes["addValues"].toArray();
            for (const QJsonValue &v : vals)
            {
                QJsonObject sv = v.toObject();
                quint32 fxi = sv["fixtureId"].toInt();
                quint32 ch = sv["channel"].toInt();
                uchar val = (uchar)sv["value"].toInt();
                scene->setValue(fxi, ch, val);
            }
        }

        // removeValues — remove channel values
        if (changes.contains("removeValues"))
        {
            QJsonArray vals = changes["removeValues"].toArray();
            for (const QJsonValue &v : vals)
            {
                QJsonObject sv = v.toObject();
                quint32 fxi = sv["fixtureId"].toInt();
                quint32 ch = sv["channel"].toInt();
                scene->unsetValue(fxi, ch);
            }
        }
    }

    // --- Chaser-specific ---
    else if (fn->type() == Function::ChaserType || fn->type() == Function::SequenceType)
    {
        Chaser *chaser = qobject_cast<Chaser*>(fn);

        if (changes.contains("fadeInMode"))
        {
            QString mode = changes["fadeInMode"].toString();
            if (mode == "Default") chaser->setFadeInMode(Chaser::Default);
            else if (mode == "Common") chaser->setFadeInMode(Chaser::Common);
            else if (mode == "PerStep") chaser->setFadeInMode(Chaser::PerStep);
        }

        if (changes.contains("fadeOutMode"))
        {
            QString mode = changes["fadeOutMode"].toString();
            if (mode == "Default") chaser->setFadeOutMode(Chaser::Default);
            else if (mode == "Common") chaser->setFadeOutMode(Chaser::Common);
            else if (mode == "PerStep") chaser->setFadeOutMode(Chaser::PerStep);
        }

        if (changes.contains("durationMode"))
        {
            QString mode = changes["durationMode"].toString();
            if (mode == "Default") chaser->setDurationMode(Chaser::Default);
            else if (mode == "Common") chaser->setDurationMode(Chaser::Common);
            else if (mode == "PerStep") chaser->setDurationMode(Chaser::PerStep);
        }

        // removeSteps — remove steps by index (descending to preserve indices)
        if (changes.contains("removeSteps"))
        {
            QJsonArray indices = changes["removeSteps"].toArray();
            QList<int> sorted;
            for (const QJsonValue &v : indices)
                sorted.append(v.toInt());
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());
            for (int idx : sorted)
                chaser->removeStep(idx);
        }

        // addSteps — insert steps at specified positions
        if (changes.contains("addSteps"))
        {
            QJsonArray stepsArr = changes["addSteps"].toArray();
            for (const QJsonValue &v : stepsArr)
            {
                QJsonObject stepObj = v.toObject();
                int index = stepObj["index"].toInt(-1);
                QJsonObject stepData = stepObj["step"].toObject();

                ChaserStep step;
                step.fid = stepData["functionId"].toInt();
                step.fadeIn = stepData["fadeIn"].toInt(0);
                step.hold = stepData["hold"].toInt(0);
                step.fadeOut = stepData["fadeOut"].toInt(0);
                step.duration = stepData["duration"].toInt(0);
                step.note = stepData["note"].toString();

                chaser->addStep(step, index);
            }
        }

        // modifySteps — replace steps at specified indices (in-place update)
        if (changes.contains("modifySteps"))
        {
            QJsonArray stepsArr = changes["modifySteps"].toArray();
            for (const QJsonValue &v : stepsArr)
            {
                QJsonObject stepObj = v.toObject();
                int index = stepObj["index"].toInt();
                QJsonObject stepData = stepObj["step"].toObject();

                ChaserStep step;
                step.fid = stepData["functionId"].toInt();
                step.fadeIn = stepData["fadeIn"].toInt(0);
                step.hold = stepData["hold"].toInt(0);
                step.fadeOut = stepData["fadeOut"].toInt(0);
                step.duration = stepData["duration"].toInt(0);
                step.note = stepData["note"].toString();

                chaser->replaceStep(step, index);
            }
        }

        // moveStep — reorder a step
        if (changes.contains("moveStep"))
        {
            QJsonObject move = changes["moveStep"].toObject();
            int from = move["from"].toInt();
            int to = move["to"].toInt();
            chaser->moveStep(from, to);
        }
    }

    // --- EFX-specific ---
    else if (fn->type() == Function::EFXType)
    {
        EFX *efx = qobject_cast<EFX*>(fn);

        if (changes.contains("algorithm"))
        {
            QString algo = changes["algorithm"].toString();
            efx->setAlgorithm(EFX::stringToAlgorithm(algo));
        }

        if (changes.contains("width"))
            efx->setWidth(changes["width"].toInt());
        if (changes.contains("height"))
            efx->setHeight(changes["height"].toInt());
        if (changes.contains("xOffset"))
            efx->setXOffset(changes["xOffset"].toInt());
        if (changes.contains("yOffset"))
            efx->setYOffset(changes["yOffset"].toInt());
        if (changes.contains("rotation"))
            efx->setRotation(changes["rotation"].toInt());
        if (changes.contains("startOffset"))
            efx->setStartOffset(changes["startOffset"].toInt());
        if (changes.contains("xFrequency"))
            efx->setXFrequency(changes["xFrequency"].toInt());
        if (changes.contains("yFrequency"))
            efx->setYFrequency(changes["yFrequency"].toInt());
        if (changes.contains("xPhase"))
            efx->setXPhase(changes["xPhase"].toInt());
        if (changes.contains("yPhase"))
            efx->setYPhase(changes["yPhase"].toInt());

        if (changes.contains("propagationMode"))
        {
            QString pm = changes["propagationMode"].toString();
            if (pm == "Parallel") efx->setPropagationMode(EFX::Parallel);
            else if (pm == "Serial") efx->setPropagationMode(EFX::Serial);
            else if (pm == "Asymmetric") efx->setPropagationMode(EFX::Asymmetric);
        }

        // removeFixtures — remove by fixtureId + headIndex
        if (changes.contains("removeFixtures"))
        {
            QJsonArray arr = changes["removeFixtures"].toArray();
            for (const QJsonValue &v : arr)
            {
                QJsonObject fo = v.toObject();
                quint32 fxi = fo["fixtureId"].toInt();
                int head = fo["headIndex"].toInt(0);
                efx->removeFixture(fxi, head);
            }
        }

        // addFixtures — add new fixtures
        if (changes.contains("addFixtures"))
        {
            QJsonArray arr = changes["addFixtures"].toArray();
            for (const QJsonValue &v : arr)
            {
                QJsonObject fo = v.toObject();
                quint32 fxi = fo["fixtureId"].toInt();
                int head = fo["head"].toInt(0);
                efx->addFixture(fxi, head);
            }
        }
    }

    // --- Collection-specific ---
    else if (fn->type() == Function::CollectionType)
    {
        Collection *coll = qobject_cast<Collection*>(fn);

        if (changes.contains("removeFunctions"))
        {
            QJsonArray arr = changes["removeFunctions"].toArray();
            for (const QJsonValue &v : arr)
                coll->removeFunction(v.toInt());
        }

        if (changes.contains("addFunctions"))
        {
            QJsonArray arr = changes["addFunctions"].toArray();
            for (const QJsonValue &v : arr)
                coll->addFunction(v.toInt());
        }
    }

    // --- Script-specific ---
    else if (fn->type() == Function::ScriptType)
    {
        Script *script = qobject_cast<Script*>(fn);

        if (changes.contains("data"))
            script->setData(changes["data"].toString());
    }

    QJsonObject extra;
    extra["functionId"] = (int)functionId;
    sendCommandResult(requestId, true, extra);
}

void AgentConnection::handleCreateChaser(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();


    Chaser *chaser = new Chaser(m_doc);
    chaser->setName(name);

    if (msg.contains("runOrder"))
        chaser->setRunOrder(Function::stringToRunOrder(msg["runOrder"].toString()));
    if (msg.contains("direction"))
        chaser->setDirection(Function::stringToDirection(msg["direction"].toString()));
    if (msg.contains("fadeInMode"))
        chaser->setFadeInMode(Chaser::stringToSpeedMode(msg["fadeInMode"].toString()));
    if (msg.contains("fadeOutMode"))
        chaser->setFadeOutMode(Chaser::stringToSpeedMode(msg["fadeOutMode"].toString()));
    if (msg.contains("durationMode"))
        chaser->setDurationMode(Chaser::stringToSpeedMode(msg["durationMode"].toString()));
    if (msg.contains("duration"))
        chaser->setDuration(msg["duration"].toInt());
    if (msg.contains("fadeIn"))
        chaser->setFadeInSpeed(msg["fadeIn"].toInt());
    if (msg.contains("fadeOut"))
        chaser->setFadeOutSpeed(msg["fadeOut"].toInt());
    if (msg.contains("tempoType"))
    {
        QString tt = msg["tempoType"].toString();
        if (tt == "Beats") chaser->setTempoType(Function::Beats);
        else chaser->setTempoType(Function::Time);
    }

    QJsonArray steps = msg["steps"].toArray();
    for (const QJsonValue &v : steps)
    {
        QJsonObject s = v.toObject();
        quint32 fid = s["functionId"].toInt();
        uint fadeIn = s.contains("fadeIn") ? s["fadeIn"].toInt() : 0;
        uint hold = s.contains("hold") ? s["hold"].toInt() : 0;
        uint fadeOut = s.contains("fadeOut") ? s["fadeOut"].toInt() : 0;

        ChaserStep step(fid, fadeIn, hold, fadeOut);
        if (s.contains("note"))
            step.note = s["note"].toString();
        chaser->addStep(step);
    }

    bool ok = m_doc->addFunction(chaser);

    if (ok)
    {
        if (msg["autoStart"].toBool())
            chaser->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)chaser->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add chaser to document";
        sendCommandResult(requestId, false, extra);
        delete chaser;
    }
}

void AgentConnection::handleCreateSequence(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    quint32 boundSceneId = (quint32)msg["boundSceneId"].toInt();

    // Validate bound scene exists and is a Scene
    Function *boundFn = m_doc->function(boundSceneId);
    if (boundFn == nullptr || boundFn->type() != Function::SceneType)
    {
        QJsonObject extra;
        extra["error"] = QString("Bound scene ID %1 does not exist or is not a Scene").arg(boundSceneId);
        sendCommandResult(requestId, false, extra);
        return;
    }


    Sequence *seq = new Sequence(m_doc);
    seq->setName(name);
    seq->setBoundSceneID(boundSceneId);

    if (msg.contains("path"))
        seq->setPath(msg["path"].toString());
    if (msg.contains("runOrder"))
        seq->setRunOrder(Function::stringToRunOrder(msg["runOrder"].toString()));
    if (msg.contains("direction"))
        seq->setDirection(Function::stringToDirection(msg["direction"].toString()));
    if (msg.contains("fadeInMode"))
        seq->setFadeInMode(Chaser::stringToSpeedMode(msg["fadeInMode"].toString()));
    if (msg.contains("fadeOutMode"))
        seq->setFadeOutMode(Chaser::stringToSpeedMode(msg["fadeOutMode"].toString()));
    if (msg.contains("durationMode"))
        seq->setDurationMode(Chaser::stringToSpeedMode(msg["durationMode"].toString()));
    if (msg.contains("duration"))
        seq->setDuration(msg["duration"].toInt());
    if (msg.contains("fadeIn"))
        seq->setFadeInSpeed(msg["fadeIn"].toInt());
    if (msg.contains("fadeOut"))
        seq->setFadeOutSpeed(msg["fadeOut"].toInt());

    QJsonArray steps = msg["steps"].toArray();
    for (const QJsonValue &v : steps)
    {
        QJsonObject s = v.toObject();
        uint fadeIn = s.contains("fadeIn") ? s["fadeIn"].toInt() : 0;
        uint hold = s.contains("hold") ? s["hold"].toInt() : 0;
        uint fadeOut = s.contains("fadeOut") ? s["fadeOut"].toInt() : 0;

        ChaserStep step(boundSceneId, fadeIn, hold, fadeOut);
        if (s.contains("note"))
            step.note = s["note"].toString();

        // Populate step values from the JSON
        QJsonArray vals = s["values"].toArray();
        for (const QJsonValue &vv : vals)
        {
            QJsonObject sv = vv.toObject();
            step.values.append(SceneValue(
                (quint32)sv["fixtureId"].toInt(),
                (quint32)sv["channel"].toInt(),
                (uchar)sv["value"].toInt()
            ));
        }

        seq->addStep(step);
    }

    bool ok = m_doc->addFunction(seq);

    if (ok)
    {
        if (msg["autoStart"].toBool())
            seq->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)seq->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add sequence to document";
        sendCommandResult(requestId, false, extra);
        delete seq;
    }
}

void AgentConnection::handleCreateEfx(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();


    EFX *efx = new EFX(m_doc);
    efx->setName(name);

    if (msg.contains("algorithm"))
        efx->setAlgorithm(EFX::stringToAlgorithm(msg["algorithm"].toString()));
    if (msg.contains("width"))
        efx->setWidth(msg["width"].toInt());
    if (msg.contains("height"))
        efx->setHeight(msg["height"].toInt());
    if (msg.contains("xOffset"))
        efx->setXOffset(msg["xOffset"].toInt());
    if (msg.contains("yOffset"))
        efx->setYOffset(msg["yOffset"].toInt());
    if (msg.contains("rotation"))
        efx->setRotation(msg["rotation"].toInt());
    if (msg.contains("startOffset"))
        efx->setStartOffset(msg["startOffset"].toInt());
    if (msg.contains("propagationMode"))
        efx->setPropagationMode(EFX::stringToPropagationMode(msg["propagationMode"].toString()));
    if (msg.contains("runOrder"))
        efx->setRunOrder(Function::stringToRunOrder(msg["runOrder"].toString()));
    if (msg.contains("direction"))
        efx->setDirection(Function::stringToDirection(msg["direction"].toString()));
    if (msg.contains("duration"))
        efx->setDuration(msg["duration"].toInt());
    if (msg.contains("fadeIn"))
        efx->setFadeInSpeed(msg["fadeIn"].toInt());
    if (msg.contains("fadeOut"))
        efx->setFadeOutSpeed(msg["fadeOut"].toInt());

    // Lissajous parameters
    if (msg.contains("xFrequency"))
        efx->setXFrequency(msg["xFrequency"].toInt());
    if (msg.contains("yFrequency"))
        efx->setYFrequency(msg["yFrequency"].toInt());
    if (msg.contains("xPhase"))
        efx->setXPhase(msg["xPhase"].toInt());
    if (msg.contains("yPhase"))
        efx->setYPhase(msg["yPhase"].toInt());

    QJsonArray fixtures = msg["fixtures"].toArray();
    for (const QJsonValue &v : fixtures)
    {
        QJsonObject fj = v.toObject();
        quint32 fid = fj["fixtureId"].toInt();
        int head = fj.contains("head") ? fj["head"].toInt() : 0;
        efx->addFixture(fid, head);
    }

    bool ok = m_doc->addFunction(efx);

    if (ok)
    {
        if (msg["autoStart"].toBool())
            efx->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)efx->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add EFX to document";
        sendCommandResult(requestId, false, extra);
        delete efx;
    }
}

void AgentConnection::handleCreateCollection(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();


    Collection *coll = new Collection(m_doc);
    coll->setName(name);

    QJsonArray fids = msg["functionIds"].toArray();
    for (const QJsonValue &v : fids)
        coll->addFunction(v.toInt());

    bool ok = m_doc->addFunction(coll);

    if (ok)
    {
        if (msg["autoStart"].toBool())
            coll->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)coll->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add collection to document";
        sendCommandResult(requestId, false, extra);
        delete coll;
    }
}

void AgentConnection::handleCreateScript(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    QString data = msg["data"].toString();

    Script *script = new Script(m_doc);
    script->setName(name);
    script->setData(data);

    if (msg.contains("path"))
        script->setPath(msg["path"].toString());
    if (msg.contains("runOrder"))
        script->setRunOrder(Function::stringToRunOrder(msg["runOrder"].toString()));
    if (msg.contains("direction"))
        script->setDirection(Function::stringToDirection(msg["direction"].toString()));

    // Reject scripts containing systemcommand (security risk)
    if (data.contains("systemcommand", Qt::CaseInsensitive))
    {
        QJsonObject extra;
        extra["error"] = "Scripts created via agent cannot use systemcommand (security restriction)";
        sendCommandResult(requestId, false, extra);
        delete script;
        return;
    }

    // Reject scripts with syntax errors
    auto errorLines = script->syntaxErrorsLines();
    if (!errorLines.isEmpty())
    {
        QJsonObject extra;
        QJsonArray errArr;
        for (const auto &line : errorLines)
            errArr.append(QJsonValue(line));
        extra["error"] = QString("Script has syntax errors on line(s): %1")
            .arg(errorLines.size());
        extra["syntaxErrorLines"] = errArr;
        sendCommandResult(requestId, false, extra);
        delete script;
        return;
    }

    bool ok = m_doc->addFunction(script);

    if (ok)
    {
        if (msg["autoStart"].toBool())
            script->start(m_doc->masterTimer(), FunctionParent::master());

        QJsonObject extra;
        extra["functionId"] = (int)script->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add script to document";
        sendCommandResult(requestId, false, extra);
        delete script;
    }
}

void AgentConnection::handleSetSimpleDesk(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QJsonArray values = msg["values"].toArray();

    for (const QJsonValue &v : values)
    {
        QJsonObject sv = v.toObject();
        uint universe = sv["universe"].toInt();
        uint channel = sv["channel"].toInt();
        uchar value = (uchar)sv["value"].toInt();
        uint absChannel = (universe << 9) | channel;
        emit simpleDeskRequested(absChannel, value);
    }

    sendCommandResult(requestId, true);
}

void AgentConnection::handleSetGrandMaster(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    uchar value = (uchar)msg["value"].toInt();

    m_doc->inputOutputMap()->setGrandMasterValue(value);

    sendCommandResult(requestId, true);
}

void AgentConnection::handleSetBlackout(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    bool blackout = msg["blackout"].toBool();

    m_doc->inputOutputMap()->setBlackout(blackout);

    sendCommandResult(requestId, true);
}

void AgentConnection::handleUpdateAgentNote(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QJsonObject target = msg["target"].toObject();
    QString targetType = target["type"].toString();
    QString agentNote = msg["agentNote"].toString();
    bool hasStructuredData = msg.contains("structuredData");
    QJsonObject structuredData;
    if (hasStructuredData)
        structuredData = msg["structuredData"].toObject();
    bool success = false;
    QString error;

    if (targetType == "fixture")
    {
        quint32 id = target["id"].toInt();
        Fixture *fxi = m_doc->fixture(id);
        if (fxi)
        {
            fxi->setAgentNote(agentNote);
            if (hasStructuredData)
                fxi->setStructuredData(structuredData);
            success = true;
        }
        else
            error = QString("Fixture %1 not found").arg(id);
    }
    else if (targetType == "fixtureDef")
    {
        QString mfr = target["manufacturer"].toString();
        QString model = target["model"].toString();
        QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef(mfr, model);
        if (def)
        {
            def->setAgentNote(agentNote);
            if (hasStructuredData)
                def->setStructuredData(structuredData);
            success = true;
        }
        else
            error = QString("Fixture definition '%1 %2' not found").arg(mfr, model);
    }
    else if (targetType == "function")
    {
        quint32 id = target["id"].toInt();
        Function *fn = m_doc->function(id);
        if (fn)
        {
            fn->setAgentNote(agentNote);
            if (hasStructuredData)
                fn->setStructuredData(structuredData);
            success = true;
        }
        else
            error = QString("Function %1 not found").arg(id);
    }
    else if (targetType == "workspace")
    {
        m_doc->setAgentNote(agentNote);
        if (hasStructuredData)
            m_doc->setStructuredData(structuredData);
        success = true;
    }
    else
    {
        error = QString("Unknown target type: %1").arg(targetType);
    }

    if (success)
    {
        m_doc->setModified();

        // Send delta so server learns the authoritative state
        QJsonObject change;
        change["action"] = "agent_note_changed";
        change["targetType"] = targetType;
        change["agentNote"] = agentNote;
        if (hasStructuredData)
            change["structuredData"] = structuredData;
        if (targetType == "fixture")
            change["targetId"] = target["id"].toInt();
        else if (targetType == "fixtureDef")
        {
            change["manufacturer"] = target["manufacturer"].toString();
            change["model"] = target["model"].toString();
        }
        else if (targetType == "function")
            change["targetId"] = target["id"].toInt();
        sendDelta(QJsonArray{change});
    }

    QJsonObject extra;
    if (!error.isEmpty())
        extra["error"] = error;
    sendCommandResult(requestId, success, extra);
}

void AgentConnection::handleCreateFixtureGroup(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    QJsonArray fixtureIds = msg["fixtureIds"].toArray();

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(name);

    // Assign fixtures — auto-placed in consecutive grid positions
    for (const QJsonValue &v : fixtureIds)
        grp->assignFixture(v.toInt());

    bool ok = m_doc->addFixtureGroup(grp);

    if (ok)
    {
        QJsonObject extra;
        extra["fixtureGroupId"] = (int)grp->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add fixture group to document";
        sendCommandResult(requestId, false, extra);
        delete grp;
    }
}

void AgentConnection::handleModifyFixtureGroup(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 groupId = (quint32)msg["fixtureGroupId"].toInt();

    FixtureGroup *grp = m_doc->fixtureGroup(groupId);
    if (grp == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Fixture group %1 not found").arg(groupId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    // Optional: rename
    if (msg.contains("name"))
        grp->setName(msg["name"].toString());

    // Add fixtures
    if (msg.contains("addFixtures"))
    {
        QJsonArray fids = msg["addFixtures"].toArray();
        for (const QJsonValue &v : fids)
            grp->assignFixture(v.toInt());
    }

    // Remove fixtures
    if (msg.contains("removeFixtures"))
    {
        QJsonArray fids = msg["removeFixtures"].toArray();
        for (const QJsonValue &v : fids)
            grp->resignFixture(v.toInt());
    }

    QJsonObject extra;
    extra["fixtureGroupId"] = (int)groupId;
    sendCommandResult(requestId, true, extra);
}

void AgentConnection::handleDeleteFixtureGroup(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 groupId = (quint32)msg["fixtureGroupId"].toInt();

    FixtureGroup *grp = m_doc->fixtureGroup(groupId);
    if (grp == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Fixture group %1 not found").arg(groupId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    bool ok = m_doc->deleteFixtureGroup(groupId);

    QJsonObject extra;
    if (!ok)
        extra["error"] = "Failed to delete fixture group";
    sendCommandResult(requestId, ok, extra);
}

void AgentConnection::handleCreatePalette(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString name = msg["name"].toString();
    QString typeStr = msg["paletteType"].toString();

    QLCPalette *pal = new QLCPalette(QLCPalette::stringToType(typeStr));
    pal->setName(name);

    // Set values from JSON array → QVariantList
    if (msg.contains("values"))
    {
        QJsonArray vals = msg["values"].toArray();
        QVariantList varList;
        for (const QJsonValue &v : vals)
            varList.append(v.toVariant());
        pal->setValues(varList);
    }

    // Fanning (optional)
    if (msg.contains("fanningType"))
        pal->setFanningType(QLCPalette::stringToFanningType(msg["fanningType"].toString()));
    if (msg.contains("fanningLayout"))
        pal->setFanningLayout(QLCPalette::stringToFanningLayout(msg["fanningLayout"].toString()));

    bool ok = m_doc->addPalette(pal);

    if (ok)
    {
        QJsonObject extra;
        extra["paletteId"] = (int)pal->id();
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Failed to add palette to document";
        sendCommandResult(requestId, false, extra);
        delete pal;
    }
}

void AgentConnection::handleModifyPalette(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 paletteId = (quint32)msg["paletteId"].toInt();

    QLCPalette *pal = m_doc->palette(paletteId);
    if (pal == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Palette %1 not found").arg(paletteId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    if (msg.contains("name"))
        pal->setName(msg["name"].toString());

    if (msg.contains("values"))
    {
        QJsonArray vals = msg["values"].toArray();
        QVariantList varList;
        for (const QJsonValue &v : vals)
            varList.append(v.toVariant());
        pal->setValues(varList);
    }

    if (msg.contains("fanningType"))
        pal->setFanningType(QLCPalette::stringToFanningType(msg["fanningType"].toString()));
    if (msg.contains("fanningLayout"))
        pal->setFanningLayout(QLCPalette::stringToFanningLayout(msg["fanningLayout"].toString()));

    QJsonObject extra;
    extra["paletteId"] = (int)paletteId;
    sendCommandResult(requestId, true, extra);
}

void AgentConnection::handleDeletePalette(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 paletteId = (quint32)msg["paletteId"].toInt();

    QLCPalette *pal = m_doc->palette(paletteId);
    if (pal == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Palette %1 not found").arg(paletteId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    bool ok = m_doc->deletePalette(paletteId);

    QJsonObject extra;
    if (!ok)
        extra["error"] = "Failed to delete palette";
    sendCommandResult(requestId, ok, extra);
}

void AgentConnection::handleSetFixturePosition(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    quint32 fixtureId = (quint32)msg["fixtureId"].toInt();
    double xPos = msg["xPos"].toDouble();
    double yPos = msg["yPos"].toDouble();
    double zPos = msg["zPos"].toDouble(0.0);

    Fixture *fxi = m_doc->fixture(fixtureId);
    if (fxi == nullptr)
    {
        QJsonObject extra;
        extra["error"] = QString("Fixture %1 not found").arg(fixtureId);
        sendCommandResult(requestId, false, extra);
        return;
    }

    MonitorProperties *props = m_doc->monitorProperties();
    props->setFixturePosition(fixtureId, 0, 0, QVector3D(xPos, yPos, zPos));

    // Optional rotation (Euler angles in degrees: pitch, yaw, roll)
    bool hasRotation = msg.contains("rotX") || msg.contains("rotY") || msg.contains("rotZ");
    if (hasRotation)
    {
        double rotX = msg["rotX"].toDouble(0.0);
        double rotY = msg["rotY"].toDouble(0.0);
        double rotZ = msg["rotZ"].toDouble(0.0);
        props->setFixtureRotation(fixtureId, 0, 0, QVector3D(rotX, rotY, rotZ));
    }

    m_doc->setModified();

    // Send delta so server updates its stage layout
    QJsonObject change;
    change["action"] = "fixture_position_changed";
    change["fixtureId"] = (int)fixtureId;
    change["xPos"] = xPos;
    change["yPos"] = yPos;
    change["zPos"] = zPos;
    if (hasRotation)
    {
        change["rotX"] = msg["rotX"].toDouble(0.0);
        change["rotY"] = msg["rotY"].toDouble(0.0);
        change["rotZ"] = msg["rotZ"].toDouble(0.0);
    }
    sendDelta(QJsonArray{change});

    sendCommandResult(requestId, true);
}

QJsonObject AgentConnection::serializeFixtureGroup(quint32 id)
{
    FixtureGroup *grp = m_doc->fixtureGroup(id);
    QJsonObject gj;
    if (grp == nullptr)
        return gj;

    gj["id"] = (int)grp->id();
    gj["name"] = grp->name();
    QJsonObject sz;
    sz["width"] = grp->size().width();
    sz["height"] = grp->size().height();
    gj["size"] = sz;
    QJsonArray fids;
    for (quint32 fid : grp->fixtureList())
        fids.append((int)fid);
    gj["fixtureIds"] = fids;
    return gj;
}

void AgentConnection::handleSessionCreated(const QJsonObject &msg)
{
    QString sessionId = msg["sessionId"].toString();
    qDebug() << "[AgentConnection] Session created:" << sessionId;

    AgentSession session;
    session.sessionId = sessionId;
    session.createdAt = QDateTime::currentDateTime();
    m_doc->addSession(session);

    emit sessionCreated(sessionId);
}

void AgentConnection::handleSessionHistory(const QJsonObject &msg)
{
    QString sessionId = msg["sessionId"].toString();
    bool expired = msg["expired"].toBool(false);
    QJsonArray messages = msg["messages"].toArray();

    if (expired)
    {
        qDebug() << "[AgentConnection] Session expired:" << sessionId;
    }
    else
    {
        qDebug() << "[AgentConnection] Session history loaded:" << sessionId
                 << "messages:" << messages.size();
    }

    emit sessionHistoryReceived(sessionId, messages, expired);
}

void AgentConnection::handleUpdateSessionMetadata(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QString sessionId = msg["sessionId"].toString();
    QString title = msg["title"].toString();
    QStringList goals;
    QJsonArray goalsArray = msg["goals"].toArray();
    for (const QJsonValue &v : goalsArray)
        goals.append(v.toString());

    m_doc->updateSession(sessionId, title, goals);

    emit sessionMetadataUpdated(sessionId);
    sendCommandResult(requestId, true);
}

void AgentConnection::handleGetDmxValues(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QJsonObject extra;

    if (msg.contains("fixtureId"))
    {
        quint32 fxiId = (quint32)msg["fixtureId"].toInt();
        Fixture *fxi = m_doc->fixture(fxiId);
        if (fxi == nullptr)
        {
            extra["error"] = QString("Fixture ID %1 not found").arg(fxiId);
            sendCommandResult(requestId, false, extra);
            return;
        }

        quint32 uni = fxi->universe();
        quint32 addr = fxi->address();
        quint32 chCount = fxi->channels();

        QList<Universe*> universes = m_doc->inputOutputMap()->claimUniverses();
        if ((int)uni >= universes.count())
        {
            m_doc->inputOutputMap()->releaseUniverses(false);
            extra["error"] = QString("Universe %1 out of range").arg(uni);
            sendCommandResult(requestId, false, extra);
            return;
        }

        const QByteArray *values = universes.at(uni)->postGMValues();
        QJsonArray channels;
        for (quint32 ch = 0; ch < chCount; ch++)
        {
            uchar val = (uchar)values->at(addr + ch);
            QJsonObject chObj;
            chObj["channel"] = (int)ch;
            chObj["value"] = (int)val;

            const QLCChannel *qlcCh = fxi->channel(ch);
            if (qlcCh != nullptr)
            {
                chObj["name"] = qlcCh->name();
                chObj["group"] = QLCChannel::groupToString(qlcCh->group());
            }

            channels.append(chObj);
        }
        m_doc->inputOutputMap()->releaseUniverses(false);

        extra["fixtureId"] = (int)fxiId;
        extra["universe"] = (int)uni;
        extra["address"] = (int)(addr + 1);  // 1-based to match QLC+ UI / Simple Desk
        extra["channels"] = channels;
        sendCommandResult(requestId, true, extra);
    }
    else if (msg.contains("universe"))
    {
        quint32 uni = (quint32)msg["universe"].toInt();

        QList<Universe*> universes = m_doc->inputOutputMap()->claimUniverses();
        if ((int)uni >= universes.count())
        {
            m_doc->inputOutputMap()->releaseUniverses(false);
            extra["error"] = QString("Universe %1 out of range").arg(uni);
            sendCommandResult(requestId, false, extra);
            return;
        }

        const QByteArray *values = universes.at(uni)->postGMValues();
        QJsonArray channels;
        for (int addr = 0; addr < values->size(); addr++)
        {
            uchar val = (uchar)values->at(addr);
            if (val > 0)
            {
                QJsonObject chObj;
                chObj["address"] = addr + 1;  // 1-based to match QLC+ UI / Simple Desk
                chObj["value"] = (int)val;
                channels.append(chObj);
            }
        }
        m_doc->inputOutputMap()->releaseUniverses(false);

        extra["universe"] = (int)uni;
        extra["channels"] = channels;
        sendCommandResult(requestId, true, extra);
    }
    else
    {
        extra["error"] = "Must specify either 'fixtureId' or 'universe'";
        sendCommandResult(requestId, false, extra);
    }
}

void AgentConnection::handleResetSimpleDesk(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();

    if (msg.contains("channels"))
    {
        // Per-channel reset: list of {universe, channel} pairs (channel is 1-based)
        QJsonArray channels = msg["channels"].toArray();
        for (const QJsonValue &v : channels)
        {
            QJsonObject ch = v.toObject();
            uint universe = ch["universe"].toInt();
            uint channel = ch["channel"].toInt();  // 1-based from protocol
            uint absChannel = (universe << 9) | channel;
            emit simpleDeskResetChannelRequested(absChannel);
        }
        sendCommandResult(requestId, true);
    }
    else if (msg.contains("universe"))
    {
        // Per-universe reset: clear all overrides for one universe
        int universe = msg["universe"].toInt();
        emit simpleDeskResetUniverseRequested(universe);
        sendCommandResult(requestId, true);
    }
    else
    {
        QJsonObject extra;
        extra["error"] = "Must specify either 'channels' or 'universe'";
        sendCommandResult(requestId, false, extra);
    }
}

void AgentConnection::handleGetRunningFunctions(const QJsonObject &msg)
{
    QString requestId = msg["requestId"].toString();
    QJsonArray running;

    foreach (Function *fn, m_doc->functions())
    {
        if (fn != nullptr && fn->isRunning())
        {
            QJsonObject fnObj;
            fnObj["id"] = (int)fn->id();
            fnObj["name"] = fn->name();
            fnObj["type"] = Function::typeToString(fn->type());
            running.append(fnObj);
        }
    }

    QJsonObject extra;
    extra["functions"] = running;
    sendCommandResult(requestId, true, extra);
}

void AgentConnection::handleVCCommand(const QJsonObject &msg, const QString &command)
{
    QString requestId = msg["requestId"].toString();

    // All VC commands require Design mode
    if (m_doc->mode() != Doc::Design)
    {
        QJsonObject extra;
        extra["error"] = "Virtual Console commands require Design mode";
        sendCommandResult(requestId, false, extra);
        return;
    }

    if (!m_vcCommandHandler)
    {
        QJsonObject extra;
        extra["error"] = "Virtual Console not available";
        sendCommandResult(requestId, false, extra);
        return;
    }

    QJsonObject result;
    bool success = m_vcCommandHandler(command, msg, result);
    sendCommandResult(requestId, success, result);

    // Send delta if the command succeeded and produced a delta action
    if (success && result.contains("deltaAction"))
    {
        QJsonObject change;
        change["action"] = result["deltaAction"].toString();
        if (result.contains("widget"))
            change["widget"] = result["widget"];
        if (result.contains("widgetId"))
            change["widgetId"] = result["widgetId"].toInt();
        sendDelta(QJsonArray{change});
    }
}

void AgentConnection::sendSessionResume(const QString &sessionId)
{
    if (m_state != Connected)
        return;

    QJsonObject msg;
    msg["type"] = "session_resume";
    msg["sessionId"] = sessionId;
    sendJson(msg);
}

/*****************************************************************************
 * Workspace sync serialization
 *****************************************************************************/

QJsonObject AgentConnection::buildWorkspaceSync()
{
    QJsonObject sync;
    sync["type"] = "workspace_sync";
    sync["clientVersion"] = AGENT_CLIENT_VERSION;

    QJsonObject caps;
    caps["geometry3d"] = false;
    caps["undo"] = false;
    sync["capabilities"] = caps;

    sync["mode"] = (m_doc->mode() == Doc::Operate) ? "Operate" : "Design";
    sync["fixtures"] = serializeFixtures();
    sync["fixtureDefs"] = serializeFixtureDefs();
    sync["functions"] = serializeFunctions();
    sync["fixtureGroups"] = serializeFixtureGroups();
    sync["channelGroups"] = serializeChannelGroups();
    sync["palettes"] = serializePalettes();
    sync["stageLayout"] = serializeStageLayout();
    sync["grandMaster"] = serializeGrandMaster();
    sync["blackout"] = m_doc->inputOutputMap()->blackout();
    sync["universeCount"] = (int)m_doc->inputOutputMap()->universes().count();
    sync["inputOutputMap"] = serializeInputOutputMap();

    if (m_vcSerializer)
        sync["virtualConsole"] = m_vcSerializer();

    if (!m_doc->agentContext().isEmpty())
        sync["agentContext"] = serializeAgentContext(m_doc->agentContext());

    sync["graphVersion"] = m_graphVersion.isEmpty() ? "v2" : m_graphVersion;

    return sync;
}

QJsonArray AgentConnection::serializeFixtures()
{
    QJsonArray arr;
    for (Fixture *fxi : m_doc->fixtures())
    {
        if (fxi == nullptr)
            continue;

        QJsonObject fj;
        fj["id"] = (int)fxi->id();
        fj["name"] = fxi->name();

        QLCFixtureDef *def = fxi->fixtureDef();
        if (def)
        {
            fj["manufacturer"] = def->manufacturer();
            fj["model"] = def->model();
        }
        else
        {
            fj["manufacturer"] = "Generic";
            fj["model"] = "Dimmer";
        }

        QLCFixtureMode *mode = fxi->fixtureMode();
        fj["mode"] = mode ? mode->name() : "";
        fj["universe"] = (int)fxi->universe();
        fj["address"] = (int)(fxi->address() + 1);  // 1-based DMX address
        fj["channels"] = (int)fxi->channels();

        if (!fxi->agentContext().isEmpty())
            fj["agentContext"] = serializeAgentContext(fxi->agentContext());

        arr.append(fj);
    }
    return arr;
}

QJsonArray AgentConnection::serializeFixtureDefs()
{
    QJsonArray arr;
    // Collect unique defs from actual fixtures
    QSet<QString> seen;

    for (Fixture *fxi : m_doc->fixtures())
    {
        if (fxi == nullptr)
            continue;

        QLCFixtureDef *def = fxi->fixtureDef();
        if (def == nullptr)
            continue;

        QString key = def->manufacturer() + "/" + def->model();
        if (seen.contains(key))
            continue;
        seen.insert(key);

        // Access through cache to ensure lazy-loaded defs are fully loaded
        QLCFixtureDef *loadedDef = m_doc->fixtureDefCache()->fixtureDef(
            def->manufacturer(), def->model());
        if (loadedDef)
            def = loadedDef;

        QJsonObject dj;
        dj["manufacturer"] = def->manufacturer();
        dj["model"] = def->model();
        dj["type"] = def->typeToString(def->type());

        // Channels
        QJsonArray channels;
        for (QLCChannel *ch : def->channels())
        {
            QJsonObject cj;
            cj["name"] = ch->name();
            cj["group"] = QLCChannel::groupToString(ch->group());
            cj["defaultValue"] = ch->defaultValue();
            cj["controlByte"] = (ch->controlByte() == QLCChannel::MSB) ? "MSB" : "LSB";

            if (ch->colour() != QLCChannel::NoColour)
                cj["colour"] = QLCChannel::colourToString(ch->colour());

            QJsonArray caps;
            for (QLCCapability *cap : ch->capabilities())
            {
                QJsonObject capj;
                capj["min"] = (int)cap->min();
                capj["max"] = (int)cap->max();
                capj["name"] = cap->name();

                if (cap->preset() != QLCCapability::Custom)
                    capj["preset"] = QLCCapability::presetToString(cap->preset());

                // Include color resources if present
                if (cap->presetType() == QLCCapability::SingleColor
                    || cap->presetType() == QLCCapability::DoubleColor)
                {
                    QVariant res0 = cap->resource(0);
                    if (res0.isValid() && res0.canConvert<QColor>())
                        capj["color"] = res0.value<QColor>().name();
                    QVariant res1 = cap->resource(1);
                    if (res1.isValid() && res1.canConvert<QColor>())
                        capj["color2"] = res1.value<QColor>().name();
                }

                caps.append(capj);
            }
            cj["capabilities"] = caps;
            channels.append(cj);
        }
        dj["channels"] = channels;

        // Modes
        QJsonArray modes;
        for (QLCFixtureMode *mode : def->modes())
        {
            QJsonObject mj;
            mj["name"] = mode->name();

            QJsonArray modeChannels;
            for (QLCChannel *ch : mode->channels())
                modeChannels.append(ch->name());
            mj["channels"] = modeChannels;

            modes.append(mj);
        }
        dj["modes"] = modes;

        if (!def->agentContext().isEmpty())
            dj["agentContext"] = serializeAgentContext(def->agentContext());

        // Physical properties (for spatial calculations)
        QLCPhysical phys = def->physical();
        if (phys.focusPanMax() > 0 || phys.focusTiltMax() > 0
            || phys.lensDegreesMin() > 0 || phys.lensDegreesMax() > 0)
        {
            QJsonObject pj;
            if (phys.focusPanMax() > 0)
                pj["focusPanMax"] = phys.focusPanMax();
            if (phys.focusTiltMax() > 0)
                pj["focusTiltMax"] = phys.focusTiltMax();
            if (!phys.focusType().isEmpty())
                pj["focusType"] = phys.focusType();
            if (phys.lensDegreesMin() > 0)
                pj["lensDegreesMin"] = phys.lensDegreesMin();
            if (phys.lensDegreesMax() > 0)
                pj["lensDegreesMax"] = phys.lensDegreesMax();
            dj["physical"] = pj;
        }

        arr.append(dj);
    }
    return arr;
}

QJsonArray AgentConnection::serializeFunctions()
{
    QJsonArray arr;
    for (Function *fn : m_doc->functions())
    {
        if (fn == nullptr)
            continue;

        arr.append(serializeFunction(fn->id()));
    }
    return arr;
}

QJsonObject AgentConnection::serializeFunction(quint32 id)
{
    Function *fn = m_doc->function(id);
    QJsonObject fj;
    if (fn == nullptr)
        return fj;

    fj["id"] = (int)fn->id();
    fj["type"] = Function::typeToString(fn->type());
    fj["name"] = fn->name();
    fj["path"] = fn->path();
    fj["runOrder"] = Function::runOrderToString(fn->runOrder());
    fj["direction"] = Function::directionToString(fn->direction());
    fj["fadeIn"] = (int)fn->fadeInSpeed();
    fj["fadeOut"] = (int)fn->fadeOutSpeed();
    fj["duration"] = (int)fn->duration();
    fj["tempoType"] = Function::tempoTypeToString(fn->tempoType());

    switch (fn->type())
    {
    case Function::SceneType:
    {
        Scene *scene = qobject_cast<Scene*>(fn);
        if (scene)
        {
            QJsonArray vals;
            for (const SceneValue &sv : scene->values())
            {
                QJsonObject vo;
                vo["fixtureId"] = (int)sv.fxi;
                vo["channel"] = (int)sv.channel;
                vo["value"] = (int)sv.value;
                vals.append(vo);
            }
            fj["values"] = vals;
        }
        break;
    }
    case Function::SequenceType:
    {
        Sequence *seq = qobject_cast<Sequence*>(fn);
        if (seq)
        {
            fj["boundSceneId"] = (int)seq->boundSceneID();
            QJsonArray stepsArr;
            for (int i = 0; i < seq->stepsCount(); i++)
            {
                ChaserStep *step = seq->stepAt(i);
                if (!step) continue;
                QJsonObject sj;
                sj["fadeIn"] = (int)step->fadeIn;
                sj["hold"] = (int)step->hold;
                sj["fadeOut"] = (int)step->fadeOut;
                sj["duration"] = (int)step->duration;
                sj["note"] = step->note;

                QJsonArray stepVals;
                for (const SceneValue &sv : step->values)
                {
                    QJsonObject vo;
                    vo["fixtureId"] = (int)sv.fxi;
                    vo["channel"] = (int)sv.channel;
                    vo["value"] = (int)sv.value;
                    stepVals.append(vo);
                }
                sj["values"] = stepVals;
                stepsArr.append(sj);
            }
            fj["steps"] = stepsArr;
        }
        break;
    }
    case Function::ChaserType:
    {
        Chaser *chaser = qobject_cast<Chaser*>(fn);
        if (chaser)
        {
            QJsonArray stepsArr;
            for (int i = 0; i < chaser->stepsCount(); i++)
            {
                ChaserStep *step = chaser->stepAt(i);
                if (!step) continue;
                QJsonObject sj;
                sj["functionId"] = (int)step->fid;
                sj["fadeIn"] = (int)step->fadeIn;
                sj["hold"] = (int)step->hold;
                sj["fadeOut"] = (int)step->fadeOut;
                sj["duration"] = (int)step->duration;
                sj["note"] = step->note;
                stepsArr.append(sj);
            }
            fj["steps"] = stepsArr;
        }
        break;
    }
    case Function::CollectionType:
    {
        Collection *coll = qobject_cast<Collection*>(fn);
        if (coll)
        {
            QJsonArray memberIds;
            for (quint32 fid : coll->functions())
                memberIds.append((int)fid);
            fj["memberFunctionIds"] = memberIds;
        }
        break;
    }
    case Function::EFXType:
    {
        EFX *efx = qobject_cast<EFX*>(fn);
        if (efx)
        {
            fj["algorithm"] = EFX::algorithmToString(efx->algorithm());
            fj["width"] = efx->width();
            fj["height"] = efx->height();
            fj["rotation"] = efx->rotation();
            fj["xOffset"] = efx->xOffset();
            fj["yOffset"] = efx->yOffset();
            fj["startOffset"] = efx->startOffset();
            fj["propagationMode"] = EFX::propagationModeToString(efx->propagationMode());

            QJsonArray efxFixtures;
            for (EFXFixture *ef : efx->fixtures())
            {
                QJsonObject efj;
                efj["fixtureId"] = (int)ef->head().fxi;
                efj["headIndex"] = ef->head().head;
                efj["startOffset"] = ef->startOffset();
                efxFixtures.append(efj);
            }
            fj["efxFixtures"] = efxFixtures;
        }
        break;
    }
    case Function::RGBMatrixType:
    {
        RGBMatrix *rgbm = qobject_cast<RGBMatrix*>(fn);
        if (rgbm)
        {
            fj["fixtureGroupId"] = (int)rgbm->fixtureGroup();
            if (rgbm->algorithm())
                fj["algorithmName"] = rgbm->algorithm()->name();
            QJsonArray colors;
            for (const QColor &c : rgbm->getColors())
                colors.append(c.name());
            fj["colors"] = colors;
        }
        break;
    }
    case Function::ScriptType:
    {
        Script *script = qobject_cast<Script*>(fn);
        if (script)
            fj["data"] = script->data();
        break;
    }
    default:
        break;
    }

    if (!fn->agentContext().isEmpty())
        fj["agentContext"] = serializeAgentContext(fn->agentContext());

    return fj;
}

QJsonObject AgentConnection::serializeFixture(quint32 id)
{
    Fixture *fxi = m_doc->fixture(id);
    QJsonObject fj;
    if (fxi == nullptr)
        return fj;

    fj["id"] = (int)fxi->id();
    fj["name"] = fxi->name();

    const QLCFixtureDef *def = fxi->fixtureDef();
    if (def)
    {
        fj["manufacturer"] = def->manufacturer();
        fj["model"] = def->model();
    }
    else
    {
        fj["manufacturer"] = "Generic";
        fj["model"] = "Dimmer";
    }

    const QLCFixtureMode *mode = fxi->fixtureMode();
    fj["mode"] = mode ? mode->name() : "";
    fj["universe"] = (int)fxi->universe();
    fj["address"] = (int)(fxi->address() + 1);  // 1-based DMX address
    fj["channels"] = (int)fxi->channels();

    QList<int> htpList = fxi->forcedHTPChannels();
    if (!htpList.isEmpty())
    {
        QJsonArray htp;
        for (int ch : htpList)
            htp.append(ch);
        fj["forcedHTP"] = htp;
    }

    QList<int> ltpList = fxi->forcedLTPChannels();
    if (!ltpList.isEmpty())
    {
        QJsonArray ltp;
        for (int ch : ltpList)
            ltp.append(ch);
        fj["forcedLTP"] = ltp;
    }

    return fj;
}

QJsonArray AgentConnection::serializeFixtureGroups()
{
    QJsonArray arr;
    for (FixtureGroup *grp : m_doc->fixtureGroups())
    {
        if (grp == nullptr)
            continue;
        QJsonObject gj;
        gj["id"] = (int)grp->id();
        gj["name"] = grp->name();
        QJsonObject sz;
        sz["width"] = grp->size().width();
        sz["height"] = grp->size().height();
        gj["size"] = sz;
        QJsonArray fixtureIds;
        for (quint32 fid : grp->fixtureList())
            fixtureIds.append((int)fid);
        gj["fixtureIds"] = fixtureIds;
        arr.append(gj);
    }
    return arr;
}

QJsonArray AgentConnection::serializeChannelGroups()
{
    QJsonArray arr;
    for (ChannelsGroup *cg : m_doc->channelsGroups())
    {
        if (cg == nullptr)
            continue;
        QJsonObject cgj;
        cgj["id"] = (int)cg->id();
        cgj["name"] = cg->name();
        QJsonArray channels;
        for (const SceneValue &sv : cg->getChannels())
        {
            QJsonObject ch;
            ch["fixtureId"] = (int)sv.fxi;
            ch["channel"] = (int)sv.channel;
            channels.append(ch);
        }
        cgj["channels"] = channels;
        arr.append(cgj);
    }
    return arr;
}

QJsonArray AgentConnection::serializePalettes()
{
    QJsonArray arr;
    for (QLCPalette *pal : m_doc->palettes())
    {
        if (pal == nullptr)
            continue;
        QJsonObject pj;
        pj["id"] = (int)pal->id();
        pj["name"] = pal->name();
        pj["type"] = QLCPalette::typeToString(pal->type());

        QVariantList vals = pal->values();
        QJsonArray valArr;
        for (const QVariant &v : vals)
            valArr.append(QJsonValue::fromVariant(v));
        if (!valArr.isEmpty())
            pj["values"] = valArr;

        pj["fanningType"] = QLCPalette::fanningTypeToString(pal->fanningType());
        pj["fanningLayout"] = QLCPalette::fanningLayoutToString(pal->fanningLayout());

        arr.append(pj);
    }
    return arr;
}

QJsonObject AgentConnection::serializeStageLayout()
{
    QJsonObject layout;
    MonitorProperties *props = m_doc->monitorProperties();

    QJsonObject gridSize;
    QVector3D gs = props->gridSize();
    // Convert grid dimensions to millimeters for protocol consistency.
    // All spatial values on the wire are mm, regardless of gridUnits setting.
    float unitToMm = (props->gridUnits() == MonitorProperties::Meters) ? 1000.0f : 304.8f;
    gridSize["width"] = gs.x() * unitToMm;
    gridSize["height"] = gs.y() * unitToMm;
    gridSize["depth"] = gs.z() * unitToMm;
    layout["gridSize"] = gridSize;

    layout["gridUnits"] = (props->gridUnits() == MonitorProperties::Meters)
                          ? "Meters" : "Feet";
    layout["showLabels"] = props->labelsVisible();

    switch (props->pointOfView())
    {
    case MonitorProperties::TopView: layout["pointOfView"] = "TopView"; break;
    case MonitorProperties::FrontView: layout["pointOfView"] = "FrontView"; break;
    case MonitorProperties::RightSideView: layout["pointOfView"] = "RightSideView"; break;
    case MonitorProperties::LeftSideView: layout["pointOfView"] = "LeftSideView"; break;
    default: layout["pointOfView"] = "Undefined"; break;
    }

    switch (props->stageType())
    {
    case MonitorProperties::StageSimple: layout["stageType"] = "Simple"; break;
    case MonitorProperties::StageBox: layout["stageType"] = "Box"; break;
    case MonitorProperties::StageRock: layout["stageType"] = "Rock"; break;
    case MonitorProperties::StageTheatre: layout["stageType"] = "Theatre"; break;
    }

    QJsonArray fixtures;
    for (quint32 fid : props->fixtureItemsID())
    {
        FixturePreviewItem item = props->fixtureProperties(fid);
        QJsonObject fj;
        fj["fixtureId"] = (int)fid;

        QJsonObject pos;
        pos["x"] = item.m_baseItem.m_position.x();
        pos["y"] = item.m_baseItem.m_position.y();
        pos["z"] = item.m_baseItem.m_position.z();
        fj["position"] = pos;

        QJsonObject rot;
        rot["x"] = item.m_baseItem.m_rotation.x();
        rot["y"] = item.m_baseItem.m_rotation.y();
        rot["z"] = item.m_baseItem.m_rotation.z();
        fj["rotation"] = rot;

        if (item.m_baseItem.m_color.isValid())
            fj["gelColor"] = item.m_baseItem.m_color.name();

        fixtures.append(fj);
    }
    layout["fixtures"] = fixtures;

    return layout;
}

QJsonObject AgentConnection::serializeGrandMaster()
{
    QJsonObject gm;
    InputOutputMap *ioMap = m_doc->inputOutputMap();
    gm["value"] = (int)ioMap->grandMasterValue();
    gm["valueMode"] = GrandMaster::valueModeToString(ioMap->grandMasterValueMode());
    gm["channelMode"] = GrandMaster::channelModeToString(ioMap->grandMasterChannelMode());
    return gm;
}

QJsonObject AgentConnection::serializeInputOutputMap()
{
    QJsonObject ioMap;
    InputOutputMap *map = m_doc->inputOutputMap();

    QJsonArray universes;
    QList<Universe*> uniList = map->universes();
    for (int i = 0; i < uniList.count(); i++)
    {
        Universe *uni = uniList.at(i);
        QJsonObject uniObj;
        uniObj["id"] = i;
        uniObj["name"] = uni->name();
        uniObj["passthrough"] = uni->passthrough();

        InputPatch *ip = map->inputPatch(i);
        if (ip != nullptr && ip->plugin() != nullptr)
        {
            QJsonObject input;
            input["plugin"] = ip->pluginName();
            input["line"] = (int)ip->input();
            input["profileName"] = ip->profileName();
            uniObj["input"] = input;
        }

        OutputPatch *op = map->outputPatch(i);
        if (op != nullptr && op->plugin() != nullptr)
        {
            QJsonObject output;
            output["plugin"] = op->pluginName();
            output["line"] = (int)op->output();
            uniObj["output"] = output;
        }

        universes.append(uniObj);
    }
    ioMap["universes"] = universes;

    // Available input profiles (name + type, not full channel maps)
    QJsonArray profiles;
    QStringList profileNameList = map->profileNames();
    for (const QString &name : profileNameList)
    {
        QLCInputProfile *p = map->profile(name);
        if (p == nullptr)
            continue;
        QJsonObject prof;
        prof["manufacturer"] = p->manufacturer();
        prof["model"] = p->model();
        prof["type"] = QLCInputProfile::typeToString(p->type());
        profiles.append(prof);
    }
    ioMap["inputProfiles"] = profiles;

    return ioMap;
}

/*****************************************************************************
 * JSON helpers
 *****************************************************************************/

void AgentConnection::sendJson(const QJsonObject &obj)
{
    if (m_webSocket == nullptr)
        return;

    QJsonDocument doc(obj);
    m_webSocket->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void AgentConnection::sendCommandResult(const QString &requestId, bool success,
                                         const QJsonObject &extra)
{
    QJsonObject result;
    result["type"] = "command_result";
    result["requestId"] = requestId;
    result["success"] = success;

    // Merge extra fields
    for (auto it = extra.begin(); it != extra.end(); ++it)
        result[it.key()] = it.value();

    sendJson(result);
}

QJsonObject AgentConnection::serializeAgentContext(const AgentContext &ctx)
{
    QJsonObject ac;
    if (!ctx.userNote.isEmpty())
        ac["userNote"] = ctx.userNote;
    if (!ctx.agentNote.isEmpty())
        ac["agentNote"] = ctx.agentNote;
    if (!ctx.structuredData.isEmpty())
        ac["structuredData"] = ctx.structuredData;
    return ac;
}

/*****************************************************************************
 * Doc signal handlers → workspace_delta
 *****************************************************************************/

void AgentConnection::connectDocSignals()
{
    connect(m_doc, &Doc::functionAdded,
            this, &AgentConnection::onFunctionAdded);
    connect(m_doc, &Doc::functionRemoved,
            this, &AgentConnection::onFunctionRemoved);
    connect(m_doc, &Doc::functionChanged,
            this, &AgentConnection::onFunctionChanged);
    connect(m_doc, &Doc::fixtureAdded,
            this, &AgentConnection::onFixtureAdded);
    connect(m_doc, &Doc::fixtureRemoved,
            this, &AgentConnection::onFixtureRemoved);
    connect(m_doc, &Doc::fixtureChanged,
            this, &AgentConnection::onFixtureChanged);
    connect(m_doc, &Doc::fixtureGroupAdded,
            this, &AgentConnection::onFixtureGroupAdded);
    connect(m_doc, &Doc::fixtureGroupRemoved,
            this, &AgentConnection::onFixtureGroupRemoved);
    connect(m_doc, &Doc::paletteAdded,
            this, &AgentConnection::onPaletteAdded);
    connect(m_doc, &Doc::paletteRemoved,
            this, &AgentConnection::onPaletteRemoved);
    connect(m_doc, &Doc::modeChanged,
            this, &AgentConnection::onModeChanged);

    InputOutputMap *ioMap = m_doc->inputOutputMap();
    connect(ioMap, &InputOutputMap::grandMasterValueChanged,
            this, &AgentConnection::onGrandMasterValueChanged);
    connect(ioMap, &InputOutputMap::blackoutChanged,
            this, &AgentConnection::onBlackoutChanged);
}

void AgentConnection::disconnectDocSignals()
{
    disconnect(m_doc, &Doc::functionAdded, this, nullptr);
    disconnect(m_doc, &Doc::functionRemoved, this, nullptr);
    disconnect(m_doc, &Doc::functionChanged, this, nullptr);
    disconnect(m_doc, &Doc::fixtureAdded, this, nullptr);
    disconnect(m_doc, &Doc::fixtureRemoved, this, nullptr);
    disconnect(m_doc, &Doc::fixtureChanged, this, nullptr);
    disconnect(m_doc, &Doc::fixtureGroupAdded, this, nullptr);
    disconnect(m_doc, &Doc::fixtureGroupRemoved, this, nullptr);
    disconnect(m_doc, &Doc::paletteAdded, this, nullptr);
    disconnect(m_doc, &Doc::paletteRemoved, this, nullptr);
    disconnect(m_doc, &Doc::modeChanged, this, nullptr);
    // Keep loading/loaded connected — those are set in constructor

    InputOutputMap *ioMap = m_doc->inputOutputMap();
    if (ioMap)
        disconnect(ioMap, nullptr, this, nullptr);
}

void AgentConnection::sendDelta(const QJsonArray &changes)
{
    if (m_state != Connected)
        return;

    QJsonObject msg;
    msg["type"] = "workspace_delta";
    msg["changes"] = changes;

    sendJson(msg);
}

void AgentConnection::onDocLoading()
{
    qDebug() << "[AgentConnection] Doc loading — disconnecting";
    // File switch invalidates the entire workspace. Disconnect cleanly
    // so the user can reconnect with the new workspace when ready.
    if (m_state != Disconnected)
        disconnectFromServer();
}

void AgentConnection::onDocLoaded()
{
    qDebug() << "[AgentConnection] Doc loaded";
    // Nothing to do — user reconnects manually when ready.
}

void AgentConnection::onFunctionAdded(quint32 id)
{
    QJsonObject change;
    change["action"] = "function_added";
    change["function"] = serializeFunction(id);
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFunctionRemoved(quint32 id)
{
    QJsonObject change;
    change["action"] = "function_removed";
    change["functionId"] = (int)id;
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFunctionChanged(quint32 id)
{
    QJsonObject change;
    change["action"] = "function_changed";
    change["function"] = serializeFunction(id);
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFixtureAdded(quint32 id)
{
    QJsonObject change;
    change["action"] = "fixture_added";
    change["fixture"] = serializeFixture(id);
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFixtureRemoved(quint32 id)
{
    QJsonObject change;
    change["action"] = "fixture_removed";
    change["fixtureId"] = (int)id;
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFixtureChanged(quint32 id)
{
    QJsonObject change;
    change["action"] = "fixture_changed";
    change["fixture"] = serializeFixture(id);
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFixtureGroupAdded(quint32 id)
{
    QJsonObject change;
    change["action"] = "fixture_group_added";
    change["fixtureGroup"] = serializeFixtureGroup(id);
    sendDelta(QJsonArray{change});
}

void AgentConnection::onFixtureGroupRemoved(quint32 id)
{
    QJsonObject change;
    change["action"] = "fixture_group_removed";
    change["fixtureGroupId"] = (int)id;
    sendDelta(QJsonArray{change});
}

void AgentConnection::onPaletteAdded(quint32 id)
{
    // Serialize palette using same format as serializePalettes()
    QLCPalette *pal = m_doc->palette(id);
    if (pal == nullptr) return;

    QJsonObject pj;
    pj["id"] = (int)pal->id();
    pj["name"] = pal->name();
    pj["type"] = QLCPalette::typeToString(pal->type());

    QVariantList vals = pal->values();
    QJsonArray valArr;
    for (const QVariant &v : vals)
        valArr.append(QJsonValue::fromVariant(v));
    if (!valArr.isEmpty())
        pj["values"] = valArr;

    pj["fanningType"] = QLCPalette::fanningTypeToString(pal->fanningType());
    pj["fanningLayout"] = QLCPalette::fanningLayoutToString(pal->fanningLayout());

    QJsonObject change;
    change["action"] = "palette_added";
    change["palette"] = pj;
    sendDelta(QJsonArray{change});
}

void AgentConnection::onPaletteRemoved(quint32 id)
{
    QJsonObject change;
    change["action"] = "palette_removed";
    change["paletteId"] = (int)id;
    sendDelta(QJsonArray{change});
}

void AgentConnection::onModeChanged()
{
    QJsonObject change;
    change["action"] = "mode_changed";
    change["mode"] = (m_doc->mode() == Doc::Operate) ? "Operate" : "Design";
    sendDelta(QJsonArray{change});
}

void AgentConnection::onGrandMasterValueChanged(uchar value)
{
    Q_UNUSED(value)
    QJsonObject change;
    change["action"] = "grand_master_changed";
    change["value"] = (int)m_doc->inputOutputMap()->grandMasterValue();
    sendDelta(QJsonArray{change});
}

void AgentConnection::onBlackoutChanged(bool state)
{
    QJsonObject change;
    change["action"] = "blackout_changed";
    change["enabled"] = state;
    sendDelta(QJsonArray{change});
}
