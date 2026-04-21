// -------------------------------------------------------
// auto_updater.cpp — Автообновление через GitHub Releases
// -------------------------------------------------------

#include "auto_updater.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QCoreApplication>
#include <QTimer>

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
            emit updateError(QStringLiteral("Не удалось проверить обновления: ") + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        if (!silent) emit updateError(QStringLiteral("Некорректный ответ от GitHub."));
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
            emit updateError(QStringLiteral("Обновление v") + remoteVersion
                             + QStringLiteral(" найдено, но установщик отсутствует."));
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

void AutoUpdater::onDownloadFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    m_downloadReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit updateError(QStringLiteral("Ошибка скачивания: ") + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    if (data.isEmpty()) {
        emit updateError(QStringLiteral("Скачанный файл пуст."));
        return;
    }

    QString fileName = reply->url().fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("JARVIS-Setup.exe");

    QString installerPath = m_downloadPath + QDir::separator() + fileName;

    QFile file(installerPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit updateError(QStringLiteral("Не удалось сохранить: ") + installerPath);
        return;
    }
    file.write(data);
    file.close();

    emit downloadFinished(installerPath);

    if (!installerPath.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        return;

    // Запускаем установщик напрямую (обычный режим с визардом)
    // Inno Setup сам умеет закрывать приложение через CloseApplications
    bool launched = QProcess::startDetached(installerPath, {});

    if (launched) {
        // Закрываем JARVIS через 2 секунды — установщик уже запущен
        QTimer::singleShot(2000, qApp, []() {
            QCoreApplication::exit(0);
        });
    } else {
        emit updateError(QStringLiteral("Не удалось запустить установщик: ") + installerPath);
    }
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