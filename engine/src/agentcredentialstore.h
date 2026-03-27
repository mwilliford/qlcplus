/*
  Q Light Controller Plus
  agentcredentialstore.h

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

#ifndef AGENTCREDENTIALSTORE_H
#define AGENTCREDENTIALSTORE_H

#include <QObject>
#include <QString>

/**
 * Secure storage for auth tokens using the OS keychain.
 *
 * Uses qtkeychain (Qt6Keychain) to store/retrieve tokens:
 *   - macOS: Keychain Services
 *   - Windows: Windows Credential Manager
 *   - Linux: libsecret (GNOME) / kwallet (KDE)
 *
 * Falls back to QSettings if no secure backend is available.
 * All operations are asynchronous — results delivered via signals.
 *
 * Stored under service name "bunnyhole-ai" with keys:
 *   - "refresh_token" — Cognito refresh token (30-day lifetime)
 */
class AgentCredentialStore : public QObject
{
    Q_OBJECT

public:
    explicit AgentCredentialStore(QObject *parent = nullptr);

    /** Read the refresh token from secure storage. Result via refreshTokenLoaded(). */
    void loadRefreshToken();

    /** Store a refresh token in secure storage. Result via refreshTokenSaved(). */
    void saveRefreshToken(const QString &token);

    /** Delete the refresh token from secure storage. Result via refreshTokenDeleted(). */
    void deleteRefreshToken();

    /** Returns true if using OS keychain, false if QSettings fallback. */
    bool isSecureBackend() const { return m_secureBackend; }

signals:
    /** Emitted when refresh token is loaded. Token is empty if not found. */
    void refreshTokenLoaded(const QString &token);

    /** Emitted when refresh token is saved. success=false on write error. */
    void refreshTokenSaved(bool success, const QString &error);

    /** Emitted when refresh token is deleted. */
    void refreshTokenDeleted(bool success, const QString &error);

private:
    static constexpr const char *SERVICE_NAME = "bunnyhole-ai";
    static constexpr const char *KEY_REFRESH_TOKEN = "refresh_token";

    /** Fall back to QSettings when keychain is unavailable. */
    void loadFromSettings();
    void saveToSettings(const QString &token);
    void deleteFromSettings();

    bool m_secureBackend;
};

#endif // AGENTCREDENTIALSTORE_H
