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

    void setModel(const QString& model) { m_model = model; }

    using ResponseCallback = std::function<void(bool success, const QString& response)>;
    void sendMessage(const QString& userMessage, ResponseCallback callback);

    bool shouldUseApi(const QString& input) const;
    bool isRequesting() const { return m_requesting; }

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
    QString m_model = QStringLiteral("claude-sonnet-4-6");
    bool m_requesting = false;
    bool m_usingEmbeddedKey = false;

    static constexpr int MAX_TOKENS = 1024;
};
