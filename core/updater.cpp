// -------------------------------------------------------
// updater.cpp — Автообновление из GitHub Releases
// -------------------------------------------------------

#include "updater.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QVersionNumber>

// ============================================================
// Конструктор
// ============================================================

Updater::Updater(QObject* parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
}

// ============================================================
// Настройки
// ============================================================

void Updater::setRepository(const QString& owner, const QString& repo)
{
    m_owner = owner;
    m_repo = repo;
}

void Updater::setCurrentVersion(const QString& version)
{
    m_currentVersion = version;
}

QString Updater::updatesDir()
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/updates");
    QDir().mkpath(dir);
    return dir;
}

// ============================================================
// Проверка обновлений
// ============================================================

void Updater::checkForUpdates()
{
    if (m_owner.isEmpty() || m_repo.isEmpty()) {
        emit updateError(QStringLiteral("Репозиторий не настроен."));
        return;
    }

    QString url = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                      .arg(m_owner, m_repo);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "JARVIS-Updater/2.0");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReleasesResponse(reply);
    });
}

void Updater::handleReleasesResponse(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit updateError(QStringLiteral("Ошибка проверки обновлений: ") + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        emit updateError(QStringLiteral("Некорректный ответ от GitHub."));
        return;
    }

    QJsonObject release = doc.object();
    QString tagName = release[QStringLiteral("tag_name")].toString();
    QString changelog = release[QStringLiteral("body")].toString();

    // Убираем префикс "v" если есть
    QString latestVersion = tagName;
    if (latestVersion.startsWith(QChar('v'))) {
        latestVersion = latestVersion.mid(1);
    }

    // Сравниваем версии
    QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
    QVersionNumber latest = QVersionNumber::fromString(latestVersion);

    if (latest <= current) {
        emit noUpdatesAvailable();
        return;
    }

    // Ищем ассеты для скачивания
    QJsonArray assets = release[QStringLiteral("assets")].toArray();

    for (const auto& assetVal : assets) {
        QJsonObject asset = assetVal.toObject();
        QString assetName = asset[QStringLiteral("name")].toString();
        QString downloadUrl = asset[QStringLiteral("browser_download_url")].toString();
        qint64 size = asset[QStringLiteral("size")].toVariant().toLongLong();

        // Определяем компонент по имени файла
        // Формат: JarvisCore.dll, JarvisPlugin_claude_api.dll, etc.
        QString componentName;
        if (assetName.contains(QStringLiteral("JarvisCore"))) {
            componentName = QStringLiteral("core");
        } else if (assetName.contains(QStringLiteral("JarvisPlugin"))) {
            componentName = assetName;
            componentName.remove(QStringLiteral("JarvisPlugin_"));
            componentName.remove(QStringLiteral(".dll"));
            componentName.remove(QStringLiteral(".so"));
            componentName.remove(QStringLiteral(".dylib"));
        } else if (assetName.contains(QStringLiteral("Jarvis")) &&
                   (assetName.endsWith(QStringLiteral(".zip")) ||
                    assetName.endsWith(QStringLiteral(".7z")))) {
            componentName = QStringLiteral("full");
        } else {
            continue;
        }

        UpdateInfo info;
        info.componentName = componentName;
        info.currentVersion = m_currentVersion;
        info.latestVersion = latestVersion;
        info.downloadUrl = downloadUrl;
        info.changelog = changelog;
        info.fileSize = size;
        info.updateAvailable = true;

        emit updateAvailable(info);
    }
}

// ============================================================
// Скачивание обновления
// ============================================================

void Updater::downloadUpdate(const UpdateInfo& info)
{
    QNetworkRequest request;
    request.setUrl(QUrl(info.downloadUrl));
    request.setRawHeader("User-Agent", "JARVIS-Updater/2.0");

    QNetworkReply* reply = m_network->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit downloadProgress(received, total);
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, info]() {
        handleDownloadResponse(reply, info);
    });
}

void Updater::handleDownloadResponse(QNetworkReply* reply, const UpdateInfo& info)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit updateError(QStringLiteral("Ошибка загрузки: ") + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();

    // Определяем имя файла из URL
    QString fileName = QUrl(info.downloadUrl).fileName();
    QString filePath = updatesDir() + QStringLiteral("/") + fileName;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit updateError(QStringLiteral("Не удалось сохранить файл: ") + filePath);
        return;
    }

    file.write(data);
    file.close();

    emit updateDownloaded(filePath);
}
