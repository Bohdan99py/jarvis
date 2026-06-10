// ═══════════════════════════════════════════════════════════════════════════
// INTEGRATION GUIDE — как подключить новые модули в существующий код JARVIS
// ═══════════════════════════════════════════════════════════════════════════
//
// 1. Скопируй 4 файла в core/:
//       languagedetector.h
//       applauncher.h
//       systemcontroller.h
//       fileviewer.h
//
// 2. В brain.h добавь члены класса:
// ────────────────────────────────────────────────────────────────────────────

// brain.h (добавить к существующим includes и членам класса)
/*
#include "languagedetector.h"
#include "applauncher.h"
#include "systemcontroller.h"

// Внутри класса Brain:
private:
    LanguageDetector m_langDetector;
    AppLauncher      m_appLauncher;
*/

// ═══════════════════════════════════════════════════════════════════════════
// 3. В brain.cpp — метод processCommand() / processInput()
//    Вставить ДО отправки в AI:
// ─────────────────────────────────────────────────────────────────────────
/*
BrainResult Brain::processInput(const QString &userInput)
{
    // ── 1. Обновляем язык сессии ──────────────────────────────────────
    m_langDetector.update(userInput);

    // ── 2. Системные команды (звук, яркость, и т.д.) ──────────────────
    auto sysResult = SystemController::tryExecuteSystemCommand(userInput);
    if (sysResult.success && !sysResult.message.isEmpty()) {
        return BrainResult{BrainResult::Action::Respond, sysResult.message};
    }

    // ── 3. Команды открытия приложений ────────────────────────────────
    // Детектируем "открой/запусти/open/launch + имя"
    {
        static const QRegularExpression reOpen(
            R"((?:открой|запусти|запустить|open|launch|start|run)\s+(.+))",
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = reOpen.match(userInput);
        if (m.hasMatch()) {
            QString appName = m.captured(1).trimmed();
            auto result = m_appLauncher.launch(appName);
            if (result.success) {
                return BrainResult{BrainResult::Action::Respond,
                    QString("Opening: %1").arg(result.resolvedPath.section('/', -1))};
            } else {
                // Не нашли — передаём в AI с контекстом
                // AI сам объяснит или предложит альтернативу
            }
        }
    }

    // ── 4. Формируем системный промпт с языком ────────────────────────
    // В buildSystemPrompt() добавь в начало:
    // prompt.prepend(m_langDetector.systemInstruction() + "\n\n");

    // ... остальная логика Brain ...
}
*/

// ═══════════════════════════════════════════════════════════════════════════
// 4. В SearchRouter — после нахождения файлов, вместо простого показа пути:
// ─────────────────────────────────────────────────────────────────────────
/*
// searchrouter.cpp (в методе handleFilesystemSearch или аналоге)

#include "fileviewer.h"

void SearchRouter::onFilesFound(const QStringList &paths, QWidget *parentWindow)
{
    if (paths.isEmpty()) {
        emit searchResult("Nothing found in filesystem for your query.");
        return;
    }

    // Показываем результаты в чате (текст)
    QString msg = QString("Found %1 in filesystem:\n").arg(paths.size());
    for (const QString &p : paths) {
        QFileInfo fi(p);
        msg += QString("• %1\n  %2\n  %3  %4 KB\n")
                   .arg(fi.fileName())
                   .arg(p)
                   .arg(fi.lastModified().toString("dd.MM.yyyy HH:mm"))
                   .arg(fi.size() / 1024);
    }

    // Открываем окно просмотра
    FileViewer::showFiles(paths, parentWindow);

    emit searchResult(msg);
}
*/

// ═══════════════════════════════════════════════════════════════════════════
// 5. В chatwindow.cpp — buildSystemPrompt():
// ─────────────────────────────────────────────────────────────────────────
/*
QString ChatWindow::buildSystemPrompt() const
{
    QString base = m_brain->systemPrompt(); // существующий промпт

    // Добавляем языковую инструкцию ПЕРЕД основным промптом
    QString langInstr = m_brain->languageDetector().systemInstruction();
    if (!langInstr.isEmpty()) {
        base = langInstr + "\n\n" + base;
    }

    return base;
}
*/

// ═══════════════════════════════════════════════════════════════════════════
// 6. CMakeLists.txt — добавить линковку для COM (аудио) и Shell:
// ─────────────────────────────────────────────────────────────────────────
/*
target_link_libraries(jarvis_core
    # ... существующие ...
    ole32          # CoCreateInstance (для IAudioEndpointVolume)
    oleaut32
    uuid
    shell32        # ShellExecuteW
    wbemuuid       # WMI (опционально, для яркости)
)
*/

// ═══════════════════════════════════════════════════════════════════════════
// 7. Тест в Settings > Agent mode — команды для проверки:
// ─────────────────────────────────────────────────────────────────────────
//   "открой steam"           → запустит Steam.exe
//   "открой калькулятор"     → calc.exe
//   "громкость 50"           → установит 50%
//   "яркость 70"             → установит 70%
//   "громче"                 → +10%
//   "тише на 20"             → -20%
//   "заблокируй"             → LockWorkStation
//   "выключи звук"           → Mute
//   "ночной режим"           → Night Light
// ═══════════════════════════════════════════════════════════════════════════
