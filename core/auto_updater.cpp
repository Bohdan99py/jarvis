// -------------------------------------------------------
// auto_updater.cpp — Автообновление через GitHub Releases
//
// Алгоритм:
//   1. GET https://api.github.com/repos/{user}/{repo}/releases/latest
//   2. Сравнить tag_name с текущей версией
//   3. Если новее — предложить скачать
//   4. Скачать .exe установщик из assets
//   5. Запустить установщик и закрыть текущее приложение
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

// ============================================================
// Конструктор
// ============================================================

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

    // Папка для скачивания
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
        if (!silent) {
            emit updateError(QStringLiteral("Не удалось проверить обновления: ")
                             + reply->errorString());
        }
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        if (!silent) {
            emit updateError(QStringLiteral("Некорректный ответ от GitHub."));
        }
        return;
    }

    QJsonObject release = doc.object();
    QString tagName = release[QStringLiteral("tag_name")].toString();
    QString body = release[QStringLiteral("body")].toString();

    // Убираем 'v' из тега если есть: "v2.1.0" → "2.1.0"
    QString remoteVersion = tagName;
    if (remoteVersion.startsWith(QChar('v')) || remoteVersion.startsWith(QChar('V'))) {
        remoteVersion = remoteVersion.mid(1);
    }

    if (!isNewerVersion(remoteVersion, m_currentVersion)) {
        if (!silent) {
            emit noUpdateAvailable();
        }
        return;
    }

    // Ищем установщик в assets
    QUrl installerUrl;
    QJsonArray assets = release[QStringLiteral("assets")].toArray();
    for (const auto& asset : assets) {
        QJsonObject assetObj = asset.toObject();
        QString name = assetObj[QStringLiteral("name")].toString();

        // Ищем .exe установщик (JARVIS-Setup-*.exe)
        if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
            name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive)) {
            installerUrl = QUrl(assetObj[QStringLiteral("browser_download_url")].toString());
            break;
        }
    }

    // Если .exe не нашли, ищем .zip
    if (installerUrl.isEmpty()) {
        for (const auto& asset : assets) {
            QJsonObject assetObj = asset.toObject();
            QString name = assetObj[QStringLiteral("name")].toString();

            if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
                installerUrl = QUrl(assetObj[QStringLiteral("browser_download_url")].toString());
                break;
            }
        }
    }

    if (installerUrl.isEmpty()) {
        if (!silent) {
            emit updateError(QStringLiteral("Обновление найдено (v") + remoteVersion
                             + QStringLiteral("), но установщик не найден в релизе."));
        }
        return;
    }

    emit updateAvailable(remoteVersion, body, installerUrl);
}

// ============================================================
// Скачивание и установка
// ============================================================

void AutoUpdater::downloadAndInstall(const QUrl& installerUrl)
{
    QNetworkRequest request;
    request.setUrl(installerUrl);
    request.setRawHeader("User-Agent", "JARVIS-AutoUpdater/1.0");
    // GitHub перенаправляет на CDN — разрешаем редиректы
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadReply = m_network->get(request);

    // Прогресс
    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            int percent = static_cast<int>(received * 100 / total);
            emit downloadProgress(percent);
        }
    });

    // Завершение
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

    // Определяем имя файла из URL
    QString fileName = reply->url().fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("JARVIS-Setup.exe");
    }

    QString filePath = m_downloadPath + QDir::separator() + fileName;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit updateError(QStringLiteral("Не удалось сохранить файл: ") + filePath);
        return;
    }

    file.write(data);
    file.close();

    emit downloadFinished(filePath);

    // Запускаем установщик и закрываем приложение
    if (filePath.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        // Inno Setup: /SILENT для тихой установки, /CLOSEAPPLICATIONS чтобы закрыл JARVIS
        QProcess::startDetached(filePath, {QStringLiteral("/SILENT"),
                                           QStringLiteral("/CLOSEAPPLICATIONS")});
        QCoreApplication::quit();
    }
}

// ============================================================
// Сравнение версий
// ============================================================

bool AutoUpdater::isNewerVersion(const QString& remote, const QString& current) const
{
    // Разбиваем "2.1.0" → [2, 1, 0]
    QStringList remoteParts = remote.split(QChar('.'));
    QStringList currentParts = current.split(QChar('.'));

    // Дополняем нулями если разная длина
    while (remoteParts.size() < 3) remoteParts.append(QStringLiteral("0"));
    while (currentParts.size() < 3) currentParts.append(QStringLiteral("0"));

    for (int i = 0; i < 3; ++i) {
        int r = remoteParts[i].toInt();
        int c = currentParts[i].toInt();

        if (r > c) return true;
        if (r < c) return false;
    }

    return false; // Равны
}
