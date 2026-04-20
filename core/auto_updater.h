#pragma once
// -------------------------------------------------------
// auto_updater.h — Автообновление через GitHub Releases
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class AutoUpdater : public QObject
{
    Q_OBJECT

public:
    explicit AutoUpdater(const QString& currentVersion,
                         const QString& githubUser,
                         const QString& githubRepo,
                         QObject* parent = nullptr);

    // Проверить наличие обновлений (асинхронно)
    void checkForUpdates(bool silent = true);

    // Скачать и установить обновление
    void downloadAndInstall(const QUrl& installerUrl);

    // Текущая версия
    QString currentVersion() const { return m_currentVersion; }

signals:
    // Найдено обновление
    void updateAvailable(const QString& newVersion,
                         const QString& releaseNotes,
                         const QUrl& downloadUrl);

    // Обновлений нет (только если silent=false)
    void noUpdateAvailable();

    // Прогресс скачивания (0-100)
    void downloadProgress(int percent);

    // Скачивание завершено, готово к установке
    void downloadFinished(const QString& installerPath);

    // Ошибка
    void updateError(const QString& error);

private slots:
    void onCheckFinished(QNetworkReply* reply, bool silent);
    void onDownloadFinished(QNetworkReply* reply);

private:
    // Сравнение версий: возвращает true если remote > current
    bool isNewerVersion(const QString& remote, const QString& current) const;

    QNetworkAccessManager* m_network = nullptr;
    QString m_currentVersion;
    QString m_githubUser;
    QString m_githubRepo;
    QString m_downloadPath;

    QNetworkReply* m_downloadReply = nullptr;
};
