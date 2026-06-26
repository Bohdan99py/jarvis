// -------------------------------------------------------
// auto_updater.cpp — Автообновление через GitHub Releases
// -------------------------------------------------------

#include "auto_updater.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

AutoUpdater::AutoUpdater(const QString& currentVersion,
                         const QString& githubUser,
                         const QString& githubRepo,
                         QObject* parent)
    : QObject(parent)
    , m_currentVersion(currentVersion)
    , m_githubUser(githubUser)
    , m_githubRepo(githubRepo)
{
    m_network = new QNetworkAccessManager(this);
    m_downloadPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

void AutoUpdater::checkForUpdates(bool silent)
{
    QString url = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                      .arg(m_githubUser, m_githubRepo);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "JARVIS-AutoUpdater/1.0");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, silent]() {
        onCheckFinished(reply, silent);
    });
}

void AutoUpdater::onCheckFinished(QNetworkReply* reply, bool silent)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (!silent)
            emit updateError(QStringLiteral("Failed to check for updates: ") + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        if (!silent) emit updateError(QStringLiteral("Invalid response from GitHub."));
        return;
    }

    QJsonObject release = doc.object();
    QString tagName = release[QStringLiteral("tag_name")].toString();
    QString body = release[QStringLiteral("body")].toString();

    QString remoteVersion = tagName;
    if (remoteVersion.startsWith(QChar('v')) || remoteVersion.startsWith(QChar('V')))
        remoteVersion = remoteVersion.mid(1);

    if (!isNewerVersion(remoteVersion, m_currentVersion)) {
        if (!silent) emit noUpdateAvailable();
        return;
    }

    QUrl installerUrl;
    QJsonArray assets = release[QStringLiteral("assets")].toArray();

    for (const auto& asset : assets) {
        QJsonObject obj = asset.toObject();
        QString name = obj[QStringLiteral("name")].toString();
        if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
            name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive)) {
            installerUrl = QUrl(obj[QStringLiteral("browser_download_url")].toString());
            break;
        }
    }

    if (installerUrl.isEmpty()) {
        for (const auto& asset : assets) {
            QJsonObject obj = asset.toObject();
            QString name = obj[QStringLiteral("name")].toString();
            if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                installerUrl = QUrl(obj[QStringLiteral("browser_download_url")].toString());
                break;
            }
        }
    }

    if (installerUrl.isEmpty()) {
        if (!silent)
            emit updateError(QStringLiteral("Update v") + remoteVersion
                             + QStringLiteral(" found, but no installer available."));
        return;
    }

    m_pendingVersion = remoteVersion;
    m_pendingUrl     = installerUrl;
    m_pendingNotes   = body;

    emit updateAvailable(remoteVersion, body, installerUrl);
}

void AutoUpdater::downloadPendingUpdate()
{
    if (m_pendingUrl.isEmpty()) return;
    downloadAndInstall(m_pendingUrl);
}

void AutoUpdater::downloadAndInstall(const QUrl& installerUrl)
{
    QNetworkRequest request;
    request.setUrl(installerUrl);
    request.setRawHeader("User-Agent", "JARVIS-AutoUpdater/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadReply = m_network->get(request);

    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, [this](qint64 received, qint64 total) {
        if (total > 0)
            emit downloadProgress(static_cast<int>(received * 100 / total));
    });

    connect(m_downloadReply, &QNetworkReply::finished, this, [this]() {
        onDownloadFinished(m_downloadReply);
    });
}

static QString winErrorString(DWORD code)
{
    switch (code) {
    case 0:                     return QStringLiteral("Success (0)");
    case ERROR_FILE_NOT_FOUND:  return QStringLiteral("File Not Found (2)");
    case ERROR_PATH_NOT_FOUND:  return QStringLiteral("Path Not Found (3)");
    case ERROR_ACCESS_DENIED:   return QStringLiteral("Access Denied (5)");
    case ERROR_CANCELLED:       return QStringLiteral("User Cancelled UAC (1223)");
    case ERROR_BAD_EXE_FORMAT:  return QStringLiteral("Bad EXE Format (193)");
    default:
        return QStringLiteral("Error %1").arg(code);
    }
}

void AutoUpdater::onDownloadFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    m_downloadReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit updateError(QStringLiteral("Download error: ") + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    if (data.isEmpty()) {
        emit updateError(QStringLiteral("Downloaded file is empty."));
        return;
    }

    QString fileName = reply->url().fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("JARVIS-Setup.exe");

    QString installerPath = m_downloadPath + QDir::separator() + fileName;

    QFile file(installerPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit updateError(QStringLiteral("Failed to save: ") + installerPath);
        return;
    }
    file.write(data);
    file.close();

    emit downloadFinished(installerPath);

    if (!installerPath.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        return;

    // Stable wide-string buffers — must outlive the ShellExecuteExW call.
    const std::wstring wPath = QDir::toNativeSeparators(installerPath).toStdWString();
    const std::wstring wDir  = QDir::toNativeSeparators(
        QFileInfo(installerPath).absolutePath()).toStdWString();

    qDebug() << "[Updater] Launching:" << installerPath;
    qDebug() << "[Updater] Working dir:" << QFileInfo(installerPath).absolutePath();

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd         = nullptr;
    sei.lpVerb       = L"runas";
    sei.lpFile       = wPath.c_str();
    sei.lpParameters = nullptr;
    sei.lpDirectory  = wDir.c_str();
    sei.nShow        = SW_SHOWNORMAL;

    SetLastError(0);
    BOOL launched = ShellExecuteExW(&sei);

    if (!launched) {
        DWORD err = GetLastError();
        qWarning() << "[Updater] ShellExecuteExW(runas) failed:"
                    << winErrorString(err);

        if (err == ERROR_CANCELLED) {
            qDebug() << "[Updater] UAC declined — retrying with 'open' verb";
            sei.lpVerb = L"open";
            SetLastError(0);
            launched = ShellExecuteExW(&sei);

            if (!launched) {
                DWORD err2 = GetLastError();
                qWarning() << "[Updater] ShellExecuteExW(open) also failed:"
                            << winErrorString(err2);
                emit updateError(
                    QStringLiteral("Failed to launch installer.\n"
                                   "Verb 'runas': %1\nVerb 'open': %2\n"
                                   "Open manually: %3")
                        .arg(winErrorString(err),
                             winErrorString(err2),
                             installerPath));
                return;
            }
        } else {
            emit updateError(
                QStringLiteral("Failed to launch installer: %1\n"
                               "Open manually: %2")
                    .arg(winErrorString(err), installerPath));
            return;
        }
    }

    if (sei.hProcess)
        CloseHandle(sei.hProcess);

    qDebug() << "[Updater] Installer launched successfully — terminating JARVIS";
    emit installerLaunched();
    ExitProcess(0);
}

bool AutoUpdater::isNewerVersion(const QString& remote, const QString& current) const
{
    QStringList rParts = remote.split(QChar('.'));
    QStringList cParts = current.split(QChar('.'));

    while (rParts.size() < 3) rParts.append(QStringLiteral("0"));
    while (cParts.size() < 3) cParts.append(QStringLiteral("0"));

    for (int i = 0; i < 3; ++i) {
        int r = rParts[i].toInt();
        int c = cParts[i].toInt();
        if (r > c) return true;
        if (r < c) return false;
    }
    return false;
}