#pragma once
// -------------------------------------------------------
// embedded_key.h — Встроенные API-ключи (XOR-обфускация)
//
// ВАЖНО: этот файл генерируется автоматически GitHub Actions
// из секретов репозитория. НЕ КОММИТЬ реальные ключи сюда.
//
// Claude ключ инжектируется через ANTHROPIC_API_KEY secret.
// Gemini ключ инжектируется через GEMINI_API_KEY secret.
// -------------------------------------------------------

#include <cstdint>
#include <QString>

// ============================================================
// XOR-декодер (одинаковый для обоих ключей)
// ============================================================

namespace EmbeddedKey {

static constexpr uint8_t XOR_PATTERN[]  = { 0x4A, 0x52, 0x56, 0x53, 0x47, 0x4D, 0x4E, 0x49 };
static constexpr int     XOR_PATTERN_LEN = 8;

inline QString decode(const uint8_t* data, int size) {
    QString result;
    result.reserve(size);
    for (int i = 0; i < size; ++i) {
        result += QChar(static_cast<char>(data[i] ^ XOR_PATTERN[i % XOR_PATTERN_LEN]));
    }
    return result;
}

// ============================================================
// Claude API key (Anthropic)
// Инжектируется через: ANTHROPIC_API_KEY GitHub Secret
// ============================================================

#ifndef ANTHROPIC_KEY_DATA
// Заглушка — заменяется при сборке через GitHub Actions
static constexpr uint8_t ANTHROPIC_KEY_DATA[] = { 0x00 };
static constexpr int     ANTHROPIC_KEY_SIZE    = 0;
#endif

inline QString claudeKey() {
    if (ANTHROPIC_KEY_SIZE == 0) return {};
    return decode(ANTHROPIC_KEY_DATA, ANTHROPIC_KEY_SIZE);
}

// ============================================================
// Gemini API key (Google)
// Инжектируется через: GEMINI_API_KEY GitHub Secret
// Используется как fallback если Ollama недоступна.
// ============================================================

#ifndef GEMINI_KEY_DATA
// Встроенный ключ (обфусцирован XOR, не plaintext)
static constexpr uint8_t GEMINI_KEY_DATA[] = {
    0x0B, 0x1B, 0x2C, 0x32, 0x14, 0x34, 0x0A, 0x70,
    0x3F, 0x19, 0x19, 0x31, 0x28, 0x28, 0x0C, 0x7C,
    0x73, 0x3E, 0x66, 0x29, 0x00, 0x7E, 0x36, 0x11,
    0x27, 0x6B, 0x39, 0x1C, 0x76, 0x3E, 0x22, 0x27,
    0x03, 0x3F, 0x1D, 0x12, 0x37, 0x28, 0x21
};
static constexpr int GEMINI_KEY_SIZE = 39;
#endif

inline QString geminiKey() {
    if (GEMINI_KEY_SIZE == 0) return {};
    return decode(GEMINI_KEY_DATA, GEMINI_KEY_SIZE);
}

// ============================================================
// Алиасы совместимости (старый API claude_api.cpp)
// ============================================================

inline bool hasEmbeddedKey() {
    return ANTHROPIC_KEY_SIZE > 0;
}

inline QString decryptApiKey() {
    return claudeKey();
}

// Алиас для Gemini
inline bool hasEmbeddedGeminiKey() {
    return GEMINI_KEY_SIZE > 0;
}

} // namespace EmbeddedKey