// -------------------------------------------------------
// gemini_api.cpp — Клиент Google Gemini API
// -------------------------------------------------------

#include "gemini_api.h"
#include "session_memory.h"
#include "embedded_key.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>

// ============================================================
// Конструктор
// ============================================================

GeminiApi::GeminiApi(SessionMemory* memory, QObject* parent)
    : QObject(parent)
    , m_memory(memory)
{
    m_network = new QNetworkAccessManager(this);
    loadApiKey();
}

// ============================================================
// API-ключ: загрузка / сохранение
// ============================================================

static QString geminiKeyFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/jarvis_gemini_apikey.dat");
}

void GeminiApi::setApiKey(const QString& key)
{
    m_apiKey = key.trimmed();
    saveApiKey();
}

void GeminiApi::loadApiKey()
{
    // 1. Сначала пробуем пользовательский ключ из файла
    QFile file(geminiKeyFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QString key = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        if (!key.isEmpty()) {
            m_apiKey = key;
            return;
        }
    }

    // 2. Fallback — встроенный ключ (XOR-обфусцирован)
    // Используем GEMINI_KEY_DATA если определён, иначе пропускаем
#ifdef GEMINI_KEY_DATA
    QString embedded = EmbeddedKey::decode(GEMINI_KEY_DATA, GEMINI_KEY_SIZE);
    if (!embedded.isEmpty()) {
        m_apiKey = embedded;
    }
#elif defined(GEMINI_KEY_SIZE)
    if (GEMINI_KEY_SIZE > 0) {
        QString embedded = EmbeddedKey::decode(GEMINI_KEY_DATA, GEMINI_KEY_SIZE);
        if (!embedded.isEmpty()) m_apiKey = embedded;
    }
#endif
}

void GeminiApi::saveApiKey()
{
    QFile file(geminiKeyFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(m_apiKey.toUtf8());
    file.close();
}

// ============================================================
// Сборка истории сообщений для Gemini
// Gemini API использует роли "user" и "model" (не "assistant")
// ============================================================

static QJsonArray buildGeminiContents(const QJsonArray& raw, const QString& newUserMessage)
{
    QJsonArray result;
    QString lastRole;

    for (const auto& val : raw) {
        QJsonObject msg = val.toObject();
        QString role    = msg[QStringLiteral("role")].toString();
        QString content = msg[QStringLiteral("content")].toString().trimmed();

        if (content.isEmpty()) continue;

        // Gemini: "assistant" → "model"
        QString geminiRole = (role == QStringLiteral("assistant"))
                             ? QStringLiteral("model")
                             : QStringLiteral("user");

        // Строгое чередование user/model
        if (geminiRole == lastRole) continue;
        if (geminiRole != QStringLiteral("user") && geminiRole != QStringLiteral("model"))
            continue;

        QJsonObject part;
        part[QStringLiteral("text")] = content;

        QJsonObject turn;
        turn[QStringLiteral("role")]  = geminiRole;
        turn[QStringLiteral("parts")] = QJsonArray{part};

        result.append(turn);
        lastRole = geminiRole;
    }

    // Убираем последний user если есть (заменим новым сообщением)
    if (!result.isEmpty()) {
        QJsonObject last = result.last().toObject();
        if (last[QStringLiteral("role")].toString() == QStringLiteral("user")) {
            result.removeLast();
        }
    }

    // Новое сообщение
    QJsonObject part;
    part[QStringLiteral("text")] = newUserMessage;

    QJsonObject turn;
    turn[QStringLiteral("role")]  = QStringLiteral("user");
    turn[QStringLiteral("parts")] = QJsonArray{part};
    result.append(turn);

    // Первым должен быть user
    while (!result.isEmpty()) {
        QJsonObject first = result.first().toObject();
        if (first[QStringLiteral("role")].toString() == QStringLiteral("user")) break;
        result.removeFirst();
    }

    // Ограничение истории
    constexpr int MAX_TURNS = 20;
    while (result.size() > MAX_TURNS) result.removeFirst();
    while (!result.isEmpty()) {
        QJsonObject first = result.first().toObject();
        if (first[QStringLiteral("role")].toString() == QStringLiteral("user")) break;
        result.removeFirst();
    }

    return result;
}

// ============================================================
// Отправка запроса
// ============================================================

void GeminiApi::sendMessage(const QString& userMessage, ResponseCallback callback)
{
    if (m_apiKey.isEmpty()) {
        callback(false, QStringLiteral(
            "Gemini API-ключ не установлен.\n"
            "Получите бесплатный ключ на aistudio.google.com\n"
            "Затем: Настройки → Gemini API-ключ"));
        return;
    }

    if (m_requesting) {
        callback(false, QStringLiteral("Запрос уже выполняется, подождите..."));
        return;
    }

    m_requesting = true;
    emit requestStarted();

    // URL: https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={key}
    QString urlStr = QStringLiteral(
        "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
        .arg(m_model, m_apiKey);

    QNetworkRequest request;
    request.setUrl(QUrl(urlStr));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // System instruction для Gemini
    QString systemPrompt = m_memory->buildSystemPrompt();

    // Собираем тело запроса
    QJsonObject body;

    // System instruction (Gemini поддерживает отдельный блок)
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysInstruction;
        QJsonObject sysPart;
        sysPart[QStringLiteral("text")] = systemPrompt;
        sysInstruction[QStringLiteral("parts")] = QJsonArray{sysPart};
        body[QStringLiteral("systemInstruction")] = sysInstruction;
    }

    // История + новое сообщение
    QJsonArray rawHistory = m_memory->recentMessagesAsJson(16);
    body[QStringLiteral("contents")] = buildGeminiContents(rawHistory, userMessage);

    // Настройки генерации
    QJsonObject genConfig;
    genConfig[QStringLiteral("maxOutputTokens")] = MAX_TOKENS;
    genConfig[QStringLiteral("temperature")]      = 0.7;
    body[QStringLiteral("generationConfig")] = genConfig;

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_network->post(request, payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        handleReply(reply, callback);
    });
}

// ============================================================
// Обработка ответа
// ============================================================

void GeminiApi::handleReply(QNetworkReply* reply, ResponseCallback callback)
{
    m_requesting = false;
    emit requestFinished();
    reply->deleteLater();

    QByteArray data = reply->readAll();
    int statusCode  = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg;

        QJsonDocument errDoc = QJsonDocument::fromJson(data);
        QString apiDetail;
        if (errDoc.isObject()) {
            QJsonObject errObj = errDoc.object();
            // Gemini error format: { "error": { "message": "..." } }
            QJsonObject errInner = errObj[QStringLiteral("error")].toObject();
            apiDetail = errInner[QStringLiteral("message")].toString();
        }

        switch (statusCode) {
        case 400:
            errorMsg = QStringLiteral("Gemini: ошибка запроса (400)");
            if (!apiDetail.isEmpty()) errorMsg += QStringLiteral(": ") + apiDetail;
            break;
        case 401:
        case 403:
            errorMsg = QStringLiteral("Gemini: неверный API-ключ. Проверьте: aistudio.google.com");
            break;
        case 429:
            errorMsg = QStringLiteral("Gemini: превышен лимит запросов. Подождите немного.");
            break;
        case 503:
            errorMsg = QStringLiteral("Gemini API перегружен. Попробуйте позже.");
            break;
        default:
            errorMsg = QStringLiteral("Gemini: ошибка API (HTTP %1)").arg(statusCode);
            if (!apiDetail.isEmpty()) errorMsg += QStringLiteral(": ") + apiDetail;
            else                      errorMsg += QStringLiteral(": ") + reply->errorString();
            break;
        }

        emit apiError(errorMsg);
        callback(false, errorMsg);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        callback(false, QStringLiteral("Gemini: некорректный ответ от API."));
        return;
    }

    QJsonObject root = doc.object();

    // Gemini response structure:
    // { "candidates": [ { "content": { "parts": [ { "text": "..." } ] } } ] }
    QJsonArray candidates = root[QStringLiteral("candidates")].toArray();
    if (candidates.isEmpty()) {
        // Проверяем promptFeedback на блокировку
        QJsonObject feedback = root[QStringLiteral("promptFeedback")].toObject();
        QString blockReason  = feedback[QStringLiteral("blockReason")].toString();
        if (!blockReason.isEmpty()) {
            callback(false, QStringLiteral("Gemini заблокировал запрос: ") + blockReason);
        } else {
            callback(false, QStringLiteral("Gemini: пустой ответ от API."));
        }
        return;
    }

    QJsonObject candidate = candidates.first().toObject();
    QJsonObject content   = candidate[QStringLiteral("content")].toObject();
    QJsonArray  parts     = content[QStringLiteral("parts")].toArray();

    QString responseText;
    for (const auto& partVal : parts) {
        QJsonObject partObj = partVal.toObject();
        responseText += partObj[QStringLiteral("text")].toString();
    }

    if (responseText.isEmpty()) {
        callback(false, QStringLiteral("Gemini: пустой ответ от API."));
        return;
    }

    callback(true, responseText);
}