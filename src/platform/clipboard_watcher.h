#pragma once
// ============================================================
// clipboard_watcher.h — Буфер обмена: история и разбор контекста
//
// Две разные функции в одном наблюдателе, и их важно не путать:
//
//   1. ИСТОРИЯ — то, что человек копировал за сессию. Работает
//      всегда, никуда ничего не отправляет, на диск не пишется.
//   2. АВТО-РАЗБОР — распознанная ошибка компилятора уезжает в
//      модель сама. Включается ТОЛЬКО явным setJarvisCore():
//      без него буфер обмена наружу не уходит вообще.
//
// История принципиально живёт только в памяти. Скопированный
// пароль, токен и ключ — обычное содержимое буфера у разработчика;
// файл с этим на диске переживёт сессию, попадёт в бэкап и станет
// той самой историей, которую потом жалеют. Плюс клипы из
// менеджеров паролей отбрасываются по флагам, которыми те их
// помечают (Clipboard Viewer Ignore).
// ============================================================

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>

class Jarvis;

// ============================================================
//  Запись истории
// ============================================================
struct ClipEntry
{
    QDateTime at;
    QString   text;
    QString   kind;        // url | path | code | error | text
    QString   sourceApp;   // откуда скопировали, если удалось узнать

    // Одна строка для списка: "10:42  Rider   code   void MainWindow::..."
    QString preview(int maxChars = 70) const;
};

class ClipboardWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardWatcher(QObject* parent = nullptr);

    // Пока ядро не задано, наблюдатель ведёт только историю.
    void setJarvisCore(Jarvis* jarvis) { m_jarvis = jarvis; }
    bool isDispatchEnabled() const { return m_jarvis != nullptr; }

    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const   { return m_enabled; }

    // --- История ---
    QVector<ClipEntry> history(int limit = 20) const;   // новые первыми
    const ClipEntry*   at(int index) const;             // 1 = последнее
    int                historySize() const { return m_history.size(); }

    // Вернуть запись в буфер обмена. Повторной записью в историю это
    // не считается — иначе список забился бы собственными возвратами.
    bool restore(int index);
    void clearHistory();

signals:
    void contextDetected(const QString& type, const QString& snippet);
    void historyChanged();

private slots:
    void onClipboardChanged();

private:
    static bool isUnrealCrashOrCppError(const QString& text);
    static bool isDrivingTestContext(const QString& text);

    // Менеджеры паролей помечают свои клипы специальными форматами —
    // единственный надёжный способ не сохранить чужой пароль.
    static bool isSensitiveClip();
    static QString detectKind(const QString& text);
    static QString foregroundAppName();

    void remember(const QString& text);
    void dispatchToLlm(const QString& clipText,
                       const QString& systemInstruction);

    Jarvis*   m_jarvis     = nullptr;
    bool      m_enabled    = true;
    QString   m_lastHash;
    QDateTime m_lastDispatch;

    QVector<ClipEntry> m_history;   // новые в конце
    bool               m_restoring = false;

    // Полсотни записей хватает, чтобы найти «то, что копировал перед
    // этим», и мало, чтобы держать в памяти чужой документ целиком.
    static constexpr int kMaxEntries   = 50;
    static constexpr int kMaxTextChars = 20000;
};
