/*
  Q Light Controller Plus
  agentauthmanager.cpp

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

#include "agentauthmanager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>

/*****************************************************************************
 * Initialization
 *****************************************************************************/

AgentAuthManager::AgentAuthManager(QObject *parent)
    : QObject(parent)
    , m_authState(Unknown)
    , m_credentialStore(this)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_staticToken(qEnvironmentVariable("AGENT_API_TOKEN"))
    , m_callbackServer(nullptr)
    , m_callbackPort(0)
{
    m_loginTimeout.setSingleShot(true);
    connect(&m_loginTimeout, &QTimer::timeout,
            this, &AgentAuthManager::onLoginTimeout);

    // Load stored refresh token on startup
    connect(&m_credentialStore, &AgentCredentialStore::refreshTokenLoaded,
            this, &AgentAuthManager::onRefreshTokenLoaded);
    m_credentialStore.loadRefreshToken();
}

AgentAuthManager::~AgentAuthManager()
{
    stopCallbackServer();
}

/*****************************************************************************
 * State
 *****************************************************************************/

void AgentAuthManager::setAuthState(AuthState state)
{
    if (m_authState != state)
    {
        m_authState = state;
        emit authStateChanged(state);
    }
}

bool AgentAuthManager::hasStoredCredentials() const
{
    return !m_staticToken.isEmpty() || !m_refreshToken.isEmpty()
           || !m_accessToken.isEmpty();
}

/*****************************************************************************
 * Configuration
 *****************************************************************************/

void AgentAuthManager::fetchServerConfig(const QUrl &serverUrl)
{
    QUrl configUrl = serverUrl;
    configUrl.setScheme(serverUrl.scheme() == "wss" ? "https" : "http");
    configUrl.setPath("/config");

    m_lastServerUrl = serverUrl;

    qDebug() << "[Auth] Fetching server config from" << configUrl;
    QNetworkReply *reply = m_networkManager->get(QNetworkRequest(configUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            qWarning() << "[Auth] Failed to fetch server config:" << reply->errorString();
            emit configFetched(false);
            return;
        }

        m_serverConfig = QJsonDocument::fromJson(reply->readAll()).object();
        qDebug() << "[Auth] Server config:" << m_serverConfig;

        m_connectUrl = m_serverConfig["connect_url"].toString();
        m_refreshUrl = m_serverConfig["refresh_url"].toString();

        if (!m_connectUrl.isEmpty())
            qDebug() << "[Auth] Web auth configured: connect=" << m_connectUrl
                     << "refresh=" << m_refreshUrl;

        emit configFetched(true);
    });
}

/*****************************************************************************
 * Authentication flow
 *****************************************************************************/

void AgentAuthManager::authenticate()
{
    // 1. Static token from env var — skip web auth entirely
    if (!m_staticToken.isEmpty())
    {
        m_accessToken = m_staticToken;
        setAuthState(Authenticated);
        emit authenticated(m_accessToken);
        return;
    }

    // 2. Already have a JWT
    if (!m_accessToken.isEmpty())
    {
        setAuthState(Authenticated);
        emit authenticated(m_accessToken);
        return;
    }

    // 3. Have a refresh token — exchange it
    if (!m_refreshToken.isEmpty())
    {
        if (isAuthEnabled())
        {
            setAuthState(Authenticating);
            refreshAccessToken(m_refreshToken);
            return;
        }

        // Have refresh token but config not loaded — fetch config first
        if (!m_lastServerUrl.isEmpty())
        {
            qDebug() << "[Auth] Have refresh token but no config, fetching...";
            setAuthState(Authenticating);
            fetchServerConfig(m_lastServerUrl);
            connect(this, &AgentAuthManager::configFetched,
                    this, [this](bool success) {
                disconnect(this, &AgentAuthManager::configFetched, this, nullptr);
                if (success && isAuthEnabled())
                    refreshAccessToken(m_refreshToken);
                else
                {
                    setAuthState(LoggedOut);
                    emit loginRequired();
                }
            });
            return;
        }
    }

    // 4. No credentials — user must login
    setAuthState(LoggedOut);
    emit loginRequired();
}

void AgentAuthManager::login()
{
    if (!isAuthEnabled())
    {
        // Config not loaded yet (or fetch failed). Try fetching before giving up.
        if (!m_lastServerUrl.isEmpty())
        {
            qDebug() << "[Auth] Auth not configured yet, re-fetching server config";
            setAuthState(Authenticating);
            fetchServerConfig(m_lastServerUrl);
            connect(this, &AgentAuthManager::configFetched,
                    this, [this](bool success) {
                disconnect(this, &AgentAuthManager::configFetched, this, nullptr);
                if (success && isAuthEnabled())
                {
                    login(); // retry now that config is loaded
                }
                else
                {
                    setAuthState(LoggedOut);
                    emit authFailed(tr("Cannot reach server. Check your internet connection."));
                }
            });
            return;
        }

        qWarning() << "[Auth] Auth not configured, cannot login";
        emit authFailed(tr("Cannot reach server. Check your internet connection."));
        return;
    }

    setAuthState(Authenticating);
    startCallbackServer();

    if (m_callbackPort == 0)
    {
        setAuthState(LoggedOut);
        emit authFailed(tr("Failed to start login server."));
        return;
    }

    // Open browser to our connect page with callback URL
    QString connectUrl = QString("%1?cb=localhost:%2")
        .arg(m_connectUrl).arg(m_callbackPort);

    qDebug() << "[Auth] Opening browser for login:" << connectUrl;
    QDesktopServices::openUrl(QUrl(connectUrl));

    m_loginTimeout.start(LOGIN_TIMEOUT_MS);
}

void AgentAuthManager::handlePastedToken(const QString &input)
{
    stopCallbackServer();

    if (input.startsWith("eyJ"))
    {
        // JWT token — use directly
        qDebug() << "[Auth] Using pasted JWT token";
        m_accessToken = input;
        setAuthState(Authenticated);
        emit authenticated(m_accessToken);
    }
    else
    {
        // Try to extract tokens from callback URL
        QRegularExpression tokenRx("token=([^&\\s]+)");
        QRegularExpression refreshRx("refresh=([^&\\s]+)");

        QRegularExpressionMatch tm = tokenRx.match(input);
        QRegularExpressionMatch rm = refreshRx.match(input);

        if (tm.hasMatch())
        {
            qDebug() << "[Auth] Using pasted callback URL";
            m_accessToken = tm.captured(1);
            if (rm.hasMatch())
            {
                m_refreshToken = rm.captured(1);
                m_credentialStore.saveRefreshToken(m_refreshToken);
            }
            setAuthState(Authenticated);
            emit authenticated(m_accessToken);
        }
        else
        {
            qWarning() << "[Auth] Unrecognized token format";
            emit authFailed(tr("Unrecognized token format."));
        }
    }
}

void AgentAuthManager::logout()
{
    qDebug() << "[Auth] Logging out";
    m_accessToken.clear();
    m_refreshToken.clear();
    m_credentialStore.deleteRefreshToken();
    stopCallbackServer();
    setAuthState(LoggedOut);
}

/*****************************************************************************
 * Credential store callbacks
 *****************************************************************************/

void AgentAuthManager::onRefreshTokenLoaded(const QString &token)
{
    if (token.isEmpty())
    {
        qDebug() << "[Auth] No stored refresh token";
        if (m_staticToken.isEmpty())
            setAuthState(LoggedOut);
    }
    else
    {
        qDebug() << "[Auth] Loaded refresh token from keychain";
        m_refreshToken = token;
        // Don't auto-authenticate — wait for connectToServer()
    }
}

/*****************************************************************************
 * Token refresh
 *****************************************************************************/

void AgentAuthManager::refreshAccessToken(const QString &refreshToken)
{
    qDebug() << "[Auth] Refreshing access token via" << m_refreshUrl;

    QUrl url(m_refreshUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["refresh_token"] = refreshToken;

    QNetworkReply *reply = m_networkManager->post(request,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onRefreshFinished(reply);
    });
}

void AgentAuthManager::onRefreshFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "[Auth] Token refresh failed:" << reply->errorString();
        QByteArray responseData = reply->readAll();
        qWarning() << "[Auth] Response:" << responseData;

        // Refresh token may be expired — clear and require re-login
        m_refreshToken.clear();
        m_credentialStore.deleteRefreshToken();
        setAuthState(LoggedOut);
        emit authFailed(tr("Authentication expired. Please login again."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject result = doc.object();

    QString accessToken = result["access_token"].toString();
    QString newRefreshToken = result["refresh_token"].toString();

    if (accessToken.isEmpty())
    {
        qWarning() << "[Auth] No access_token in refresh response";
        setAuthState(LoggedOut);
        emit authFailed(tr("Login failed: no token received."));
        return;
    }

    qDebug() << "[Auth] Got refreshed access token";
    m_accessToken = accessToken;

    if (!newRefreshToken.isEmpty())
    {
        m_refreshToken = newRefreshToken;
        m_credentialStore.saveRefreshToken(newRefreshToken);
    }

    setAuthState(Authenticated);
    emit authenticated(m_accessToken);
}

/*****************************************************************************
 * Local callback server
 *****************************************************************************/

void AgentAuthManager::startCallbackServer()
{
    if (m_callbackServer)
        return;

    m_callbackServer = new QTcpServer(this);
    connect(m_callbackServer, &QTcpServer::newConnection,
            this, &AgentAuthManager::onCallbackConnection);

    for (quint16 port : CALLBACK_PORTS)
    {
        if (m_callbackServer->listen(QHostAddress::LocalHost, port))
        {
            m_callbackPort = port;
            qDebug() << "[Auth] Callback server on port" << m_callbackPort;
            return;
        }
    }

    qWarning() << "[Auth] Failed to bind any callback port";
    delete m_callbackServer;
    m_callbackServer = nullptr;
    m_callbackPort = 0;
}

void AgentAuthManager::stopCallbackServer()
{
    m_loginTimeout.stop();
    if (m_callbackServer)
    {
        m_callbackServer->close();
        delete m_callbackServer;
        m_callbackServer = nullptr;
        m_callbackPort = 0;
    }
}

void AgentAuthManager::onLoginTimeout()
{
    qWarning() << "[Auth] Login timed out";
    stopCallbackServer();
    setAuthState(LoggedOut);
    emit authFailed(tr("Login timed out. Please try again."));
}

void AgentAuthManager::onCallbackConnection()
{
    if (!m_callbackServer)
        return;

    QTcpSocket *socket = m_callbackServer->nextPendingConnection();
    if (!socket)
        return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray data = socket->readAll();
        QString request = QString::fromUtf8(data);

        // Ignore non-callback requests (favicon, etc.)
        if (!request.contains("GET /callback?"))
        {
            socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
            socket->flush();
            socket->disconnectFromHost();
            socket->deleteLater();
            return;
        }

        // Parse: GET /callback?token=JWT&refresh=REFRESH_TOKEN
        QString token, refresh;
        QRegularExpression tokenRx("token=([^&\\s]+)");
        QRegularExpression refreshRx("refresh=([^&\\s]+)");

        QRegularExpressionMatch m;
        m = tokenRx.match(request);
        if (m.hasMatch()) token = m.captured(1);
        m = refreshRx.match(request);
        if (m.hasMatch()) refresh = m.captured(1);

        // Respond to browser
        QString html = !token.isEmpty()
            ? "<html><body><h2>Login successful!</h2><p>You can close this tab and return to QLC+.</p></body></html>"
            : "<html><body><h2>Login failed</h2><p>No token received.</p></body></html>";

        socket->write(QString(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n%1"
        ).arg(html).toUtf8());
        socket->flush();
        socket->disconnectFromHost();
        socket->deleteLater();

        if (!token.isEmpty())
        {
            qDebug() << "[Auth] Received callback with tokens";
            QString capturedToken = token;
            QString capturedRefresh = refresh;
            QTimer::singleShot(0, this, [this, capturedToken, capturedRefresh]() {
                stopCallbackServer();
                m_accessToken = capturedToken;
                if (!capturedRefresh.isEmpty())
                {
                    m_refreshToken = capturedRefresh;
                    m_credentialStore.saveRefreshToken(capturedRefresh);
                }
                setAuthState(Authenticated);
                emit authenticated(m_accessToken);
            });
        }
        else
        {
            qWarning() << "[Auth] Callback missing token";
            QTimer::singleShot(0, this, [this]() {
                stopCallbackServer();
                setAuthState(LoggedOut);
                emit authFailed(tr("Login failed: no token received."));
            });
        }
    });
}
