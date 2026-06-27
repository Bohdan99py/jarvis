#pragma once
// -------------------------------------------------------
// audio_manager.h — Lightweight UI sound effects for JARVIS
// Uses procedurally generated WAV buffers (no external files).
// -------------------------------------------------------

#include <QObject>
#include <QSoundEffect>
#include <QTemporaryFile>
#include <QDir>
#include <QStandardPaths>
#include <QtMath>
#include <QByteArray>
#include <QDataStream>

#include "jarvis_core_export.h"

class JARVIS_CORE_EXPORT AudioManager : public QObject
{
    Q_OBJECT

public:
    // Full = speech + UI sounds, MuteSpeech = UI sounds only, MuteAll = silence
    enum AudioMode { Full = 0, MuteSpeech = 1, MuteAll = 2 };

    explicit AudioManager(QObject* parent = nullptr)
        : QObject(parent)
    {
        generateSounds();
    }

    AudioMode mode() const { return m_mode; }

    void setMode(AudioMode m)
    {
        m_mode = m;
        emit modeChanged(m_mode);
    }

    void cycleMode()
    {
        setMode(static_cast<AudioMode>((static_cast<int>(m_mode) + 1) % 3));
    }

    bool speechAllowed() const { return m_mode == Full; }
    bool uiSoundsAllowed() const { return m_mode != MuteAll; }

    QString modeLabel() const
    {
        switch (m_mode) {
        case Full:      return QStringLiteral("🔊");
        case MuteSpeech: return QStringLiteral("🔕");
        case MuteAll:   return QStringLiteral("🔇");
        }
        return QStringLiteral("🔊");
    }

    QString modeTooltip() const
    {
        switch (m_mode) {
        case Full:       return QStringLiteral("Full Sound — click to mute speech");
        case MuteSpeech: return QStringLiteral("Speech Muted — click to mute all");
        case MuteAll:    return QStringLiteral("All Muted — click to enable sound");
        }
        return QString();
    }

    void playSuccess()       { play(m_successEffect); }
    void playWarning()       { play(m_warningEffect); }
    void playListening()     { play(m_listeningEffect); }

signals:
    void modeChanged(AudioMode mode);

private:
    void play(QSoundEffect* effect)
    {
        if (m_mode == MuteAll || !effect) return;
        effect->play();
    }

    // WAV header for 16-bit mono PCM
    static QByteArray makeWav(const QVector<qint16>& samples, int sampleRate)
    {
        QByteArray wav;
        QDataStream ds(&wav, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        quint32 dataSize = static_cast<quint32>(samples.size() * 2);
        // RIFF header
        ds.writeRawData("RIFF", 4);
        ds << quint32(36 + dataSize);
        ds.writeRawData("WAVE", 4);
        // fmt chunk
        ds.writeRawData("fmt ", 4);
        ds << quint32(16);                       // chunk size
        ds << quint16(1);                        // PCM
        ds << quint16(1);                        // mono
        ds << quint32(sampleRate);               // sample rate
        ds << quint32(sampleRate * 2);           // byte rate
        ds << quint16(2);                        // block align
        ds << quint16(16);                       // bits per sample
        // data chunk
        ds.writeRawData("data", 4);
        ds << dataSize;
        for (qint16 s : samples) ds << s;
        return wav;
    }

    QSoundEffect* effectFromSamples(const QVector<qint16>& samples, int sampleRate,
                                     const QString& name)
    {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    + QStringLiteral("/jarvis_audio");
        QDir().mkpath(dir);
        QString path = dir + QStringLiteral("/") + name + QStringLiteral(".wav");
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(makeWav(samples, sampleRate));
            f.close();
        }
        auto* effect = new QSoundEffect(this);
        effect->setSource(QUrl::fromLocalFile(path));
        effect->setVolume(0.35f);
        return effect;
    }

    void generateSounds()
    {
        constexpr int SR = 22050;

        // Success: two-tone rising chime (C5→E5, ~200ms)
        {
            QVector<qint16> buf;
            int len1 = SR * 100 / 1000;
            int len2 = SR * 120 / 1000;
            for (int i = 0; i < len1; ++i) {
                double t = static_cast<double>(i) / SR;
                double env = 1.0 - static_cast<double>(i) / len1;
                buf.append(static_cast<qint16>(env * 12000 * qSin(2.0 * M_PI * 523.25 * t)));
            }
            for (int i = 0; i < len2; ++i) {
                double t = static_cast<double>(i) / SR;
                double env = 1.0 - static_cast<double>(i) / len2;
                buf.append(static_cast<qint16>(env * 12000 * qSin(2.0 * M_PI * 659.25 * t)));
            }
            m_successEffect = effectFromSamples(buf, SR, QStringLiteral("success"));
        }

        // Warning: low buzz pulse (~180ms, A3)
        {
            QVector<qint16> buf;
            int len = SR * 180 / 1000;
            for (int i = 0; i < len; ++i) {
                double t = static_cast<double>(i) / SR;
                double env = (1.0 - static_cast<double>(i) / len);
                double sig = qSin(2.0 * M_PI * 220.0 * t) + 0.3 * qSin(2.0 * M_PI * 440.0 * t);
                buf.append(static_cast<qint16>(env * 8000 * sig));
            }
            m_warningEffect = effectFromSamples(buf, SR, QStringLiteral("warning"));
        }

        // Listening: short bright ping (G5, ~120ms)
        {
            QVector<qint16> buf;
            int len = SR * 120 / 1000;
            for (int i = 0; i < len; ++i) {
                double t = static_cast<double>(i) / SR;
                double env = 1.0 - static_cast<double>(i) / len;
                env *= env; // exponential decay
                buf.append(static_cast<qint16>(env * 10000 * qSin(2.0 * M_PI * 783.99 * t)));
            }
            m_listeningEffect = effectFromSamples(buf, SR, QStringLiteral("listening"));
        }
    }

    AudioMode     m_mode            = Full;
    QSoundEffect* m_successEffect   = nullptr;
    QSoundEffect* m_warningEffect   = nullptr;
    QSoundEffect* m_listeningEffect = nullptr;
};
