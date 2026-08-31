#pragma once
// -------------------------------------------------------
// dev_advisor.h — Фоновый советник по проекту
//
// Пока пользователь работает, JARVIS периодически перечитывает индекс и
// профиль проекта и ищет то, что человек обычно замечает поздно:
//   • исходник есть на диске, но не добавлен в сборку;
//   • #include ссылается на файл, которого нет;
//   • .qrc перечисляет ресурс, которого нет на диске;
//   • ассет лежит в проекте, но нигде не используется;
//   • файл разросся до нескольких тысяч строк;
//   • накопились TODO/FIXME;
//   • в свежих файлах — хвостовые пробелы и отсутствие перевода строки.
//
// Мелкие безопасные правки (форматирование, добавление файла в
// CMakeLists) применяются САМИ — с бэкапом в Jarvis Data/advisor_backups
// и записью в журнал. Всё, что крупнее, только предлагается: удалять
// «неиспользуемый» ассет или дробить файл за пользователя нельзя.
//
// Локальные эвристики бесплатны и работают офлайн. Раз в несколько часов
// поверх них может пройти разбор через Claude — но только если есть
// свежие изменения, ключ API на месте и пользователь не ждёт ответа.
// -------------------------------------------------------

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QDateTime>
#include <QJsonObject>

#include "jarvis_core_export.h"

class ProjectIndexer;
class ProjectProfile;
class ClaudeApi;
class QTimer;

struct DevFinding
{
    enum Severity { Info, Warning, Important };

    // Стабильный идентификатор (kind|file|key): при следующем проходе та
    // же проблема опознаётся как та же самая, а не показывается заново.
    QString   id;
    QString   kind;        // "not-in-build", "broken-include", "missing-resource", ...
    Severity  severity = Info;
    QString   title;       // одна строка для UI
    QString   detail;      // что именно и что предлагается сделать
    QString   file;        // относительный путь
    int       line = 0;
    bool      autoFixed = false;   // правка уже применена автоматически
    QString   fixNote;             // что сделано и где лежит бэкап
    bool      dismissed = false;   // пользователь отмахнулся
    QDateTime foundAt;

    QJsonObject toJson() const;
    static DevFinding fromJson(const QJsonObject& obj);
    QString severityIcon() const;
};

class JARVIS_CORE_EXPORT DevAdvisor : public QObject
{
    Q_OBJECT

public:
    explicit DevAdvisor(QObject* parent = nullptr);

    void setIndexer(ProjectIndexer* indexer) { m_indexer = indexer; }
    void setProfile(ProjectProfile* profile) { m_profile = profile; }
    void setClaudeApi(ClaudeApi* api)        { m_claude  = api; }
    void setUiEnglish(bool english)          { m_english = english; }

    // Смена проекта: подхватываем сохранённые находки этого проекта.
    void setProjectRoot(const QString& root);
    QString projectRoot() const { return m_root; }

    void start(int intervalMinutes = 20);
    void stop();
    bool isRunning() const;

    // Ручной прогон (команда в чате) — работает даже если советник выключен.
    void runNow();

    bool isEnabled() const       { return m_enabled; }
    void setEnabled(bool on);
    bool autoFixEnabled() const  { return m_autoFix; }
    void setAutoFixEnabled(bool on);
    bool deepAnalysisEnabled() const { return m_deepAnalysis; }
    void setDeepAnalysisEnabled(bool on);

    const QVector<DevFinding>& findings() const { return m_findings; }
    QVector<DevFinding> openFindings(int maxItems = 50) const;

    // Готовый текст для чата.
    QString report(int maxItems = 12) const;

    void dismiss(const QString& id);
    void dismissAll();

signals:
    // newCount — сколько находок появилось впервые за этот проход.
    void findingsUpdated(int newCount);
    // Мелкая правка применена сама — строка для лога и уведомления.
    void autoFixApplied(const QString& message);
    // Самое важное из найденного — для панели предложений.
    void adviceReady(const QString& description, const QString& action);

private slots:
    void onTimerFired();

private:
    void runScan(bool manual);

    // --- Эвристики ---
    void checkFilesNotInBuild();
    void checkBrokenIncludes();
    void checkResources();
    void checkUnusedAssets();
    void checkHugeFiles();
    void checkTodos();
    void checkFormatting();

    // --- Автоправки ---
    bool fixWhitespace(const QString& absPath, QString& note);
    bool addFilesToCMake(const QString& cmakeAbsPath,
                         const QStringList& fileNames,
                         QString& note);
    // Бэкапы советник не делает сам: правки идут через EditJournal —
    // тогда «отмени правки» откатывает и то, что применил советник, а не
    // только то, что написала модель.
    void    logFix(const QString& message) const;

    // --- Разбор через LLM ---
    void maybeRunDeepAnalysis(bool manual);
    void applyDeepAnalysis(const QString& response);

    // --- Служебное ---
    void addFinding(DevFinding finding);
    void mergeFresh();
    QString cmakeForFile(const QString& absPath) const;
    QString cmakeContent(const QString& absPath);
    QString absPathFor(const QString& relPath) const;
    QString findingsFilePath() const;
    void save() const;
    void load();
    // Не tr(): имя перекрыло бы QObject::tr и сбивало бы с толку.
    QString txt(const char* ru, const char* en) const;

    ProjectIndexer* m_indexer = nullptr;
    ProjectProfile* m_profile = nullptr;
    ClaudeApi*      m_claude  = nullptr;
    QTimer*         m_timer   = nullptr;

    QString m_root;
    bool    m_english      = false;
    bool    m_enabled      = true;
    bool    m_autoFix      = true;
    bool    m_deepAnalysis = true;
    bool    m_scanning     = false;

    QVector<DevFinding> m_findings;   // текущее состояние (сохраняется на диск)
    QVector<DevFinding> m_fresh;      // накопитель текущего прохода
    QHash<QString, QString> m_cmakeCache;  // путь -> содержимое (на один проход)

    QDateTime m_lastDeepRun;

    // Потолки: советник не должен ни тормозить приложение, ни заваливать
    // пользователя простынёй из сорока замечаний.
    static constexpr int MAX_FINDINGS_PER_KIND = 5;
    static constexpr int MAX_FORMAT_FIXES      = 8;
    static constexpr int HUGE_FILE_LINES       = 1800;
    static constexpr int DEEP_ANALYSIS_HOURS   = 6;
};
