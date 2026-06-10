#pragma once
#include <QString>
#include <QMap>
#include <QStringList>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <windows.h>

// Запускает приложения по имени.
// Порядок поиска:
//   1. Таблица алиасов (calc, steam, notepad…)
//   2. Реестр Windows (HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths)
//   3. PATH (QStandardPaths / where.exe)
//   4. Жёсткие популярные пути (Program Files, Program Files (x86))
class AppLauncher {
public:
    AppLauncher() { buildAliasTable(); }

    struct LaunchResult {
        bool    success = false;
        QString resolvedPath;   // реальный путь к exe
        QString errorMessage;
    };

    // Запустить приложение по имени/запросу. name — то, что распознал Brain.
    LaunchResult launch(const QString &name) const {
        QString exe = resolve(name.trimmed().toLower());
        if (exe.isEmpty()) {
            return {false, {}, QString("Could not find application: %1").arg(name)};
        }
        // ShellExecuteW — обходит SmartScreen блокировку как и в AutoUpdater
        HINSTANCE hr = ShellExecuteW(
            nullptr, L"open",
            reinterpret_cast<LPCWSTR>(exe.utf16()),
            nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(hr) <= 32) {
            return {false, exe, QString("ShellExecute failed (code %1) for: %2")
                        .arg(reinterpret_cast<intptr_t>(hr)).arg(exe)};
        }
        return {true, exe, {}};
    }

    // Только резолвинг пути без запуска (для отображения в UI)
    QString resolve(const QString &nameLower) const {
        // 1. Таблица алиасов
        if (m_aliases.contains(nameLower))
            return m_aliases.value(nameLower);

        // 1b. Частичное совпадение с алиасом
        for (auto it = m_aliases.constBegin(); it != m_aliases.constEnd(); ++it) {
            if (nameLower.contains(it.key()) || it.key().contains(nameLower)) {
                if (QFileInfo::exists(it.value())) return it.value();
            }
        }

        // 2. Реестр Windows: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths
        {
            QString regPath = queryRegistry(nameLower);
            if (!regPath.isEmpty() && QFileInfo::exists(regPath)) return regPath;
            // Попробуем с .exe
            if (!nameLower.endsWith(".exe")) {
                regPath = queryRegistry(nameLower + ".exe");
                if (!regPath.isEmpty() && QFileInfo::exists(regPath)) return regPath;
            }
        }

        // 3. where.exe — ищет в PATH
        {
            QString found = whereExe(nameLower);
            if (!found.isEmpty()) return found;
            if (!nameLower.endsWith(".exe")) {
                found = whereExe(nameLower + ".exe");
                if (!found.isEmpty()) return found;
            }
        }

        // 4. Жёсткий обход популярных папок
        QStringList programDirs = {
            "C:/Program Files",
            "C:/Program Files (x86)",
            qEnvironmentVariable("LOCALAPPDATA"),
            qEnvironmentVariable("APPDATA"),
        };
        QString exeName = nameLower.endsWith(".exe") ? nameLower : (nameLower + ".exe");
        for (const QString &dir : programDirs) {
            if (dir.isEmpty()) continue;
            // Прямой путь
            QString candidate = dir + "/" + exeName;
            if (QFileInfo::exists(candidate)) return candidate;
            // Подпапка с именем приложения
            candidate = dir + "/" + nameLower + "/" + exeName;
            if (QFileInfo::exists(candidate)) return candidate;
        }

        return {};
    }

    // Список всех доступных алиасов (для команды "что умеешь открыть")
    QStringList knownAliases() const { return m_aliases.keys(); }

private:
    QMap<QString, QString> m_aliases; // ключ — нижний регистр

    void buildAliasTable() {
        // --- Стандартные Windows ---
        add("блокнот",      "notepad");
        add("notepad",      "C:/Windows/System32/notepad.exe");
        add("калькулятор",  "calc");
        add("calc",         "C:/Windows/System32/calc.exe");
        add("calculator",   "C:/Windows/System32/calc.exe");
        add("проводник",    "explorer");
        add("explorer",     "C:/Windows/explorer.exe");
        add("paint",        "C:/Windows/System32/mspaint.exe");
        add("mspaint",      "C:/Windows/System32/mspaint.exe");
        add("рисование",    "C:/Windows/System32/mspaint.exe");
        add("cmd",          "C:/Windows/System32/cmd.exe");
        add("командная строка", "C:/Windows/System32/cmd.exe");
        add("терминал",     "C:/Windows/System32/cmd.exe");
        add("powershell",   "C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
        add("диспетчер задач", "C:/Windows/System32/Taskmgr.exe");
        add("taskmgr",      "C:/Windows/System32/Taskmgr.exe");
        add("regedit",      "C:/Windows/regedit.exe");
        add("реестр",       "C:/Windows/regedit.exe");
        add("службы",       "C:/Windows/System32/services.msc");
        add("msc",          "C:/Windows/System32/mmc.exe");
        add("snipping",     "C:/Windows/System32/SnippingTool.exe");
        add("ножницы",      "C:/Windows/System32/SnippingTool.exe");
        add("wordpad",      "C:/Program Files/Windows NT/Accessories/wordpad.exe");

        // --- Браузеры ---
        add("chrome",       findFirst({
            "C:/Program Files/Google/Chrome/Application/chrome.exe",
            "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
        }));
        add("firefox",      findFirst({
            "C:/Program Files/Mozilla Firefox/firefox.exe",
            "C:/Program Files (x86)/Mozilla Firefox/firefox.exe",
        }));
        add("brave",        findFirst({
            "C:/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe",
            qEnvironmentVariable("LOCALAPPDATA") + "/BraveSoftware/Brave-Browser/Application/brave.exe",
        }));
        add("edge",         findFirst({
            "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
            "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
        }));
        add("браузер",      "chrome"); // будет разрезолвлен через алиас повторно

        // --- Steam и игры ---
        add("steam",        findFirst({
            "C:/Program Files (x86)/Steam/Steam.exe",
            "C:/Program Files/Steam/Steam.exe",
            qEnvironmentVariable("PROGRAMFILES(X86)") + "/Steam/Steam.exe",
        }));
        add("epic",         findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/EpicGamesLauncher/Portal/Binaries/Win32/EpicGamesLauncher.exe",
            qEnvironmentVariable("PROGRAMDATA")  + "/Epic/EpicGamesLauncher/Portal/Binaries/Win32/EpicGamesLauncher.exe",
        }));
        add("epic games",   resolve("epic"));
        add("gog",          findFirst({"C:/Program Files (x86)/GOG Galaxy/GalaxyClient.exe"}));

        // --- Офис (LibreOffice) ---
        add("libreoffice",  findFirst({
            "C:/Program Files/LibreOffice/program/soffice.exe",
            "C:/Program Files (x86)/LibreOffice/program/soffice.exe",
        }));
        add("word",         findFirst({
            "C:/Program Files/Microsoft Office/root/Office16/WINWORD.EXE",
            "C:/Program Files (x86)/Microsoft Office/root/Office16/WINWORD.EXE",
            "C:/Program Files/Microsoft Office/Office16/WINWORD.EXE",
            // LibreOffice Writer как fallback
            "C:/Program Files/LibreOffice/program/swriter.exe",
        }));
        add("excel",        findFirst({
            "C:/Program Files/Microsoft Office/root/Office16/EXCEL.EXE",
            "C:/Program Files (x86)/Microsoft Office/root/Office16/EXCEL.EXE",
            "C:/Program Files/LibreOffice/program/scalc.exe",
        }));
        add("powerpoint",   findFirst({
            "C:/Program Files/Microsoft Office/root/Office16/POWERPNT.EXE",
            "C:/Program Files/LibreOffice/program/simpress.exe",
        }));
        add("заметки",      findFirst({
            "C:/Windows/System32/notepad.exe",  // fallback — блокнот
        }));
        add("notes",        resolve("заметки"));
        add("ворд",         resolve("word"));
        add("эксель",       resolve("excel"));

        // --- IDE / Dev ---
        add("clion",        findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/Programs/CLion/bin/clion64.exe",
            "C:/Program Files/JetBrains/CLion 2026.1/bin/clion64.exe",
        }));
        add("rider",        findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Rider/bin/rider64.exe",
            "C:/Program Files/JetBrains/Rider 2026.1/bin/rider64.exe",
        }));
        add("vscode",       findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/Programs/Microsoft VS Code/Code.exe",
            "C:/Program Files/Microsoft VS Code/Code.exe",
        }));
        add("vs code",      resolve("vscode"));
        add("visual studio code", resolve("vscode"));

        // --- Мессенджеры ---
        add("telegram",     findFirst({
            qEnvironmentVariable("APPDATA") + "/Telegram Desktop/Telegram.exe",
            qEnvironmentVariable("LOCALAPPDATA") + "/Telegram Desktop/Telegram.exe",
        }));
        add("discord",      findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/Discord/Update.exe",
            qEnvironmentVariable("APPDATA") + "/Discord/app-1.0.9019/Discord.exe",
        }));

        // --- Медиа ---
        add("vlc",          findFirst({
            "C:/Program Files/VideoLAN/VLC/vlc.exe",
            "C:/Program Files (x86)/VideoLAN/VLC/vlc.exe",
        }));
        add("spotify",      findFirst({
            qEnvironmentVariable("APPDATA") + "/Spotify/Spotify.exe",
            qEnvironmentVariable("LOCALAPPDATA") + "/Spotify/Spotify.exe",
        }));

        // --- Графика ---
        add("photoshop",    findFirst({
            "C:/Program Files/Adobe/Adobe Photoshop 2024/Photoshop.exe",
            "C:/Program Files/Adobe/Adobe Photoshop 2025/Photoshop.exe",
        }));
        add("krita",        findFirst({
            "C:/Program Files/Krita (x64)/bin/krita.exe",
            "C:/Program Files/krita/bin/krita.exe",
        }));
        add("blender",      findFirst({
            "C:/Program Files/Blender Foundation/Blender 4.0/blender.exe",
            "C:/Program Files/Blender Foundation/Blender 4.1/blender.exe",
            "C:/Program Files/Blender Foundation/Blender 4.2/blender.exe",
        }));
        add("davinci",      findFirst({
            "C:/Program Files/Blackmagic Design/DaVinci Resolve/Resolve.exe",
        }));

        // --- Прочее ---
        add("obs",          findFirst({
            "C:/Program Files/obs-studio/bin/64bit/obs64.exe",
            "C:/Program Files (x86)/obs-studio/bin/64bit/obs64.exe",
        }));
        add("unreal",       findFirst({
            "C:/Program Files/Epic Games/UE_5.4/Engine/Binaries/Win64/UnrealEditor.exe",
            "C:/Program Files/Epic Games/UE_5.5/Engine/Binaries/Win64/UnrealEditor.exe",
        }));
        add("unreal engine", resolve("unreal"));
        add("ue5",          resolve("unreal"));
        add("deepl",        findFirst({
            qEnvironmentVariable("LOCALAPPDATA") + "/DeepL/DeepL.exe",
        }));
        add("snappy driver", "C:/SDI/SDI_auto.exe");
    }

    // Добавить алиас. Если значение — пустая строка (findFirst не нашёл) — не добавляем.
    void add(const QString &alias, const QString &path) {
        if (!path.isEmpty())
            m_aliases.insert(alias.toLower(), path);
    }

    // Найти первый существующий путь из списка
    static QString findFirst(const QStringList &paths) {
        for (const QString &p : paths)
            if (!p.isEmpty() && QFileInfo::exists(p)) return p;
        return {};
    }

    // Запрос реестра: HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\<name>
    static QString queryRegistry(const QString &exeName) {
        QSettings reg(
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exeName,
            QSettings::NativeFormat);
        QString val = reg.value("Default").toString();
        // Убираем возможные кавычки
        val.remove('"');
        return val;
    }

    // Запуск where.exe для поиска в PATH
    static QString whereExe(const QString &name) {
        QProcess p;
        p.start("where.exe", {name});
        if (!p.waitForFinished(2000)) return {};
        QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
        // where может вернуть несколько строк — берём первую
        QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            QString first = lines.first().trimmed();
            if (QFileInfo::exists(first)) return first;
        }
        return {};
    }
};
