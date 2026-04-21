#pragma once
// -------------------------------------------------------
// claude_api.h — Клиент Anthropic Claude API
//                Гибридный режим: локальный fallback + API
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class SessionMemory;

class ClaudeApi : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeApi(SessionMemory* memory, QObject* parent = nullptr);

    // Установить API-ключ
    void setApiKey(const QString& key);
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    // Установить модель (по умолчанию claude-sonnet-4-20250514)
    void setModel(const QString& model) { m_model = model; }

    // Отправить запрос к Claude API (асинхронно)
    // callback вызывается с ответом или ошибкой
    using ResponseCallback = std::function<void(bool success, const QString& response)>;
    void sendMessage(const QString& userMessage, ResponseCallback callback);

    // Проверить, нужно ли отправлять в API или обработать локально
    // true = отправить в API, false = обработать локально
    bool shouldUseApi(const QString& input) const;

    // Состояние
    bool isRequesting() const { return m_requesting; }

    // Загрузить / сохранить API-ключ
    void loadApiKey();
    void saveApiKey();

    signals:
        void requestStarted();
    void requestFinished();
    void apiError(const QString& error);

private:
    void handleReply(QNetworkReply* reply, ResponseCallback callback);

    QNetworkAccessManager* m_network = nullptr;
    SessionMemory* m_memory = nullptr;

    QString m_apiKey;
    QString m_model = QStringLiteral("claude-sonnet-4-6-20250827");
    bool m_requesting = false;
    bool m_usingEmbeddedKey = false;  // true = используется вшитый ключ

    static constexpr int MAX_TOKENS = 1024;
};