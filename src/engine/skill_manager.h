#pragma once
// -------------------------------------------------------
// skill_manager.h — Модульные «скиллы» J.A.R.V.I.S.
//
// Скилл — это папка со знаниями (как лего-блок):
//   skills/<id>/skill.json  — манифест (имя, описание, фичи)
//   skills/<id>/prompt.md   — знания, попадающие в system prompt
//
// В отличие от DLL-плагинов (plugin_manager.h) скиллу не нужна
// компиляция: любой пользователь может написать свой скилл в
// блокноте, положить папку в "Jarvis Data/skills" — и JARVIS
// станет экспертом в новой области. Скиллы можно включать/
// выключать на лету и докачивать после установки.
//
// Два источника скиллов:
//   1) Рядом с exe:      <app>/skills/<id>/    (поставляются с
//      установщиком; опциональные компоненты — можно не ставить)
//   2) Папка пользователя: Documents/Jarvis Data/skills/<id>/
//      (сюда же при первом запуске копируются скиллы из (1),
//      чтобы пользователь мог их читать/править/удалять)
//
// Манифест skill.json:
// {
//   "id": "electronics",
//   "name":        { "ru": "Электроника", "en": "Electronics" },
//   "description": { "ru": "...",         "en": "..." },
//   "version": "1.0.0",
//   "features": ["kicad"],       // гейты нативных возможностей ядра
//   "default_enabled": true
// }
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

#include "jarvis_core_export.h"

struct SkillInfo
{
    QString     id;             // папка/уникальный идентификатор
    QString     nameRu;
    QString     nameEn;
    QString     descriptionRu;
    QString     descriptionEn;
    QString     version;
    QStringList features;       // нативные фичи ядра, которые включает скилл
    bool        defaultEnabled = false;
    bool        enabled        = false;
    QString     dirPath;        // откуда загружен
    QString     promptText;     // содержимое prompt.md (уже обрезано по лимиту)

    QString displayName(bool english) const
    {
        const QString primary = english ? nameEn : nameRu;
        if (!primary.isEmpty()) return primary;
        return english ? nameRu : nameEn; // fallback на другой язык
    }
    QString description(bool english) const
    {
        const QString primary = english ? descriptionEn : descriptionRu;
        if (!primary.isEmpty()) return primary;
        return english ? descriptionRu : descriptionEn;
    }
};

class JARVIS_CORE_EXPORT SkillManager : public QObject
{
    Q_OBJECT

public:
    // Известные нативные фичи ядра, которые скиллы могут включать.
    // "code_actions" — парсинг [FILE:]/[DIFF:]/[CMD:] блоков и работа
    // IDE-агента. Без него JARVIS остаётся обычным ассистентом.
    static QString featureCodeActions() { return QStringLiteral("code_actions"); }

    explicit SkillManager(QObject* parent = nullptr);

    // Пересканировать обе папки скиллов. Скиллы из папки exe, которых
    // ещё не было у пользователя, копируются в пользовательскую папку
    // (один раз — удаление пользователем уважается и не «воскрешает» их).
    void scan();

    const QVector<SkillInfo>& skills() const { return m_skills; }

    bool isEnabled(const QString& id) const;
    void setEnabled(const QString& id, bool enabled);

    // true, если хотя бы один ВКЛЮЧЁННЫЙ скилл декларирует фичу
    bool isFeatureEnabled(const QString& feature) const;

    // Конкатенация prompt.md всех включённых скиллов — блоками
    // "=== SKILL: NAME ===" для system prompt.
    QString promptBlocks() const;

    // Импорт скилла из внешней папки (копирование в пользовательскую).
    // Возвращает пустую строку при успехе, иначе текст ошибки.
    QString importSkill(const QString& sourceDir, bool english);

    // Папка пользовательских скиллов (для кнопки "Открыть папку")
    static QString userSkillsDir();

signals:
    // Состав или состояние скиллов изменилось — ядро должно
    // перекачать промпт-блоки в SessionMemory.
    void skillsChanged();

private:
    static bool readManifest(const QString& skillDir, SkillInfo& out);
    static bool copySkillDir(const QString& src, const QString& dst);

    void loadState();
    void saveState() const;
    QString stateFilePath() const;

    QVector<SkillInfo> m_skills;

    // Legacy-режим: скиллов нет вообще и система скиллов ни разу не
    // инициализировалась (пустой installed_once). Так бывает, когда
    // старая установка обновилась одним exe без папки skills/. Чтобы
    // обновление молча не отняло у пользователя IDE-агента, в этом
    // режиме code_actions считается включённым, а SessionMemory
    // использует встроенный fallback-блок кодинга.
    bool m_legacyMode = false;

    // Персистентное состояние (skills_state.json):
    QHash<QString, bool> m_enabledState;   // id -> включён (явный выбор)
    QStringList          m_installedOnce;  // id, уже скопированные из exe-папки

    // Ограничение на размер знаний одного скилла в system prompt —
    // защита бюджета токенов от гигантских prompt.md.
    static constexpr int MAX_PROMPT_CHARS = 12000;
};
