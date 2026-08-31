// ============================================================
// speech_cache.cpp — On-disk cache of synthesized speech
// ============================================================
#include "speech_cache.h"
#include "jarvis_paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

SpeechCache& SpeechCache::instance()
{
    static SpeechCache inst;
    return inst;
}

QString SpeechCache::cacheDir() const
{
    // subPath() создаёт только родителя последнего элемента: для
    // "cache/tts" это "cache", а сама папка остаётся несуществующей и
    // запись в неё молча проваливается. Создаём явно — один раз.
    static const QString dir = []() {
        const QString path = JarvisPaths::subPath(QStringLiteral("cache/tts"));
        QDir().mkpath(path);
        return path;
    }();

    return dir;
}

QString SpeechCache::pathForKey(const QString& key) const
{
    return cacheDir() + QStringLiteral("/") + key + QStringLiteral(".wav");
}

// В ключ входит всё, что слышно. Параметры подачи берутся числами, а не
// именем стиля: два стиля, синтезируемые одинаково, честно делят один
// файл, а изменение темпа у стиля само собой обесценивает старые записи.
QString SpeechCache::makeKey(const QString& provider,
                             const QString& voiceId,
                             const QString& language,
                             const StyleParams& params,
                             const QString& text)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(provider.toUtf8());
    hash.addData("|");
    hash.addData(voiceId.toUtf8());
    hash.addData("|");
    hash.addData(language.toUtf8());
    hash.addData("|");
    hash.addData(QString::number(params.lengthScale, 'f', 2).toUtf8());
    hash.addData("|");
    hash.addData(QString::number(params.sentenceSilence, 'f', 2).toUtf8());
    hash.addData("|");
    hash.addData(text.toUtf8());

    return QString::fromLatin1(hash.result().toHex());
}

QString SpeechCache::lookup(const QString& key)
{
    if (key.isEmpty())
        return QString();

    const QString path = pathForKey(key);

    QFileInfo info(path);
    if (!info.exists() || info.size() < 64)
        return QString();

    // Отметка времени = «последний раз пригодилось». Время доступа NTFS
    // для этого не годится: Windows по умолчанию его не обновляет.
    QFile file(path);
    if (file.open(QIODevice::Append)) {   // открыть на запись, ничего не меняя
        file.setFileTime(QDateTime::currentDateTime(),
                          QFileDevice::FileModificationTime);
        file.close();
    }

    return path;
}

bool SpeechCache::contains(const QString& key) const
{
    if (key.isEmpty())
        return false;
    return QFileInfo::exists(pathForKey(key));
}

QString SpeechCache::store(const QString& key, const QString& wavPath)
{
    if (key.isEmpty() || !QFile::exists(wavPath))
        return QString();

    const QString target = pathForKey(key);

    QMutexLocker lock(&m_mutex);

    // Кто-то успел записать тот же ключ, пока шёл синтез: своя копия
    // не нужна, играем ту, что уже лежит.
    if (QFile::exists(target)) {
        QFile::remove(wavPath);
        return target;
    }

    if (!QFile::rename(wavPath, target)) {
        // Между Documents и %TEMP% может оказаться граница томов —
        // тогда переименование не работает, копируем.
        if (!QFile::copy(wavPath, target)) {
            qWarning() << "[SpeechCache] cannot store" << target;
            return QString();
        }
        QFile::remove(wavPath);
    }

    const qint64 size = QFileInfo(target).size();
    if (m_totalBytes < 0) {
        // Первое обращение — единственный полный обход папки за сессию.
        m_totalBytes = 0;
        const QFileInfoList files = QDir(cacheDir()).entryInfoList(
            {QStringLiteral("*.wav")}, QDir::Files);
        for (const QFileInfo& fi : files)
            m_totalBytes += fi.size();
    } else {
        m_totalBytes += size;
    }

    if (m_totalBytes > MAX_BYTES)
        pruneLocked();

    return target;
}

void SpeechCache::pruneLocked()
{
    QFileInfoList files = QDir(cacheDir()).entryInfoList(
        {QStringLiteral("*.wav")}, QDir::Files, QDir::Time | QDir::Reversed);

    const qint64 target = static_cast<qint64>(MAX_BYTES * PRUNE_TARGET);
    int removed = 0;

    // entryInfoList отсортирован от самых давних — они и уходят первыми.
    for (const QFileInfo& fi : files) {
        if (m_totalBytes <= target)
            break;
        const qint64 size = fi.size();
        if (QFile::remove(fi.absoluteFilePath())) {
            m_totalBytes -= size;
            ++removed;
        }
    }

    qDebug() << "[SpeechCache] pruned" << removed << "entries, now"
             << (m_totalBytes / 1024 / 1024) << "MB";
}

qint64 SpeechCache::totalBytes()
{
    QMutexLocker lock(&m_mutex);

    if (m_totalBytes < 0) {
        m_totalBytes = 0;
        const QFileInfoList files = QDir(cacheDir()).entryInfoList(
            {QStringLiteral("*.wav")}, QDir::Files);
        for (const QFileInfo& fi : files)
            m_totalBytes += fi.size();
    }

    return m_totalBytes;
}

int SpeechCache::entryCount() const
{
    return QDir(cacheDir()).entryList({QStringLiteral("*.wav")}, QDir::Files).size();
}

void SpeechCache::clear()
{
    QMutexLocker lock(&m_mutex);

    QDir dir(cacheDir());
    const QStringList files = dir.entryList({QStringLiteral("*.wav")}, QDir::Files);
    for (const QString& name : files)
        QFile::remove(dir.absoluteFilePath(name));

    m_totalBytes = 0;
    qDebug() << "[SpeechCache] cleared" << files.size() << "entries";
}
