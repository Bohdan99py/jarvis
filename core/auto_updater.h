#pragma once
// -------------------------------------------------------
// auto_updater.h — Автообновление через GitHub Releases
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QUrl>

#include "jarvis_core_export.h"

class QNetworkAccessManager;
class QNetworkReply;

class JARVIS_CORE_EXPORT AutoUpdater : public QObject
{
    Q_OBJECT

public:
    explicit AutoUpdater(const QString& currentVersion,
                         const QString& githubUser,
                         const QString& githubRepo,
                         QObject* parent = nullptr);

    void checkForUpdates(bool silent = false);
    void downloadPendingUpdate();

    QString pendingVersion() const { return m_pendingVersion; }
    QString pendingNotes()   const { return m_pendingNotes; }
    bool    hasUpdate()      const { return !m_pendingVersion.isEmpty(); }

    signals:
        void updateAvailable(const QString& version, const QString& releaseNotes, const QUrl& url);
    void noUpdateAvailable();
    void downloadProgress(int percent);          // 0..100
    void downloadFinished(const QString& path);  // file saved — UI opens folder
    void updateError(const QString& message);

private:
    void downloadAndInstall(const QUrl& installerUrl);
    void onCheckFinished(QNetworkReply* reply, bool silent);
    void onDownloadFinished(QNetworkReply* reply);
    bool isNewerVersion(const QString& remote, const QString& current) const;

    QString  m_currentVersion;
    QString  m_githubUser;
    QString  m_githubRepo;
    QString  m_downloadPath;

    QString  m_pendingVersion;
    QUrl     m_pendingUrl;
    QString  m_pendingNotes;

    QNetworkAccessManager* m_network       = nullptr;
    QNetworkReply*         m_downloadReply = nullptr;
};