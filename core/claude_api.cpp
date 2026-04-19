// -------------------------------------------------------
// claude_api.cpp — Клиент Anthropic Claude API
// -------------------------------------------------------

#include "claude_api.h"
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

// ============================================================
// Конструктор
// ============================================================

ClaudeApi::ClaudeApi(SessionMemory* memory, QObject* parent)
    : QObject(parent)
    , m_memory(memory)
{
    m_network = new QNetworkAccessManager(this);

    // Приоритет загрузки ключа:
    // 1. Пользовательский ключ (из файла) — если уже вводил apikey
    // 2. Вшитый ключ (из embedded_key.h) — для новых пользователей
    loadApiKey();

    if (m_apiKey.isEmpty() && EmbeddedKey::hasEmbeddedKey()) {
        m_apiKey = EmbeddedKey::decryptApiKey();
        m_usingEmbeddedKey = true;
    }
}

// ============================================================
// API-ключ: загрузка / сохранение
// ============================================================

static QString apiKeyFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/jarvis_apikey.dat");
}

void ClaudeApi::setApiKey(const QString& key)
{
    m_apiKey = key.trimmed();
    m_usingEmbeddedKey = false;
    saveApiKey();
}

void ClaudeApi::loadApiKey()
{
    QFile file(apiKeyFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    QString key = QString::fromUtf8(file.readAll()).trimmed();
    file.close();

    if (!key.isEmpty()) {
        m_apiKey = key;
        m_usingEmbeddedKey = false;
    }
}

void ClaudeApi::saveApiKey()
{
    QFile file(apiKeyFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(m_apiKey.toUtf8());
    file.close();
}

// ============================================================
// Решение: API или локально?
// ============================================================

bool ClaudeApi::shouldUseApi(const QString& input) const
{
    if (m_apiKey.isEmpty()) return false;

    QString lower = input.trimmed().toLower();

    // Локальные команды — НЕ отправляем в API
    static const QStringList localPrefixes = {
        QStringLiteral("время"),       QStringLiteral("time"),
        QStringLiteral("дата"),        QStringLiteral("date"),
        QStringLiteral("блокнот"),     QStringLiteral("notepad"),
        QStringLiteral("калькулятор"), QStringLiteral("calc"),
        QStringLiteral("проводник"),   QStringLiteral("explorer"),
        QStringLiteral("диспетчер"),   QStringLiteral("taskmgr"),
        QStringLiteral("настройки"),   QStringLiteral("settings"),
        QStringLiteral("браузер"),     QStringLiteral("browser"),
        QStringLiteral("chrome"),
        QStringLiteral("запусти"),     QStringLiteral("открой"),     QStringLiteral("launch"),
        QStringLiteral("найди"),       QStringLiteral("search"),     QStringLiteral("гугл"),
        QStringLiteral("youtube"),     QStringLiteral("ютуб"),
        QStringLiteral("заблокируй"), QStringLiteral("lock"),
        QStringLiteral("напечатай"),   QStringLiteral("type"),
        QStringLiteral("нажми"),       QStringLiteral("press"),
        QStringLiteral("комбо"),       QStringLiteral("combo"),
        QStringLiteral("помощь"),      QStringLiteral("help"),
        QStringLiteral("привет"),      QStringLiteral("hello"),      QStringLiteral("hi"),
        QStringLiteral("кто я"),       QStringLiteral("username"),
        QStringLiteral("apikey"),      QStringLiteral("ключ"),
        QStringLiteral("запомни"),     QStringLiteral("remember"),
        QStringLiteral("вспомни"),     QStringLiteral("recall"),
        QStringLiteral("память"),      QStringLiteral("memory"),
        QStringLiteral("статистика"), QStringLiteral("stats"),
    };

    for (const auto& prefix : localPrefixes) {
        if (lower.startsWith(prefix) || lower.contains(prefix)) {
            return false;
        }
    }

    return true;
}

// ============================================================
// Сборка корректной истории сообщений для API
// ============================================================

// Claude API требует строгое чередование user/assistant.
// Первое сообщение должно быть user.
// Два подряд с одной ролью запрещены.
static QJsonArray buildValidMessages(const QJsonArray& raw, const QString& newUserMessage)
{
    QJsonArray result;

    // 1. Собираем из истории, пропуская нарушения чередования
    QString lastRole;
    for (const auto& val : raw) {
        QJsonObject msg = val.toObject();
        QString role = msg[QStringLiteral("role")].toString();
        QString content = msg[QStringLiteral("content")].toString().trimmed();

        // Пропускаем пустые
        if (content.isEmpty()) continue;

        // Пропускаем если та же роль что и предыдущая
        if (role == lastRole) continue;

        // Допускаем только user и assistant
        if (role != QStringLiteral("user") && role != QStringLiteral("assistant"))
            continue;

        result.append(msg);
        lastRole = role;
    }

    // 2. Добавляем новое сообщение пользователя
    // Если последнее сообщение тоже user — убираем его (оставляем только новое)
    if (!result.isEmpty()) {
        QJsonObject last = result.last().toObject();
        if (last[QStringLiteral("role")].toString() == QStringLiteral("user")) {
            result.removeLast();
        }
    }

    QJsonObject userMsg;
    userMsg[QStringLiteral("role")]    = QStringLiteral("user");
    userMsg[QStringLiteral("content")] = newUserMessage;
    result.append(userMsg);

    // 3. Гарантируем что первое сообщение — user
    while (!result.isEmpty()) {
        QJsonObject first = result.first().toObject();
        if (first[QStringLiteral("role")].toString() == QStringLiteral("user"))
            break;
        result.removeFirst();
    }

    // 4. Ограничиваем длину (последние N сообщений, сохраняя чередование)
    constexpr int MAX_MESSAGES = 20;
    while (result.size() > MAX_MESSAGES) {
        result.removeFirst();
    }
    // После обрезки снова проверяем, что первое — user
    while (!result.isEmpty()) {
        QJsonObject first = result.first().toObject();
        if (first[QStringLiteral("role")].toString() == QStringLiteral("user"))
            break;
        result.removeFirst();
    }

    return result;
}

// ============================================================
// Отправка запроса
// ============================================================

void ClaudeApi::sendMessage(const QString& userMessage, ResponseCallback callback)
{
    if (m_apiKey.isEmpty()) {
        callback(false, QStringLiteral(
            "API-ключ не установлен. Используйте команду:\n"
            "apikey <ваш-ключ-anthropic>\n"
            "для подключения Claude API."));
        return;
    }

    if (m_requesting) {
        callback(false, QStringLiteral("Запрос уже выполняется, подождите..."));
        return;
    }

    m_requesting = true;
    emit requestStarted();

    // Формируем тело запроса
    QJsonObject body;
    body[QStringLiteral("model")]      = m_model;
    body[QStringLiteral("max_tokens")] = MAX_TOKENS;

    // System prompt с контекстом
    body[QStringLiteral("system")] = m_memory->buildSystemPrompt();

    // Корректная история + новое сообщение
    QJsonArray rawHistory = m_memory->recentMessagesAsJson(16);
    QJsonArray messages = buildValidMessages(rawHistory, userMessage);

    body[QStringLiteral("messages")] = messages;

    // HTTP-запрос
    QNetworkRequest request;
    request.setUrl(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_network->post(request, payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        handleReply(reply, callback);
    });
}

// ============================================================
// Обработка ответа
// ============================================================

void ClaudeApi::handleReply(QNetworkReply* reply, ResponseCallback callback)
{
    m_requesting = false;
    emit requestFinished();

    reply->deleteLater();

    QByteArray data = reply->readAll();
    int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg;

        // Пытаемся извлечь детали ошибки из тела ответа
        QJsonDocument errDoc = QJsonDocument::fromJson(data);
        QString apiDetail;
        if (errDoc.isObject()) {
            QJsonObject errObj = errDoc.object();
            QJsonObject errInner = errObj[QStringLiteral("error")].toObject();
            apiDetail = errInner[QStringLiteral("message")].toString();
        }

        switch (statusCode) {
        case 400:
            errorMsg = QStringLiteral("Ошибка запроса (400)");
            if (!apiDetail.isEmpty()) {
                errorMsg += QStringLiteral(": ") + apiDetail;
            }
            break;
        case 401:
            errorMsg = QStringLiteral("Неверный API-ключ. Проверьте: apikey <ключ>");
            break;
        case 403:
            errorMsg = QStringLiteral("Доступ запрещён (403). Проверьте аккаунт на console.anthropic.com");
            break;
        case 429:
            errorMsg = QStringLiteral("Превышен лимит запросов. Подождите немного.");
            break;
        case 529:
            errorMsg = QStringLiteral("API перегружен. Попробуйте позже.");
            break;
        default:
            errorMsg = QStringLiteral("Ошибка API (HTTP %1)").arg(statusCode);
            if (!apiDetail.isEmpty()) {
                errorMsg += QStringLiteral(": ") + apiDetail;
            } else {
                errorMsg += QStringLiteral(": ") + reply->errorString();
            }
            break;
        }

        emit apiError(errorMsg);
        callback(false, errorMsg);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        callback(false, QStringLiteral("Некорректный ответ от API."));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray content = root[QStringLiteral("content")].toArray();

    QString responseText;
    for (const auto& block : content) {
        QJsonObject blockObj = block.toObject();
        if (blockObj[QStringLiteral("type")].toString() == QStringLiteral("text")) {
            responseText += blockObj[QStringLiteral("text")].toString();
        }
    }

    if (responseText.isEmpty()) {
        callback(false, QStringLiteral("Пустой ответ от API."));
        return;
    }

    callback(true, responseText);
}