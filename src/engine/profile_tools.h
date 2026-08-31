#pragma once
// -------------------------------------------------------
// profile_tools.h — Профили и уровень доверия как инструменты
//
// Режим (ModeManager) уже был профилем поведения модели —
// скиллы плюс блок в system prompt. Системная часть профиля
// (разрешения, уведомления, громкость, стартовый сценарий)
// живёт в том же mode.json, поэтому отдельной сущности
// "Profile" нет: профиль — это режим целиком.
//
// Регистратор лежит в engine, а не в agent: ему нужен
// ModeManager, а agent про engine ничего не знает и знать
// не должен — зависимость идёт только в одну сторону.
// -------------------------------------------------------

class ToolRegistry;
class ModeManager;
class PermissionGate;

namespace JarvisTools {

// list_profiles / set_profile / set_permission_mode.
//
// set_permission_mode умеет только УЖЕСТОЧАТЬ: разрешить себе больше
// модель не может ни при каких формулировках — это делается руками
// из меню. Ослабление через профиль возможно, но профиль переключается
// с подтверждением, и в диалоге видно, какой уровень он ставит.
void registerProfileTools(ToolRegistry& registry,
                          ModeManager* modes,
                          PermissionGate* gate,
                          bool english);

} // namespace JarvisTools
