#pragma once
// ============================================================
// clipboard_watcher.h — Smart Clipboard Context Analyzer
//
// Monitors system clipboard for contextual patterns and
// silently dispatches matched content to the LLM backend:
//   • C++ / MSVC / Unreal Engine errors → auto-debug
//   • French driving theory context → auto-explain
// ============================================================

#include <QObject>
#include <QString>
#include <QDateTime>

class Jarvis;

class ClipboardWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardWatcher(QObject* parent = nullptr);

    void setJarvisCore(Jarvis* jarvis) { m_jarvis = jarvis; }
    void setEnabled(bool on) { m_enabled = on; }
    bool isEnabled() const   { return m_enabled; }

signals:
    void contextDetected(const QString& type, const QString& snippet);

private slots:
    void onClipboardChanged();

private:
    static bool isUnrealCrashOrCppError(const QString& text);
    static bool isDrivingTestContext(const QString& text);

    void dispatchToLlm(const QString& clipText,
                       const QString& systemInstruction);

    Jarvis*   m_jarvis     = nullptr;
    bool      m_enabled    = true;
    QString   m_lastHash;
    QDateTime m_lastDispatch;
};
