#pragma once
// -------------------------------------------------------
// gemini_api.h — Клиент Google Gemini API (бесплатный)
//                Используется для простых бесед (не код)
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class SessionMemory;

class GeminiApi : public QObject
{
    Q_OBJECT

public:
    explicit GeminiApi(SessionMemory* memory, QObject* parent = nullptr);

    // Установить API-ключ (бесплатный ключ с aistudio.google.com)
    void setApiKey(const QString& key);
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }
    QString apiKey() const { return m_apiKey; }  // для ScreenAgent и меню

    // Модель (по умолчанию gemini-2.0-flash — самая быстрая и бесплатная)
    void setModel(const QString& model) { m_model = model; }
    QString model() const { return m_model; }

    // Отправить запрос (асинхронно)
    using ResponseCallback = std::function<void(bool success, const QString& response)>;
    void sendMessage(const QString& userMessage, ResponseCallback callback);

    // Состояние
    bool isRequesting() const { return m_requesting; }

    // Загрузить / сохранить ключ
    void loadApiKey();
    void saveApiKey();

signals:
    void requestStarted();
    void requestFinished();
    void apiError(const QString& error);

private:
    void handleReply(QNetworkReply* reply, ResponseCallback callback);

    QNetworkAccessManager* m_network   = nullptr;
    SessionMemory*         m_memory    = nullptr;

    QString m_apiKey;
    QString m_model      = QStringLiteral("gemini-2.0-flash");
    bool    m_requesting = false;

    static constexpr int MAX_TOKENS = 1024;
};
