/*
  Q Light Controller Plus
  gdtfshareclient.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "gdtfshareclient.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrlQuery>

GDTFShareClient::GDTFShareClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // Cache in user's app data: ~/Library/Application Support/qlcplus/gdtf-cache/
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/gdtf-cache");

    // Read env var credentials for automated testing.
    // When set, fetchList() auto-logs in if not already authenticated.
    m_envUser = qEnvironmentVariable("GDTF_SHARE_USER");
    m_envPass = qEnvironmentVariable("GDTF_SHARE_PASSWORD");
    if (!m_envUser.isEmpty())
        qDebug() << "[GDTFShare] Env credentials detected for user:" << m_envUser;
}

void GDTFShareClient::ensureCacheDir()
{
    QDir dir(m_cacheDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
}

void GDTFShareClient::handleAuthError()
{
    m_loggedIn = false;
    emit loginRequired();
}

void GDTFShareClient::login(const QString &username, const QString &password)
{
    QUrl url(QStringLiteral("%1/login.php").arg(kBaseUrl));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    QJsonObject body;
    body[QStringLiteral("user")] = username;
    body[QStringLiteral("password")] = password;

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            m_loggedIn = false;
            emit loginFailed(reply->errorString());
            return;
        }

        // Check the JSON response for result:true/false
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();

        if (!obj.value(QStringLiteral("result")).toBool())
        {
            m_loggedIn = false;
            QString err = obj.value(QStringLiteral("error")).toString(
                QStringLiteral("Login failed"));
            emit loginFailed(err);
            return;
        }

        // Session cookie is automatically stored by QNetworkAccessManager's cookie jar.
        m_loggedIn = true;
        qDebug() << "[GDTFShare] Login successful";
        emit loginSucceeded();
    });
}

void GDTFShareClient::fetchList()
{
    // Auto-login from env vars if not yet authenticated
    if (!m_loggedIn && !m_envUser.isEmpty() && !m_envPass.isEmpty())
    {
        // Login first, then fetchList will be called again from loginSucceeded
        login(m_envUser, m_envPass);
        return;
    }

    QUrl url(QStringLiteral("%1/getList.php").arg(kBaseUrl));
    QNetworkRequest req(url);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403)
            {
                handleAuthError();
                return;
            }
            emit listFailed(reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        if (!root.value(QStringLiteral("result")).toBool())
        {
            QString err = root.value(QStringLiteral("error")).toString(
                QStringLiteral("Failed to fetch fixture list"));
            emit listFailed(err);
            return;
        }

        QVariantList fixtures;
        QJsonArray list = root.value(QStringLiteral("list")).toArray();
        for (const QJsonValue &val : list)
        {
            if (!val.isObject())
                continue;
            QJsonObject obj = val.toObject();
            QVariantMap entry;
            // rid is an integer in the API response
            entry[QStringLiteral("rid")] =
                QString::number(obj.value(QStringLiteral("rid")).toInt());
            entry[QStringLiteral("manufacturer")] =
                obj.value(QStringLiteral("manufacturer")).toString();
            // API uses "fixture" not "name"
            entry[QStringLiteral("name")] =
                obj.value(QStringLiteral("fixture")).toString();
            entry[QStringLiteral("revision")] =
                obj.value(QStringLiteral("revision")).toString();
            entry[QStringLiteral("rating")] =
                obj.value(QStringLiteral("rating")).toString();
            fixtures.append(entry);
        }

        qDebug() << "[GDTFShare] Fetched" << fixtures.size() << "fixtures";
        emit listReady(fixtures);
    });
}

void GDTFShareClient::download(const QString &revisionId,
                                const QString &manufacturer,
                                const QString &model)
{
    ensureCacheDir();

    QUrl url(QStringLiteral("%1/downloadFile.php").arg(kBaseUrl));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("rid"), revisionId);
    url.setQuery(query);

    QNetworkRequest req(url);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, revisionId, manufacturer, model]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403)
            {
                handleAuthError();
                return;
            }
            emit downloadFailed(reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        if (data.isEmpty())
        {
            emit downloadFailed(QStringLiteral("Empty response for revision %1").arg(revisionId));
            return;
        }

        // Build filename: sanitize manufacturer and model for filesystem safety
        QString safeMfg = manufacturer;
        safeMfg.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_\\-]")),
                        QStringLiteral("_"));
        QString safeName = model;
        safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_\\-]")),
                         QStringLiteral("_"));

        QString filename;
        if (!safeMfg.isEmpty() && !safeName.isEmpty())
            filename = QStringLiteral("%1_%2_%3.gdtf").arg(safeMfg, safeName, revisionId);
        else
            filename = QStringLiteral("gdtf_%1.gdtf").arg(revisionId);

        QString localPath = m_cacheDir + QStringLiteral("/") + filename;

        QFile file(localPath);
        if (!file.open(QIODevice::WriteOnly))
        {
            emit downloadFailed(QStringLiteral("Cannot write to %1").arg(localPath));
            return;
        }
        file.write(data);
        file.close();

        qDebug() << "[GDTFShare] Downloaded" << filename << "(" << data.size() << "bytes)";
        emit downloadComplete(localPath);
    });
}
