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

// ============================================================
// Проверка обновлений
// ============================================================

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

    // Ищем установщик в assets
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

// ============================================================
// Скачивание отложенного обновления
// ============================================================

void AutoUpdater::downloadPendingUpdate()
{
    if (m_pendingUrl.isEmpty()) return;
    downloadAndInstall(m_pendingUrl);
}

// ============================================================
// Скачивание и установка
// ============================================================

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

    // Создаём bat-скрипт который:
    //   1. Ждёт закрытия Jarvis.exe (до 15 секунд)
    //   2. Запускает установщик в тихом режиме
    //   3. Удаляет себя
    QString batPath = m_downloadPath + QDir::separator() + QStringLiteral("jarvis_update.bat");

    QFile bat(batPath);
    if (bat.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString script = QStringLiteral(
            "@echo off\r\n"
            "echo Waiting for JARVIS to close...\r\n"
            "set /a TRIES=0\r\n"
            ":WAIT_LOOP\r\n"
            "tasklist /FI \"IMAGENAME eq Jarvis.exe\" 2>NUL | find /I /N \"Jarvis.exe\" >NUL\r\n"
            "if \"%ERRORLEVEL%\"==\"0\" (\r\n"
            "    set /a TRIES+=1\r\n"
            "    if %TRIES% GEQ 30 (\r\n"
            "        echo Timeout. Killing JARVIS...\r\n"
            "        taskkill /F /IM Jarvis.exe >NUL 2>&1\r\n"
            "        timeout /t 2 /nobreak >NUL\r\n"
            "        goto :RUN_SETUP\r\n"
            "    )\r\n"
            "    timeout /t 1 /nobreak >NUL\r\n"
            "    goto :WAIT_LOOP\r\n"
            ")\r\n"
            ":RUN_SETUP\r\n"
            "echo Starting installer...\r\n"
            "start \"\" \"%1\" /SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS\r\n"
            "timeout /t 3 /nobreak >NUL\r\n"
            "del \"%~f0\"\r\n"
        ).arg(installerPath);

        bat.write(script.toLocal8Bit());
        bat.close();

        // Запускаем bat скрытно (без окна)
        QProcess::startDetached(
            QStringLiteral("cmd.exe"),
            {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("/min"),
             QStringLiteral("\"JARVIS Update\""), batPath}
        );

        // Закрываем JARVIS через 500мс (даём bat-скрипту стартовать)
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    } else {
        // Fallback: запускаем установщик напрямую
        QProcess::startDetached(installerPath, {QStringLiteral("/SILENT"),
                                                QStringLiteral("/CLOSEAPPLICATIONS")});
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }
}

// ============================================================
// Сравнение версий
// ============================================================

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
