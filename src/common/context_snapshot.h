#pragma once
// -------------------------------------------------------
// context_snapshot.h — Снимок состояния системы
//
// Собирается перед каждым запросом и передаётся в Brain.
// Содержит всё что может помочь понять намерение:
//   - время, активное окно, открытые процессы
//   - последние N команд из SessionMemory
//   - состояние проекта (индексирован ли, корень)
//   - режим работы (vibecoding, multiagent)
//   - что в буфере обмена
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include <QDateTime>

struct ContextSnapshot
{
    // --- Время ---
    QDateTime   capturedAt;          // момент снимка
    int         hourOfDay = 0;       // 0–23, для определения "утро/вечер"

    // --- Активное окно ОС (до того как фокус перешёл к Jarvis) ---
    QString     activeWindowTitle;   // например "SpawnEnemy.cpp - CLion"
    QString     activeWindowProcess; // например "clion64.exe", "ue5.exe", "chrome.exe"

    // --- Последние команды в сессии ---
    QStringList recentCommands;      // последние 8 команд пользователя (текст)
    QString     lastResponse;        // последний ответ Джарвиса

    // --- Состояние проекта ---
    bool        projectIndexed = false;
    QString     projectRoot;         // например "D:/Projects/MyGame"
    QStringList recentProjectFiles;  // до 10 файлов измен. недавно (из индекса)

    // --- Буфер обмена ---
    QString     clipboardText;       // первые 500 символов буфера (если текст)

    // --- Режимы ---
    bool        vibeCodingMode  = false;
    bool        multiAgentMode  = false;

    // --- Запущенные процессы (relevantOnly) ---
    // Фильтруем только интересные: ue5, clion, rider, chrome, firefox,
    // blender, krita, photoshop, discord, obs, steam
    QStringList runningApps;

    // --- Вспомогательные методы ---

    // Есть ли признак что пользователь в данный момент работает с кодом
    bool isInCodingContext() const
    {
        if (vibeCodingMode) return true;
        if (projectIndexed) return true;

        const QString proc = activeWindowProcess.toLower();
        return proc.contains(QStringLiteral("clion"))
            || proc.contains(QStringLiteral("rider"))
            || proc.contains(QStringLiteral("devenv"))  // Visual Studio
            || proc.contains(QStringLiteral("code"))    // VS Code
            || proc.contains(QStringLiteral("notepad"))
            || activeWindowTitle.toLower().contains(QStringLiteral(".cpp"))
            || activeWindowTitle.toLower().contains(QStringLiteral(".h"));
    }

    // Есть ли признак что пользователь в UE5
    bool isInUE5Context() const
    {
        const QString proc = activeWindowProcess.toLower();
        const QString title = activeWindowTitle.toLower();
        return proc.contains(QStringLiteral("ue5"))
            || proc.contains(QStringLiteral("unrealed"))
            || proc.contains(QStringLiteral("unrealengine"))
            || title.contains(QStringLiteral("unreal editor"))
            || runningApps.filter(QStringLiteral("ue5"), Qt::CaseInsensitive).size() > 0
            || runningApps.filter(QStringLiteral("UnrealEditor"), Qt::CaseInsensitive).size() > 0;
    }

    // Есть ли признак что пользователь работает с электроникой
    // (KiCad schematic/PCB, embedded firmware IDEs)
    bool isInElectronicsContext() const
    {
        const QString proc = activeWindowProcess.toLower();
        const QString title = activeWindowTitle.toLower();
        static const QStringList markers = {
            QStringLiteral("kicad"),    QStringLiteral("eeschema"),
            QStringLiteral("pcbnew"),   QStringLiteral("arduino"),
            QStringLiteral("platformio"), QStringLiteral("eagle"),
            QStringLiteral("altium"),   QStringLiteral("easyeda"),
            QStringLiteral("ltspice"),  QStringLiteral("proteus"),
        };
        for (const auto& m : markers) {
            if (proc.contains(m) || title.contains(m)) return true;
            if (runningApps.filter(m, Qt::CaseInsensitive).size() > 0) return true;
        }
        return false;
    }

    // Есть ли признак что пользователь в браузере
    bool isInBrowserContext() const
    {
        const QString proc = activeWindowProcess.toLower();
        return proc.contains(QStringLiteral("chrome"))
            || proc.contains(QStringLiteral("firefox"))
            || proc.contains(QStringLiteral("msedge"))
            || proc.contains(QStringLiteral("opera"))
            || proc.contains(QStringLiteral("brave"));
    }

    // Последняя команда из истории
    QString lastCommand() const
    {
        return recentCommands.isEmpty() ? QString() : recentCommands.last();
    }

    // Предпоследняя команда (для цепочки диалога)
    QString prevCommand() const
    {
        return recentCommands.size() >= 2
            ? recentCommands[recentCommands.size() - 2]
            : QString();
    }
};
