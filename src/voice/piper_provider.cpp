// ============================================================
// piper_provider.cpp — Offline neural TTS (piper.exe + ONNX voices)
// ============================================================
#include "piper_provider.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

void PiperProvider::setRuntime(const QString& exePath,
                                const QString& modelRu,
                                const QString& modelEn)
{
    m_exePath = exePath;
    m_modelRu = modelRu;
    m_modelEn = modelEn;
}

QString PiperProvider::modelForRequest(const SpeechRequest& req) const
{
    bool russian = false;

    if (!req.language.isEmpty()) {
        russian = req.language.startsWith(QStringLiteral("ru"), Qt::CaseInsensitive);
    } else {
        for (const QChar& ch : req.text) {
            if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF) {
                russian = true;
                break;
            }
        }
    }

    QString modelPath = russian ? m_modelRu : m_modelEn;
    if (modelPath.isEmpty())
        modelPath = russian ? m_modelEn : m_modelRu;

    return modelPath;
}

QString PiperProvider::voiceId(const SpeechRequest& req) const
{
    return QFileInfo(modelForRequest(req)).fileName();
}

bool PiperProvider::synthesize(const SpeechRequest& req, const QString& outPath)
{
    const QString modelPath = modelForRequest(req);
    if (modelPath.isEmpty() || m_exePath.isEmpty())
        return false;

    const StyleParams sp = styleParams(req.style);

    QProcess proc;
    proc.setProgram(m_exePath);
    proc.setArguments({
        QStringLiteral("--model"), modelPath,
        QStringLiteral("--output_file"), outPath,
        // Темп и паузы — здесь стиль перестаёт быть пометкой в
        // структуре и становится слышимым.
        QStringLiteral("--length_scale"),     QString::number(sp.lengthScale, 'f', 2),
        QStringLiteral("--sentence_silence"), QString::number(sp.sentenceSilence, 'f', 2),
    });
    proc.start();
    if (!proc.waitForStarted(3000)) {
        qWarning() << "[Piper] piper.exe failed to start";
        return false;
    }

    proc.write(req.text.toUtf8());
    proc.closeWriteChannel();

    if (!proc.waitForFinished(15000) || proc.exitCode() != 0 || !QFile::exists(outPath)) {
        qWarning() << "[Piper] synthesis failed:" << proc.readAllStandardError();
        QFile::remove(outPath);
        return false;
    }

    return true;
}
