// -------------------------------------------------------
// skill_manager.cpp — Модульные «скиллы» J.A.R.V.I.S.
// -------------------------------------------------------

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

QString SkillManager::userSkillsDir()
{
    return JarvisPaths::subPath(QStringLiteral("skills"));
}

static QString bundledSkillsDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/skills");
}

QString SkillManager::stateFilePath() const
{
    return userSkillsDir() + QStringLiteral("/skills_state.json");
}

// ============================================================
// Конструктор
// ============================================================

SkillManager::SkillManager(QObject* parent)
    : QObject(parent)
{
    scan();
}

// ============================================================
// Манифест
// ============================================================

static QString localizedField(const QJsonValue& v, const QString& lang)
{
    // Поддерживаем и объект {"ru": "...", "en": "..."} и простую строку
    if (v.isObject()) return v.toObject().value(lang).toString();
    if (lang == QStringLiteral("en")) return v.toString(); // строка = оба языка
    return v.toString();
}

bool SkillManager::readManifest(const QString& skillDir, SkillInfo& out)
{
    QFile f(skillDir + QStringLiteral("/skill.json"));
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[Skills] Bad manifest in" << skillDir << ":" << err.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    out.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (out.id.isEmpty())
        out.id = QFileInfo(skillDir).fileName(); // fallback: имя папки

    out.nameRu        = localizedField(obj.value(QStringLiteral("name")), QStringLiteral("ru"));
    out.nameEn        = localizedField(obj.value(QStringLiteral("name")), QStringLiteral("en"));
    out.descriptionRu = localizedField(obj.value(QStringLiteral("description")), QStringLiteral("ru"));
    out.descriptionEn = localizedField(obj.value(QStringLiteral("description")), QStringLiteral("en"));
    out.version       = obj.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
    out.defaultEnabled = obj.value(QStringLiteral("default_enabled")).toBool(false);

    out.features.clear();
    for (const auto& v : obj.value(QStringLiteral("features")).toArray())
        out.features << v.toString();

    // Иерархия для UI. Форматы:
    //   "category": "development"                          — только верхний уровень
    //   "category": "development/unreal"                   — короткая запись через '/'
    //   "category": { "id": "development",
    //                 "label": {"ru":"...","en":"..."} }   — с локализованной подписью
    //   "subcategory": "unreal"  |  { "id": "unreal", "label": {...} }
    auto parseHier = [&obj](const QString& key,
                            QString& id,
                            QString& labelRu, QString& labelEn) {
        const QJsonValue v = obj.value(key);
        if (v.isString()) {
            id = v.toString().trimmed();
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            id = o.value(QStringLiteral("id")).toString().trimmed();
            const QJsonValue lbl = o.value(QStringLiteral("label"));
            labelRu = localizedField(lbl, QStringLiteral("ru"));
            labelEn = localizedField(lbl, QStringLiteral("en"));
        }
    };
    parseHier(QStringLiteral("category"),
              out.category, out.categoryLabelRu, out.categoryLabelEn);
    parseHier(QStringLiteral("subcategory"),
              out.subcategory, out.subcategoryLabelRu, out.subcategoryLabelEn);

    // Разрешаем короткую запись "development/unreal"
    if (out.subcategory.isEmpty() && out.category.contains(QLatin1Char('/'))) {
        const int slash = out.category.indexOf(QLatin1Char('/'));
        out.subcategory = out.category.mid(slash + 1);
        out.category    = out.category.left(slash);
    }

    out.dirPath = skillDir;

    // Знания: prompt.md (имя файла можно переопределить в манифесте)
    const QString promptFile = obj.value(QStringLiteral("prompt_file"))
                                   .toString(QStringLiteral("prompt.md"));
    QFile pf(skillDir + QStringLiteral("/") + promptFile);
    if (pf.open(QIODevice::ReadOnly)) {
        QString text = QString::fromUtf8(pf.readAll()).trimmed();
        if (text.size() > MAX_PROMPT_CHARS) {
            text = text.left(MAX_PROMPT_CHARS)
                 + QStringLiteral("\n...(skill knowledge truncated)");
        }
        out.promptText = text;
    }

    return true;
}

// ============================================================
// Состояние (enabled/installed_once)
// ============================================================

void SkillManager::loadState()
{
    m_enabledState.clear();
    m_installedOnce.clear();

    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject enabled = root.value(QStringLiteral("enabled")).toObject();
    for (auto it = enabled.begin(); it != enabled.end(); ++it)
        m_enabledState.insert(it.key(), it.value().toBool());

    for (const auto& v : root.value(QStringLiteral("installed_once")).toArray())
        m_installedOnce << v.toString();
}

void SkillManager::saveState() const
{
    QJsonObject enabled;
    for (auto it = m_enabledState.begin(); it != m_enabledState.end(); ++it)
        enabled.insert(it.key(), it.value());

    QJsonObject root;
    root.insert(QStringLiteral("enabled"), enabled);
    root.insert(QStringLiteral("installed_once"),
                QJsonArray::fromStringList(m_installedOnce));

    QFile f(stateFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

// ============================================================
// Сканирование
// ============================================================

static QStringList listSkillDirs(const QString& baseDir)
{
    QStringList result;
    const QDir dir(baseDir);
    if (!dir.exists()) return result;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(fi.absoluteFilePath() + QStringLiteral("/skill.json")))
            result << fi.absoluteFilePath();
    }
    return result;
}

bool SkillManager::copySkillDir(const QString& src, const QString& dst)
{
    QDir().mkpath(dst);
    const QDir srcDir(src);
    bool ok = true;
    for (const QFileInfo& fi : srcDir.entryInfoList(
             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = dst + QStringLiteral("/") + fi.fileName();
        if (fi.isDir()) {
            ok = copySkillDir(fi.absoluteFilePath(), target) && ok;
        } else {
            QFile::remove(target); // перезапись при повторном импорте
            ok = QFile::copy(fi.absoluteFilePath(), target) && ok;
        }
    }
    return ok;
}

int SkillManager::compareVersions(const QString& a, const QString& b)
{
    const QStringList pa = a.split(QChar('.'));
    const QStringList pb = b.split(QChar('.'));
    const int count = qMax(pa.size(), pb.size());

    for (int i = 0; i < count; ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

void SkillManager::scan()
{
    loadState();

    const QString userDir = userSkillsDir(); // создаётся автоматически

    // 1) Скиллы из папки exe, ещё не виденные пользователем, копируем
    //    к нему — там их можно читать, править и удалять. Копирование
    //    одноразовое (installed_once), чтобы удалённый пользователем
    //    скилл не возвращался после каждого запуска.
    bool stateDirty = false;
    for (const QString& srcDir : listSkillDirs(bundledSkillsDir())) {
        SkillInfo info;
        if (!readManifest(srcDir, info)) continue;
        if (m_installedOnce.contains(info.id)) continue;

        const QString dst = userDir + QStringLiteral("/") + info.id;
        if (!QFileInfo::exists(dst + QStringLiteral("/skill.json"))) {
            if (copySkillDir(srcDir, dst))
                qDebug() << "[Skills] Installed bundled skill:" << info.id;
        }
        m_installedOnce << info.id;
        stateDirty = true;
    }

    // 1b) Обновление уже установленных скиллов. Без этого знания скилла
    //     навсегда застывали в той версии, что была при первом запуске:
    //     новая сборка несёт новый prompt.md, а пользователь продолжает
    //     жить со старым. Обновляем только при выросшей версии в
    //     манифесте — и с бэкапом, если пользователь правил файл сам.
    for (const QString& srcDir : listSkillDirs(bundledSkillsDir())) {
        SkillInfo bundled;
        if (!readManifest(srcDir, bundled)) continue;

        const QString dst = userDir + QStringLiteral("/") + bundled.id;
        SkillInfo installed;
        if (!QFileInfo::exists(dst + QStringLiteral("/skill.json"))) continue;
        if (!readManifest(dst, installed)) continue;
        if (compareVersions(installed.version, bundled.version) >= 0) continue;

        // Правки пользователя не выбрасываем молча — кладём рядом .bak.
        const QString userPrompt = dst + QStringLiteral("/prompt.md");
        if (QFileInfo::exists(userPrompt)
            && installed.promptText != bundled.promptText) {
            const QString backup = userPrompt + QStringLiteral(".bak");
            QFile::remove(backup);
            QFile::copy(userPrompt, backup);
        }

        if (copySkillDir(srcDir, dst)) {
            qDebug() << "[Skills] Updated skill" << bundled.id
                     << installed.version << "->" << bundled.version;
        }
    }

    // 2) Загружаем все скиллы пользователя
    m_skills.clear();
    for (const QString& dir : listSkillDirs(userDir)) {
        SkillInfo info;
        if (!readManifest(dir, info)) continue;

        // Дубликат id — берём первый найденный
        const bool dup = std::any_of(m_skills.begin(), m_skills.end(),
            [&info](const SkillInfo& s) { return s.id == info.id; });
        if (dup) continue;

        info.enabled = m_enabledState.value(info.id, info.defaultEnabled);
        m_skills.append(info);
    }

    std::sort(m_skills.begin(), m_skills.end(),
              [](const SkillInfo& a, const SkillInfo& b) { return a.id < b.id; });

    if (stateDirty) saveState();

    m_legacyMode = m_skills.isEmpty() && m_installedOnce.isEmpty();
    if (m_legacyMode)
        qDebug() << "[Skills] No skill packs found — legacy mode (coding enabled)";

    qDebug() << "[Skills] Loaded" << m_skills.size() << "skills from" << userDir;
    emit skillsChanged();
}

// ============================================================
// Включение / выключение / фичи
// ============================================================

bool SkillManager::isEnabled(const QString& id) const
{
    for (const SkillInfo& s : m_skills)
        if (s.id == id) return s.enabled;
    return false;
}

void SkillManager::setEnabled(const QString& id, bool enabled)
{
    for (SkillInfo& s : m_skills) {
        if (s.id != id) continue;
        if (s.enabled == enabled) return;
        s.enabled = enabled;
        m_enabledState.insert(id, enabled);
        saveState();
        emit skillsChanged();
        return;
    }
}

bool SkillManager::isFeatureEnabled(const QString& feature) const
{
    if (m_legacyMode && feature == featureCodeActions()) return true;
    for (const SkillInfo& s : m_skills)
        if (s.enabled && s.features.contains(feature)) return true;
    return false;
}

QString SkillManager::promptBlocks() const
{
    QString blocks;
    for (const SkillInfo& s : m_skills) {
        if (!s.enabled || s.promptText.isEmpty()) continue;
        const QString title = s.nameEn.isEmpty() ? s.id : s.nameEn;
        blocks += QStringLiteral("=== SKILL: ") + title.toUpper()
                + QStringLiteral(" ===\n") + s.promptText + QStringLiteral("\n\n");
    }
    return blocks;
}

// ============================================================
// Импорт
// ============================================================

QString SkillManager::importSkill(const QString& sourceDir, bool english)
{
    SkillInfo info;
    if (!readManifest(sourceDir, info)) {
        return english
            ? QStringLiteral("The folder does not contain a valid skill.json manifest.")
            : QStringLiteral("В папке нет корректного манифеста skill.json.");
    }

    const QString dst = userSkillsDir() + QStringLiteral("/") + info.id;
    if (!copySkillDir(sourceDir, dst)) {
        return english
            ? QStringLiteral("Failed to copy skill files.")
            : QStringLiteral("Не удалось скопировать файлы скилла.");
    }

    // Импортированный вручную скилл включаем сразу — пользователь
    // явно выразил намерение им пользоваться.
    m_enabledState.insert(info.id, true);
    if (!m_installedOnce.contains(info.id)) m_installedOnce << info.id;
    saveState();

    scan();
    return QString();
}
