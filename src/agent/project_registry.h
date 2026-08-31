#pragma once
// -------------------------------------------------------
// project_registry.h — Проекты как объекты, а не как пути
//
// ProjectIndexer и ProjectProfile знают ВСЁ про один проект — тот,
// корень которого им задали. Чего не было: списка проектов вообще.
// Поэтому «запусти мой ESP32 проект» упиралось в догадку модели о
// пути, а «собери» — в догадку о команде сборки.
//
//   JARVIS        C:/.../jarvis          Qt/CMake C++   rider
//   ESP32 Car     C:/.../esp32_car       PlatformIO     code
//   Rally         C:/.../RallySim        Unreal 5       rider
//
// Реестр хранит по проекту то, что нельзя вывести из содержимого
// папки: чем его открывать, чем собирать, чем запускать. Всё
// остальное (язык, таргеты, ассеты) по-прежнему считает индексатор
// — дублировать это здесь незачем.
//
// Открытие проекта — не просто запись в файл: активный проект
// задаёт корень индексатора, а значит и «этот проект» для git,
// контекста и советника. Реестр про эти подсистемы не знает и
// зовёт лямбду, которую ему дали снаружи.
// -------------------------------------------------------

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class ToolRegistry;
class PermissionGate;

// ============================================================
//  Проект
// ============================================================
struct ProjectEntry
{
    QString     name;          // "ESP32 Car" — ключ, регистр не важен
    QString     path;          // корень
    QString     kind;          // "PlatformIO / ESP32", "Qt/CMake C++", ...
    QString     ide;           // алиас для AppLauncher: rider, clion, code
    QString     buildCommand;  // как собрать — команда для cmd.exe
    QString     runCommand;    // как запустить
    QString     docs;          // ссылка или путь к документации
    QStringList tags;

    QDateTime   lastOpened;
    int         openCount = 0;

    bool isValid() const { return !name.isEmpty() && !path.isEmpty(); }
    bool exists() const;
    QString human() const;

    static ProjectEntry fromJson(const QJsonObject& obj);
    QJsonObject         toJson() const;
};

// ============================================================
//  ProjectRegistry
// ============================================================
class ProjectRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ProjectRegistry(QObject* parent = nullptr);

    // Что делать при открытии проекта: задать корень индексатора,
    // пересобрать профиль, сказать об этом в ленту. Всё это живёт в
    // engine и intelligence, куда слою действий ходу нет.
    using Activator = std::function<void(const ProjectEntry&)>;
    void setActivator(Activator activator) { m_activator = std::move(activator); }

    void    load();
    bool    save() const;
    QString storagePath() const;

    QVector<ProjectEntry> all() const { return m_projects; }
    const ProjectEntry*   find(const QString& name) const;      // по имени, регистр не важен
    const ProjectEntry*   forPath(const QString& path) const;   // какому проекту принадлежит путь
    QStringList           names() const;
    int                   count() const { return m_projects.size(); }

    bool addOrReplace(const ProjectEntry& project);
    bool remove(const QString& name);

    // Открыть: пометить активным, позвать активатор, при желании
    // запустить IDE. Возвращает готовый отчёт.
    QString open(const QString& name, bool launchIde, bool* okOut = nullptr);

    QString activeName() const { return m_active; }
    const ProjectEntry* active() const { return find(m_active); }

    // Сборка и запуск — это команда, записанная человеком один раз.
    // Реестр её только исполняет и не пытается угадать за него.
    QString runCommandFor(const QString& name, bool build, bool* okOut = nullptr);

    // По содержимому папки: "PlatformIO / ESP32", "Unreal Engine 5", ...
    // Вместе с типом угадываются команды сборки и запуска — их всегда
    // можно переписать, но пустое поле никто заполнять не станет.
    static ProjectEntry sniff(const QString& path);

    QString summaryForModel() const;

signals:
    void listChanged();
    void projectOpened(const QString& name, const QString& path);

private:
    QVector<ProjectEntry> m_projects;
    QString               m_active;
    Activator             m_activator;
};

namespace JarvisTools {

// list_projects / add_project / open_project / build_project / remove_project
void registerProjectTools(ToolRegistry& registry, ProjectRegistry* projects);

} // namespace JarvisTools
