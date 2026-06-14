// =============================================================================
// learned_commands.cpp
// =============================================================================

#include "learned_commands.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QRegularExpression>
#include <QProcess>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

LearnedCommands::LearnedCommands(QObject* parent)
    : QObject(parent)
{
    load();
}

// ---------------------------------------------------------------------------
// persistPath
// ---------------------------------------------------------------------------
QString LearnedCommands::persistPath() const
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + QStringLiteral("/Jarvis");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/learned_commands.json");
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------
void LearnedCommands::load()
{
    QFile f(persistPath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    for (const QJsonValue& val : doc.array()) {
        const QJsonObject obj = val.toObject();
        LearnedCommand cmd;
        cmd.id = obj[QStringLiteral("id")].toString();
        cmd.description = obj[QStringLiteral("description")].toString();
        cmd.confidence = static_cast<float>(obj[QStringLiteral("confidence")].toDouble(0.9));
        cmd.useCount   = obj[QStringLiteral("use_count")].toInt(0);
        cmd.learnedAt  = QDateTime::fromString(
            obj[QStringLiteral("learned_at")].toString(), Qt::ISODate);
        cmd.lastUsed   = QDateTime::fromString(
            obj[QStringLiteral("last_used")].toString(), Qt::ISODate);

        for (const QJsonValue& p : obj[QStringLiteral("trigger_patterns")].toArray())
            cmd.triggerPatterns.append(p.toString());

        for (const QJsonValue& sv : obj[QStringLiteral("steps")].toArray()) {
            const QJsonObject so = sv.toObject();
            LearnedStep step;
            const QString typeStr = so[QStringLiteral("type")].toString();
            if      (typeStr == QStringLiteral("shell"))      step.type = LearnedStep::Type::Shell;
            else if (typeStr == QStringLiteral("key"))        step.type = LearnedStep::Type::KeyPress;
            else if (typeStr == QStringLiteral("type"))       step.type = LearnedStep::Type::TypeText;
            else if (typeStr == QStringLiteral("click"))      step.type = LearnedStep::Type::Click;
            else if (typeStr == QStringLiteral("url"))        step.type = LearnedStep::Type::OpenUrl;
            else if (typeStr == QStringLiteral("wincmd"))     step.type = LearnedStep::Type::WinCommand;
            step.value   = so[QStringLiteral("value")].toString();
            step.delayMs = so[QStringLiteral("delay_ms")].toInt(200);
            cmd.steps.append(step);
        }

        if (cmd.isValid()) m_commands.append(cmd);
    }
}

void LearnedCommands::save()
{
    QJsonArray arr;
    for (const auto& cmd : m_commands) {
        QJsonObject obj;
        obj[QStringLiteral("id")]          = cmd.id;
        obj[QStringLiteral("description")] = cmd.description;
        obj[QStringLiteral("confidence")]  = static_cast<double>(cmd.confidence);
        obj[QStringLiteral("use_count")]   = cmd.useCount;
        obj[QStringLiteral("learned_at")]  = cmd.learnedAt.toString(Qt::ISODate);
        obj[QStringLiteral("last_used")]   = cmd.lastUsed.toString(Qt::ISODate);

        QJsonArray patterns;
        for (const QString& p : cmd.triggerPatterns) patterns.append(p);
        obj[QStringLiteral("trigger_patterns")] = patterns;

        QJsonArray steps;
        for (const auto& s : cmd.steps) {
            QJsonObject so;
            switch (s.type) {
                case LearnedStep::Type::Shell:      so[QStringLiteral("type")] = QStringLiteral("shell");  break;
                case LearnedStep::Type::KeyPress:   so[QStringLiteral("type")] = QStringLiteral("key");    break;
                case LearnedStep::Type::TypeText:   so[QStringLiteral("type")] = QStringLiteral("type");   break;
                case LearnedStep::Type::Click:      so[QStringLiteral("type")] = QStringLiteral("click");  break;
                case LearnedStep::Type::OpenUrl:    so[QStringLiteral("type")] = QStringLiteral("url");    break;
                case LearnedStep::Type::WinCommand: so[QStringLiteral("type")] = QStringLiteral("wincmd"); break;
            }
            so[QStringLiteral("value")]    = s.value;
            so[QStringLiteral("delay_ms")] = s.delayMs;
            steps.append(so);
        }
        obj[QStringLiteral("steps")] = steps;
        arr.append(obj);
    }

    QFile f(persistPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

// ---------------------------------------------------------------------------
// normalize — нижний регистр, убрать пунктуацию
// ---------------------------------------------------------------------------
QString LearnedCommands::normalize(const QString& input) const
{
    QString s = input.toLower().trimmed();
    static const QRegularExpression rePunct(QStringLiteral("[^а-яёa-z0-9\\s]"));
    s.remove(rePunct);
    // Схлопнуть пробелы
    static const QRegularExpression reSpaces(QStringLiteral("\\s+"));
    s = s.replace(reSpaces, QStringLiteral(" ")).trimmed();
    return s;
}

// ---------------------------------------------------------------------------
// similarity — процент совпадающих слов (Jaccard на биграммах слов)
// ---------------------------------------------------------------------------
float LearnedCommands::similarity(const QString& a, const QString& b) const
{
    const QStringList wa = normalize(a).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList wb = normalize(b).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (wa.isEmpty() || wb.isEmpty()) return 0.0f;

    // Bigrams
    auto bigrams = [](const QStringList& words) -> QSet<QString> {
        QSet<QString> bg;
        for (const QString& w : words) bg.insert(w); // unigrams тоже
        for (int i = 0; i + 1 < words.size(); ++i)
            bg.insert(words[i] + QStringLiteral(" ") + words[i+1]);
        return bg;
    };

    const QSet<QString> ba = bigrams(wa);
    const QSet<QString> bb = bigrams(wb);
    const int inter = static_cast<int>((ba & bb).size());
    const int uni   = static_cast<int>((ba | bb).size());
    return uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 0.0f;
}

// ---------------------------------------------------------------------------
// findMatch
// ---------------------------------------------------------------------------
const LearnedCommand* LearnedCommands::findMatch(const QString& input) const
{
    const LearnedCommand* best = nullptr;
    float bestScore = kMatchThreshold;

    for (const auto& cmd : m_commands) {
        for (const QString& pattern : cmd.triggerPatterns) {
            const float score = similarity(input, pattern);
            if (score > bestScore) {
                bestScore = score;
                best = &cmd;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// execute — выполнить команду локально
// ---------------------------------------------------------------------------
QString LearnedCommands::execute(const LearnedCommand& cmd)
{
    // Ищем команду в списке и обновляем счётчик
    for (auto& c : m_commands) {
        if (c.id == cmd.id) {
            ++c.useCount;
            c.lastUsed = QDateTime::currentDateTime();
            break;
        }
    }

    QStringList doneSteps;
    bool ok = true;

    for (const LearnedStep& step : cmd.steps) {
        switch (step.type) {

        case LearnedStep::Type::Shell: {
#ifdef Q_OS_WIN
            const HINSTANCE r = ShellExecuteW(
                nullptr, L"open",
                reinterpret_cast<LPCWSTR>(step.value.utf16()),
                nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(r) <= 32)
                QProcess::startDetached(step.value, {});
#else
            QProcess::startDetached(step.value, {});
#endif
            doneSteps.append(QStringLiteral("▶ ") + step.value);
            break;
        }

        case LearnedStep::Type::OpenUrl: {
#ifdef Q_OS_WIN
            ShellExecuteW(nullptr, L"open",
                reinterpret_cast<LPCWSTR>(step.value.utf16()),
                nullptr, nullptr, SW_SHOWNORMAL);
#endif
            doneSteps.append(QStringLiteral("🌐 ") + step.value);
            break;
        }

        case LearnedStep::Type::KeyPress: {
#ifdef Q_OS_WIN
            // Парсим "ctrl+c", "alt+f4", "enter", etc.
            const QStringList parts = step.value.toLower().split(
                QLatin1Char('+'), Qt::SkipEmptyParts);
            QVector<INPUT> inputs;

            auto addKey = [&inputs](WORD vk, bool down) {
                INPUT inp = {};
                inp.type = INPUT_KEYBOARD;
                inp.ki.wVk = vk;
                if (!down) inp.ki.dwFlags = KEYEVENTF_KEYUP;
                inputs.append(inp);
            };

            auto strToVk = [](const QString& s) -> WORD {
                if (s == QStringLiteral("ctrl"))   return VK_CONTROL;
                if (s == QStringLiteral("alt"))    return VK_MENU;
                if (s == QStringLiteral("shift"))  return VK_SHIFT;
                if (s == QStringLiteral("win"))    return VK_LWIN;
                if (s == QStringLiteral("enter"))  return VK_RETURN;
                if (s == QStringLiteral("esc"))    return VK_ESCAPE;
                if (s == QStringLiteral("tab"))    return VK_TAB;
                if (s == QStringLiteral("space"))  return VK_SPACE;
                if (s == QStringLiteral("f4"))     return VK_F4;
                if (s == QStringLiteral("f5"))     return VK_F5;
                if (s.length() == 1) return static_cast<WORD>(VkKeyScanW(s[0].unicode()) & 0xFF);
                return 0;
            };

            QVector<WORD> vks;
            for (const QString& p : parts) { WORD v = strToVk(p); if (v) vks.append(v); }
            for (WORD v : vks) addKey(v, true);
            std::reverse(vks.begin(), vks.end());
            for (WORD v : vks) addKey(v, false);

            if (!inputs.isEmpty())
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
#endif
            doneSteps.append(QStringLiteral("⌨ ") + step.value);
            break;
        }

        case LearnedStep::Type::TypeText: {
#ifdef Q_OS_WIN
            for (QChar c : step.value) {
                SHORT vk = VkKeyScanW(c.unicode());
                if (vk == -1) continue;
                INPUT inp[2] = {};
                inp[0].type = inp[1].type = INPUT_KEYBOARD;
                inp[0].ki.wVk = inp[1].ki.wVk = static_cast<WORD>(vk & 0xFF);
                if (vk & 0x100) { // Shift нужен
                    INPUT shift[2] = {};
                    shift[0].type = shift[1].type = INPUT_KEYBOARD;
                    shift[0].ki.wVk = shift[1].ki.wVk = VK_SHIFT;
                    shift[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, shift, sizeof(INPUT)); // нажать shift перед
                }
                inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, inp, sizeof(INPUT));
                QThread::msleep(10);
            }
#endif
            doneSteps.append(QStringLiteral("📝 typed: ") + step.value);
            break;
        }

        case LearnedStep::Type::Click: {
#ifdef Q_OS_WIN
            // value = "x,y" в пикселях
            const QStringList xy = step.value.split(QLatin1Char(','));
            if (xy.size() == 2) {
                const int x = xy[0].trimmed().toInt();
                const int y = xy[1].trimmed().toInt();
                SetCursorPos(x, y);
                QThread::msleep(50);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                QThread::msleep(50);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                doneSteps.append(QString(QStringLiteral("🖱 click(%1,%2)")).arg(x).arg(y));
            }
#endif
            break;
        }

        case LearnedStep::Type::WinCommand: {
            const QString val = step.value.toLower();
#ifdef Q_OS_WIN
            if (val == QStringLiteral("lock"))     LockWorkStation();
            if (val == QStringLiteral("minimize")) ShowWindow(GetForegroundWindow(), SW_MINIMIZE);
            if (val == QStringLiteral("maximize")) ShowWindow(GetForegroundWindow(), SW_MAXIMIZE);
#endif
            doneSteps.append(QStringLiteral("⚙ ") + step.value);
            break;
        }
        } // switch

        if (step.delayMs > 0)
            QThread::msleep(static_cast<unsigned long>(step.delayMs));
    }

    save();
    emit commandExecuted(cmd.id, ok);
    return doneSteps.join(QStringLiteral("\n"));
}

// ---------------------------------------------------------------------------
// extractStepsFromResponse — парсим ответ AI
// ---------------------------------------------------------------------------
QList<LearnedStep> LearnedCommands::extractStepsFromResponse(const QString& response) const
{
    QList<LearnedStep> steps;

    // Паттерны для извлечения:

    // URL — открыть в браузере
    static const QRegularExpression reUrl(
        QStringLiteral(R"(https?://[^\s\)\]\>\"\']+)"));
    auto urlIt = reUrl.globalMatch(response);
    while (urlIt.hasNext()) {
        const auto m = urlIt.next();
        LearnedStep s;
        s.type  = LearnedStep::Type::OpenUrl;
        s.value = m.captured(0);
        s.delayMs = 500;
        steps.append(s);
    }

    // ShellExecute/run команды в блоках кода: ``` ... ``` или `cmd`
    static const QRegularExpression reCode(
        QStringLiteral(R"(```(?:\w+\n)?(.*?)```|`([^`\n]+)`)"),
        QRegularExpression::DotMatchesEverythingOption);
    auto codeIt = reCode.globalMatch(response);
    while (codeIt.hasNext()) {
        const auto m = codeIt.next();
        const QString code = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).trimmed();
        if (code.isEmpty()) continue;

        // Только однострочные простые команды
        if (code.contains(QLatin1Char('\n')) && code.split(QLatin1Char('\n')).size() > 3)
            continue;

        // Пропускаем code-блоки с кодом C++/Python — они не для ShellExecute
        if (code.contains(QStringLiteral("#include")) ||
            code.contains(QStringLiteral("def ")) ||
            code.contains(QStringLiteral("class "))) continue;

        // Только если это похоже на команду ОС
        if (code.startsWith(QStringLiteral("start ")) ||
            code.startsWith(QStringLiteral("taskkill")) ||
            code.startsWith(QStringLiteral("explorer")) ||
            code.startsWith(QStringLiteral("notepad")) ||
            code.startsWith(QStringLiteral("calc")) ||
            code.endsWith(QStringLiteral(".exe")))
        {
            LearnedStep s;
            s.type  = LearnedStep::Type::Shell;
            s.value = code.split(QLatin1Char('\n')).first().trimmed();
            steps.append(s);
        }
    }

    return steps;
}

// ---------------------------------------------------------------------------
// learnFromApiResponse
// ---------------------------------------------------------------------------
void LearnedCommands::learnFromApiResponse(
    const QString& userInput,
    const QString& aiResponse,
    const QString& actionTaken)
{
    QList<LearnedStep> steps = extractStepsFromResponse(aiResponse);

    // Если нет исполняемых шагов — ничему учиться нечему
    // (например, ответ был просто текстовым объяснением)
    if (steps.isEmpty()) return;

    // Проверяем: такая команда уже известна?
    const LearnedCommand* existing = findMatch(userInput);
    if (existing) {
        // Уже знаем — просто обновляем счётчик (уже сделано в execute())
        return;
    }

    // Создаём новую запись
    LearnedCommand cmd;
    cmd.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cmd.triggerPatterns.append(normalize(userInput));
    cmd.steps       = steps;
    cmd.description = actionTaken.isEmpty() ? userInput : actionTaken;
    cmd.confidence  = 0.75f; // начинаем с умеренной уверенностью
    cmd.useCount    = 0;
    cmd.learnedAt   = QDateTime::currentDateTime();

    m_commands.append(cmd);
    save();
    emit commandLearned(cmd);
}

// ---------------------------------------------------------------------------
// saveCommand / removeCommand / allCommands
// ---------------------------------------------------------------------------
void LearnedCommands::saveCommand(const LearnedCommand& cmd)
{
    // Обновить если есть, добавить если нет
    for (auto& c : m_commands) {
        if (c.id == cmd.id) { c = cmd; save(); return; }
    }
    m_commands.append(cmd);
    save();
}

void LearnedCommands::removeCommand(const QString& id)
{
    m_commands.removeIf([&id](const LearnedCommand& c){ return c.id == id; });
    save();
}

QList<LearnedCommand> LearnedCommands::allCommands() const
{
    return m_commands;
}
