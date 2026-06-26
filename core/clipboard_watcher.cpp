// ============================================================
// clipboard_watcher.cpp — Smart Clipboard Context Analyzer
// ============================================================
#include "clipboard_watcher.h"
#include "jarvis.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
//  Construction + signal wiring
// ============================================================

ClipboardWatcher::ClipboardWatcher(QObject* parent)
    : QObject(parent)
{
    QClipboard* cb = QGuiApplication::clipboard();
    if (cb) {
        connect(cb, &QClipboard::dataChanged,
                this, &ClipboardWatcher::onClipboardChanged);
        qDebug() << "[ClipboardWatcher] Connected to system clipboard";
    }
}

// ============================================================
//  Clipboard change handler
// ============================================================

void ClipboardWatcher::onClipboardChanged()
{
    if (!m_enabled || !m_jarvis) return;

    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb) return;

    const QString text = cb->text().trimmed();
    if (text.isEmpty() || text.length() < 10 || text.length() > 5000)
        return;

    // Dedup: skip if same content as last dispatch
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(),
                                 QCryptographicHash::Md5).toHex());
    if (hash == m_lastHash) return;

    // Throttle: at most one dispatch per 3 seconds
    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastDispatch.isValid() && m_lastDispatch.msecsTo(now) < 3000)
        return;

    // Pattern matching
    if (isUnrealCrashOrCppError(text)) {
        m_lastHash     = hash;
        m_lastDispatch = now;

        qDebug() << "[ClipboardWatcher] C++/UE error detected ("
                 << text.length() << "chars)";
        emit contextDetected(QStringLiteral("cpp_error"), text.left(200));

        dispatchToLlm(text, QStringLiteral(
            "[CLIPBOARD_CONTEXT: The user copied a C++/Unreal Engine compiler error or crash log. "
            "Analyze it, identify the root cause, suggest a fix. "
            "Be concise — 2-3 sentences for the fix, then the corrected code if applicable. "
            "Respond in the same language the error is in (usually English).]"));
        return;
    }

    if (isDrivingTestContext(text)) {
        m_lastHash     = hash;
        m_lastDispatch = now;

        qDebug() << "[ClipboardWatcher] Driving test context detected ("
                 << text.length() << "chars)";
        emit contextDetected(QStringLiteral("driving_test"), text.left(200));

        dispatchToLlm(text, QStringLiteral(
            "[CLIPBOARD_CONTEXT: The user copied text from a French driving theory test "
            "(Code de la route / Codes Rousseau). "
            "Translate the French question into Russian. "
            "Explain the correct answer and the traffic rule behind it. "
            "Explain why incorrect options are wrong. "
            "Use simple, clear Russian. Mention speed limits, priority rules, "
            "or braking distances if relevant.]"));
        return;
    }
}

// ============================================================
//  Pattern detectors
// ============================================================

bool ClipboardWatcher::isUnrealCrashOrCppError(const QString& text)
{
    static const QRegularExpression reMsvc(
        QStringLiteral("error C\\d{4}:"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reUeLog(
        QStringLiteral("Log(?:Windows|PlayLevel|Init|Compile|BlueprintCompiler|UObjectGlobals):"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reAssert(
        QStringLiteral("Assertion failed:|check\\(|ensure\\(|Fatal error!|Unhandled Exception"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reLinker(
        QStringLiteral("(?:LNK\\d{4}|undefined reference|unresolved external)"),
        QRegularExpression::CaseInsensitiveOption);

    return reMsvc.match(text).hasMatch()
        || reUeLog.match(text).hasMatch()
        || reAssert.match(text).hasMatch()
        || reLinker.match(text).hasMatch();
}

bool ClipboardWatcher::isDrivingTestContext(const QString& text)
{
    static const QRegularExpression reFrench(
        QStringLiteral("(?:code de la route|permis de conduire|"
                       "codes rousseau|auto[- ]?[eé]cole|"
                       "priorit[eé] [àa] droite|c[eé]dez le passage|"
                       "limitation de vitesse|distance de freinage|"
                       "panneau de signalisation|feu tricolore|"
                       "rond[- ]?point|giratoire|"
                       "marquez un temps d'arr[eê]t|"
                       "vous devez|vous pouvez|"
                       "r[ée]ponse [A-D]|bonne r[eé]ponse)"),
        QRegularExpression::CaseInsensitiveOption);

    return reFrench.match(text).hasMatch();
}

// ============================================================
//  LLM dispatch
// ============================================================

void ClipboardWatcher::dispatchToLlm(const QString& clipText,
                                      const QString& systemInstruction)
{
    if (!m_jarvis) return;

    const QString enriched = systemInstruction
        + QStringLiteral("\n\n--- CLIPBOARD CONTENT ---\n")
        + clipText;

    m_jarvis->processCommand(enriched);
}
