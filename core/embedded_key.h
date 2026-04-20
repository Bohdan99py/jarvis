#pragma once
// -------------------------------------------------------
// embedded_key.h — Вшитый API-ключ с обфускацией
//
// Ключ хранится в XOR-зашифрованном виде, чтобы его
// нельзя было найти простым strings/hex-поиском в бинарнике.
//
// КАК ИСПОЛЬЗОВАТЬ:
// 1. Запусти утилиту: scripts/generate_key.bat <твой-api-ключ>
//    Она напечатает массив байтов для вставки ниже.
//
// 2. Или вручную: замени ENCRYPTED_KEY_DATA и ENCRYPTED_KEY_SIZE.
//
// ВАЖНО: Это НЕ криптографическая защита. Это обфускация,
// чтобы ключ не светился plaintext в .exe/.dll.
// Для продакшена с платными пользователями — используй
// прокси-сервер (см. README).
// -------------------------------------------------------

#include <QString>
#include <QByteArray>

namespace EmbeddedKey {

// XOR-ключ для обфускации (рандомный, можешь менять)
static constexpr uint8_t XOR_KEY[] = {
    0x4A, 0x41, 0x52, 0x56, 0x49, 0x53,  // "JARVIS"
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE,
    0x13, 0x37, 0x42, 0x69
};
static constexpr int XOR_KEY_LEN = sizeof(XOR_KEY);

// ============================================================
// ЗАШИФРОВАННЫЙ API-КЛЮЧ
// Замени эти данные на свои (сгенерируй через generate_key)
// ============================================================

// Пустой ключ по умолчанию — замени после генерации
static constexpr uint8_t ENCRYPTED_KEY_DATA[] = { 0x00 };
static constexpr int ENCRYPTED_KEY_SIZE = 0;  // 0 = ключ не вшит

// ============================================================
// Функции
// ============================================================

inline QByteArray xorDecrypt(const uint8_t* data, int size)
{
    QByteArray result(size, '\0');
    for (int i = 0; i < size; ++i) {
        result[i] = static_cast<char>(data[i] ^ XOR_KEY[i % XOR_KEY_LEN]);
    }
    return result;
}

inline QString decryptApiKey()
{
    if (ENCRYPTED_KEY_SIZE == 0) {
        return QString();  // Ключ не вшит
    }
    QByteArray decrypted = xorDecrypt(ENCRYPTED_KEY_DATA, ENCRYPTED_KEY_SIZE);
    return QString::fromUtf8(decrypted);
}

inline bool hasEmbeddedKey()
{
    return ENCRYPTED_KEY_SIZE > 0;
}

} // namespace EmbeddedKey
