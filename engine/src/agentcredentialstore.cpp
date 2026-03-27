/*
  Q Light Controller Plus
  agentcredentialstore.cpp

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

#include "agentcredentialstore.h"

#include <QDebug>
#include <QSettings>

#if defined(HAVE_QTKEYCHAIN)
#include <qt6keychain/keychain.h>
#endif

static const char *SETTINGS_GROUP = "agent/credentials";

AgentCredentialStore::AgentCredentialStore(QObject *parent)
    : QObject(parent)
#if defined(HAVE_QTKEYCHAIN)
    , m_secureBackend(true)
#else
    , m_secureBackend(false)
#endif
{
}

// --- Load ---

void AgentCredentialStore::loadRefreshToken()
{
#if defined(HAVE_QTKEYCHAIN)
    auto *job = new QKeychain::ReadPasswordJob(SERVICE_NAME);
    job->setKey(KEY_REFRESH_TOKEN);
    // QueuedConnection ensures the lambda runs on our thread, not the keychain worker
    connect(job, &QKeychain::ReadPasswordJob::finished, this, [this, job]() {
        if (job->error() == QKeychain::NoError) {
            qDebug() << "[CredentialStore] Loaded refresh token from keychain";
            emit refreshTokenLoaded(job->textData());
        } else if (job->error() == QKeychain::EntryNotFound) {
            qDebug() << "[CredentialStore] No refresh token in keychain";
            emit refreshTokenLoaded(QString());
        } else {
            qWarning() << "[CredentialStore] Keychain read failed:"
                        << job->errorString() << "— falling back to QSettings";
            m_secureBackend = false;
            loadFromSettings();
        }
        job->deleteLater();
    });
    job->start();
#else
    loadFromSettings();
#endif
}

void AgentCredentialStore::loadFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    QString token = settings.value("refresh_token").toString();
    settings.endGroup();

    if (!token.isEmpty())
        qDebug() << "[CredentialStore] Loaded refresh token from QSettings (insecure)";
    else
        qDebug() << "[CredentialStore] No refresh token in QSettings";

    emit refreshTokenLoaded(token);
}

// --- Save ---

void AgentCredentialStore::saveRefreshToken(const QString &token)
{
#if defined(HAVE_QTKEYCHAIN)
    if (m_secureBackend) {
        auto *job = new QKeychain::WritePasswordJob(SERVICE_NAME);
        job->setKey(KEY_REFRESH_TOKEN);
        job->setTextData(token);
        connect(job, &QKeychain::WritePasswordJob::finished, this, [this, job, token]() {
            if (job->error() == QKeychain::NoError) {
                qDebug() << "[CredentialStore] Saved refresh token to keychain";
                emit refreshTokenSaved(true, QString());
            } else {
                qWarning() << "[CredentialStore] Keychain write failed:"
                            << job->errorString() << "— falling back to QSettings";
                m_secureBackend = false;
                saveToSettings(token);
            }
            job->deleteLater();
        });
        job->start();
        return;
    }
#endif
    saveToSettings(token);
}

void AgentCredentialStore::saveToSettings(const QString &token)
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("refresh_token", token);
    settings.endGroup();

    qDebug() << "[CredentialStore] Saved refresh token to QSettings (insecure)";
    emit refreshTokenSaved(true, QString());
}

// --- Delete ---

void AgentCredentialStore::deleteRefreshToken()
{
#if defined(HAVE_QTKEYCHAIN)
    if (m_secureBackend) {
        auto *job = new QKeychain::DeletePasswordJob(SERVICE_NAME);
        job->setKey(KEY_REFRESH_TOKEN);
        connect(job, &QKeychain::DeletePasswordJob::finished, this, [this, job]() {
            if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound) {
                qDebug() << "[CredentialStore] Deleted refresh token from keychain";
                emit refreshTokenDeleted(true, QString());
            } else {
                qWarning() << "[CredentialStore] Keychain delete failed:" << job->errorString();
                emit refreshTokenDeleted(false, job->errorString());
            }
            job->deleteLater();
        });
        job->start();
        return;
    }
#endif
    deleteFromSettings();
}

void AgentCredentialStore::deleteFromSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.remove("refresh_token");
    settings.endGroup();

    qDebug() << "[CredentialStore] Deleted refresh token from QSettings";
    emit refreshTokenDeleted(true, QString());
}
