/*
  Q Light Controller Plus
  gdtfshareclient.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef GDTFSHARECLIENT_H
#define GDTFSHARECLIENT_H

#include <QObject>
#include <QNetworkCookieJar>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief REST client for gdtf-share.com public API.
 *
 * Three operations:
 *   1. login(user, pass) — POST /apis/public/login.php, stores session cookie
 *   2. fetchList()       — GET  /apis/public/getList.php, returns fixture metadata
 *   3. download(rid)     — GET  /apis/public/downloadFile.php?id={rid}, saves to cache
 *
 * Session cookies expire after 2 hours of inactivity. On 401/403 from
 * any endpoint, loginRequired() is emitted so the UI can prompt re-auth.
 *
 * Downloaded GDTF files are cached in the user's app data directory.
 * Per gdtf-share.com Terms of Service, cached files are for personal use
 * only and must not be redistributed.
 */
class GDTFShareClient : public QObject
{
    Q_OBJECT

public:
    explicit GDTFShareClient(QObject *parent = nullptr);

    /** @return true if a session cookie exists (may be expired). */
    bool isLoggedIn() const { return m_loggedIn; }

    /** @return the local cache directory path. */
    QString cacheDir() const { return m_cacheDir; }

    /** Login to gdtf-share.com. Emits loginSucceeded() or loginFailed(). */
    void login(const QString &username, const QString &password);

    /** Fetch the full fixture list. Emits listReady() or listFailed().
     *  If GDTF_SHARE_USER/PASSWORD env vars are set and not yet logged in,
     *  auto-logs in first. */
    void fetchList();

    /** @return true if GDTF_SHARE_USER and GDTF_SHARE_PASSWORD env vars are set. */
    bool hasEnvCredentials() const { return !m_envUser.isEmpty(); }

    /** Download a GDTF file by revision ID. Emits downloadComplete() or downloadFailed(). */
    void download(const QString &revisionId, const QString &manufacturer = {},
                  const QString &model = {});

signals:
    void loginSucceeded();
    void loginFailed(const QString &error);
    void loginRequired();  ///< Session expired, need re-auth

    /** Full fixture list from gdtf-share. Each entry is a QVariantMap with
     *  at least: "rid", "manufacturer", "name", "revision". */
    void listReady(const QVariantList &fixtures);
    void listFailed(const QString &error);

    /** A GDTF file was downloaded and saved to the cache.
     *  @param localPath absolute path to the cached .gdtf file */
    void downloadComplete(const QString &localPath);
    void downloadFailed(const QString &error);

private:
    void handleAuthError();
    void ensureCacheDir();

    QNetworkAccessManager *m_nam;
    QString m_cacheDir;
    bool m_loggedIn = false;

    // Env var credentials for automated testing
    QString m_envUser;
    QString m_envPass;

    static constexpr const char *kBaseUrl = "https://gdtf-share.com/apis/public";
};

#endif // GDTFSHARECLIENT_H
