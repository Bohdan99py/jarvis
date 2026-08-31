#pragma once
// -------------------------------------------------------
// edit_journal.h — Журнал правок и откат
//
// Пока JARVIS только создавал файлы, «отмена» была не нужна: не то
// написал — попроси переписать. С появлением перемещений, копирований,
// правки ресурсов и операций над картинками цена ошибки другая: файл
// уезжает или перезаписывается, и вернуть его руками уже нечем.
//
// Поэтому каждое изменение на диске проходит через журнал:
//   • прежнее содержимое копируется в Jarvis Data/edit_backups/<батч>/
//   • операция записывается в edit_journal.json
//   • откат возвращает ВЕСЬ последний батч целиком (один ответ модели —
//     один батч), а не отдельную строчку: половина применённого рефакторинга
//     хуже, чем весь или ничего.
//
// Журнал хранит последние MAX_BATCHES батчей; старые бэкапы удаляются
// вместе с записями, чтобы папка не росла бесконечно.
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

#include "jarvis_core_export.h"

struct EditOp
{
    // External — действие, которое изменило машину, но не файл: убитый
    // процесс, набранный в чужом окне текст, выполненная команда, коммит.
    // Откатить его журнал не может и не делает вид, что может: такая
    // операция существует, чтобы отчёт об откате был честным — «файлы
    // вернул, процесс не воскрешу».
    enum Type { Created, Modified, Deleted, Moved, External };

    Type    type = Modified;
    QString path;        // абсолютный путь (для Moved — куда переехало)
    QString previousPath;// только для Moved — откуда
    QString backup;      // копия прежнего содержимого (пусто для Created)
    QString detail;      // только для External — что именно произошло

    bool isReversible() const { return type != External; }
};

struct EditBatch
{
    QString          id;
    QString          label;     // краткое описание («ответ ассистента», «советник»)
    QDateTime        at;
    QVector<EditOp>  ops;
};

class JARVIS_CORE_EXPORT EditJournal
{
public:
    static EditJournal& instance();

    EditJournal(const EditJournal&)            = delete;
    EditJournal& operator=(const EditJournal&) = delete;

    // Батч = одна пачка операций, которая откатывается целиком.
    // beginBatch допускает вложенные вызовы: считается только внешний.
    void beginBatch(const QString& label);
    void endBatch();
    bool inBatch() const { return m_depth > 0; }

    // Тот же батч через RAII — для инструментов реестра, где выход из
    // обработчика возможен полудюжиной return'ов и endBatch легко
    // потерять. Вложенность работает как обычно: если снаружи уже
    // открыт батч ответа модели, скоуп в него вливается.
    class Scope
    {
    public:
        explicit Scope(const QString& label) { EditJournal::instance().beginBatch(label); }
        ~Scope()                             { EditJournal::instance().endBatch(); }
        Scope(const Scope&)            = delete;
        Scope& operator=(const Scope&) = delete;
    };

    // Вызывать ДО записи/удаления: журнал успевает снять копию.
    void recordCreate(const QString& absPath);
    void recordModify(const QString& absPath);
    void recordDelete(const QString& absPath);
    void recordMove(const QString& fromAbs, const QString& toAbs);

    // Действие без файла: «убит процесс chrome.exe», «выполнено:
    // ninja jarvis», «коммит a1b2c3». Вызывать ПОСЛЕ успеха — записывать
    // то, чего не произошло, хуже, чем не записывать вовсе.
    void recordExternal(const QString& description);

    // Перемещение приходится записывать ДО операции — потом исходного
    // пути уже нет. Если операция не удалась, запись надо снять, иначе
    // откат будет возвращать файл, который никуда не уезжал.
    void discardLastOp();

    // Путь копии, снятой последней операцией записи. Нужен вызывающим,
    // которые показывают пользователю, куда именно легла страховка.
    QString lastBackupPath() const { return m_lastBackup; }

    // Откат последнего батча. Возвращает готовый текст отчёта.
    QString undoLast(bool english);

    // Что можно откатить (для чата и UI).
    QString history(bool english, int maxBatches = 5) const;
    bool    hasUndoableBatch() const { return !m_batches.isEmpty(); }

private:
    EditJournal();

    void    load();
    void    save() const;
    QString backupFor(const QString& absPath) const;
    void    pruneOldBatches();
    static QString journalPath();
    static QString backupsRoot();

    QVector<EditBatch> m_batches;
    EditBatch          m_current;
    int                m_depth = 0;
    QString            m_lastBackup;

    static constexpr int MAX_BATCHES = 20;
};
