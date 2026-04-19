#pragma once
// -------------------------------------------------------
// updater.h — Автообновление из GitHub Releases
//
// Проверяет новые версии, скачивает обновлённые DLL,
// заменяет файлы без полной пересборки.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonObject>

class QNetworkAccessManager;
class QNetworkReply;

struct UpdateInfo
{
    QString componentName;   // "core", "plugin_claude_api", etc.
    QString currentVersion;
    QString latestVersion;
    QString downloadUrl;
    QString changelog;
    qint64 fileSize = 0;
    bool updateAvailable = false;
};

class Updater : public QObject
{
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);

    // Настройки
    void setRepository(const QString& owner, const QString& repo);
    void setCurrentVersion(const QString& version);

    // Проверить обновления
    void checkForUpdates();

    // Скачать и применить обновление
    void downloadUpdate(const UpdateInfo& info);

    // Получить путь к папке обновлений
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
