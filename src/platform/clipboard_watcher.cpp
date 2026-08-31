// ============================================================
// clipboard_watcher.cpp — Smart Clipboard Context Analyzer
// ============================================================
#include "clipboard_watcher.h"
#include "jarvis.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN     // иначе rpcndr.h делает #define small char
#endif

#include <QGuiApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>

#include <windows.h>

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
    if (!m_enabled) return;

    // Возврат из истории — не новое копирование.
    if (m_restoring) return;

    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb) return;

    // Клип из менеджера паролей не попадает ни в историю, ни в модель.
    if (isSensitiveClip()) {
        qDebug() << "[ClipboardWatcher] skipped a clip marked as sensitive";
        return;
    }

    const QString clip = cb->text().trimmed();
    if (clip.isEmpty())
        return;

    // История ведётся всегда — она локальная. Всё, что ниже, уже про
    // отправку наружу и требует явно заданного ядра.
    remember(clip);

    if (!m_jarvis) return;

    const QString text = clip;
    if (text.length() < 10 || text.length() > 5000)
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
//  История
// ============================================================

QString ClipEntry::preview(int maxChars) const
{
    QString flat = text;
    flat.replace(QChar('\n'), QChar(' '));
    flat.replace(QChar('\t'), QChar(' '));
    flat = flat.simplified();
    if (flat.length() > maxChars)
        flat = flat.left(maxChars - 3) + QStringLiteral("...");

    QString line = at.toString(QStringLiteral("HH:mm"));
    if (!sourceApp.isEmpty())
        line += QStringLiteral("  ") + sourceApp;
    line += QStringLiteral("  [") + kind + QStringLiteral("]  ") + flat;
    return line;
}

bool ClipboardWatcher::isSensitiveClip()
{
    // Оба формата — де-факто стандарт: их выставляют KeePass, 1Password,
    // Bitwarden и менеджеры Windows, чтобы историю их клипов не вели.
    static const UINT kIgnore  = RegisterClipboardFormatW(L"Clipboard Viewer Ignore");
    static const UINT kExclude = RegisterClipboardFormatW(
        L"ExcludeClipboardContentFromMonitorProcessing");

    return (kIgnore  && IsClipboardFormatAvailable(kIgnore))
        || (kExclude && IsClipboardFormatAvailable(kExclude));
}

QString ClipboardWatcher::detectKind(const QString& text)
{
    static const QRegularExpression reUrl(
        QStringLiteral("^(?:https?|ftp)://\\S+$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rePath(
        QStringLiteral("^(?:[A-Za-z]:[\\\\/]|\\\\\\\\|/)[^\\n]{2,}$"));
    static const QRegularExpression reCode(
        QStringLiteral("(?:[;{}]\\s*$|^\\s*(?:#include|import |def |class |void |const |"
                       "public |private |function |SELECT |return )|::)"),
        QRegularExpression::MultilineOption);

    if (isUnrealCrashOrCppError(text))       return QStringLiteral("error");
    if (reUrl.match(text.trimmed()).hasMatch())  return QStringLiteral("url");
    if (rePath.match(text.trimmed()).hasMatch()) return QStringLiteral("path");
    if (reCode.match(text).hasMatch())       return QStringLiteral("code");
    return QStringLiteral("text");
}

QString ClipboardWatcher::foregroundAppName()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return QString();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return QString();

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return QString();

    wchar_t path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(proc, 0, path, &size))
        name = QFileInfo(QString::fromWCharArray(path, size)).completeBaseName();
    CloseHandle(proc);
    return name;
}

void ClipboardWatcher::remember(const QString& text)
{
    // Подряд идущие одинаковые клипы — это одно копирование, о котором
    // система сообщила дважды (так делают многие приложения).
    if (!m_history.isEmpty() && m_history.last().text == text)
        return;

    ClipEntry e;
    e.at        = QDateTime::currentDateTime();
    e.text      = text.length() > kMaxTextChars ? text.left(kMaxTextChars) : text;
    e.kind      = detectKind(text);
    e.sourceApp = foregroundAppName();

    m_history.append(e);
    while (m_history.size() > kMaxEntries)
        m_history.removeFirst();

    emit historyChanged();
}

QVector<ClipEntry> ClipboardWatcher::history(int limit) const
{
    QVector<ClipEntry> out;
    const int count = (limit <= 0) ? m_history.size() : qMin(limit, m_history.size());
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out.append(m_history.at(m_history.size() - 1 - i));   // новые первыми
    return out;
}

const ClipEntry* ClipboardWatcher::at(int index) const
{
    // 1 — последнее скопированное: человек считает от «только что»,
    // а не от начала сессии.
    if (index < 1 || index > m_history.size())
        return nullptr;
    return &m_history.at(m_history.size() - index);
}

bool ClipboardWatcher::restore(int index)
{
    const ClipEntry* entry = at(index);
    if (!entry)
        return false;

    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb)
        return false;

    m_restoring = true;
    cb->setText(entry->text);
    m_restoring = false;
    return true;
}

void ClipboardWatcher::clearHistory()
{
    m_history.clear();
    emit historyChanged();
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
