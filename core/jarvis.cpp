// -------------------------------------------------------
// jarvis.cpp — Ядро J.A.R.V.I.S.: команды, TTS, плагины
// -------------------------------------------------------

#include "jarvis.h"
#include "virtual_keyboard.h"
#include "session_memory.h"
#include "claude_api.h"
#include "action_predictor.h"
#include "plugin_manager.h"
#include "updater.h"

#include <sapi.h>
#include <shellapi.h>
#include <lmcons.h>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QThread>
#include <QMutexLocker>
#include <QMap>
#include <QRegularExpression>
#include <QCoreApplication>

// ============================================================
// Конструктор / Деструктор
// ============================================================

Jarvis::Jarvis(QObject* parent)
    : QObject(parent)
{
    m_memory      = new SessionMemory(this);
    m_claudeApi   = new ClaudeApi(m_memory, this);
    m_predictor   = new ActionPredictor(m_memory, this);
    m_keyEmulator = new KeyEmulator(this);
    m_pluginMgr   = new PluginManager(this, this);
    m_updater     = new Updater(this);

    // Настраиваем апдейтер — замени на свой репозиторий
    m_updater->setRepository(QStringLiteral("YOUR_GITHUB_USER"),
                             QStringLiteral("jarvis"));
    m_updater->setCurrentVersion(QCoreApplication::applicationVersion());

    registerCommands();

    // Загружаем плагины из папки рядом с exe
    QString pluginsDir = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/plugins");
    m_pluginMgr->loadPlugins(pluginsDir);

    // Реакция на ошибки API
    connect(m_claudeApi, &ClaudeApi::apiError, this, [this](const QString& err) {
        emit asyncResponseError(err);
    });

    // Реакция на плагины
    connect(m_pluginMgr, &PluginManager::pluginLoaded, this, [this](const QString& name) {
        emit logRequested(QStringLiteral("СИСТЕМА"),
                          QStringLiteral("Плагин загружен: ") + name,
                          QStringLiteral("#00ff88"));
    });
    connect(m_pluginMgr, &PluginManager::pluginError, this,
            [this](const QString& name, const QString& error) {
        emit logRequested(QStringLiteral("ОШИБКА"),
                          QStringLiteral("Плагин ") + name + QStringLiteral(": ") + error,
                          QStringLiteral("#ff4466"));
    });
}

Jarvis::~Jarvis()
{
    m_pluginMgr->unloadAll();
    m_predictor->savePatterns();
    m_memory->savePersistent();
}

// ============================================================
// Регистрация команд
// ============================================================

void Jarvis::registerCommands()
{
    // --- Управление API-ключом ---
    m_registry.registerCommand(
        {QStringLiteral("apikey "), QStringLiteral("ключ ")},
        [this](const QString& s) { return cmdSetApiKey(s); },
        QStringLiteral("apikey <ключ> — установить Claude API-ключ"),
        true
    );

    // --- Память ---
    m_registry.registerCommand(
        {QStringLiteral("запомни "), QStringLiteral("remember ")},
        [this](const QString& s) { return cmdRememberFact(s); },
        QStringLiteral("запомни <ключ>=<значение> — запомнить факт"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("вспомни "), QStringLiteral("recall ")},
        [this](const QString& s) { return cmdRecallFact(s); },
        QStringLiteral("вспомни <ключ> — вспомнить факт"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("память"), QStringLiteral("memory")},
        [this](const QString& s) { return cmdShowMemory(s); },
        QStringLiteral("память — показать сохранённые факты")
    );

    m_registry.registerCommand(
        {QStringLiteral("статистика"), QStringLiteral("stats")},
        [this](const QString& s) { return cmdShowStats(s); },
        QStringLiteral("статистика — статистика использования")
    );

    // --- Обновления и плагины ---
    m_registry.registerCommand(
        {QStringLiteral("обновление"), QStringLiteral("update")},
        [this](const QString& s) { return cmdCheckUpdate(s); },
        QStringLiteral("обновление — проверить обновления")
    );

    m_registry.registerCommand(
        {QStringLiteral("плагины"), QStringLiteral("plugins")},
        [this](const QString& s) { return cmdListPlugins(s); },
        QStringLiteral("плагины — список плагинов")
    );

    m_registry.registerCommand(
        {QStringLiteral("перезагрузи "), QStringLiteral("reload ")},
        [this](const QString& s) { return cmdReloadPlugin(s); },
        QStringLiteral("перезагрузи <плагин> — hot-reload плагина"),
        true
    );

    // --- Приветствие ---
    m_registry.registerCommand(
        {QStringLiteral("привет"), QStringLiteral("здравств"),
         QStringLiteral("hello"), QStringLiteral("hi")},
        [this](const QString& s) { return cmdGreeting(s); },
        QStringLiteral("привет — приветствие")
    );

    // --- Время / Дата ---
    m_registry.registerCommand(
        {QStringLiteral("время"), QStringLiteral("time"), QStringLiteral("час")},
        [this](const QString& s) { return cmdTime(s); },
        QStringLiteral("время — текущее время")
    );

    m_registry.registerCommand(
        {QStringLiteral("дата"), QStringLiteral("date"), QStringLiteral("день")},
        [this](const QString& s) { return cmdDate(s); },
        QStringLiteral("дата — текущая дата")
    );

    m_registry.registerCommand(
        {QStringLiteral("кто я"), QStringLiteral("имя"), QStringLiteral("username")},
        [this](const QString& s) { return cmdUserName(s); },
        QStringLiteral("кто я — имя пользователя")
    );

    // --- Приложения ---
    m_registry.registerCommand(
        {QStringLiteral("блокнот"), QStringLiteral("notepad")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("notepad.exe")); },
        QStringLiteral("блокнот — открыть Notepad")
    );
    m_registry.registerCommand(
        {QStringLiteral("калькулятор"), QStringLiteral("calc")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("calc.exe")); },
        QStringLiteral("калькулятор — открыть калькулятор")
    );
    m_registry.registerCommand(
        {QStringLiteral("проводник"), QStringLiteral("explorer")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("explorer.exe")); },
        QStringLiteral("проводник — открыть Explorer")
    );
    m_registry.registerCommand(
        {QStringLiteral("диспетчер"), QStringLiteral("taskmgr")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("taskmgr.exe")); },
        QStringLiteral("диспетчер — диспетчер задач")
    );
    m_registry.registerCommand(
        {QStringLiteral("настройки"), QStringLiteral("settings")},
        [this](const QString&) { return cmdLaunchApp(QStringLiteral("ms-settings:")); },
        QStringLiteral("настройки — параметры Windows")
    );
    m_registry.registerCommand(
        {QStringLiteral("браузер"), QStringLiteral("chrome"), QStringLiteral("browser")},
        [this](const QString& s) { return cmdBrowser(s); },
        QStringLiteral("браузер — открыть Google")
    );

    // --- Запуск произвольного приложения ---
    m_registry.registerCommand(
        {QStringLiteral("запусти "), QStringLiteral("открой "), QStringLiteral("launch ")},
        [this](const QString& s) {
            QString app = extractArg(s, {QStringLiteral("запусти "),
                                         QStringLiteral("открой "),
                                         QStringLiteral("launch ")});
            return cmdLaunchApp(app);
        },
        QStringLiteral("запусти <программа> — запуск приложения"),
        true
    );

    // --- Поиск ---
    m_registry.registerCommand(
        {QStringLiteral("найди "), QStringLiteral("search "), QStringLiteral("гугл")},
        [this](const QString& s) {
            QString q = extractArg(s, {QStringLiteral("найди "),
                                       QStringLiteral("search "),
                                       QStringLiteral("гугл ")});
            return cmdWebSearch(q);
        },
        QStringLiteral("найди <запрос> — поиск в Google"),
        true
    );

    // --- YouTube ---
    m_registry.registerCommand(
        {QStringLiteral("youtube"), QStringLiteral("ютуб")},
        [this](const QString& s) {
            QString q = extractArg(s, {QStringLiteral("youtube "),
                                       QStringLiteral("ютуб ")});
            return cmdYoutube(q);
        },
        QStringLiteral("youtube <запрос> — поиск на YouTube"),
        true
    );

    // --- Блокировка ---
    m_registry.registerCommand(
        {QStringLiteral("заблокируй"), QStringLiteral("lock")},
        [this](const QString& s) { return cmdLock(s); },
        QStringLiteral("заблокируй — блокировка экрана")
    );

    // --- Виртуальная клавиатура ---
    m_registry.registerCommand(
        {QStringLiteral("напечатай "), QStringLiteral("type ")},
        [this](const QString& s) { return cmdTypeText(s); },
        QStringLiteral("напечатай <текст> — напечатать в активном окне"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("нажми "), QStringLiteral("press ")},
        [this](const QString& s) { return cmdPressKey(s); },
        QStringLiteral("нажми <клавиша> — нажать клавишу"),
        true
    );

    m_registry.registerCommand(
        {QStringLiteral("комбо "), QStringLiteral("combo ")},
        [this](const QString& s) { return cmdCombo(s); },
        QStringLiteral("комбо <клавиши> — комбинация клавиш"),
        true
    );

    // --- Помощь (последним) ---
    m_registry.registerCommand(
        {QStringLiteral("помощь"), QStringLiteral("help"), QStringLiteral("команд")},
        [this](const QString&) { return cmdHelp(QString()); },
        QStringLiteral("помощь — список команд")
    );
}

// ============================================================
// Утилиты
// ============================================================

QString Jarvis::extractArg(const QString& input, const QStringList& prefixes)
{
    QString trimmed = input.trimmed();
    QString lower = trimmed.toLower();

    for (const auto& prefix : prefixes) {
        if (lower.startsWith(prefix)) {
            return trimmed.mid(prefix.length()).trimmed();
        }
    }
    return trimmed;
}

WORD Jarvis::parseVirtualKey(const QString& name)
{
    static const QMap<QString, WORD> keyMap = {
        {QStringLiteral("enter"),     VK_RETURN},
        {QStringLiteral("tab"),       VK_TAB},
        {QStringLiteral("escape"),    VK_ESCAPE},
        {QStringLiteral("esc"),       VK_ESCAPE},
        {QStringLiteral("space"),     VK_SPACE},
        {QStringLiteral("пробел"),    VK_SPACE},
        {QStringLiteral("backspace"), VK_BACK},
        {QStringLiteral("delete"),    VK_DELETE},
        {QStringLiteral("up"),        VK_UP},
        {QStringLiteral("down"),      VK_DOWN},
        {QStringLiteral("left"),      VK_LEFT},
        {QStringLiteral("right"),     VK_RIGHT},
        {QStringLiteral("home"),      VK_HOME},
        {QStringLiteral("end"),       VK_END},
        {QStringLiteral("pageup"),    VK_PRIOR},
        {QStringLiteral("pagedown"),  VK_NEXT},
        {QStringLiteral("insert"),    VK_INSERT},
        {QStringLiteral("ctrl"),      VK_CONTROL},
        {QStringLiteral("alt"),       VK_MENU},
        {QStringLiteral("shift"),     VK_SHIFT},
        {QStringLiteral("win"),       VK_LWIN},
        {QStringLiteral("f1"),  VK_F1},  {QStringLiteral("f2"),  VK_F2},
        {QStringLiteral("f3"),  VK_F3},  {QStringLiteral("f4"),  VK_F4},
        {QStringLiteral("f5"),  VK_F5},  {QStringLiteral("f6"),  VK_F6},
        {QStringLiteral("f7"),  VK_F7},  {QStringLiteral("f8"),  VK_F8},
        {QStringLiteral("f9"),  VK_F9},  {QStringLiteral("f10"), VK_F10},
        {QStringLiteral("f11"), VK_F11}, {QStringLiteral("f12"), VK_F12},
    };

    QString lower = name.trimmed().toLower();
    auto it = keyMap.find(lower);
    if (it != keyMap.end()) return it.value();

    if (lower.length() == 1) {
        QChar ch = lower.at(0).toUpper();
        if (ch >= QChar('A') && ch <= QChar('Z'))
            return static_cast<WORD>(ch.unicode());
    }
    return 0;
}

// ============================================================
// TTS — SAPI в отдельном QThread
// ============================================================

void Jarvis::speakAsync(const QString& text)
{
    if (text.isEmpty()) return;
    if (!m_ttsMutex.tryLock()) return;

    m_speaking.store(true);
    emit speakingChanged(true);

    QString copy = text;

    QThread* thread = QThread::create([this, copy]() {
        ComInitializer threadCom;
        if (!threadCom.ok()) {
            m_ttsMutex.unlock();
            return;
        }

        ISpVoice* voice = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_SpVoice, nullptr, CLSCTX_ALL,
            IID_ISpVoice, reinterpret_cast<void**>(&voice)
        );

        if (SUCCEEDED(hr) && voice) {
            voice->SetRate(1);
            voice->SetVolume(100);
            std::wstring wtext = copy.toStdWString();
            voice->Speak(wtext.c_str(), SPF_DEFAULT, nullptr);
            voice->Release();
        }

        m_speaking.store(false);
        m_ttsMutex.unlock();

        QMetaObject::invokeMethod(this, [this]() {
            emit speakingChanged(false);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

// ============================================================
// Обработка команд (гибридный режим)
// ============================================================

QString Jarvis::processCommand(const QString& input)
{
    QString s = input.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("Введите команду или напишите «помощь».");
    }

    // Сохраняем в историю
    m_memory->addMessage(QStringLiteral("user"), s);

    // 1. Пробуем локальные команды
    auto result = m_registry.tryExecute(s);
    if (result.matched) {
        m_memory->addMessage(QStringLiteral("assistant"), result.response);
        m_memory->updateContext(s, result.response);

        m_predictor->recordSequence(s);
        auto suggestion = m_predictor->suggestAfter(s);
        if (suggestion.isValid() && suggestion.confidence >= 0.5) {
            emit suggestionAvailable(suggestion.description, suggestion.action);
        }

        return result.response;
    }

    // 2. Пробуем плагины
    QString pluginResponse;
    if (m_pluginMgr->tryHandleCommand(s, pluginResponse)) {
        m_memory->addMessage(QStringLiteral("assistant"), pluginResponse);
        m_memory->updateContext(s, pluginResponse);
        m_predictor->recordSequence(s);
        return pluginResponse;
    }

    // 3. Если есть API-ключ — отправляем в Claude API
    if (m_claudeApi->shouldUseApi(s)) {
        m_claudeApi->sendMessage(s, [this, s](bool success, const QString& response) {
            if (success) {
                m_memory->addMessage(QStringLiteral("assistant"), response);
                m_memory->updateContext(s, response);
                handleClaudeResponse(response);
                m_predictor->recordSequence(s);
                emit asyncResponseReady(response);
            } else {
                emit asyncResponseError(response);
            }
        });

        return QString(); // Ответ придёт асинхронно
    }

    // 4. Fallback без API
    QString fallback = QStringLiteral(
        "Не понял команду. Напишите «помощь» для списка команд.\n"
        "Для свободного диалога установите API-ключ: apikey <ваш-ключ>"
    );
    m_memory->addMessage(QStringLiteral("assistant"), fallback);
    return fallback;
}

// ============================================================
// Обработка ответа Claude (парсинг [CMD:...])
// ============================================================

void Jarvis::handleClaudeResponse(const QString& response)
{
    static const QRegularExpression cmdPattern(
        QStringLiteral(R"(\[CMD:(.+?)\])"));

    QRegularExpressionMatchIterator it = cmdPattern.globalMatch(response);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString cmd = match.captured(1).trimmed();
        auto result = m_registry.tryExecute(cmd);
        Q_UNUSED(result)
    }
}

// ============================================================
// PluginHost реализация
// ============================================================

void Jarvis::addMessage(const QString& role, const QString& content)
{
    m_memory->addMessage(role, content);
}

QJsonArray Jarvis::recentMessages(int max) const
{
    return m_memory->recentMessagesAsJson(max);
}

void Jarvis::rememberFact(const QString& key, const QString& value)
{
    m_memory->rememberFact(key, value);
}

QString Jarvis::recallFact(const QString& key) const
{
    return m_memory->recallFact(key);
}

QJsonObject Jarvis::allFacts() const
{
    return m_memory->allFacts();
}

QJsonObject Jarvis::commandStats() const
{
    return m_memory->commandStats();
}

QString Jarvis::buildSystemPrompt() const
{
    return m_memory->buildSystemPrompt();
}

void Jarvis::registerCommand(const QStringList& keywords,
                              CommandHandler handler,
                              const QString& description,
                              bool prefixMatch)
{
    m_registry.registerCommand(keywords, std::move(handler), description, prefixMatch);
}

void Jarvis::appendLog(const QString& who, const QString& text, const QString& color)
{
    emit logRequested(who, text, color);
}

void Jarvis::showSuggestion(const QString& description, const QString& action)
{
    emit suggestionAvailable(description, action);
}

void Jarvis::setStatus(const QString& text, const QString& color)
{
    emit statusRequested(text, color);
}

QString Jarvis::executeCommand(const QString& input)
{
    return processCommand(input);
}

// ============================================================
// Команды: API и память
// ============================================================

QString Jarvis::cmdSetApiKey(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("apikey "),
                                      QStringLiteral("ключ ")});
    if (key.isEmpty()) {
        if (m_claudeApi->hasApiKey()) {
            return QStringLiteral("API-ключ установлен. Для замены: apikey <новый-ключ>");
        }
        return QStringLiteral("Укажите ключ: apikey <ваш-anthropic-api-key>");
    }

    m_claudeApi->setApiKey(key);
    return QStringLiteral("API-ключ сохранён. Claude API подключён.");
}

QString Jarvis::cmdRememberFact(const QString& input)
{
    QString arg = extractArg(input, {QStringLiteral("запомни "),
                                      QStringLiteral("remember ")});
    int eqPos = arg.indexOf(QChar('='));
    if (eqPos <= 0) {
        return QStringLiteral("Формат: запомни ключ=значение\n"
                              "Пример: запомни проект=JARVIS на Unreal Engine");
    }

    QString key   = arg.left(eqPos).trimmed();
    QString value = arg.mid(eqPos + 1).trimmed();

    m_memory->rememberFact(key, value);
    return QStringLiteral("Запомнил: ") + key + QStringLiteral(" = ") + value;
}

QString Jarvis::cmdRecallFact(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("вспомни "),
                                      QStringLiteral("recall ")});
    if (key.isEmpty()) {
        return QStringLiteral("Укажите что вспомнить: вспомни <ключ>");
    }

    QString value = m_memory->recallFact(key);
    if (value.isEmpty()) {
        return QStringLiteral("Не помню ничего о «") + key + QStringLiteral("».");
    }
    return key + QStringLiteral(": ") + value;
}

QString Jarvis::cmdShowMemory(const QString&)
{
    QJsonObject facts = m_memory->allFacts();
    if (facts.isEmpty()) {
        return QStringLiteral("Память пуста. Используйте: запомни ключ=значение");
    }

    QString text = QStringLiteral("Сохранённые факты:\n");
    for (auto it = facts.begin(); it != facts.end(); ++it) {
        text += QStringLiteral("  ") + it.key() + QStringLiteral(": ")
              + it.value().toString() + QStringLiteral("\n");
    }

    text += QStringLiteral("\nСессий в памяти: ")
          + QString::number(m_memory->pastSessionSummaries().size());
    text += QStringLiteral("\nСообщений за сессию: ")
          + QString::number(m_memory->messageCount());

    return text.trimmed();
}

QString Jarvis::cmdShowStats(const QString&)
{
    QJsonObject stats = m_memory->commandStats();
    if (stats.isEmpty()) {
        return QStringLiteral("Статистика пуста — пока недостаточно данных.");
    }

    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    QString text = QStringLiteral("Статистика команд:\n");
    int shown = 0;
    for (const auto& [cmd, count] : sorted) {
        text += QStringLiteral("  ") + cmd + QStringLiteral(": ")
              + QString::number(count) + QStringLiteral(" раз\n");
        if (++shown >= 15) break;
    }

    text += QStringLiteral("\nВсего за сессию: ")
          + QString::number(m_memory->taskContext().commandCount)
          + QStringLiteral(" команд");

    auto suggestions = m_predictor->suggest(3);
    if (!suggestions.isEmpty()) {
        text += QStringLiteral("\n\nПредложения:");
        for (const auto& s : suggestions) {
            text += QStringLiteral("\n  -> ") + s.description
                  + QStringLiteral(" (") + QString::number(int(s.confidence * 100))
                  + QStringLiteral("%)");
        }
    }

    return text.trimmed();
}

// ============================================================
// Команды: Обновления и плагины
// ============================================================

QString Jarvis::cmdCheckUpdate(const QString&)
{
    m_updater->checkForUpdates();

    connect(m_updater, &Updater::updateAvailable, this,
            [this](const UpdateInfo& info) {
                QString msg = QStringLiteral("Доступно обновление: v")
                              + info.latestVersion
                              + QStringLiteral(" (текущая: v")
                              + info.currentVersion + QStringLiteral(")\n")
                              + info.changelog.left(200);
                emit asyncResponseReady(msg);
            }, Qt::SingleShotConnection);

    connect(m_updater, &Updater::noUpdatesAvailable, this,
            [this]() {
                emit asyncResponseReady(
                    QStringLiteral("Вы используете последнюю версию."));
            }, Qt::SingleShotConnection);

    connect(m_updater, &Updater::updateError, this,
            [this](const QString& err) {
                emit asyncResponseError(err);
            }, Qt::SingleShotConnection);

    return QStringLiteral("Проверяю обновления...");
}

QString Jarvis::cmdListPlugins(const QString&)
{
    auto plugins = m_pluginMgr->plugins();
    if (plugins.isEmpty()) {
        return QStringLiteral("Плагины не загружены.\n"
                              "Положите DLL-плагины в папку plugins/ рядом с Jarvis.exe");
    }

    QString text = QStringLiteral("Загруженные плагины:\n");
    for (const auto& p : plugins) {
        QString status = p.loaded ? QStringLiteral("[ON]") : QStringLiteral("[OFF]");
        text += QStringLiteral("  ") + status + QStringLiteral(" ")
              + p.displayName + QStringLiteral(" v") + p.version
              + QStringLiteral("\n");
    }

    return text.trimmed();
}

QString Jarvis::cmdReloadPlugin(const QString& input)
{
    QString name = extractArg(input, {QStringLiteral("перезагрузи "),
                                       QStringLiteral("reload ")});
    if (name.isEmpty()) {
        return QStringLiteral("Укажите имя плагина: перезагрузи <имя>");
    }

    if (m_pluginMgr->reloadPlugin(name)) {
        return QStringLiteral("Плагин перезагружен: ") + name;
    }
    return QStringLiteral("Не удалось перезагрузить плагин: ") + name;
}

// ============================================================
// Остальные команды
// ============================================================

QString Jarvis::cmdTime(const QString&)
{
    return QStringLiteral("Сейчас ") + QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

QString Jarvis::cmdDate(const QString&)
{
    QDate d = QDate::currentDate();
    static const char* months[] = {
        "", "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"
    };
    static const char* days[] = {
        "", "понедельник", "вторник", "среда", "четверг",
        "пятница", "суббота", "воскресенье"
    };
    return QString::fromUtf8("%1, %2 %3 %4")
        .arg(QString::fromUtf8(days[d.dayOfWeek()]))
        .arg(d.day())
        .arg(QString::fromUtf8(months[d.month()]))
        .arg(d.year());
}

QString Jarvis::cmdGreeting(const QString&)
{
    int hour = QTime::currentTime().hour();
    QString g;
    if      (hour < 6)  g = QStringLiteral("Доброй ночи");
    else if (hour < 12) g = QStringLiteral("Доброе утро");
    else if (hour < 18) g = QStringLiteral("Добрый день");
    else                g = QStringLiteral("Добрый вечер");

    QString name = cmdUserName(QString()).replace(QStringLiteral("Вы — "), QString());
    return g + QStringLiteral(", ") + name + QStringLiteral("! Чем могу помочь?");
}

QString Jarvis::cmdUserName(const QString&)
{
    wchar_t buf[UNLEN + 1];
    DWORD sz = UNLEN + 1;
    if (GetUserNameW(buf, &sz)) {
        return QStringLiteral("Вы — ") + QString::fromWCharArray(buf);
    }
    return QStringLiteral("Не удалось определить имя.");
}

QString Jarvis::cmdHelp(const QString&)
{
    QString help = m_registry.helpText();
    help += QStringLiteral("\n\n— Свободный диалог —\n"
                           "Любой вопрос → отправляется в Claude API\n"
                           "(требуется apikey)");
    return help;
}

QString Jarvis::cmdLaunchApp(const QString& app)
{
    if (app.isEmpty()) {
        return QStringLiteral("Укажите приложение для запуска.");
    }
    std::wstring wapp = app.toStdWString();
    HINSTANCE result = ShellExecuteW(
        nullptr, L"open", wapp.c_str(), nullptr, nullptr, SW_SHOWNORMAL
    );
    if (reinterpret_cast<intptr_t>(result) > 32) {
        return QStringLiteral("Запускаю: ") + app;
    }
    return QStringLiteral("Не удалось запустить: ") + app;
}

QString Jarvis::cmdWebSearch(const QString& query)
{
    if (query.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com")));
        return QStringLiteral("Открываю Google.");
    }
    QString url = QStringLiteral("https://www.google.com/search?q=")
                  + QString::fromUtf8(QUrl::toPercentEncoding(query));
    QDesktopServices::openUrl(QUrl(url));
    return QStringLiteral("Ищу: ") + query;
}

QString Jarvis::cmdYoutube(const QString& query)
{
    if (query.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.youtube.com")));
        return QStringLiteral("Открываю YouTube.");
    }
    QString url = QStringLiteral("https://www.youtube.com/results?search_query=")
                  + QString::fromUtf8(QUrl::toPercentEncoding(query));
    QDesktopServices::openUrl(QUrl(url));
    return QStringLiteral("Ищу на YouTube: ") + query;
}

QString Jarvis::cmdLock(const QString&)
{
    LockWorkStation();
    return QStringLiteral("Блокирую экран.");
}

QString Jarvis::cmdBrowser(const QString&)
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com")));
    return QStringLiteral("Открываю браузер.");
}

// ============================================================
// Виртуальная клавиатура
// ============================================================

QString Jarvis::cmdTypeText(const QString& input)
{
    QString text = extractArg(input, {QStringLiteral("напечатай "),
                                      QStringLiteral("type ")});
    if (text.isEmpty()) {
        return QStringLiteral("Укажите текст для набора.");
    }

    m_keyEmulator->pressCombo({VK_MENU, VK_TAB});
    QThread::msleep(300);
    m_keyEmulator->typeText(text, 30);
    return QStringLiteral("Печатаю: ") + text;
}

QString Jarvis::cmdPressKey(const QString& input)
{
    QString keyName = extractArg(input, {QStringLiteral("нажми "),
                                         QStringLiteral("press ")});
    if (keyName.isEmpty()) {
        return QStringLiteral("Укажите клавишу (enter, tab, escape...).");
    }

    WORD vk = parseVirtualKey(keyName);
    if (vk == 0) {
        return QStringLiteral("Неизвестная клавиша: ") + keyName;
    }

    m_keyEmulator->pressKey(vk);
    return QStringLiteral("Нажимаю: ") + keyName;
}

QString Jarvis::cmdCombo(const QString& input)
{
    QString comboStr = extractArg(input, {QStringLiteral("комбо "),
                                          QStringLiteral("combo ")});
    if (comboStr.isEmpty()) {
        return QStringLiteral("Укажите комбинацию (ctrl+c, alt+tab...).");
    }

    QStringList parts = comboStr.toLower().split(QStringLiteral("+"),
                                                  Qt::SkipEmptyParts);
    std::vector<WORD> keys;
    for (const auto& part : parts) {
        WORD vk = parseVirtualKey(part.trimmed());
        if (vk == 0) {
            return QStringLiteral("Неизвестная клавиша в комбинации: ") + part.trimmed();
        }
        keys.push_back(vk);
    }

    if (keys.empty()) {
        return QStringLiteral("Не удалось разобрать комбинацию.");
    }

    m_keyEmulator->pressCombo(
        std::initializer_list<WORD>(keys.data(), keys.data() + keys.size())
    );
    return QStringLiteral("Нажимаю комбинацию: ") + comboStr;
}
