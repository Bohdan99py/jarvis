#pragma once
// -------------------------------------------------------
// claude_api.h — Клиент Anthropic Claude API
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <functional>

#include "jarvis_core_export.h"

class QNetworkAccessManager;
class QNetworkReply;
class SessionMemory;

class JARVIS_CORE_EXPORT ClaudeApi : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeApi(SessionMemory* memory, QObject* parent = nullptr);

    void setApiKey(const QString& key);
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }
    QString apiKey() const { return m_apiKey; }  // для ScreenAgent Vision

    void setModel(const QString& model) { m_model = model; }

    using ResponseCallback = std::function<void(bool success, const QString& response)>;
    void sendMessage(const QString& userMessage, ResponseCallback callback);

    bool shouldUseApi(const QString& input) const;
    bool isRequesting() const { return m_requesting; }

    // true, если последний ответ был обрезан по лимиту max_tokens
    // (stop_reason == "max_tokens"). Используется для автопродолжения
    // генерации больших файлов — см. Jarvis::handleClaudeCodeResponse.
    bool wasTruncated() const { return m_lastStopReason == QStringLiteral("max_tokens"); }
    QString lastStopReason() const { return m_lastStopReason; }

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
    QString m_model = QStringLiteral("claude-sonnet-5");
    bool m_requesting = false;
    bool m_usingEmbeddedKey = false;
    QString m_lastStopReason; // "end_turn" | "max_tokens" | "stop_sequence" | ...

    // 8192 — безопасный потолок для неstreaming-запроса (Sonnet 4.6 поддерживает
    // до 64K, но без стриминга длинные генерации рискуют таймаутом на стороне
    // API; 8192 токенов ≈ 1500-2500 строк кода за один ответ, ~3 мин на 45 ток/с).
    // Для файлов больше этого — автопродолжение (см. Jarvis::handleClaudeCodeResponse).
    static constexpr int MAX_TOKENS = 8192;
};
