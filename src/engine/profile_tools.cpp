// -------------------------------------------------------
// profile_tools.cpp — см. profile_tools.h
// -------------------------------------------------------

#include "profile_tools.h"

#include "mode_manager.h"
#include "permission_gate.h"
#include "tool_registry.h"

#include <QDebug>

namespace {

// Ищем режим и по id, и по человеческому имени: модель зовёт профиль
// так, как его назвал пользователь ("режим разработки"), а не по id.
const ModeInfo* findMode(ModeManager* modes, const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return nullptr;

    for (const ModeInfo& m : modes->modes()) {
        if (m.id.compare(q, Qt::CaseInsensitive) == 0)
            return &m;
    }
    for (const ModeInfo& m : modes->modes()) {
        if (m.nameRu.compare(q, Qt::CaseInsensitive) == 0
            || m.nameEn.compare(q, Qt::CaseInsensitive) == 0)
            return &m;
    }
    // Частичное совпадение — последним, иначе "работа" перебьёт точный id
    for (const ModeInfo& m : modes->modes()) {
        if (m.nameRu.contains(q, Qt::CaseInsensitive)
            || m.nameEn.contains(q, Qt::CaseInsensitive))
            return &m;
    }
    return nullptr;
}

} // namespace

namespace JarvisTools {

void registerProfileTools(ToolRegistry& reg,
                          ModeManager* modes,
                          PermissionGate* gate,
                          bool english)
{
    if (!modes || !gate) {
        qWarning() << "[Tools] registerProfileTools: modes or gate is null";
        return;
    }

    // --------------------------------------------------------
    //  list_profiles
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("list_profiles");
        t.category    = QStringLiteral("profiles");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "List the available profiles (work modes). A profile bundles the skills "
            "JARVIS uses, its tone, the permission level, notification policy, volume "
            "and an optional workflow to run when it turns on.");
        t.schema  = ToolSchema::empty();
        t.handler = [modes, gate, english](const QJsonObject&) -> ToolResult {
            const QString activeId = modes->activeId();

            QStringList lines;
            for (const ModeInfo& m : modes->modes()) {
                QString line = QStringLiteral("%1%2 [%3]%4")
                                   .arg(m.icon.isEmpty() ? QString() : m.icon + QChar(' '),
                                        m.displayName(english),
                                        m.id,
                                        m.id == activeId ? QStringLiteral("  <- active")
                                                         : QString());
                const QString sys = m.system.summary(english);
                if (!sys.isEmpty())
                    line += QStringLiteral("\n    ") + sys;
                lines << line;
            }

            lines << QStringLiteral("\nCurrent permission level: %1 (%2)")
                         .arg(permissionModeName(gate->mode()),
                              permissionModeDescription(gate->mode(), true));

            return ToolResult::success(lines.join(QChar('\n')),
                                       QStringLiteral("Профилей: %1").arg(modes->modes().size()));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  set_profile
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("set_profile");
        t.category    = QStringLiteral("profiles");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Switch to a profile by id or name. This can change how much JARVIS is "
            "allowed to do on its own, mute notifications, set the volume and start "
            "a workflow - so the user is asked to confirm.");
        t.schema  = ToolSchema().str("profile", "Profile id or name").build();
        t.preview = [modes, english](const QJsonObject& a) {
            const QString query = a.value(QStringLiteral("profile")).toString();
            const ModeInfo* m = findMode(modes, query);
            if (!m)
                return QStringLiteral("Переключить профиль на \"%1\"").arg(query);

            // В диалоге подтверждения должно быть видно, ЧТО именно
            // изменится: профиль может поднять уровень доверия, а это
            // не то, что подтверждают вслепую.
            QString text = QStringLiteral("Профиль: %1").arg(m->displayName(english));
            const QString sys = m->system.summary(english);
            if (!sys.isEmpty())
                text += QStringLiteral("\n%1").arg(sys);
            return text;
        };
        t.handler = [modes, english](const QJsonObject& a) -> ToolResult {
            const QString query = a.value(QStringLiteral("profile")).toString();
            const ModeInfo* m = findMode(modes, query);
            if (!m) {
                QStringList available;
                for (const ModeInfo& mi : modes->modes())
                    available << mi.id;
                return ToolResult::failure(
                    QStringLiteral("No profile matching '%1'. Available: %2")
                        .arg(query, available.join(QStringLiteral(", "))));
            }

            const QString name = m->displayName(english);
            const QString id   = m->id;
            const QString sys  = m->system.summary(english);

            // activate() перечитывает список, поэтому указатель m после
            // вызова использовать нельзя — всё нужное скопировано выше.
            modes->activate(id);

            return ToolResult::success(
                QStringLiteral("Profile '%1' is now active.%2")
                    .arg(name, sys.isEmpty() ? QString()
                                             : QStringLiteral(" (%1)").arg(sys)),
                QStringLiteral("Профиль: %1").arg(name));
        };
        reg.registerTool(t);
    }

    // --------------------------------------------------------
    //  set_permission_mode — только в сторону строгости
    // --------------------------------------------------------
    {
        ToolSpec t;
        t.name        = QStringLiteral("set_permission_mode");
        t.category    = QStringLiteral("profiles");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Tighten how much JARVIS may do without asking: 'paranoid' (ask about "
            "everything), 'balanced' (ask before changes), 'trusted' (ask only before "
            "destructive actions). This tool can only make the level STRICTER - "
            "loosening it is done by the user from the menu, never by the model.");
        t.schema = ToolSchema()
                       .choice("mode", { QStringLiteral("paranoid"),
                                         QStringLiteral("balanced"),
                                         QStringLiteral("trusted") },
                               "Target permission level")
                       .build();
        t.handler = [gate](const QJsonObject& a) -> ToolResult {
            const PermissionMode current = gate->mode();
            const PermissionMode wanted  = permissionModeFromString(
                a.value(QStringLiteral("mode")).toString(), current);

            if (wanted == current) {
                return ToolResult::success(
                    QStringLiteral("Already at '%1'.").arg(permissionModeName(current)),
                    QStringLiteral("Уровень уже %1").arg(permissionModeName(current)));
            }

            // Меньшее значение enum = строже (Paranoid=0 ... Trusted=2).
            if (static_cast<int>(wanted) > static_cast<int>(current)) {
                return ToolResult::failure(
                    QStringLiteral(
                        "Refused: '%1' is more permissive than the current '%2'. "
                        "Only the user can widen what JARVIS may do without asking - "
                        "tell them to change it in the Profile menu.")
                        .arg(permissionModeName(wanted), permissionModeName(current)));
            }

            gate->setMode(wanted);
            return ToolResult::success(
                QStringLiteral("Permission level tightened to '%1' (%2).")
                    .arg(permissionModeName(wanted),
                         permissionModeDescription(wanted, true)),
                QStringLiteral("Уровень доверия: %1").arg(permissionModeName(wanted)));
        };
        reg.registerTool(t);
    }
}

} // namespace JarvisTools
