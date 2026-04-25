#pragma once
// -------------------------------------------------------
// updater.h — Автообновление из GitHub Releases
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonObject>

#include "jarvis_core_export.h"

class QNetworkAccessManager;
class QNetworkReply;

struct UpdateInfo
{
    QString componentName;
    QString currentVersion;
    QString latestVersion;
    QString downloadUrl;
    QString changelog;
    qint64 fileSize = 0;
    bool updateAvailable = false;
};

class JARVIS_CORE_EXPORT Updater : public QObject
{
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);

    void setRepository(const QString& owner, const QString& repo);
    void setCurrentVersion(const QString& version);

    void checkForUpdates();
    void downloadUpdate(const UpdateInfo& info);

    static QString updatesDir();

signals:
    void updateAvailable(const UpdateInfo& info);
    void noUpdatesAvailable();
    void downloadProgress(qint64 received, qint64 total);
    void updateDownloaded(const QString& filePath);
    void updateError(const QString& error);
    void updateApplied(const QString& componentName);

private:
    void handleReleasesResponse(QNetworkReply* reply);
    void handleDownloadResponse(QNetworkReply* reply, const UpdateInfo& info);

    QNetworkAccessManager* m_network = nullptr;
    QString m_owner;
    QString m_repo;
    QString m_currentVersion;
};
