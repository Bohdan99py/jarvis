#pragma once
#include <QString>

// Определяет язык текста и хранит текущий язык сессии.
// Логика: если в тексте >= 2 кириллических символа → RU, иначе EN.
// После каждого сообщения пользователя язык обновляется и сохраняется
// для передачи в системный промпт Claude.
//
// ИСПРАВЛЕНИЕ: дефолт Unknown вместо Russian.
// Если пользователь пишет на EN, а предыдущий язык был Russian — это
// вызывало Claude отвечать по-русски на английские вопросы.
class LanguageDetector {
public:
    enum class Language { Russian, English, Unknown };

    // Определить язык строки
    static Language detect(const QString &text) {
        int cyrillicCount = 0;
        int latinCount    = 0;
        for (const QChar &ch : text) {
            ushort u = ch.unicode();
            if ((u >= 0x0410 && u <= 0x044F) || u == 0x0451 || u == 0x0401)
                ++cyrillicCount;
            else if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))
                ++latinCount;
        }
        if (cyrillicCount >= 2) return Language::Russian;
        if (latinCount    >= 2) return Language::English;
        // Короткие/цифровые сообщения — Unknown (не меняем текущий)
        return Language::Unknown;
    }

    // Обновить текущий язык сессии на основе сообщения пользователя.
    // ВСЕГДА обновляем при явном обнаружении языка — не ждём "изменения".
    // Это гарантирует что EN запрос после RU сразу даёт EN инструкцию.
    bool update(const QString &userMessage) {
        Language detected = detect(userMessage);
        if (detected == Language::Unknown) return false;
        bool changed = (detected != m_current);
        m_current = detected;
        return changed;
    }

    Language current() const { return m_current; }

    // Строка для системного промпта — указывает Claude на каком языке отвечать.
    // ИСПРАВЛЕНИЕ: при Unknown не форсируем русский — позволяем Claude
    // самому выбрать язык ответа по контексту запроса.
    QString systemInstruction() const {
        switch (m_current) {
        case Language::Russian:
            return QStringLiteral(
                "IMPORTANT: The user is writing in Russian. "
                "Always respond in Russian. "
                "Do not switch to English unless explicitly asked.");
        case Language::English:
            return QStringLiteral(
                "IMPORTANT: The user is writing in English. "
                "Always respond in English. "
                "Do not switch to Russian without explicit request.");
        default:
            // Unknown: не форсируем язык — Claude отвечает на языке запроса
            return QStringLiteral(
                "Respond in the same language the user used in their message.");
        }
    }

    // Имя языка для отображения в UI
    QString languageName() const {
        switch (m_current) {
        case Language::Russian: return QStringLiteral("RU");
        case Language::English: return QStringLiteral("EN");
        default:                return QStringLiteral("AUTO");
        }
    }

private:
    // ИСПРАВЛЕНИЕ: дефолт Unknown — не форсируем язык до первого сообщения.
    // Раньше был Russian, что ломало ответы на английские запросы.
    Language m_current = Language::Unknown;
};
