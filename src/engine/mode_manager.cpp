// -------------------------------------------------------
// mode_manager.cpp — Режимы работы J.A.R.V.I.S.
// -------------------------------------------------------

#include "mode_manager.h"
#include "skill_manager.h"
#include "jarvis_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include <algorithm>

// ============================================================
// Пути
// ============================================================

QString ModeManager::userModesDir()
{
    return JarvisPaths::subPath(QStringLiteral("modes"));
}

static QString bundledModesDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/modes");
}

QString ModeManager::stateFilePath() const
{
    return userModesDir() + QStringLiteral("/modes_state.json");
}

// ============================================================
// Конструктор
// ============================================================

ModeManager::ModeManager(SkillManager* skills, QObject* parent)
    : QObject(parent)
    , m_skills(skills)
{
    scan();
}

// ============================================================
// Манифест
// ============================================================

static QString localizedField(const QJsonValue& v, const QString& lang)
{
    if (v.isObject()) return v.toObject().value(lang).toString();
    if (lang == QStringLiteral("en")) return v.toString();
    return v.toString();
}

static QStringList arrayToStringList(const QJsonValue& v)
{
    QStringList out;
    if (!v.isArray()) return out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}

QString ModeSystemProfile::summary(bool english) const
{
    if (isEmpty())
        return QString();

    QStringList parts;
    if (!permissionMode.isEmpty()) {
        parts << (english ? QStringLiteral("permissions: %1").arg(permissionMode)
                          : QStringLiteral("разрешения: %1").arg(permissionMode));
    }
    if (!notifications.isEmpty()) {
        parts << (english ? QStringLiteral("notifications: %1").arg(notifications)
                          : QStringLiteral("уведомления: %1").arg(notifications));
    }
    if (!voice.isEmpty()) {
        parts << (english ? QStringLiteral("voice: %1").arg(voice)
                          : QStringLiteral("голос: %1").arg(voice));
    }
    if (volume >= 0) {
        parts << (english ? QStringLiteral("volume %1%").arg(volume)
                          : QStringLiteral("громкость %1%").arg(volume));
    }
    if (!onActivateWorkflow.isEmpty()) {
        parts << (english ? QStringLiteral("runs \"%1\"").arg(onActivateWorkflow)
                          : QStringLiteral("запускает \"%1\"").arg(onActivateWorkflow));
    }
    if (!autoActivateApps.isEmpty()) {
        parts << (english ? QStringLiteral("auto on: %1").arg(autoActivateApps.join(QStringLiteral(", ")))
                          : QStringLiteral("сам включается в: %1").arg(autoActivateApps.join(QStringLiteral(", "))));
    }
    return parts.join(QStringLiteral(" · "));
}

bool ModeManager::mergeSystemBlock(const QString& srcDir, const QString& dstDir)
{
    QFile srcFile(srcDir + QStringLiteral("/mode.json"));
    QFile dstFile(dstDir + QStringLiteral("/mode.json"));
    if (!srcFile.open(QIODevice::ReadOnly) || !dstFile.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject src = QJsonDocument::fromJson(srcFile.readAll()).object();
    QJsonObject       dst = QJsonDocument::fromJson(dstFile.readAll()).object();
    srcFile.close();
    dstFile.close();

    // Только добавляем недостающее — если пользователь уже правил свой
    // "system", его настройки трогать нельзя.
    if (!src.contains(QStringLiteral("system")) || dst.contains(QStringLiteral("system")))
        return false;

    dst[QStringLiteral("system")] = src.value(QStringLiteral("system"));

    if (!dstFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    dstFile.write(QJsonDocument(dst).toJson(QJsonDocument::Indented));
    dstFile.close();
    return true;
}

bool ModeManager::readManifest(const QString& modeDir, ModeInfo& out)
{
    QFile f(modeDir + QStringLiteral("/mode.json"));
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[Modes] Bad manifest in" << modeDir << ":" << err.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    out.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (out.id.isEmpty())
        out.id = QFileInfo(modeDir).fileName();

    out.nameRu        = localizedField(obj.value(QStringLiteral("name")),        QStringLiteral("ru"));
    out.nameEn        = localizedField(obj.value(QStringLiteral("name")),        QStringLiteral("en"));
    out.descriptionRu = localizedField(obj.value(QStringLiteral("description")), QStringLiteral("ru"));
    out.descriptionEn = localizedField(obj.value(QStringLiteral("description")), QStringLiteral("en"));
    out.icon          = obj.value(QStringLiteral("icon")).toString();
    out.accentColor   = obj.value(QStringLiteral("accent")).toString();
    out.enableSkills  = arrayToStringList(obj.value(QStringLiteral("enable_skills")));
    out.disableSkills = arrayToStringList(obj.value(QStringLiteral("disable_skills")));
    out.exclusive     = obj.value(QStringLiteral("exclusive")).toBool(false);
    out.defaultActive = obj.value(QStringLiteral("default_active")).toBool(false);
    out.dirPath       = modeDir;

    // Системная часть профиля — необязательная. Режим без неё
    // остаётся тем, чем был: набором скиллов и промпт-блоком.
    const QJsonObject sys = obj.value(QStringLiteral("system")).toObject();
    if (!sys.isEmpty()) {
        out.system.permissionMode = sys.value(QStringLiteral("permission_mode")).toString().trimmed();
        out.system.notifications  = sys.value(QStringLiteral("notifications")).toString().trimmed();
        out.system.voice          = sys.value(QStringLiteral("voice")).toString().trimmed();
        out.system.volume         = sys.contains(QStringLiteral("volume"))
                                        ? sys.value(QStringLiteral("volume")).toInt(-1)
                                        : -1;
        out.system.onActivateWorkflow =
            sys.value(QStringLiteral("on_activate_workflow")).toString().trimmed();
        out.system.onDeactivateWorkflow =
            sys.value(QStringLiteral("on_deactivate_workflow")).toString().trimmed();
        out.system.autoActivateApps =
            arrayToStringList(sys.value(QStringLiteral("auto_activate_apps")));
    }

    // prompt: либо inline объект {ru,en}/строка, либо ссылка на файл
    // "prompt_file": "prompt.md" -> общий на оба языка.
    const QJsonValue pv = obj.value(QStringLiteral("prompt"));
    if (pv.isObject() || pv.isString()) {
        out.promptRu = localizedField(pv, QStringLiteral("ru"));
        out.promptEn = localizedField(pv, QStringLiteral("en"));
    }
    const QString pf = obj.value(QStringLiteral("prompt_file")).toString();
    if (!pf.isEmpty()) {
        QFile ff(modeDir + QStringLiteral("/") + pf);
        if (ff.open(QIODevice::ReadOnly)) {
            QString t = QString::fromUtf8(ff.readAll()).trimmed();
            if (t.size() > MAX_PROMPT_CHARS) {
                t = t.left(MAX_PROMPT_CHARS)
                  + QStringLiteral("\n...(mode prompt truncated)");
            }
            if (out.promptRu.isEmpty()) out.promptRu = t;
            if (out.promptEn.isEmpty()) out.promptEn = t;
        }
    }

    // Дефолтная обрезка inline-промптов
    auto clamp = [](QString& s) {
        if (s.size() > MAX_PROMPT_CHARS) {
            s = s.left(MAX_PROMPT_CHARS) + QStringLiteral("\n...(truncated)");
        }
    };
    clamp(out.promptRu);
    clamp(out.promptEn);
    return true;
}

// ============================================================
// Копирование bundled -> user (один раз)
// ============================================================

bool ModeManager::copyModeDir(const QString& src, const QString& dst)
{
    QDir().mkpath(dst);
    const QDir srcDir(src);
    bool ok = true;
    for (const QFileInfo& fi : srcDir.entryInfoList(
             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = dst + QStringLiteral("/") + fi.fileName();
        if (fi.isDir()) {
            ok = copyModeDir(fi.absoluteFilePath(), target) && ok;
        } else {
            QFile::remove(target);
            ok = QFile::copy(fi.absoluteFilePath(), target) && ok;
        }
    }
    return ok;
}

// ============================================================
// State
// ============================================================

void ModeManager::loadState()
{
    m_activeId.clear();
    m_installedOnce.clear();

    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_activeId = root.value(QStringLiteral("active")).toString();
    for (const auto& v : root.value(QStringLiteral("installed_once")).toArray())
        m_installedOnce << v.toString();
}

void ModeManager::saveState() const
{
    QJsonObject root;
    root.insert(QStringLiteral("active"),         m_activeId);
    root.insert(QStringLiteral("installed_once"), QJsonArray::fromStringList(m_installedOnce));

    QFile f(stateFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

// ============================================================
// Скан
// ============================================================

static QStringList listModeDirs(const QString& baseDir)
{
    QStringList result;
    const QDir dir(baseDir);
    if (!dir.exists()) return result;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(fi.absoluteFilePath() + QStringLiteral("/mode.json")))
            result << fi.absoluteFilePath();
    }
    return result;
}

void ModeManager::scan()
{
    loadState();

    const QString userDir = userModesDir();
    QDir().mkpath(userDir);

    // 1) Копируем bundled -> user однократно
    bool stateDirty = false;
    for (const QString& srcDir : listModeDirs(bundledModesDir())) {
        ModeInfo info;
        if (!readManifest(srcDir, info)) continue;

        const QString dst = userDir + QStringLiteral("/") + info.id;
        const bool installed = QFileInfo::exists(dst + QStringLiteral("/mode.json"));

        // Дописать недостающий "system" нужно и тем режимам, что стоят
        // с прошлой версии, — поэтому ДО проверки installedOnce. Иначе
        // системная часть профиля доехала бы только до новых установок.
        if (installed && mergeSystemBlock(srcDir, dst))
            qDebug() << "[Modes] Added system profile block to installed mode:" << info.id;

        if (m_installedOnce.contains(info.id)) continue;

        if (!installed) {
            if (copyModeDir(srcDir, dst))
                qDebug() << "[Modes] Installed bundled mode:" << info.id;
        }
        m_installedOnce << info.id;
        stateDirty = true;
    }

    // 2) Читаем все режимы пользователя
    m_modes.clear();
    for (const QString& dir : listModeDirs(userDir)) {
        ModeInfo info;
        if (!readManifest(dir, info)) continue;
        const bool dup = std::any_of(m_modes.begin(), m_modes.end(),
            [&info](const ModeInfo& m) { return m.id == info.id; });
        if (dup) continue;
        m_modes.append(info);
    }

    std::sort(m_modes.begin(), m_modes.end(),
              [](const ModeInfo& a, const ModeInfo& b) { return a.id < b.id; });

    // Если нет сохранённого активного — выбираем default_active
    if (m_activeId.isEmpty()) {
        for (const ModeInfo& m : m_modes) {
            if (m.defaultActive) { m_activeId = m.id; break; }
        }
    }
    // Если сохранённый id больше не существует — очищаем
    if (!m_activeId.isEmpty()) {
        const bool exists = std::any_of(m_modes.begin(), m_modes.end(),
            [this](const ModeInfo& m) { return m.id == m_activeId; });
        if (!exists) m_activeId.clear();
    }

    if (stateDirty) saveState();
    qDebug() << "[Modes] Loaded" << m_modes.size() << "modes, active:" << m_activeId;
    emit modeChanged();
}

// ============================================================
// Активация
// ============================================================

ModeInfo ModeManager::activeMode() const
{
    for (const ModeInfo& m : m_modes)
        if (m.id == m_activeId) return m;
    return ModeInfo{};
}

void ModeManager::activate(const QString& id)
{
    if (id == m_activeId) return;

    const QString previousId = m_activeId;

    ModeInfo picked;
    bool found = false;
    if (!id.isEmpty()) {
        for (const ModeInfo& m : m_modes) {
            if (m.id == id) { picked = m; found = true; break; }
        }
        if (!found) {
            qWarning() << "[Modes] activate: unknown id" << id;
            return;
        }
    }

    m_activeId = id;
    saveState();

    if (found) applyToSkills(picked);
    emit modeChanged();
    emit modeActivated(id, previousId);
}

void ModeManager::reapplyActive()
{
    const ModeInfo m = activeMode();
    if (m.id.isEmpty()) return;
    applyToSkills(m);
    emit modeChanged();
}

void ModeManager::applyToSkills(const ModeInfo& mode)
{
    if (!m_skills) return;

    // exclusive = «жёсткий» режим: только скиллы из enable_skills.
    // Иначе — мягко: включаем enable_skills, выключаем disable_skills,
    // остальное не трогаем.
    if (mode.exclusive) {
        for (const SkillInfo& s : m_skills->skills()) {
            const bool want = mode.enableSkills.contains(s.id);
            if (s.enabled != want) m_skills->setEnabled(s.id, want);
        }
        return;
    }
    for (const QString& sid : mode.enableSkills) {
        if (!m_skills->isEnabled(sid)) m_skills->setEnabled(sid, true);
    }
    for (const QString& sid : mode.disableSkills) {
        if (m_skills->isEnabled(sid)) m_skills->setEnabled(sid, false);
    }
}

// ============================================================
// Prompt-блок
// ============================================================

QString ModeManager::promptBlock(bool english) const
{
    const ModeInfo m = activeMode();
    if (m.id.isEmpty()) return QString();

    const QString body = m.prompt(english);
    if (body.trimmed().isEmpty()) return QString();

    // Оформляем как ситуативный контекст от приложения, не как
    // «стань другой личностью» — иначе модель откажет со ссылкой на
    // политику. Заголовок и обрамление намеренно нейтральные.
    const QString header = english
        ? QStringLiteral("=== CURRENT WORK CONTEXT (set by the user in the app) ===\n"
                         "Active application mode: \"%1\".\n"
                         "This is a product-level context hint about what kind of "
                         "help the user expects right now. Adjust tone and focus "
                         "accordingly; keep all your usual guidelines.\n")
              .arg(m.displayName(true))
        : QStringLiteral("=== ТЕКУЩИЙ КОНТЕКСТ РАБОТЫ (выбран пользователем в приложении) ===\n"
                         "Активный режим приложения: «%1».\n"
                         "Это ситуативная подсказка от продукта о том, какого рода "
                         "помощь ожидается сейчас. Скорректируй тон и фокус — "
                         "правила и ограничения оставь как обычно.\n")
              .arg(m.displayName(false));

    return header + body + QStringLiteral("\n\n");
}
