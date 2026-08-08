#pragma once
// -------------------------------------------------------
// mode_manager.h — Режимы работы J.A.R.V.I.S.
//
// Режим — это профиль поведения приложения (как «Solution
// Configuration» в Visual Studio): выбранный режим меняет
// (1) набор включённых скиллов и (2) контекстный блок в
// system prompt, который подсказывает JARVIS'у в каком
// «настроении» помогать.
//
// modes/<id>/mode.json:
// {
//   "id": "gamedev_unreal",
//   "name":        { "ru": "Разработка игры (Unreal)", "en": "Game Dev (Unreal)" },
//   "description": { "ru": "...", "en": "..." },
//   "prompt":      { "ru": "...", "en": "..." },
//   "icon": "🎮",
//   "accent": "#ff6b6b",
//   "enable_skills":  ["coding", "unreal_cpp", "unreal_blueprints"],
//   "disable_skills": ["philosopher"],
//   "exclusive": false,    // true = выключить ВСЕ скиллы кроме enable_skills
//   "default_active": false
// }
//
// Промпт-блок сознательно оформляется как «контекст от
// приложения», а не как переопределение личности модели —
// иначе Claude отклонит запрос со ссылкой на политику
// (см. комментарий в session_memory.cpp вокруг buildSystemPrompt).
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "jarvis_core_export.h"

class SkillManager;

struct ModeInfo
{
    QString     id;
    QString     nameRu;
    QString     nameEn;
    QString     descriptionRu;
    QString     descriptionEn;
    QString     promptRu;         // контекстный блок для system prompt (RU)
    QString     promptEn;         // контекстный блок для system prompt (EN)
    QString     icon;             // emoji для UI, опционально
    QString     accentColor;      // #RRGGBB для UI-карточки, опционально
    QStringList enableSkills;     // id скиллов, которые режим включает
    QStringList disableSkills;    // id скиллов, которые режим выключает
    bool        exclusive     = false; // выключить всё, кроме enableSkills
    bool        defaultActive = false; // выбирать по умолчанию при первом запуске
    QString     dirPath;          // откуда загружен

    QString displayName(bool english) const
    {
        const QString primary = english ? nameEn : nameRu;
        if (!primary.isEmpty()) return primary;
        return english ? nameRu : nameEn;
    }
    QString description(bool english) const
    {
        const QString primary = english ? descriptionEn : descriptionRu;
        if (!primary.isEmpty()) return primary;
        return english ? descriptionRu : descriptionEn;
    }
    QString prompt(bool english) const
    {
        const QString primary = english ? promptEn : promptRu;
        if (!primary.isEmpty()) return primary;
        return english ? promptRu : promptEn;
    }
};

class JARVIS_CORE_EXPORT ModeManager : public QObject
{
    Q_OBJECT
public:
    // Ссылка на SkillManager нужна, чтобы применять enable/disable списки.
    explicit ModeManager(SkillManager* skills, QObject* parent = nullptr);

    void scan();

    const QVector<ModeInfo>& modes() const { return m_modes; }

    QString  activeId() const { return m_activeId; }
    ModeInfo activeMode() const;   // пустой ModeInfo если ничего не активно

    // Активирует режим и применяет его к SkillManager (enable/disable/exclusive).
    // Пустой id = "снять режим" (никакой промпт-блок не добавляется, скиллы
    // остаются в том же состоянии как сейчас — режим не «затирает» ручные
    // правки после снятия).
    void activate(const QString& id);

    // Принудительно применить текущий активный режим к SkillManager.
    // Используется на старте: scan() восстанавливает activeId из файла,
    // но не трогает скиллы (чтобы конструктор ModeManager не имел побочных
    // эффектов до подключения обработчиков). Jarvis вызывает это, когда
    // всё подключено.
    void reapplyActive();

    // Готовый блок для SessionMemory — либо пустая строка, либо
    // "=== CURRENT WORK CONTEXT ===\n<mode.prompt>\n\n".
    // english — язык, на котором сейчас общается пользователь.
    QString promptBlock(bool english) const;

    static QString userModesDir();

signals:
    void modeChanged();   // выбран другой режим или сменился набор режимов

private:
    static bool readManifest(const QString& modeDir, ModeInfo& out);
    static bool copyModeDir(const QString& src, const QString& dst);

    void loadState();
    void saveState() const;
    QString stateFilePath() const;

    // Применить enable/disable/exclusive текущего режима к SkillManager.
    void applyToSkills(const ModeInfo& mode);

    SkillManager*     m_skills = nullptr;
    QVector<ModeInfo> m_modes;
    QString           m_activeId;
    QStringList       m_installedOnce;   // для одноразового копирования из bundled

    static constexpr int MAX_PROMPT_CHARS = 6000;
};
