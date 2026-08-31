// ============================================================
// welcome_controller.cpp — см. welcome_controller.h
// ============================================================

#include "welcome_controller.h"

#include "jarvis.h"
#include "claude_api.h"
#include "database_manager.h"
#include "lang.h"
#include "llm_cache_manager.h"
#include "memory_consolidation.h"
#include "pdf_distiller.h"
#include "project_indexer.h"
#include "self_journal.h"
#include "user_profile_extended.h"

#include <QCoreApplication>
#include <QDate>
#include <QTime>
#include <QTimer>
#include <QVariantMap>

WelcomeController::WelcomeController(Jarvis* jarvis, QObject* parent)
    : QObject(parent)
    , m_jarvis(jarvis)
{
    // Диск, база и индексатор о своих изменениях не сообщают:
    // driveStatusChanged эмитится из тика консолидации, а он ходит
    // раз в 15 минут — отключённый накопитель столько же и висел бы
    // на экране как подключённый. Поэтому опрос.
    //
    // Две секунды: экран приветствия человек читает секунды, а не
    // минуты, и вставленная флешка должна появиться, пока он ещё
    // смотрит. Стоимость тика — QStorageInfo и пара запросов к уже
    // открытой базе.
    m_poll = new QTimer(this);
    m_poll->setInterval(2000);
    connect(m_poll, &QTimer::timeout, this, &WelcomeController::refresh);

    refresh();
}

void WelcomeController::setActive(bool on)
{
    if (m_active == on) return;
    m_active = on;

    if (m_active) {
        // Обновляемся сразу, а не через две секунды: панель только что
        // показали, и первое, что видит человек, должно быть свежим.
        refresh();
        m_poll->start();
    } else {
        m_poll->stop();
    }

    emit activeChanged();
}

QString WelcomeController::version() const
{
    return QCoreApplication::applicationVersion();
}

namespace {

void addLine(QVariantList& out, const QString& text, const char* tone)
{
    QVariantMap m;
    m[QStringLiteral("text")] = text;
    m[QStringLiteral("tone")] = QLatin1String(tone);
    out.append(m);
}

} // namespace

void WelcomeController::refresh()
{
    // Собираем в локальные переменные и сравниваем с текущими: опрос
    // идёт раз в две секунды, и безусловный changed() перестраивал бы
    // Repeater панели на каждый тик — а меняется там почти никогда.
    QString      greeting;
    QString      today;
    QVariantList lines;
    QString      thought;
    int          unverified = 0;

    // ── Приветствие ───────────────────────────────────────
    const QString nickname = UserProfileExtended::instance().nickname();
    if (nickname.isEmpty()) {
        greeting = IS_EN
            ? QStringLiteral("System Initialized. Awaiting identity calibration…")
            : QStringLiteral("Система инициализирована. Ожидание калибровки идентичности…");
    } else {
        const int hour = QTime::currentTime().hour();
        QString timeGreet;
        if      (hour < 6)  timeGreet = IS_EN ? QStringLiteral("Still up, %1?") : QStringLiteral("Не спишь, %1?");
        else if (hour < 12) timeGreet = IS_EN ? QStringLiteral("Morning, %1.")  : QStringLiteral("Доброе утро, %1.");
        else if (hour < 18) timeGreet = IS_EN ? QStringLiteral("Afternoon, %1."): QStringLiteral("Добрый день, %1.");
        else                timeGreet = IS_EN ? QStringLiteral("Evening, %1.")  : QStringLiteral("Добрый вечер, %1.");
        greeting = timeGreet.arg(nickname);
    }

    today = QDate::currentDate().toString(QStringLiteral("dd MMM yyyy"));

    // ── Строки статуса ────────────────────────────────────

    // Три разных состояния, а не два. Внешний накопитель не настроен
    // вовсе — это норма, а не тревога: писать «автономный режим»
    // жёлтым каждому, у кого его нет, значит приучить не смотреть на
    // жёлтое. Настроен, но не подключён — вот это стоит показать.
    const auto& mc = MemoryConsolidation::instance();
    const bool driveConfigured = !mc.externalRoot().isEmpty();
    const auto drive = mc.checkDriveStatus();

    if (drive.connected) {
        addLine(lines, IS_EN
            ? QStringLiteral("External Core [%1 GB] — %2 GB free")
                  .arg(QString::number(drive.totalGb(), 'f', 0),
                       QString::number(drive.freeGb(), 'f', 1))
            : QStringLiteral("Внешний накопитель [%1 ГБ] — свободно %2 ГБ")
                  .arg(QString::number(drive.totalGb(), 'f', 0),
                       QString::number(drive.freeGb(), 'f', 1)),
            "ok");
    } else if (driveConfigured) {
        addLine(lines, IS_EN
            ? QStringLiteral("External core not connected — running from local cache")
            : QStringLiteral("Внешний накопитель не подключён — работаю из локального кэша"),
            "warn");
    } else {
        addLine(lines, IS_EN ? QStringLiteral("Local storage (no external core configured)")
                             : QStringLiteral("Локальное хранилище (внешний накопитель не настроен)"),
                "ok");
    }

    const bool claudeOk = m_jarvis && m_jarvis->claudeApi()
                       && m_jarvis->claudeApi()->hasApiKey();
    addLine(lines, claudeOk ? (IS_EN ? QStringLiteral("Claude API online")
                              : QStringLiteral("Claude API на связи"))
                     : (IS_EN ? QStringLiteral("Claude API — no key")
                              : QStringLiteral("Claude API — нет ключа")),
            claudeOk ? "ok" : "error");

    const bool dbOk = DatabaseManager::instance().isOpen();
    addLine(lines, dbOk ? (IS_EN ? QStringLiteral("Database online")
                          : QStringLiteral("База данных на связи"))
                 : (IS_EN ? QStringLiteral("Database OFFLINE")
                          : QStringLiteral("База данных НЕДОСТУПНА")),
            dbOk ? "ok" : "error");

    const auto indep = LlmCacheManager::instance()
        .independenceStats(LlmCacheManager::kDesktopOwnerId, 7);
    if (indep.total > 0) {
        addLine(lines, IS_EN
            ? QStringLiteral("Independence (7d): %1% (%2/%3 answered locally)")
                  .arg(QString::number(indep.pct(), 'f', 0))
                  .arg(indep.local).arg(indep.total)
            : QStringLiteral("Самостоятельность (7д): %1% (%2/%3 локально)")
                  .arg(QString::number(indep.pct(), 'f', 0))
                  .arg(indep.local).arg(indep.total),
            "ok");
    }

    if (m_jarvis && m_jarvis->projectIndexer()
        && m_jarvis->projectIndexer()->fileCount() > 0) {
        addLine(lines, IS_EN
            ? QStringLiteral("Project: %1 (%2 files)")
                  .arg(m_jarvis->projectIndexer()->projectRoot().section(QChar('/'), -1))
                  .arg(m_jarvis->projectIndexer()->fileCount())
            : QStringLiteral("Проект: %1 (%2 файлов)")
                  .arg(m_jarvis->projectIndexer()->projectRoot().section(QChar('/'), -1))
                  .arg(m_jarvis->projectIndexer()->fileCount()),
            "ok");
    }

    // ── Текущая мысль ─────────────────────────────────────
    const auto doubts    = SelfJournal::instance().topDoubtsForVerification(1);
    const int doubtCount = SelfJournal::instance().unresolvedDoubtCount();
    const int pdfChunks  = PdfDistiller::instance().totalChunks();
    const int pdfDoubts  = PdfDistiller::instance().doubtCount();

    if (!doubts.isEmpty()) {
        const auto& d = doubts.first();
        // Обрезка живёт здесь, а не в разметке: длина строки — свойство
        // данных, и QML не должен знать, что 80 знаков это потолок.
        thought = (IS_EN ? QStringLiteral("Reflecting on: «%1» (confidence %2)")
                           : QStringLiteral("Обдумываю: «%1» (уверенность %2)"))
                        .arg(d.content.left(80),
                             QString::number(d.confidence, 'f', 2));
    } else if (pdfChunks > 0) {
        thought = IS_EN
            ? QStringLiteral("Knowledge base: %1 chunks distilled").arg(pdfChunks)
            : QStringLiteral("База знаний: %1 фрагментов извлечено").arg(pdfChunks);
    } else {
        thought = IS_EN
            ? QStringLiteral("Idle — awaiting new data to learn from")
            : QStringLiteral("Ожидание — готов к обучению");
    }

    unverified = doubtCount + pdfDoubts;

    if (greeting == m_greeting && today == m_today && lines == m_statusLines
        && thought == m_thought && unverified == m_unverified)
        return;

    m_greeting    = greeting;
    m_today       = today;
    m_statusLines = lines;
    m_thought     = thought;
    m_unverified  = unverified;

    emit changed();
}
