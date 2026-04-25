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

    void checkForUpdates(bool silent = true);
    void downloadAndInstall(const QUrl& installerUrl);

    QString currentVersion() const { return m_currentVersion; }

    bool    hasPendingUpdate() const { return !m_pendingUrl.isEmpty(); }
    QString pendingVersion()   const { return m_pendingVersion; }
    QUrl    pendingUrl()       const { return m_pendingUrl; }
    QString pendingNotes()     const { return m_pendingNotes; }

    void downloadPendingUpdate();

signals:
    void updateAvailable(const QString& newVersion,
                         const QString& releaseNotes,
                         const QUrl& downloadUrl);
    void noUpdateAvailable();
    void downloadProgress(int percent);
    void downloadFinished(const QString& installerPath);
    void updateError(const QString& error);

private slots:
    void onCheckFinished(QNetworkReply* reply, bool silent);
    void onDownloadFinished(QNetworkReply* reply);

private:
    bool isNewerVersion(const QString& remote, const QString& current) const;

    QNetworkAccessManager* m_network = nullptr;
    QString m_currentVersion;
    QString m_githubUser;
    QString m_githubRepo;
    QString m_downloadPath;

    QString m_pendingVersion;
    QUrl    m_pendingUrl;
    QString m_pendingNotes;

    QNetworkReply* m_downloadReply = nullptr;
};
