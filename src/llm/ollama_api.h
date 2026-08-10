#pragma once
// -------------------------------------------------------
// ollama_api.h — Клиент Ollama (OpenAI-compatible endpoint)
// Локальный LLM-роутинг через Ollama.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <functional>

// Ollama поднимает OpenAI-совместимый сервер на localhost:11434
// Модель выбирается пользователем (или дефолтная из настроек).

class OllamaApi : public QObject
{
public:
    explicit OllamaApi(QObject* parent = nullptr)
        : QObject(parent)
        , m_nam(new QNetworkAccessManager(this))
    {
        QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
        m_host  = cfg.value(QStringLiteral("ollama/host"),  QStringLiteral("http://localhost:11434")).toString();
        m_model = cfg.value(QStringLiteral("ollama/model"), QStringLiteral("llama3")).toString();
    }

    // ── Настройки ──────────────────────────────────────────────────────
    void setHost(const QString& host) {
        m_host = host;
        QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
            .setValue(QStringLiteral("ollama/host"), host);
    }
    void setModel(const QString& model) {
        m_model = model;
        QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
            .setValue(QStringLiteral("ollama/model"), model);
    }

    QString host()  const { return m_host;  }
    QString model() const { return m_model; }

    // Всегда "подключён", если Ollama запущена
    bool hasApiKey() const { return true; }
    void setApiKey(const QString&) {}   // не нужен, оставлен для совместимости

    // Системный промпт (персона JARVIS + директивы) — задаётся Jarvis'ом
    // перед запросом, уходит отдельным system-сообщением.
    void setSystemPrompt(const QString& prompt) { m_systemPrompt = prompt; }

    // ── Основной метод — отправка сообщения ────────────────────────────
    // Использует /v1/chat/completions (OpenAI-compatible).
    // callback(success, responseText)
    void sendMessage(const QString& message,
                     std::function<void(bool, const QString&)> callback)
    {
        QJsonObject body;
        body[QStringLiteral("model")]  = m_model;
        body[QStringLiteral("stream")] = false;

        QJsonArray messages;
        if (!m_systemPrompt.isEmpty()) {
            QJsonObject sys;
            sys[QStringLiteral("role")]    = QStringLiteral("system");
            sys[QStringLiteral("content")] = m_systemPrompt;
            messages.append(sys);
        }
        QJsonObject msg;
        msg[QStringLiteral("role")]    = QStringLiteral("user");
        msg[QStringLiteral("content")] = message;
        messages.append(msg);
        body[QStringLiteral("messages")] = messages;

        QNetworkRequest req(QUrl(m_host + QStringLiteral("/v1/chat/completions")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("Authorization", "Bearer ollama");
        req.setTransferTimeout(60000); // 60 сек — локальные модели медленнее

        QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson());

        connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                callback(false, QStringLiteral("Ollama недоступна: ") + reply->errorString()
                              + QStringLiteral("\n\nПроверь что Ollama запущена: ollama serve"));
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isNull()) {
                callback(false, QStringLiteral("Ollama вернула невалидный JSON."));
                return;
            }

            // OpenAI format: choices[0].message.content
            QString text = doc[QStringLiteral("choices")]
                              .toArray().at(0)
                              .toObject()[QStringLiteral("message")]
                              .toObject()[QStringLiteral("content")]
                              .toString().trimmed();

            if (text.isEmpty()) {
                callback(false, QStringLiteral("Ollama вернула пустой ответ."));
                return;
            }

            callback(true, text);
        });
    }

    // ── Список доступных моделей (/api/tags) ───────────────────────────
    void fetchModels(std::function<void(QStringList)> callback)
    {
        QNetworkRequest req(QUrl(m_host + QStringLiteral("/api/tags")));
        req.setTransferTimeout(5000);

        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
            reply->deleteLater();
            QStringList models;

            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                for (const QJsonValue& v : doc[QStringLiteral("models")].toArray()) {
                    QString name = v.toObject()[QStringLiteral("name")].toString();
                    if (!name.isEmpty()) models.append(name);
                }
            }

            callback(models);
        });
    }

    // Асинхронная проверка доступности Ollama.
    // callback(true) — доступна, callback(false) — не запущена.
    void checkAvailability(std::function<void(bool available, const QString& info)> callback)
    {
        QNetworkRequest req(QUrl(m_host + QStringLiteral("/api/tags")));
        req.setTransferTimeout(3000);  // 3 сек — быстрый ping

        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, callback, this]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                callback(false, QString());
                return;
            }

            // Собираем список установленных моделей
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QStringList models;
            for (const QJsonValue& v : doc[QStringLiteral("models")].toArray()) {
                QString name = v.toObject()[QStringLiteral("name")].toString();
                if (!name.isEmpty()) models.append(name);
            }

            // Проверяем что нужная модель установлена
            bool modelFound = models.contains(m_model)
                           || models.contains(m_model + ":latest");

            if (!modelFound && !models.isEmpty()) {
                // Модель не найдена — берём первую доступную
                m_model = models.first();
                QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
                    .setValue(QStringLiteral("ollama/model"), m_model);
            }

            QString info = models.isEmpty()
                ? QStringLiteral("Ollama запущена, но моделей нет. Выполни: ollama pull llama3")
                : QStringLiteral("Модели: ") + models.join(QStringLiteral(", "));

            callback(!models.isEmpty(), info);
        });
    }

private:
    QNetworkAccessManager* m_nam  = nullptr;
    QString                m_host;
    QString                m_model;
    QString                m_systemPrompt;
};