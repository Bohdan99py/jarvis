#pragma once
// ============================================================
// piper_provider.h — Offline neural TTS (piper.exe + ONNX voices)
//
// Локальный голос JARVIS. Работает без сети и без денег, поэтому
// остаётся последним рубежом перед SAPI: что бы ни случилось с
// ключами и интернетом, ассистент продолжает говорить.
//
// Поиск и скачивание runtime и моделей остались в
// VoiceSynthesisManager — это установка, а не синтез. Провайдер
// получает готовые пути через setRuntime().
// ============================================================

#include "voice_provider.h"

#include <QString>

class PiperProvider : public VoiceProvider
{
public:
    QString id() const override { return QStringLiteral("piper"); }
    QString displayName() const override { return QStringLiteral("Piper (offline)"); }

    bool isAvailable() const override
    {
        return !m_exePath.isEmpty()
            && (!m_modelRu.isEmpty() || !m_modelEn.isEmpty());
    }

    // Зовётся после того, как менеджер нашёл или доставил runtime.
    void setRuntime(const QString& exePath,
                    const QString& modelRu,
                    const QString& modelEn);

    QString voiceId(const SpeechRequest& req) const override;
    bool synthesize(const SpeechRequest& req, const QString& outPath) override;

private:
    // Язык из запроса важнее эвристики по буквам: «ESP32 connected» в
    // русской сессии должен звучать тем же голосом, что и остальное,
    // если ядро так решило.
    QString modelForRequest(const SpeechRequest& req) const;

    QString m_exePath;
    QString m_modelRu;
    QString m_modelEn;
};
