#pragma once
// ============================================================
// speech_phrases.h — Canonical system phrases
//
// Дежурные реплики JARVIS собраны в одном месте по двум причинам.
//
// Первая: их можно синтезировать заранее. Кэш работает по точному
// совпадению текста, а совпадать нечему, если каждое место в коде
// формулирует «сборка упала» по-своему.
//
// Вторая: «Не получилось» и «Операция завершилась неуспешно» — это
// разные ассистенты. Дежурные фразы задают тон, и держать их рядом
// проще, чем вылавливать по всему проекту.
//
// Реплики с параметром («ESP32 подключён к COM5») сюда не попадают:
// они кэшируются сами по себе, после первого произнесения.
// ============================================================

#include "speech_request.h"

#include <QString>
#include <QVector>

enum class SystemPhrase {
    SystemReady,
    Listening,
    Processing,
    TaskCompleted,
    TaskFailed,
    BuildCompleted,
    BuildFailed,
    DeviceConnected,
    DeviceDisconnected,
    AccessDenied,
    PermissionRequired,
    Offline,
    RequestTimedOut,
    NightMode
};

namespace SpeechPhrases {

inline QString text(SystemPhrase phrase, bool english)
{
    switch (phrase) {
    case SystemPhrase::SystemReady:
        return english ? QStringLiteral("System ready.")
                       : QStringLiteral("Система готова.");
    case SystemPhrase::Listening:
        return english ? QStringLiteral("Listening.")
                       : QStringLiteral("Слушаю.");
    case SystemPhrase::Processing:
        return english ? QStringLiteral("Working on it.")
                       : QStringLiteral("Работаю.");
    case SystemPhrase::TaskCompleted:
        return english ? QStringLiteral("Done.")
                       : QStringLiteral("Готово.");
    case SystemPhrase::TaskFailed:
        return english ? QStringLiteral("That didn't work.")
                       : QStringLiteral("Не получилось.");
    case SystemPhrase::BuildCompleted:
        return english ? QStringLiteral("Build completed. No errors.")
                       : QStringLiteral("Сборка завершена. Ошибок нет.");
    case SystemPhrase::BuildFailed:
        return english ? QStringLiteral("The build failed.")
                       : QStringLiteral("Сборка завершилась с ошибкой.");
    case SystemPhrase::DeviceConnected:
        return english ? QStringLiteral("Device connected.")
                       : QStringLiteral("Устройство подключено.");
    case SystemPhrase::DeviceDisconnected:
        return english ? QStringLiteral("Device disconnected.")
                       : QStringLiteral("Устройство отключено.");
    case SystemPhrase::AccessDenied:
        return english ? QStringLiteral("Access denied.")
                       : QStringLiteral("Доступ запрещён.");
    case SystemPhrase::PermissionRequired:
        return english ? QStringLiteral("I need your permission for that.")
                       : QStringLiteral("Нужно твоё разрешение.");
    // Две реплики ниже произносятся ровно тогда, когда сети нет, —
    // сетевой синтез в этот момент недоступен по определению. Им кэш
    // нужнее всех остальных.
    case SystemPhrase::Offline:
        return english ? QStringLiteral("I'm offline right now, can't reach the server.")
                       : QStringLiteral("Я сейчас не в сети, не могу подключиться к серверу.");
    case SystemPhrase::RequestTimedOut:
        return english ? QStringLiteral("Request timed out. I can't reach the server right now.")
                       : QStringLiteral("Запрос не прошёл, сервер не отвечает. Попробуй позже.");
    case SystemPhrase::NightMode:
        return english ? QStringLiteral("Quiet mode is on.")
                       : QStringLiteral("Тихий режим включён.");
    }
    return QString();
}

inline SpeechStyle style(SystemPhrase phrase)
{
    switch (phrase) {
    case SystemPhrase::BuildFailed:
    case SystemPhrase::TaskFailed:
    case SystemPhrase::AccessDenied:
    case SystemPhrase::Offline:
    case SystemPhrase::RequestTimedOut:
        return SpeechStyle::Warning;
    case SystemPhrase::NightMode:
        return SpeechStyle::Whisper;
    default:
        return SpeechStyle::Informative;
    }
}

// Группа склейки: фразы одной подсистемы, пришедшие подряд, — это почти
// всегда одно событие, разложенное на пять сообщений. Побеждает более
// важная, при равенстве — последняя, поэтому «подключено → отключено»
// за полторы секунды честно скажет последнее состояние.
inline QString group(SystemPhrase phrase)
{
    switch (phrase) {
    case SystemPhrase::SystemReady:
    case SystemPhrase::Listening:
    case SystemPhrase::Processing:
        return QStringLiteral("system");
    case SystemPhrase::TaskCompleted:
    case SystemPhrase::TaskFailed:
        return QStringLiteral("task");
    case SystemPhrase::BuildCompleted:
    case SystemPhrase::BuildFailed:
        return QStringLiteral("build");
    case SystemPhrase::DeviceConnected:
    case SystemPhrase::DeviceDisconnected:
        return QStringLiteral("device");
    case SystemPhrase::AccessDenied:
    case SystemPhrase::PermissionRequired:
        return QStringLiteral("permission");
    case SystemPhrase::Offline:
    case SystemPhrase::RequestTimedOut:
        return QStringLiteral("network");
    // Объявление о тихом режиме приходит по одному и склеивать его
    // не с чем.
    case SystemPhrase::NightMode:
        return QString();
    }
    return QString();
}

// Готовый запрос: язык проставлен явно, чтобы «Build failed» в русской
// сессии не уехал на английский голос по буквам в тексте.
inline SpeechRequest request(SystemPhrase phrase, bool english,
                              SpeechSource source = SpeechSource::System)
{
    SpeechRequest r;
    r.text      = text(phrase, english);
    r.language  = english ? QStringLiteral("en") : QStringLiteral("ru");
    r.style     = style(phrase);
    r.source    = source;
    r.cacheable = true;
    r.priority  = (r.style == SpeechStyle::Warning) ? SpeechPriority::Important
                                                     : SpeechPriority::Normal;
    r.aggregateGroup = group(phrase);
    return r;
}

// Всё, что имеет смысл синтезировать заранее — на обоих языках:
// Telegram-сессия может идти по-английски, пока интерфейс русский.
inline QVector<SpeechRequest> warmupSet()
{
    static const SystemPhrase all[] = {
        SystemPhrase::SystemReady,      SystemPhrase::Listening,
        SystemPhrase::Processing,       SystemPhrase::TaskCompleted,
        SystemPhrase::TaskFailed,       SystemPhrase::BuildCompleted,
        SystemPhrase::BuildFailed,      SystemPhrase::DeviceConnected,
        SystemPhrase::DeviceDisconnected, SystemPhrase::AccessDenied,
        SystemPhrase::PermissionRequired, SystemPhrase::Offline,
        SystemPhrase::RequestTimedOut,  SystemPhrase::NightMode,
    };

    QVector<SpeechRequest> out;
    for (const SystemPhrase p : all) {
        out.append(request(p, false));
        out.append(request(p, true));
    }
    return out;
}

} // namespace SpeechPhrases
