#pragma once
// -------------------------------------------------------
// embedded_key.h — Встроенные API-ключи (XOR-обфускация)
//
// ВАЖНО: этот файл генерируется автоматически GitHub Actions
// из секретов репозитория. НЕ КОММИТЬ реальные ключи сюда.
//
// Claude ключ инжектируется через ANTHROPIC_API_KEY secret.
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
    if constexpr (ANTHROPIC_KEY_SIZE == 0) return {};
    return decode(ANTHROPIC_KEY_DATA, ANTHROPIC_KEY_SIZE);
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


} // namespace EmbeddedKey