#pragma once
// ============================================================
// jarvis_paths.h — Единая точка правды для путей хранения
//                   данных J.A.R.V.I.S. на диске пользователя
//
// ПОЧЕМУ ЭТОТ ФАЙЛ СУЩЕСТВУЕТ:
//   Раньше каждый модуль (database_manager, voice_input,
//   passive_listener, session_memory, user_profile,
//   action_predictor, claude_api, project_indexer,
//   screenshot_learner, learned_commands, mainwindow...) сам
//   вызывал QStandardPaths::writableLocation(AppDataLocation),
//   и кое-где ещё вручную доклеивал разные суффиксы
//   ("/Jarvis", "/voice_dataset" и т.п.). Это:
//     1) Прятало все данные в скрытой системной папке AppData,
//        которую пользователь обычно не видит и не открывает.
//     2) Создавало риск рассинхрона между модулями, если кто-то
//        однажды дописывал/удалял суффикс не везде одинаково.
//
//   Теперь ВСЕ модули обязаны брать базовую папку только
//   через JarvisPaths::dataRoot() / JarvisPaths::subPath(...).
//   Папка лежит в Documents — пользователь её сразу видит,
//   может скопировать для бэкапа, может стереть руками, чтобы
//   начать обучение с нуля, и она не путается с тем, из какой
//   именно сборки (CLion debug / установленный релиз) запущен
//   JARVIS — обе сборки используют один и тот же путь по коду.
//
// ПОВЕДЕНИЕ:
//   - Если папка/файлы уже существуют (апдейт версии, повторная
//     установка, переключение между debug- и release-сборкой) —
//     JARVIS открывает их как есть, ничего не теряется.
//   - Если папки нет (первый запуск на чистой машине) — создаётся
//     с нуля, обучение начинается с пустого датасета.
// ============================================================

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace JarvisPaths {

// Имя корневой подпапки внутри "Документов" пользователя.
// Если в будущем понадобится переименовать — меняется в ОДНОМ месте.
inline QString rootFolderName()
{
    return QStringLiteral("Jarvis Data");
}

// Полный путь к корневой папке данных JARVIS, например:
//   Windows: C:/Users/<User>/Documents/Jarvis Data
//   (QStandardPaths сам разворачивает DocumentsLocation для
//    текущего пользователя на любой машине — ничего не захардкожено)
// Папка гарантированно создаётся при первом вызове, если её нет.
inline QString dataRoot()
{
    const QString documents = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);

    // Резервный вариант на случай, если DocumentsLocation недоступен
    // (редкие urезанные окружения/политики профиля) — тогда падаем
    // обратно на AppData, чтобы JARVIS не упал совсем без рабочей папки.
    const QString base = documents.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : documents;

    const QString root = base + QStringLiteral("/") + rootFolderName();
    QDir().mkpath(root);
    return root;
}

// Путь к подпапке/файлу внутри корня данных JARVIS, с автосозданием
// промежуточных директорий. Примеры:
//   subPath("jarvis.db")                  -> .../Jarvis Data/jarvis.db
//   subPath("voice_dataset")              -> .../Jarvis Data/voice_dataset
//   subPath("memory/session.json")        -> .../Jarvis Data/memory/session.json
inline QString subPath(const QString& relative)
{
    const QString root = dataRoot();
    const QString full = root + QStringLiteral("/") + relative;

    // Если relative содержит подпапки — создаём их тоже
    const QFileInfo fi(full);
    if (!fi.absolutePath().isEmpty()) {
        QDir().mkpath(fi.absolutePath());
    }
    return full;
}

} // namespace JarvisPaths
