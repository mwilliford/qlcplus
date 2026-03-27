/*
  Q Light Controller Plus
  agentauthmanager.h

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

#ifndef AGENTAUTHMANAGER_H
#define AGENTAUTHMANAGER_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>
#include <QJsonObject>

#include "agentcredentialstore.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

/**
 * Manages authentication for the agent connection.
 *
 * Handles three auth modes:
 *   1. Static token (AGENT_API_TOKEN env var) — dev/testing
 *   2. Stored refresh token → JWT refresh — returning user
 *   3. Browser login via web app → tokens via callback — first-time login
 *
 * Owns the credential store (OS keychain) and local callback server.
 * AgentConnection delegates all auth to this class and waits for
 * authenticated() before opening the WebSocket.
 */
class AgentAuthManager : public QObject
{
    Q_OBJECT

public:
    enum AuthState
    {
        Unknown,        ///< Haven't checked credentials yet
        LoggedOut,      ///< No credentials available
        Authenticating, ///< Exchanging tokens or waiting for callback
        Authenticated   ///< Have a valid JWT, ready to connect
    };
    Q_ENUM(AuthState)

    explicit AgentAuthManager(QObject *parent = nullptr);
    ~AgentAuthManager();

    AuthState authState() const { return m_authState; }

    /** The current agent JWT. Empty if not authenticated. */
    QString accessToken() const { return m_accessToken; }

    /** True if web auth is configured (connect_url from server /config). */
    bool isAuthEnabled() const { return !m_connectUrl.isEmpty(); }

    /** True if we have a stored refresh token or static token. */
    bool hasStoredCredentials() const;

    /** Clear the in-memory JWT so authenticate() will re-exchange the refresh token. */
    void clearAccessToken() { m_accessToken.clear(); setAuthState(Unknown); }

    /** Stop the local callback server (called on disconnect/cancel). */
    void stopCallbackServer();

    /** Fetch config from the server's /config endpoint. Non-blocking, fail-safe. */
    void fetchServerConfig(const QUrl &serverUrl);

    /** Last fetched server config (full JSON). Empty if not yet fetched. */
    QJsonObject serverConfig() const { return m_serverConfig; }

public slots:
    /**
     * Ensure we have a valid token. Tries in order:
     *   1. Return existing JWT if valid
     *   2. Exchange refresh token for new JWT
     *   3. Emit loginRequired() if no credentials
     */
    void authenticate();

    /** Start browser login flow (opens web app /connect page). */
    void login();

    /** Handle a manually pasted token or callback URL.
     *  Accepts: JWT (eyJ...), callback URL (...?token=X&refresh=Y). */
    void handlePastedToken(const QString &input);

    /** Clear all credentials and tokens. */
    void logout();

signals:
    /** JWT is ready — AgentConnection can proceed with WebSocket. */
    void authenticated(const QString &accessToken);

    /** No credentials available, user must login via browser. */
    void loginRequired();

    /** Auth failed (expired refresh token, network error, etc). */
    void authFailed(const QString &error);

    /** Auth state changed. */
    void authStateChanged(AgentAuthManager::AuthState state);

    /** Server config fetched (or failed). */
    void configFetched(bool success);

private slots:
    void onRefreshTokenLoaded(const QString &token);
    void onCallbackConnection();
    void onLoginTimeout();

private:
    void setAuthState(AuthState state);
    void refreshAccessToken(const QString &refreshToken);
    void onRefreshFinished(QNetworkReply *reply);
    void startCallbackServer();

    AuthState m_authState;
    AgentCredentialStore m_credentialStore;
    QNetworkAccessManager *m_networkManager;

    // Tokens
    QString m_accessToken;      // Agent JWT — short-lived, in-memory only
    QString m_refreshToken;     // Long-lived, persisted in keychain
    QString m_staticToken;      // From AGENT_API_TOKEN env var

    // Server config (full JSON from /config endpoint)
    QJsonObject m_serverConfig;

    // Web auth config (from server /config)
    QString m_connectUrl;   // e.g. "https://www.bunnyhole.ai/connect"
    QString m_refreshUrl;   // e.g. "https://api.bunnyhole.ai/api/auth/refresh"
    QUrl m_lastServerUrl;   // For re-fetching config on login retry

    // Local callback server (receives tokens from browser redirect)
    QTcpServer *m_callbackServer;
    quint16 m_callbackPort;
    QTimer m_loginTimeout;
    static constexpr int LOGIN_TIMEOUT_MS = 600000; // 10 minutes
    static constexpr quint16 CALLBACK_PORTS[] = {19876, 19877, 19878, 19879, 19880};
};

#endif // AGENTAUTHMANAGER_H
