#pragma once
// -------------------------------------------------------
// context_snapshot.h — снимок контекста для Brain
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include "memory_limits.h"

struct ContextSnapshot
{
    // Последние команды пользователя (для уточнения намерения)
    QStringList recentCommands;

    // Последний ответ JARVIS (для уточнений типа "повтори последнее")
    QString lastResponse;

    // Проект ли индексирован?
    bool projectIndexed = false;

    // Корень проекта (если есть)
    QString projectRoot;

    // Недавние файлы проекта (для RAG контекста)
    QStringList recentProjectFiles;

    // Режим ли вайб-кодинга? (для мультиагентного режима)
    bool vibeCodingMode = false;

    // Мультиагентный режим (Claude для кода, Gemini для бесед)?
    bool multiAgentMode = false;

    // OPTIMIZED: Phase 1 - Reduced context window sizes
    // Max 5 commands (was 8) - saves ~2KB
    static constexpr int MAX_RECENT_COMMANDS_SNAPSHOT = MAX_RECENT_COMMANDS;
    // Max 5 files (was 10) - saves ~3KB
    static constexpr int MAX_RECENT_FILES_SNAPSHOT = MAX_RECENT_PROJECT_FILES;
};
