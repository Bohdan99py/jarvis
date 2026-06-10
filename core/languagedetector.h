#pragma once
#include <QString>
#include <QSet>
#include <QMap>

// Определяет язык текста и хранит текущий язык сессии.
// Логика: если в тексте >= 2 кириллических символа → RU, иначе EN.
// После каждого сообщения пользователя язык обновляется и сохраняется
// для передачи в системный промпт Claude.
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
        // Короткие/цифровые сообщения — возвращаем Unknown (не меняем текущий)
        return Language::Unknown;
    }

    // Обновить текущий язык сессии на основе сообщения пользователя.
    // Возвращает true если язык изменился.
    bool update(const QString &userMessage) {
        Language detected = detect(userMessage);
        if (detected == Language::Unknown) return false;
        if (detected == m_current) return false;
        m_current = detected;
        return true;
    }

    Language current() const { return m_current; }

    // Строка для системного промпта — указывает Claude на каком языке отвечать
    QString systemInstruction() const {
        switch (m_current) {
        case Language::Russian:
            return QStringLiteral(
                "ВАЖНО: Пользователь пишет по-русски. "
                "Отвечай ВСЕГДА на русском языке, даже если вопрос задан на английском. "
                "Не переключайся на английский без явной просьбы.");
        case Language::English:
            return QStringLiteral(
                "IMPORTANT: The user writes in English. "
                "Always respond in English, even if asked in another language. "
                "Do not switch to Russian without explicit request.");
        default:
            return {};
        }
    }

    // Имя языка для отображения в UI
    QString languageName() const {
        return m_current == Language::Russian ? QStringLiteral("RU") : QStringLiteral("EN");
    }

private:
    Language m_current = Language::Russian; // дефолт — русский (аудитория проекта)
};
