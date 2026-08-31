#pragma once
// -------------------------------------------------------
// git_tools.h — Git как инструменты, а не как shell-команда
//
// Через run_command git и так работал, но у этого три беды:
// run_command имеет риск Dangerous и потому спрашивает подтверждение
// даже на «покажи, что я менял»; модель каждый раз сочиняет флаги;
// а в журнале действий остаётся строка «выполнена команда», из
// которой не видно, что это был git.
//
// Здесь чтение (status, diff, log) объявлено безопасным и выполняется
// молча, а единственное изменяющее действие — commit — проходит
// подтверждение как обычный Moderate-инструмент. Push намеренно нет:
// отправку наружу человек делает сам.
//
// Репозиторий по умолчанию берётся не из догадки модели, а из того,
// что открыто прямо сейчас: провайдер отдаёт корень проекта из
// ProjectIndexer.
// -------------------------------------------------------

#include <QString>

#include <functional>

class ToolRegistry;

namespace JarvisTools {

using RepoProvider = std::function<QString()>;

// git_status / git_diff / git_log / git_commit
void registerGitTools(ToolRegistry& registry, RepoProvider defaultRepo = {});

} // namespace JarvisTools
