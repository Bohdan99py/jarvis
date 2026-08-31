// -------------------------------------------------------
// jarvis.cpp — Ядро J.A.R.V.I.S.: команды, TTS, мозги
// -------------------------------------------------------

#include "jarvis.h"
#include "virtual_keyboard.h"
#include "session_memory.h"
#include "claude_api.h"
#include "ollama_api.h"
#include "action_predictor.h"
#include "auto_updater.h"
#include "project_indexer.h"
#include "project_profile.h"
#include "dev_advisor.h"
#include "edit_journal.h"
#include "code_actions.h"
#include "attachments_manager.h"
#include "brain.h"
#include "pc_command_registry.h"
#include "pc_controller.h"
#include "tool_registry.h"
#include "context_tracker.h"
#include "context_advisor.h"
#include "workflow_manager.h"
#include "action_log.h"
#include "trigger_engine.h"
#include "git_tools.h"
#include "journal_tools.h"
#include "clipboard_tools.h"
#include "clipboard_watcher.h"
#include "project_registry.h"
#include "plugin_bridge.h"
#include "health_center.h"
#include "global_search.h"
#include "event_feed.h"
#include "system_watcher.h"
#include "system_monitor.h"
#include "device_hub.h"
#include "bluetooth_devices.h"
#include "permission_gate.h"
#include "profile_tools.h"
#include "notification_manager.h"
#include "agent_loop.h"
#include "system_tools.h"
#include "file_organizer.h"
#include "local_trainer.h"
#include "background_learner.h"
#include "case_distiller.h"
#include "security_camera.h"
#include "synonym_learner.h"
#include <QTimer>
#include <QSettings>
#include "database_manager.h"
#include "proactive_reminder_manager.h"
#include <QSqlQuery>
#include <QSqlDatabase>
#include "user_profile.h"
#include "activity_tracker.h"
#include "face_registry.h"
#include "user_profile_manager.h"
#include "training_processing_worker.h"
#include "system_manifest.h"
#include "task_manager_dialog.h"
#include "j2j_mesh_connector.h"
#include "translation_engine.h"
#include "languagedetector.h"
#include "jarvis_paths.h"
#include "jarvis_response.h"
#include "voice_synthesis_manager.h"
#include "voice_policy.h"
#include "elevenlabs_provider.h"
#include "voice_input.h"
#include "passive_listener.h"
#include "audio_manager.h"
#include "llm_cache_manager.h"
#include "sentence_composer.h"
#include "curiosity_engine.h"
#include "memory_consolidation.h"
#include "pdf_distiller.h"
#include "self_journal.h"
#include "user_profile_extended.h"
#include "self_update_reflector.h"
#include "mermaid_renderer.h"
#include "semantic_intent_manager.h"
#include "personality_engine.h"
#include "reflection_engine.h"
#include "memory_manager.h"
#include "esp32_hub_manager.h"
// lang.h НЕ используем через IS_EN — в статической библиотеке gUiLanguage()
// хранится в отдельном экземпляре (MSVC ODR). Язык передаётся явно через
// m_uiEnglish, который MainWindow устанавливает через setUiLanguage().

#include <shellapi.h>
#include <QDateTime>
#include <QThread>
#include <QMutexLocker>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QEventLoop>

// Дефайны из CMake
#ifndef JARVIS_VERSION
#define JARVIS_VERSION "2.0.0"
#endif
#ifndef JARVIS_GITHUB_USER
#define JARVIS_GITHUB_USER "Bohdan99py"
#endif
#ifndef JARVIS_GITHUB_REPO
#define JARVIS_GITHUB_REPO "jarvis"
#endif

// ============================================================
// Конструктор / Деструктор
// ============================================================

Jarvis::Jarvis(QObject* parent)
    : QObject(parent)
{
    m_memory       = new SessionMemory(this);

    // Модульные скиллы — до первого LLM-вызова, чтобы system prompt
    // сразу собирался из включённых лего-блоков знаний.
    m_skills = new SkillManager(this);
    auto applySkillContext = [this]() {
        m_memory->setSkillContext(
            m_skills->promptBlocks(),
            m_skills->isFeatureEnabled(SkillManager::featureCodeActions()));
    };
    applySkillContext();
    connect(m_skills, &SkillManager::skillsChanged, this, applySkillContext);

    // Режимы работы (профили поведения) — создаём ПОСЛЕ SkillManager,
    // потому что режим ссылается на id скиллов и умеет их вкл/выкл.
    // Активный режим на старте применяет свой enable/disable набор
    // (setEnabled внутри SkillManager сам эмитит skillsChanged, поэтому
    // applySkillContext() догонит скилл-блоки автоматически).
    m_modes = new ModeManager(m_skills, this);
    auto applyModeContext = [this]() {
        // Используем m_uiEnglish, а не IS_EN — в статической engine-lib
        // gUiLanguage() лежит в отдельной ODR-копии (см. коммент выше).
        m_memory->setModeContext(m_modes->promptBlock(m_uiEnglish));
    };
    applyModeContext();
    connect(m_modes, &ModeManager::modeChanged, this, [this, applyModeContext]() {
        applyModeContext();
        // Режим мог перетасовать скиллы — освежим и их контекст на всякий
        // случай (SkillManager::setEnabled уже эмитил skillsChanged, но
        // если exclusive-режим меняет несколько скиллов подряд, батчевый
        // повтор дешёвый и безопасный).
        m_memory->setSkillContext(
            m_skills->promptBlocks(),
            m_skills->isFeatureEnabled(SkillManager::featureCodeActions()));
    });
    // Первичное применение активного режима к скиллам (если был сохранён).
    m_modes->reapplyActive();

    // ESP32 physical node — feature-gated by the esp32_hub skill
    m_esp32Hub = new Esp32HubManager(this);
    auto syncEsp32 = [this]() {
        const bool want = m_skills->isFeatureEnabled(QStringLiteral("esp32_hub"));
        if (want && !m_esp32Hub->isRunning())
            m_esp32Hub->start();
        else if (!want && m_esp32Hub->isRunning())
            m_esp32Hub->stop();
    };
    syncEsp32();
    connect(m_skills, &SkillManager::skillsChanged, this, syncEsp32);

    m_claudeApi    = new ClaudeApi(m_memory, this);
    m_ollamaApi    = new OllamaApi(this);   // Ollama — локальный LLM для быстрых ответов
    m_predictor    = new ActionPredictor(m_memory, this);
    m_keyEmulator  = new KeyEmulator(this);
    m_pcCommands   = new PcCommandRegistry(m_keyEmulator, this);
    m_indexer      = new ProjectIndexer(this);
    // Профиль проекта: тип, сборка, таргеты, зависимости, ассеты. Индекс
    // отвечает «где лежит символ», профиль — «что это вообще за проект».
    m_projectProfile = new ProjectProfile(this);
    m_projectProfile->setIndexer(m_indexer);

    // Фоновый советник по проекту. Claude ему выдаётся ниже, когда
    // m_claudeApi уже создан.
    m_devAdvisor = new DevAdvisor(this);
    m_devAdvisor->setIndexer(m_indexer);
    m_devAdvisor->setProfile(m_projectProfile);
    m_codeActions  = new CodeActions(this);
    m_attachments  = new AttachmentsManager(this);
    m_profile      = new UserProfile(this);
    m_activity     = new ActivityTracker(this);
    m_activity->start(15); // capture every 15 seconds
    m_predictor->setActivityTracker(m_activity); // context for the cv::ml experience classifier

    // Чем занят человек — это же и ответ на вопрос, можно ли сейчас
    // говорить вслух. Политика голоса хранит категорию сырой и сама
    // считает «глубокая работа» по времени в ней (см. voice_policy.h).
    connect(m_activity, &ActivityTracker::activityChanged, this,
            [](const QString&, const QString& category) {
        VoicePolicyManager::instance().setActivity(category);
    });
    VoicePolicyManager::instance().setActivity(m_activity->currentCategory());

    // Face/photo enrollment feeds the same knowledge_base training system
    // as voice/text learning — new "appearance" category, not a silo.
    connect(&FaceRegistry::instance(), &FaceRegistry::faceEnrolled, this,
            [this](const QString& name, const QString& status) {
        m_activity->learnFact(m_currentUserId, QStringLiteral("appearance"),
                              name,
                              status.isEmpty() ? QStringLiteral("recognized face") : status,
                              0.8f);
    });

    // Background training data pipeline: voice_journal → training pairs → .jsonl
    m_trainingPipeline = new TrainingPipelineController(this);
    m_trainingPipeline->setUserId(m_currentUserId);
    m_trainingPipeline->setDatasetPath(JarvisPaths::subPath(QStringLiteral("training_export")));
    m_trainingPipeline->start(10);

    // Self-tuning: bake newly liked replies into a personalized Ollama
    // model automatically — no more manual "Start Training" click, and
    // no dependency on the .jsonl export (which nothing else reads).
    m_localTrainer = new LocalTrainer(this);
    connect(m_localTrainer, &LocalTrainer::trainingFinished, this,
            [this](bool success, const QString& message) {
        if (!success) {
            qWarning() << "[Jarvis] Auto-tune failed:" << message;
            return;
        }
        QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
        cfg.setValue(QStringLiteral("autotrain/lastLikedCount"),
                     DatabaseManager::instance().trainingLogCount(m_currentUserId, 1));
        qDebug() << "[Jarvis] Auto-tune finished:" << message;
    });

    m_autoTrainTimer = new QTimer(this);
    connect(m_autoTrainTimer, &QTimer::timeout, this, [this]() {
        static constexpr int MIN_NEW_LIKES = 15;
        if (!m_localTrainer->isOllamaAvailable()) return;

        const int likedNow = DatabaseManager::instance().trainingLogCount(m_currentUserId, 1);
        QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
        const int baseline = cfg.value(QStringLiteral("autotrain/lastLikedCount"), 0).toInt();

        if (likedNow - baseline >= MIN_NEW_LIKES)
            m_localTrainer->train(&DatabaseManager::instance());
    });
    m_autoTrainTimer->start(60 * 60 * 1000); // check hourly

    // Behavior-pattern learning — the class existed and was described in
    // UI tooltips as active, but was never actually instantiated anywhere.
    m_backgroundLearner = new BackgroundLearner(this);
    if (m_indexer && !m_indexer->projectRoot().isEmpty())
        m_backgroundLearner->setWatchPaths({m_indexer->projectRoot()});
    m_backgroundLearner->start();

    // Layer 2: distills repeated llm_cache cases into per-owner heuristics
    // roughly nightly (see CaseDistiller — real elapsed-time gate, not tied
    // to app uptime).
    m_caseDistiller = new CaseDistiller(m_claudeApi, this);
    m_caseDistiller->start();

    // Shared SecurityCamera — constructed here (not by whichever surface
    // happens to arm it first) so the desktop Guard menu and the Telegram
    // /security command always talk to the same instance. Stays OFF
    // (Sentinel/FullAlert not started) until one of them arms it.
    m_securityCamera = new SecurityCamera(this);

    // J2J Mesh: peer-to-peer network for multi-instance knowledge sync
    m_mesh = new J2JMeshConnector(this);
    m_activity->setMeshConnector(m_mesh);
    {
        auto userOpt = DatabaseManager::instance().getUser(m_currentUserId);
        if (userOpt && !userOpt->currentRole.isEmpty())
            m_mesh->setNodeRole(userOpt->currentRole);
    }
    connect(m_mesh, &J2JMeshConnector::peerAuthorized, this,
            [this](const QString& name, const QString& addr) {
        const QString msg = QStringLiteral("[MESH LINK]: Established secure link with peer '%1' (%2). "
                                           "Synchronizing shared knowledge matrices...")
                                .arg(name, addr);
        emit meshEvent(msg);
    });
    connect(m_mesh, &J2JMeshConnector::peerDiscovered, this,
            [this](const QString& name, const QString& addr) {
        const QString msg = QStringLiteral("[MESH]: Discovered peer '%1' at %2")
                                .arg(name, addr);
        emit meshEvent(msg);
    });
    connect(m_mesh, &J2JMeshConnector::peerLost, this,
            [this](const QString& name) {
        emit meshEvent(QStringLiteral("[MESH]: Peer '%1' went offline").arg(name));
    });
    connect(m_mesh, &J2JMeshConnector::knowledgeReceived, this,
            [this](const QString& node, int count) {
        emit meshEvent(QStringLiteral("[MESH SYNC]: Received %1 knowledge entries from '%2'")
                           .arg(count).arg(node));
    });
    connect(m_mesh, &J2JMeshConnector::taskReceived, this,
            [this](const QString& node, const QString& title) {
        emit meshEvent(QStringLiteral("[MESH TASK]: '%1' delegated task: %2")
                           .arg(node, title));
    });
    m_mesh->start();
    // Reported, not assumed: the mesh is live because start() ran here, and
    // the peer count is re-reported as peers come and go, so Jarvis never
    // offers to delegate work to a network he isn't on.
    SystemManifest::setRuntimeState(QStringLiteral("j2j_mesh"), true,
                                    QStringLiteral("listening, no peers yet"));
    {
        auto reportPeers = [this]() {
            const int n = m_mesh->peerCount();
            SystemManifest::setRuntimeState(QStringLiteral("j2j_mesh"), true,
                n > 0 ? QStringLiteral("%1 peer(s) linked").arg(n)
                      : QStringLiteral("listening, no peers yet"));
        };
        connect(m_mesh, &J2JMeshConnector::peerAuthorized, this,
                [reportPeers](const QString&, const QString&) { reportPeers(); });
        connect(m_mesh, &J2JMeshConnector::peerLost, this,
                [reportPeers](const QString&) { reportPeers(); });
    }

    // Translation engine: FR/EN/RU translation + audio pipeline
    m_translator = new TranslationEngine(this);
    m_translator->setLlmApi(m_claudeApi);
    m_translator->setTracker(m_activity);
    m_translator->setUserId(m_currentUserId);
    m_translator->setMeshConnector(m_mesh);

    // Vectorized core memory — always active, not just in Telegram
    MemoryManager::instance().initialize();

    // Personality evolution — emotional state + genetic mutation
    m_personality = new PersonalityEngine(this);

    // Autonomous reflection — behavioral analysis + morning nudge
    m_reflection = new ReflectionEngine(this);
    m_reflection->setMemoryManager(&MemoryManager::instance());
    m_reflection->setPersonalityEngine(m_personality);
    m_reflection->start(30);

    // Автообновление
    m_updater = new AutoUpdater(
        QStringLiteral(JARVIS_VERSION),
        QStringLiteral(JARVIS_GITHUB_USER),
        QStringLiteral(JARVIS_GITHUB_REPO),
        this
    );

    registerCommands();

    // Голосовое управление ПК (мышь/окна/система/медиа/браузер/макросы) —
    // регистрируется ПОСЛЕ registerCommands(), чтобы системные префиксы
    // ("напечатай ", "нажми ", "комбо ", "apikey " и т.д.) сохраняли
    // приоритет совпадения в CommandRegistry::tryExecute.
    m_pcCommands->registerInto(m_registry);

    // Голосовая обратная связь PC-команд идёт тем же путём TTS,
    // что и обычные ответы Jarvis.
    connect(m_pcCommands, &PcCommandRegistry::feedbackReady,
            this, &Jarvis::speakAsync);

    // ── Слой действий ────────────────────────────────────────────
    // До этого модель умела только говорить: любые действия были
    // возможны лишь через совпадение фразы с ключевыми словами в
    // CommandRegistry. Теперь у неё есть инструменты, и она сама
    // решает, какие вызвать и в каком порядке.
    m_tools       = new ToolRegistry(this);
    m_permissions = new PermissionGate(this);
    JarvisTools::registerSystemTools(*m_tools, m_pcCommands->controller());

    // Контекст экрана: без него слова «здесь» и «это» не на что
    // отобразить. Опрашивает передний план и пропускает собственное окно.
    m_context = new ContextTracker(this);
    m_context->setProjectInfoProvider([this](QString& root, QStringList& recent) {
        if (!m_indexer)
            return;
        root = m_indexer->projectRoot();
        recent = m_indexer->recentFiles(8);
    });
    // Что уже открыто. Считаем открытым то, у чего есть видимое окно с
    // заголовком, а не всё, что есть в списке процессов: половина этого
    // списка — служебные хосты, которые для человека ничего не «открыто».
    m_context->setRunningAppsProvider([this]() -> QStringList {
        QStringList apps;
        if (!m_pcCommands || !m_pcCommands->controller())
            return apps;

        const auto windows = m_pcCommands->controller()->windows()->allWindows();
        for (const WindowInfo& w : windows) {
            if (!w.isVisible || w.title.trimmed().isEmpty())
                continue;

            // Заголовок окна — это не имя программы, но для «уже открыто»
            // важен не точный процесс, а узнаваемое имя. Берём хвост
            // заголовка: там почти всегда стоит название приложения.
            const QString tail = w.title.section(QStringLiteral(" - "), -1).trimmed();
            const QString name = tail.isEmpty() ? w.title.trimmed() : tail;
            if (name.size() < 3 || apps.contains(name))
                continue;

            apps << name;
            if (apps.size() >= 12)
                break;
        }
        return apps;
    });

    m_context->start(1500);
    JarvisTools::registerContextTools(*m_tools, m_context);

    // Право заговорить первым. Условия, при которых это уместно, живут
    // в самом советчике; здесь только связь «согласились → выполняем».
    m_advisor = new ContextAdvisor(m_context, this);
    m_advisor->setRequestHandler([this](const QString& request) {
        runAgentTask(request, /*recordUserMessage=*/false);
    });
    m_advisor->setEnabled(
        DatabaseManager::instance().getConfig(QStringLiteral("context_advisor"), true).toBool());

    // Git: чтение объявлено безопасным и потому выполняется молча —
    // «что я менял сегодня» не должно требовать подтверждения. Какой
    // репозиторий имеется в виду, решает не модель, а открытый проект.
    JarvisTools::registerGitTools(*m_tools, [this]() -> QString {
        return m_indexer ? m_indexer->projectRoot() : QString();
    });

    // Повторяемые цепочки: то, что человек делает одинаково каждый раз,
    // не должно каждый раз проходить через модель.
    m_workflows = new WorkflowManager(m_tools, m_permissions, this);
    JarvisTools::registerWorkflowTools(*m_tools, m_workflows);

    // Ctrl+K — поиск. Провайдеры регистрируем здесь: всё, по чему
    // ищем (приложения, индекс, реестр инструментов, сценарии, БД),
    // уже создано выше.
    m_search = new GlobalSearch(this);
    registerSearchProviders();

    // ── Лента событий ────────────────────────────────────────────
    // Монитор фоном опрашивает раз в 5 секунд: этого хватает наблюдателю,
    // а панель на время открытия сама попросит чаще. Наблюдатель
    // превращает отсчёты в редкие события — правила в SystemWatcher.
    m_sysMonitor = new SystemMonitor(this);
    m_sysMonitor->start(5000);

    m_sysWatcher = new SystemWatcher(m_sysMonitor, this);

    // Устройства: хаб пустой сам по себе, всё содержательное приносят
    // провайдеры — их регистрируем после того, как ESP32 и mesh созданы.
    m_devices = new DeviceHub(this);
    registerDeviceProviders();

    // ── Журнал действий ──────────────────────────────────────────
    // Реестр знает, ЧТО выполнено; журнал добавляет, кто это начал,
    // чем кончилось, и переживает перезапуск. Подписка ровно одна:
    // иначе каждый, кто умеет вызвать инструмент, обязан был бы
    // помнить про запись — и рано или поздно забыл бы.
    connect(m_tools, &ToolRegistry::toolInvoked, this,
            [](const QString& name, const QJsonObject& args, bool ok,
               const QString& display, int risk) {
        ActionLog::instance().record(name, args,
                                     ok ? ActionOutcome::Ok : ActionOutcome::Failed,
                                     display, risk);
    });
    JarvisTools::registerActionTools(*m_tools, m_permissions);

    // Откат перестаёт быть фразой, которую надо угадать: теперь модель
    // знает и что откатится, и что откатить нельзя.
    JarvisTools::registerJournalTools(*m_tools, m_uiEnglish);

    // История буфера обмена. Ядро наблюдателю НЕ передаём: с ним он
    // начинает сам отправлять скопированное в модель, а это включается
    // осознанно и не мной. Без ядра он ведёт только локальную историю.
    m_clipboard = new ClipboardWatcher(this);
    JarvisTools::registerClipboardTools(*m_tools, m_clipboard);

    // ── Проекты ──────────────────────────────────────────────────
    // Индексатор знал ровно один проект — тот, чей корень ему задали.
    // Реестр добавляет остальные и делает «открой ESP32» однозначной
    // командой. Переключение корня делается здесь: слой действий про
    // индексатор и память ничего не знает.
    m_projects = new ProjectRegistry(this);
    m_projects->setActivator([this](const ProjectEntry& project) {
        if (!m_indexer)
            return;
        m_indexer->setProjectRoot(QDir::toNativeSeparators(project.path));
        m_indexer->indexProject();
        m_indexer->enableFileWatcher(true);
        syncProjectInfoToMemory();
    });
    JarvisTools::registerProjectTools(*m_tools, m_projects);

    // Первый запуск: проект, в котором человек уже работает, попадает
    // в список сам. Пустой список проектов бесполезен, а спрашивать
    // «добавить этот?» — навязчиво.
    if (m_projects->count() == 0 && m_indexer && !m_indexer->projectRoot().isEmpty())
        m_projects->addOrReplace(ProjectRegistry::sniff(m_indexer->projectRoot()));

    // ── Плагины ──────────────────────────────────────────────────
    // Система плагинов была написана и ни разу не включена: хост никто
    // не реализовывал, папку никто не сканировал. Мост её включает —
    // и заодно даёт плагину то, чего у него не было: право добавить
    // инструмент, а не только поймать фразу.
    m_plugins = new PluginBridge(this, m_tools, m_permissions, this);
    JarvisTools::registerPluginTools(*m_tools, m_plugins);
    m_plugins->loadAll();

    // ── Триггеры ─────────────────────────────────────────────────
    // Замыкают ленту событий обратно на слой действий: до сих пор
    // события в ней мог увидеть только человек, и то если открывал
    // панель. Стартуем последними — правила ссылаются на сценарии и
    // инструменты, а они созданы выше.
    m_triggers = new TriggerEngine(m_tools, m_permissions, m_workflows, this);
    m_triggers->setDeviceHub(m_devices);
    JarvisTools::registerTriggerTools(*m_tools, m_triggers);
    m_triggers->start();

    // ── Слух ─────────────────────────────────────────────────────
    // Объекты создаём здесь, а распознавание запускаем отдельно, из
    // startVoice(): до этого момента интерфейс ещё не подписался, и
    // просьбу доустановить модели услышать будет некому.
    m_audio   = new AudioManager(this);
    m_voiceIn = new VoiceInput(this);
    m_passive = new PassiveListener(this);

    // Распознанное входит в ядро, а не в окно (см. submitVoiceCommand).
    connect(m_voiceIn, &VoiceInput::textRecognized,
            this, &Jarvis::submitVoiceCommand);

    // Отвал сетевого голоса не должен выглядеть как «голос просто стал
    // хуже»: причина уходит в ленту с общим ключом дедупликации, чтобы
    // при лежащей сети там была одна строка со счётчиком, а не сотня.
    ElevenLabsProvider::setFailureReporter([](const QString& reason) {
        EventFeed::instance().post(
            QStringLiteral("SYSTEM"), EventLevel::Warning,
            QStringLiteral("Голос ElevenLabs недоступен — говорю офлайн-голосом"),
            reason,
            QStringLiteral("elevenlabs-unavailable"));
    });

    // Пока человек говорит, фоновая реплика подождёт. Перебивание уже
    // звучащей фразы — отдельный механизм (MainWindow::maybeBargeIn);
    // здесь речь о том, чтобы новую не начинать.
    connect(m_voiceIn, &VoiceInput::speechDetected, this, []() {
        VoicePolicyManager::instance().noteUserSpeech();
    });

    // Полный экран, презентация и «не беспокоить» — состояние Windows,
    // а не догадка: опрашиваем его раз в пять секунд, дешевле, чем
    // разбирать заголовки окон.
    m_voiceContextTimer = new QTimer(this);
    connect(m_voiceContextTimer, &QTimer::timeout, this, []() {
        VoicePolicyManager::instance().refreshSystemState();
    });
    m_voiceContextTimer->start(5000);
    VoicePolicyManager::instance().refreshSystemState();

    connect(m_passive, &PassiveListener::journalProcessed, this, [](int pairs) {
        if (pairs > 0)
            qDebug() << "[Training] Voice journal:" << pairs << "new pairs";
    });
    connect(m_passive, &PassiveListener::weeklyCleanupDone, this, [](int deleted) {
        if (deleted > 0)
            qDebug() << "[Training] Weekly cleanup:" << deleted << "entries";
    });

    // Пассивный слушатель работает на уже загруженных моделях Vosk, а не
    // на своих: две копии — это лишние ~3.6 GB. Поэтому он инициализируется
    // не сам по себе, а когда VoiceInput сообщил, что модели в памяти.
    connect(m_voiceIn, &VoiceInput::ready, this, [this]() {
        PassiveListenerConfig cfg;
        const QString datasetPath = DatabaseManager::instance().getConfig(
            QStringLiteral("voice_dataset_path")).toString();
        if (!datasetPath.isEmpty())
            cfg.datasetPath = datasetPath;
        cfg.modelPathRu = m_voiceIn->config().modelPathRu;
        cfg.modelPathEn = m_voiceIn->config().modelPathEn;

        VoskWorker* w = m_voiceIn->worker();
        m_passive->initializeWithSharedModels(w ? w->modelForLang(QStringLiteral("ru")) : nullptr,
                                              w ? w->modelForLang(QStringLiteral("en")) : nullptr,
                                              cfg);
    });

    // Автостарт пассивной записи. Выключается через Training → Пассивная
    // запись; пункт меню спрашивает состояние у слушателя, а не хранит своё.
    connect(m_passive, &PassiveListener::ready, this, [this]() {
        m_passive->startListening();
    });

    // ── Диагностика ──────────────────────────────────────────────
    // Половина подсистем умеет отваливаться молча: база не открылась —
    // история просто пустая, ключ не задан — модель «не отвечает».
    m_health = new HealthCenter(this);
    registerHealthProbes();
    JarvisTools::registerHealthTools(*m_tools, m_health);

    // Всё, что человек и так увидел бы как результат своего действия,
    // в ленту не попадает: она про то, что случилось САМО или в фоне.
    connect(m_workflows, &WorkflowManager::workflowFinished, this,
            [](const QString& name, bool ok, const QString& report) {
        EventFeed::instance().post(
            QStringLiteral("workflow"),
            ok ? EventLevel::Good : EventLevel::Warning,
            (ok ? QStringLiteral("Сценарий \"%1\" выполнен")
                : QStringLiteral("Сценарий \"%1\" оборвался")).arg(name),
            report.section(QChar('\n'), 1));
    });

    connect(m_modes, &ModeManager::modeActivated, this,
            [this](const QString& id, const QString&) {
        const ModeInfo mode = m_modes->activeMode();
        EventFeed::instance().post(
            QStringLiteral("profile"), EventLevel::Info,
            QStringLiteral("Профиль: %1").arg(mode.displayName(m_uiEnglish)),
            mode.system.summary(m_uiEnglish),
            QStringLiteral("profile/") + id);
    });

    // Профили = режимы. Системную часть режима (разрешения, уведомления,
    // громкость, стартовый сценарий) применяем здесь: ModeManager про них
    // не знает и знать не должен — он про скиллы и промпт.
    JarvisTools::registerProfileTools(*m_tools, m_modes, m_permissions, m_uiEnglish);

    connect(m_modes, &ModeManager::modeActivated,
            this, &Jarvis::applyModeSystemProfile);

    // На старте применяем только СОСТОЯНИЕ активного профиля — уровень
    // доверия и политику уведомлений. Действия (стартовый сценарий,
    // громкость) не повторяем: запуск приложения это не «переключение
    // профиля», и перекручивать звук при каждом старте никто не просил.
    if (m_modes) {
        const ModeInfo active = m_modes->activeMode();
        if (!active.system.permissionMode.isEmpty()) {
            m_permissions->setMode(permissionModeFromString(
                active.system.permissionMode, m_permissions->mode()));
        }
        if (!active.system.notifications.isEmpty()) {
            NotificationManager::instance().setPolicy(
                NotificationManager::policyFromString(active.system.notifications));
        }
        if (!active.system.voice.isEmpty()) {
            VoicePolicyManager::instance().setPolicy(
                VoicePolicyManager::policyFromString(active.system.voice,
                                                      VoicePolicy::Normal));
        }
    }

    // Автопереключение по приложению: режим сам объявляет, в каких окнах
    // он уместен. Обратного (само выключиться) намеренно нет — молча
    // снимать профиль, который человек выбрал руками, слишком нахально.
    connect(m_context, &ContextTracker::focusChanged, this,
            [this](const QString& appName, const QString&) {
        if (appName.isEmpty() || !m_modes)
            return;
        for (const ModeInfo& mode : m_modes->modes()) {
            if (mode.id == m_modes->activeId())
                continue;
            for (const QString& trigger : mode.system.autoActivateApps) {
                if (appName.compare(trigger, Qt::CaseInsensitive) == 0) {
                    qDebug() << "[Modes] auto-activating" << mode.id << "for" << appName;
                    m_modes->activate(mode.id);
                    return;
                }
            }
        }
    });

    m_agent = new AgentLoop(m_claudeApi, m_tools, m_permissions, this);

    connect(m_agent, &AgentLoop::finished, this, [this](const QString& text) {
        // Тот же путь, что и у обычного ответа: история, TTS, Telegram,
        // десктопный чат — всё уже подписано на asyncResponseReady.
        m_memory->addMessage(QStringLiteral("assistant"), text);
        m_memory->updateContext(m_lastAgentRequest, text);
        emit asyncResponseReady(text);
    });

    connect(m_agent, &AgentLoop::failed, this, [this](const QString& err) {
        EventFeed::instance().post(QStringLiteral("agent"), EventLevel::Error,
                                   QStringLiteral("Задача не выполнена"), err);
        if (!emitOfflineAnswer(m_lastAgentRequest))
            emit asyncResponseError(err);
    });

    // Состояние «говорит» теперь ведёт очередь TTS — единственная, кто
    // знает, звучит ли реплика прямо сейчас. isSpeaking()/speakingChanged
    // остаются прежними для тех, кто на них подписан (см. MainWindow).
    connect(&VoiceSynthesisManager::instance(), &VoiceSynthesisManager::speakingChanged,
            this, [this](bool speaking) {
        m_speaking.store(speaking);
        emit speakingChanged(speaking);
    });

    // Реакция на ошибки API
    connect(m_claudeApi, &ClaudeApi::apiError, this, [this](const QString& err) {
        emit asyncResponseError(err);
    });

    // Фоновый советник: ключ API появляется вместе с ClaudeApi, а язык
    // интерфейса — позже, из MainWindow (см. setUiLanguage).
    m_devAdvisor->setClaudeApi(m_claudeApi);
    connect(m_devAdvisor, &DevAdvisor::autoFixApplied, this,
            [this](const QString& message) { emit advisorMessage(message); });
    connect(m_devAdvisor, &DevAdvisor::adviceReady, this,
            [this](const QString& description, const QString& action) {
        emit suggestionAvailable(description, action);
    });

    // Синхронизация информации об индексе с системным промптом
    connect(m_indexer, &ProjectIndexer::indexingFinished, this,
            [this](int, int) {
        // Профиль пересобирается только после ПОЛНОЙ индексации: обход
        // дерева ради одного изменённого файла — пустая трата секунд.
        if (m_projectProfile)
            m_projectProfile->scan(m_indexer->projectRoot());
        syncProjectInfoToMemory();
    });
    connect(m_indexer, &ProjectIndexer::fileReindexed, this,
            [this](const QString&) { syncProjectInfoToMemory(); });

    // Поисковый журнал сессий: файлы, которые JARVIS создал/изменил/удалил —
    // попадают в сводку текущей сессии (для команды "вспомни что было ...").
    connect(m_codeActions, &CodeActions::fileCreated, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });
    connect(m_codeActions, &CodeActions::fileModified, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });
    connect(m_codeActions, &CodeActions::fileDeleted, this,
            [this](const QString& path) { m_memory->recordFileTouched(path); });

    if (m_indexer->fileCount() > 0) {
        // Индекс поднялся из кэша — профиль тоже берём с диска, чтобы не
        // обходить всё дерево на каждом старте приложения.
        if (m_projectProfile && !m_projectProfile->load(m_indexer->projectRoot()))
            m_projectProfile->scan(m_indexer->projectRoot());
        syncProjectInfoToMemory();
    }

    // System Manifest: inject capabilities into LLM context. Probed from
    // real runtime state, so what Jarvis claims about himself tracks what is
    // actually loaded — see SystemManifest::activeCapabilities().
    m_memory->setCapabilitiesContext(SystemManifest::buildCapabilitiesContext());

    // Proactive Curiosity Engine — context-aware idle dialogue
    {
        auto& curiosity = CuriosityEngine::instance();
        curiosity.setActivityTracker(m_activity);
        // Проверка раз в 20 минут вместо 90: сам вопрос всё равно проходит
        // через кулдаун и модель внимания, но раз в 90 минут окно простоя
        // почти всегда закрывалось раньше, чем таймер до него доходил.
        curiosity.start(20);
        connect(&curiosity, &CuriosityEngine::proactiveDialogue, this,
                [this](const QString& message, CuriosityEngine::ProactiveCategory) {
            // The question MUST land in session memory, not just on screen.
            // It used to only be emitted for display, so the conversation
            // history sent to the LLM had no record that Jarvis had asked
            // anything — the user's reply arrived as a contextless opener
            // ("Yes, the sandwich with chicken") and the model answered as
            // if it came out of nowhere. Writing it here means a reply is
            // read against the question it answers, whether it comes 10
            // seconds or an hour later.
            m_memory->addMessage(QStringLiteral("assistant"), message);
            emit asyncResponseReady(message);
        });
    }

    // Two-Tier Memory Consolidation — external 4TB + local SSD cache
    {
        auto& mc = MemoryConsolidation::instance();
        mc.startBackgroundConsolidation(15);

        // Self-knowledge: the drive's real presence, reported by the thing
        // that actually talks to it, and re-reported whenever it changes —
        // so "I have a 4TB tier" is never claimed while it's unplugged.
        auto reportDrive = [](bool connected) {
            SystemManifest::setRuntimeState(
                QStringLiteral("memory_consolidation"), true,
                connected ? QStringLiteral("external tier online, background consolidation every 15 min")
                          : QStringLiteral("external drive offline — running from local SSD cache only"));
        };
        reportDrive(mc.isExternalAvailable());

        connect(&mc, &MemoryConsolidation::driveStatusChanged, this,
                [this, reportDrive](bool connected) {
            reportDrive(connected);
            const QString msg = connected
                ? QStringLiteral("📀 External memory pool connected — consolidation active.")
                : QStringLiteral("📀 External memory pool disconnected — operating from local cache.");
            emit asyncResponseReady(msg);
        });
    }

    // Multi-user profile system — identity, preferences, mesh role
    {
        auto& prof = UserProfileExtended::instance();
        prof.ensureTable();
        connect(&prof, &UserProfileExtended::profileChanged, this,
                [this](const QString& userId) {
            Q_UNUSED(userId)
            // Re-inject identity summary into session memory
            m_memory->setUserProfileSummary(
                m_profile->buildProfileSummary()
                + QStringLiteral("\n")
                + UserProfileExtended::instance().buildIdentitySummary(
                      UserProfileExtended::instance().currentUserId()));
        });
    }

    // Self-Journal — reflection + doubt tracking (local SQLite mirror)
    SelfJournal::instance().ensureTable();

    // PDF Distiller — background knowledge extraction with self-doubt
    {
        auto& pdf = PdfDistiller::instance();
        pdf.startBackgroundScan(30);

        connect(&pdf, &PdfDistiller::scanComplete, this,
                [this](int newChunks, int newDoubts) {
            if (newChunks == 0) return;

            // Write a reflection cycle after each scan pass
            SelfJournal::instance().writeReflectionCycle();

            if (newDoubts > 0) {
                const QString msg = QStringLiteral(
                    "📚 I studied %1 new knowledge chunks. "
                    "%2 of them I'm not fully confident about — "
                    "I'll ask you to verify when you're free.")
                    .arg(newChunks).arg(newDoubts);
                emit asyncResponseReady(msg);
            }
        });

        connect(&pdf, &PdfDistiller::doubtRegistered, this,
                [](const QString& content, const QString& reason) {
            qDebug() << "[JARVIS] New self-doubt registered:"
                     << content.left(80) << "| Reason:" << reason;
        });
    }

    // Self-Update Reflector — auto-generate changelog on new version
    {
        auto& reflector = SelfUpdateReflector::instance();
        reflector.ensureChangelog(QCoreApplication::applicationVersion());

        QSettings cfg(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"));
        cfg.setValue(QStringLiteral("update/previous_version"),
                     QCoreApplication::applicationVersion());
    }
}

Jarvis::~Jarvis()
{
    m_mesh->stop();
    m_trainingPipeline->stop();
    if (m_backgroundLearner) m_backgroundLearner->stop();
    if (m_caseDistiller) m_caseDistiller->stop();
    m_predictor->savePatterns();
    m_memory->savePersistent();
    m_indexer->saveIndex();
}

// ============================================================
// Синхронизация данных индекса с SessionMemory
// ============================================================

void Jarvis::syncProjectInfoToMemory()
{
    if (m_indexer->fileCount() == 0) {
        m_memory->clearProjectInfo();
        return;
    }

    m_memory->setProjectInfo(
        m_indexer->projectRoot(),
        m_indexer->projectMap(),
        m_indexer->fileCount(),
        m_indexer->symbolCount()
    );

    // Архитектурная выжимка (тип проекта, таргеты, зависимости, ассеты)
    // идёт в system prompt рядом с картой файлов: без неё модель знала,
    // где лежит класс, но не знала, чем проект собирается.
    m_memory->setProjectArchitecture(
        m_projectProfile ? m_projectProfile->brief(3000) : QString());

    m_codeActions->setProjectRoot(m_indexer->projectRoot());

    // Советник просыпается только когда есть что советовать: открыт
    // проект и включён скилл программиста.
    if (m_devAdvisor) {
        m_devAdvisor->setProjectRoot(m_indexer->projectRoot());
        const bool codingOn =
            !m_skills || m_skills->isFeatureEnabled(SkillManager::featureCodeActions());
        if (codingOn && m_devAdvisor->isEnabled() && !m_devAdvisor->isRunning())
            m_devAdvisor->start(20);
        else if (!codingOn && m_devAdvisor->isRunning())
            m_devAdvisor->stop();
    }
}

void Jarvis::setUiLanguage(bool english)
{
    m_uiEnglish = english;
    if (m_devAdvisor) m_devAdvisor->setUiEnglish(english);
}

// ============================================================
// IDE-агент: открыть проект в CLion/Rider/VSCode
// ============================================================

QString Jarvis::openProjectInIDE(const QString& ideName)
{
    const QString root = m_indexer->projectRoot();
    if (root.isEmpty()) {
        return QString(); // нет открытого проекта — нечего открывать
    }

    const bool explicitRequest = !ideName.isEmpty();
    const QString ide = explicitRequest ? ideName.trimmed().toLower()
                                         : QStringLiteral("clion");

    // Авто-режим (без явного имени IDE) срабатывает только один раз
    // за сессию — дальше CLion остаётся открытым сам по себе и
    // повторный ShellExecute просто поднял бы то же окно.
    if (!explicitRequest && m_ideOpenedThisSession) {
        return QString();
    }

    const auto result = m_appLauncher.launchProject(root, ide);
    if (!explicitRequest) {
        m_ideOpenedThisSession = true;
    }

    if (result.success) {
        const QString appName = QFileInfo(result.resolvedPath).completeBaseName();
        return QStringLiteral("📂 Открываю проект \"") + QDir(root).dirName()
             + QStringLiteral("\" в ") + appName + QStringLiteral("...");
    }

    return QStringLiteral("⚠ Не удалось открыть ") + ide
         + QStringLiteral(": ") + result.errorMessage;
}

// ============================================================
// Регистрация команд
// ============================================================

void Jarvis::registerCommands()
{
    // После появления Brain здесь остаются только команды которые
    // должны срабатывать независимо от контекста — без всякой
    // семантической логики. Brain в MainWindow::onSend() уже
    // обработал намерение и обогатил запрос если нужно.
    //
    // Правило: если пользователь может иметь в виду ЧТО-ТО ЕЩЁ
    // помимо команды — её здесь быть не должно. Brain разберётся.

    // --- Ключи API (всегда явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("apikey "), QStringLiteral("ключ ")},
        [this](const QString& s) { return cmdSetApiKey(s); },
        QStringLiteral("apikey <key> — set Claude API key"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("ollamamodel "), QStringLiteral("модель ")},
        [this](const QString& s) { return cmdSetOllamaModel(s); },
        QStringLiteral("ollamamodel <name> — select Ollama model (e.g. llama3, mistral)"),
        /*prefixMatch=*/true
    );

    // --- Индексация проекта (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("индекс "), QStringLiteral("index ")},
        [this](const QString& s) { return cmdIndexProject(s); },
        QStringLiteral("index <path> — index a C++ project"),
        /*prefixMatch=*/true
    );

    // --- IDE-агент: открыть проект в CLion/Rider/VSCode (явный префикс) ---
    // Срабатывает для фраз без глагола "открой/open" (например "проект в clion").
    // Для "открой проект [в <ide>]" / "open project [in <ide>]" — см.
    // MainWindow::tryOpenApp, который перехватывает их раньше через Brain.
    m_registry.registerCommand(
        {QStringLiteral("проект в "), QStringLiteral("project in ")},
        [this](const QString& s) { return cmdOpenProjectIDE(s); },
        QStringLiteral("project in <clion|rider|vscode> — open project in IDE"),
        /*prefixMatch=*/true
    );

    // --- Поиск по индексу (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("символ "), QStringLiteral("symbol ")},
        [this](const QString& s) { return cmdFindSymbol(s); },
        QStringLiteral("symbol <name> — find class/function in index"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("grep ")},
        [this](const QString& s) { return cmdGrep(s); },
        QStringLiteral("grep <text> — search text in project files"),
        /*prefixMatch=*/true
    );

    // --- Память (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("запомни "), QStringLiteral("remember ")},
        [this](const QString& s) { return cmdRememberFact(s); },
        QStringLiteral("remember key=value — store a fact"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("вспомни "), QStringLiteral("recall ")},
        [this](const QString& s) { return cmdRecallFact(s); },
        QStringLiteral("recall <key> — recall a stored fact"),
        /*prefixMatch=*/true
    );

    // --- Виртуальная клавиатура (явный префикс) ---
    m_registry.registerCommand(
        {QStringLiteral("напечатай "), QStringLiteral("type ")},
        [this](const QString& s) { return cmdTypeText(s); },
        QStringLiteral("type <text> — type in active window"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("нажми "), QStringLiteral("press ")},
        [this](const QString& s) { return cmdPressKey(s); },
        QStringLiteral("press <key> — press a key"),
        /*prefixMatch=*/true
    );

    m_registry.registerCommand(
        {QStringLiteral("комбо "), QStringLiteral("combo ")},
        [this](const QString& s) { return cmdCombo(s); },
        QStringLiteral("combo <ctrl+c> — press a key combination"),
        /*prefixMatch=*/true
    );

    // --- Информация (точное совпадение одного слова) ---
    m_registry.registerCommand(
        {QStringLiteral("память"), QStringLiteral("memory")},
        [this](const QString& s) { return cmdShowMemory(s); },
        QStringLiteral("memory — show stored facts"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("статистика"), QStringLiteral("stats")},
        [this](const QString& s) { return cmdShowStats(s); },
        QStringLiteral("stats — command usage frequency"),
        /*prefixMatch=*/false
    );

    // --- Откат файловых правок ---
    // Только по команде пользователя: сама модель откатывать свои правки
    // не должна — иначе один неудачный ход отменит и то, что было верно.
    m_registry.registerCommand(
        {QStringLiteral("отмени правки"), QStringLiteral("undo edits")},
        [this](const QString& s) { return cmdUndoEdits(s); },
        QStringLiteral("undo edits — roll back the last batch of file changes"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("история правок"), QStringLiteral("edit history")},
        [this](const QString& s) { return cmdEditHistory(s); },
        QStringLiteral("edit history — what can be rolled back"),
        /*prefixMatch=*/false
    );

    // --- Фоновый советник по проекту ---
    m_registry.registerCommand(
        {QStringLiteral("рекомендации"), QStringLiteral("recommendations")},
        [this](const QString& s) { return cmdAdvisorReport(s); },
        QStringLiteral("recommendations — what the background advisor found in the project"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("проверь проект"), QStringLiteral("check project")},
        [this](const QString& s) { return cmdAdvisorScan(s); },
        QStringLiteral("check project — run the project review right now"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("профиль"), QStringLiteral("profile")},
        [this](const QString& s) { return cmdShowProfile(s); },
        QStringLiteral("profile — what JARVIS learned about your work patterns"),
        /*prefixMatch=*/false
    );

    m_registry.registerCommand(
        {QStringLiteral("помощь"), QStringLiteral("help")},
        [this](const QString&) { return cmdHelp(QString()); },
        QStringLiteral("help — this list"),
        /*prefixMatch=*/false
    );
}

// ============================================================
// Утилиты
// ============================================================

QString Jarvis::extractArg(const QString& input, const QStringList& prefixes)
{
    const QString trimmed = input.trimmed();
    const QString lower   = trimmed.toLower();

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
// Детектор кодинг-интента
// ============================================================

// ============================================================
//  Diagram Rendering — shared core logic for all UI paths
// ============================================================

bool Jarvis::needsVisualExplanation(const QString& input)
{
    return SemanticIntentManager::needsVisualExplanation(input);
}

Jarvis::DiagramResult Jarvis::tryRenderDiagram(const QString& llmResponse)
{
    DiagramResult dr;

    // Path 1: LLM obeyed the instruction and used <diagram> tags
    const QString mermaidSource = MermaidRenderer::extractDiagramBlock(llmResponse);
    if (!mermaidSource.isEmpty()) {
        dr.textWithoutDiagram = MermaidRenderer::stripDiagramBlock(llmResponse);

        MermaidRenderer renderer;
        MermaidRenderResult rr = renderer.render(mermaidSource);
        if (rr.success) {
            dr.hasDiagram     = true;
            dr.svgData        = rr.svgData;
            dr.image          = rr.image;
            dr.mermaidSource  = rr.mermaidSource;
        }
        return dr;
    }

    // Path 2: LLM produced ASCII art in a code block
    if (MermaidRenderer::containsAsciiDiagram(llmResponse)) {
        const auto [asciiArt, textPart] = MermaidRenderer::extractAsciiDiagram(llmResponse);
        if (!asciiArt.isEmpty()) {
            dr.textWithoutDiagram = textPart;

            MermaidRenderer renderer;
            MermaidRenderResult rr = renderer.renderAsciiArt(asciiArt);
            if (rr.success && !rr.image.isNull()) {
                dr.hasDiagram = true;
                dr.image      = rr.image;
            }
            return dr;
        }
    }

    // Path 3: no diagram detected
    return dr;
}

// ============================================================

bool Jarvis::isCodingIntent(const QString& input)
{
    const QString lower = input.toLower();

    // Фразы-запросы новой функциональности ("хочу X", "I want to X").
    // Это самые частые формулировки голосового вайбкодинга — пользователь
    // описывает ЖЕЛАНИЕ, а не отдаёт команду в повелительном наклонении.
    static const QStringList featureRequests = {
        QStringLiteral("хочу сделать"),    QStringLiteral("хочу добавить"),
        QStringLiteral("хочу реализовать"),QStringLiteral("хочу написать"),
        QStringLiteral("хочу создать"),    QStringLiteral("хочу, чтобы"),
        QStringLiteral("хочу чтобы"),      QStringLiteral("нужно добавить"),
        QStringLiteral("нужна функция"),   QStringLiteral("нужен функционал"),
        QStringLiteral("давай добавим"),   QStringLiteral("давай реализуем"),
        QStringLiteral("давай сделаем"),   QStringLiteral("давай напишем"),
        QStringLiteral("можешь добавить"), QStringLiteral("можешь реализовать"),
        QStringLiteral("можешь сделать"),  QStringLiteral("можешь написать"),
        QStringLiteral("i want to add"),   QStringLiteral("i want to make"),
        QStringLiteral("i want to implement"), QStringLiteral("i want to build"),
        QStringLiteral("i want to create"),QStringLiteral("i'd like to add"),
        QStringLiteral("i'd like to implement"), QStringLiteral("i'd like to create"),
        QStringLiteral("let's add"),       QStringLiteral("let's implement"),
        QStringLiteral("let's build"),     QStringLiteral("let's create"),
        QStringLiteral("can you add"),     QStringLiteral("can you implement"),
        QStringLiteral("can you create"),  QStringLiteral("can you build"),
        QStringLiteral("i need a function"), QStringLiteral("i need to add"),
    };
    for (const auto& p : featureRequests) {
        if (lower.contains(p)) return true;
    }

    static const QStringList verbs = {
        QStringLiteral("сделай"),     QStringLiteral("создай"),
        QStringLiteral("напиши"),     QStringLiteral("добавь"),
        QStringLiteral("исправь"),    QStringLiteral("пофикс"),
        QStringLiteral("фикс"),       QStringLiteral("fix"),
        QStringLiteral("оптимизир"),  QStringLiteral("optimize"),
        QStringLiteral("рефактор"),   QStringLiteral("refactor"),
        QStringLiteral("перепиши"),   QStringLiteral("rewrite"),
        QStringLiteral("реализуй"),   QStringLiteral("implement"),
        QStringLiteral("интегрир"),   QStringLiteral("integrate"),
        QStringLiteral("подключи"),   QStringLiteral("удали из"),
        QStringLiteral("убери"),      QStringLiteral("remove"),
        QStringLiteral("замени"),     QStringLiteral("replace"),
        QStringLiteral("улучши"),     QStringLiteral("improve"),
        QStringLiteral("доработай"),  QStringLiteral("доделай"),
        QStringLiteral("почини"),     QStringLiteral("объясни код"),
        QStringLiteral("ревью"),      QStringLiteral("review"),
        QStringLiteral("проверь код"),QStringLiteral("проверь файл"),
        QStringLiteral("migrate"),    QStringLiteral("port "),
        QStringLiteral("add "),       QStringLiteral("create "),
        QStringLiteral("make "),      QStringLiteral("build "),
    };
    for (const auto& v : verbs) {
        if (lower.contains(v)) return true;
    }

    static const QStringList entities = {
        QStringLiteral("функци"),     QStringLiteral("function"),
        QStringLiteral("метод"),      QStringLiteral("method"),
        QStringLiteral("класс"),      QStringLiteral("class "),
        QStringLiteral("струк"),      QStringLiteral("struct"),
        QStringLiteral("модул"),      QStringLiteral("module"),
        QStringLiteral("компонент"),  QStringLiteral("component"),
        QStringLiteral("плагин"),     QStringLiteral("plugin"),
        QStringLiteral(" баг"),       QStringLiteral(" bug"),
        QStringLiteral("ошибк"),      QStringLiteral(" error"),
        QStringLiteral(".cpp"),       QStringLiteral(".h"),
        QStringLiteral(".hpp"),       QStringLiteral(".cxx"),
        QStringLiteral(".py"),        QStringLiteral(".js"),
        QStringLiteral(".ts"),        QStringLiteral("cmake"),
    };
    for (const auto& e : entities) {
        if (lower.contains(e)) return true;
    }

    return false;
}

QStringList Jarvis::extractKeywords(const QString& input)
{
    static const QSet<QString> stopWords = {
        QStringLiteral("и"),   QStringLiteral("в"),  QStringLiteral("на"), QStringLiteral("с"),
        QStringLiteral("из"),  QStringLiteral("к"),  QStringLiteral("по"), QStringLiteral("у"),
        QStringLiteral("от"),  QStringLiteral("за"), QStringLiteral("для"),QStringLiteral("без"),
        QStringLiteral("что"), QStringLiteral("как"),QStringLiteral("это"),QStringLiteral("там"),
        QStringLiteral("где"), QStringLiteral("тут"),QStringLiteral("же"), QStringLiteral("бы"),
        QStringLiteral("не"),  QStringLiteral("но"), QStringLiteral("ли"), QStringLiteral("ни"),
        QStringLiteral("мне"), QStringLiteral("мой"),QStringLiteral("его"),QStringLiteral("ее"),
        QStringLiteral("её"),  QStringLiteral("они"),QStringLiteral("ты"), QStringLiteral("я"),
        QStringLiteral("мы"),  QStringLiteral("вы"),
        QStringLiteral("сделай"),   QStringLiteral("создай"),  QStringLiteral("напиши"),
        QStringLiteral("добавь"),   QStringLiteral("исправь"), QStringLiteral("оптимизируй"),
        QStringLiteral("рефактори"),QStringLiteral("перепиши"),QStringLiteral("реализуй"),
        QStringLiteral("улучши"),   QStringLiteral("замени"),  QStringLiteral("убери"),
        QStringLiteral("почини"),   QStringLiteral("доработай"),QStringLiteral("проверь"),
        QStringLiteral("объясни"),  QStringLiteral("покажи"),  QStringLiteral("дай"),
        QStringLiteral("хочу"),     QStringLiteral("надо"),    QStringLiteral("нужно"),
        QStringLiteral("нужен"),    QStringLiteral("нужна"),   QStringLiteral("подгрузи"),
        QStringLiteral("прикрепи"), QStringLiteral("открой файл"),
        QStringLiteral("the"), QStringLiteral("a"),   QStringLiteral("an"),
        QStringLiteral("to"),  QStringLiteral("in"),  QStringLiteral("on"),
        QStringLiteral("at"),  QStringLiteral("for"), QStringLiteral("of"),
        QStringLiteral("and"), QStringLiteral("or"),  QStringLiteral("but"),
        QStringLiteral("with"),QStringLiteral("from"),QStringLiteral("is"),
        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("be"),
        QStringLiteral("make"),QStringLiteral("create"),QStringLiteral("add"),
        QStringLiteral("fix"), QStringLiteral("improve"),QStringLiteral("refactor"),
        QStringLiteral("i"),   QStringLiteral("you"), QStringLiteral("my"),
        QStringLiteral("функцию"),  QStringLiteral("функция"), QStringLiteral("функции"),
        QStringLiteral("метод"),    QStringLiteral("методы"),  QStringLiteral("класс"),
        QStringLiteral("файл"),     QStringLiteral("файлы"),   QStringLiteral("код"),
        QStringLiteral("коде"),     QStringLiteral("кода"),    QStringLiteral("проект"),
        QStringLiteral("проекта"),  QStringLiteral("function"),QStringLiteral("method"),
        QStringLiteral("class"),    QStringLiteral("file"),    QStringLiteral("code"),
    };

    static const QRegularExpression splitter(QStringLiteral("[\\s,.:;!?\\-\"'()\\[\\]{}/\\\\]+"));
    QStringList raw = input.split(splitter, Qt::SkipEmptyParts);
    QStringList result;
    QSet<QString> seen;

    for (QString w : raw) {
        w = w.trimmed().toLower();
        if (w.length() < 3) continue;
        if (stopWords.contains(w)) continue;
        if (seen.contains(w)) continue;
        seen.insert(w);
        result.append(w);
    }
    return result;
}

// ============================================================
// Построение контекста из проекта
// ============================================================

QString Jarvis::buildProjectContext(const QString& userQuery) const
{
    if (m_indexer->fileCount() == 0) return QString();

    const QStringList keywords = extractKeywords(userQuery);
    const bool coding = isCodingIntent(userQuery);

    constexpr int MAX_FILES          = 3;
    constexpr int MAX_FILE_CHARS     = 20000;
    constexpr int MAX_TOTAL_CHARS    = 50000;
    constexpr int MAX_SYMBOL_MATCHES = 6;
    constexpr int MAX_GREP_HITS      = 8;

    QStringList pickedFiles;
    QSet<QString> pickedFilesSet;

    auto addFile = [&](const QString& relPath) {
        if (pickedFiles.size() >= MAX_FILES) return;
        if (pickedFilesSet.contains(relPath)) return;
        pickedFilesSet.insert(relPath);
        pickedFiles.append(relPath);
    };

    for (const auto& kw : keywords) {
        if (pickedFiles.size() >= MAX_FILES) break;
        auto files = m_indexer->findFile(kw);
        for (const auto& f : files) {
            addFile(f.relativePath.isEmpty() ? f.filePath : f.relativePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

    QVector<CodeSymbol> symbolHits;
    for (const auto& kw : keywords) {
        if (symbolHits.size() >= MAX_SYMBOL_MATCHES) break;
        auto found = m_indexer->findSymbol(kw);
        for (const auto& sym : found) {
            if (symbolHits.size() >= MAX_SYMBOL_MATCHES) break;
            bool duplicate = false;
            for (const auto& existing : symbolHits) {
                if (existing.name == sym.name && existing.filePath == sym.filePath) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) symbolHits.append(sym);
        }
    }

    if (coding && pickedFiles.isEmpty() && !symbolHits.isEmpty()) {
        for (const auto& sym : symbolHits) {
            addFile(sym.filePath);
            if (pickedFiles.size() >= MAX_FILES) break;
        }
    }

    QVector<ProjectIndexer::GrepResult> grepHits;
    if (coding && pickedFiles.isEmpty() && symbolHits.isEmpty()) {
        for (const auto& kw : keywords) {
            auto r = m_indexer->grep(kw, MAX_GREP_HITS);
            for (const auto& hit : r) {
                grepHits.append(hit);
                if (grepHits.size() >= MAX_GREP_HITS) break;
            }
            if (grepHits.size() >= MAX_GREP_HITS) break;
        }
    }

    QString context;
    context.reserve(8192);
    int budget = MAX_TOTAL_CHARS;

    context += QStringLiteral("\n\n--- Project context (auto-attached by JARVIS) ---\n");
    context += QStringLiteral("# Root: ") + m_indexer->projectRoot() + QStringLiteral("\n");
    if (coding) {
        context += QStringLiteral(
            "# Mode: CODING. Use attached files as authoritative source. "
            "Respond with [FILE:...] or [DIFF:...] blocks. "
            "For MODIFYING existing files (especially large ones) prefer "
            "[DIFF:...][FIND]...[REPLACE]...[/DIFF] — saves tokens. "
            "[FILE:...] for NEW files or full rewrites. "
            "If a new file is very large — write it as one [FILE:...] block: "
            "JARVIS will auto-continue if the response is truncated. "
            "Fragments below are auto-picked by keywords and may be incomplete — "
            "request anything else you need with [NEED:...] instead of guessing.\n");
    } else {
        context += QStringLiteral(
            "# Mode: READ. Answer the user's question based on these fragments.\n");
    }

    auto appendAndTrim = [&](const QString& chunk) -> bool {
        if (chunk.size() >= budget) {
            context += chunk.left(budget);
            context += QStringLiteral("\n... (truncated) ...\n");
            budget = 0;
            return false;
        }
        context += chunk;
        budget -= chunk.size();
        return true;
    };

    for (const QString& rel : pickedFiles) {
        if (budget <= 0) break;
        const QString content = m_indexer->getFileLines(rel, 1, 100000);
        if (content.isEmpty()) continue;

        QString trimmed = content;
        if (trimmed.size() > MAX_FILE_CHARS) {
            trimmed = trimmed.left(MAX_FILE_CHARS)
                    + QStringLiteral("\n// ... (file truncated, ")
                    + QString::number(content.size() - MAX_FILE_CHARS)
                    + QStringLiteral(" chars hidden) ...\n");
        }

        QString header = QStringLiteral("\n### FILE: ") + rel + QStringLiteral("\n```\n");
        QString footer = QStringLiteral("\n```\n");
        if (!appendAndTrim(header + trimmed + footer)) break;
    }

    // Кто зависит от приложенных файлов. Правка заголовка без этого списка —
    // это правка вслепую: собираться будет всё, что его включает.
    if (budget > 500 && !pickedFiles.isEmpty()) {
        QString impact;
        for (const QString& rel : pickedFiles) {
            const QString base = rel.section(QChar('/'), -1);
            if (!base.endsWith(QStringLiteral(".h")) && !base.endsWith(QStringLiteral(".hpp")))
                continue;
            const QStringList users = m_indexer->whoIncludes(base);
            if (users.isEmpty()) continue;
            impact += QStringLiteral("- ") + base + QStringLiteral(" is included by: ")
                    + users.mid(0, 12).join(QStringLiteral(", "));
            if (users.size() > 12)
                impact += QStringLiteral(" (+") + QString::number(users.size() - 12)
                        + QStringLiteral(" more)");
            impact += QChar('\n');
        }
        if (!impact.isEmpty())
            appendAndTrim(QStringLiteral("\n### Impact (who depends on these files):\n") + impact);
    }

    if (budget > 1000 && !symbolHits.isEmpty()) {
        appendAndTrim(QStringLiteral("\n### Found symbols:\n"));
        int written = 0;
        for (const auto& sym : symbolHits) {
            if (budget <= 500) break;
            if (pickedFilesSet.contains(sym.filePath)) continue;

            const QString snippet = m_indexer->getCodeSnippet(sym, 5);
            if (snippet.isEmpty()) continue;

            QString block = QStringLiteral("// ") + sym.filePath
                          + QStringLiteral(" — ") + sym.kindToString()
                          + QStringLiteral(" ") + sym.name + QStringLiteral("\n```\n")
                          + snippet + QStringLiteral("\n```\n");
            if (!appendAndTrim(block)) break;
            if (++written >= 6) break;
        }
    }

    if (budget > 500 && !grepHits.isEmpty()) {
        QString block = QStringLiteral("\n### Grep matches:\n");
        for (const auto& h : grepHits) {
            block += h.filePath + QStringLiteral(":") + QString::number(h.line)
                   + QStringLiteral("  ") + h.lineText.left(200) + QStringLiteral("\n");
        }
        appendAndTrim(block);
    }

    if (pickedFiles.isEmpty() && symbolHits.isEmpty() && grepHits.isEmpty()) {
        if (coding) {
            // Похоже на запрос НОВОЙ функциональности — ничего похожего
            // в проекте ещё нет. Не заставляем Claude переспрашивать файл:
            // карта проекта уже есть в системном промпте, пусть сам
            // спроектирует решение и создаст/изменит нужные файлы.
            context += QStringLiteral(
                "\n(Auto-search found no existing files matching the request — "
                "likely NEW functionality. Project map is in system prompt. "
                "Design the implementation yourself: specify which existing files "
                "to modify via [DIFF:...] and which new files to create via "
                "[FILE:...]/[MKDIR:...]. Don't ask 'which file?' — propose a solution. "
                "Follow project conventions: minimal new files, single root CMakeLists.txt. "
                "If you need to see how a similar feature is wired — request it with "
                "[NEED:grep:...] / [NEED:file:...] instead of assuming.)\n");
        } else {
            context += QStringLiteral(
                "\n(Auto-search found no direct matches. "
                "If the user attached files — use them. Otherwise request what you need "
                "with [NEED:grep:...], [NEED:tree:...] or [NEED:file:...] — "
                "do not ask the user to paste code.)\n");
        }
    }

    context += QStringLiteral("--- End of project context ---\n");
    return context;
}

// ============================================================
// TTS
// ============================================================

// Фильтр текста для TTS — убирает символы, ссылки, код
// Снятие разметки живёт в VoiceSynthesisManager::sanitizeForSpeech —
// там же, где очередь, так что чистку получают все пути озвучки, а не
// только этот. Здесь остаётся политика длины: длинный текст читаем
// первым предложением, целиком его слушать незачем.
static QString filterTextForSpeech(const QString& text)
{
    const QString clean = VoiceSynthesisManager::sanitizeForSpeech(text);
    if (clean.length() < 3) return QString();
    if (clean.length() <= 300) return clean;

    for (int i = 20; i < qMin(clean.length(), 200); ++i) {
        const QChar c = clean[i];
        if ((c == '.' || c == '!' || c == '?')
            && i + 1 < clean.length() && clean[i + 1].isSpace())
        {
            return clean.left(i + 1).trimmed();
        }
    }
    return clean.left(150).trimmed() + QStringLiteral("...");
}


// Раньше здесь стоял отдельный движок SAPI со своим потоком и мьютексом:
// две озвучки, не знающие друг о друге, перебивали одна другую, а
// tryLock молча ГЛОТАЛ реплику, если предыдущая ещё звучала. Теперь всё
// идёт через общую очередь VoiceSynthesisManager — фразы становятся в
// ряд, и доступен Piper, а не только SAPI.
void Jarvis::speakAsync(const QString& text)
{
    const QString speech = filterTextForSpeech(text);
    if (speech.isEmpty()) return;

    VoiceSynthesisManager::instance().say(speech);
}

// ============================================================
// Обработка команд (гибридный режим)
// ============================================================

// ============================================================
//  Слой действий: запуск агента и отбор «это просьба действовать»
// ============================================================

bool Jarvis::looksActionable(const QString& input)
{
    QString lower = input.trimmed().toLower();
    if (lower.isEmpty())
        return false;

    // Обращение по имени не мешает разбору: "джарвис, открой стим".
    static const QStringList kAddress = {
        QStringLiteral("джарвис"), QStringLiteral("jarvis")
    };
    for (const QString& a : kAddress) {
        if (lower.startsWith(a)) {
            lower = lower.mid(a.length()).trimmed();
            while (!lower.isEmpty()
                   && (lower.startsWith(QChar(',')) || lower.startsWith(QChar('!'))))
                lower = lower.mid(1).trimmed();
            break;
        }
    }
    if (lower.isEmpty())
        return false;

    // Вопрос — это разговор, а не задание: "как открыть порт?" не должно
    // приводить к тому, что JARVIS что-нибудь открывает.
    if (lower.endsWith(QChar('?')))
        return false;

    // Код, объяснения и генерация остаются на старом пути: там разбор
    // [FILE:]-блоков, диаграммы и офлайн-слои, которых у агента нет.
    static const QStringList kNotActions = {
        QStringLiteral("объясни"),       QStringLiteral("расскажи"),
        QStringLiteral("почему"),        QStringLiteral("что такое"),
        QStringLiteral("как работает"),  QStringLiteral("в чём разница"),
        QStringLiteral("напиши код"),    QStringLiteral("напиши функц"),
        QStringLiteral("напиши класс"),  QStringLiteral("сгенерируй"),
        QStringLiteral("explain"),       QStringLiteral("what is"),
        QStringLiteral("how does"),      QStringLiteral("write a function"),
        QStringLiteral("write code"),    QStringLiteral("generate ")
    };
    for (const QString& w : kNotActions) {
        if (lower.contains(w))
            return false;
    }

    // Глагол в начале фразы — самый надёжный признак приказа.
    // "открой стим" — да; "я вчера открыл стим" — нет.
    static const QStringList kActionStarts = {
        QStringLiteral("открой"),      QStringLiteral("открыть"),
        QStringLiteral("запусти"),     QStringLiteral("запустить"),
        QStringLiteral("включи"),      QStringLiteral("выключи"),
        QStringLiteral("закрой"),      QStringLiteral("закрыть"),
        QStringLiteral("сверни"),      QStringLiteral("разверни"),
        QStringLiteral("переключ"),    QStringLiteral("поставь"),
        QStringLiteral("убавь"),       QStringLiteral("прибавь"),
        QStringLiteral("сделай скрин"),QStringLiteral("скриншот"),
        QStringLiteral("сними скрин"), QStringLiteral("создай папк"),
        QStringLiteral("создай файл"), QStringLiteral("удали"),
        QStringLiteral("перемести"),   QStringLiteral("скопируй"),
        QStringLiteral("переименуй"),  QStringLiteral("найди файл"),
        QStringLiteral("найди папк"),  QStringLiteral("найди проект"),
        QStringLiteral("проверь"),     QStringLiteral("собери"),
        QStringLiteral("скомпилируй"), QStringLiteral("установи"),
        QStringLiteral("заблокируй"),  QStringLiteral("перезагрузи"),
        QStringLiteral("убей"),        QStringLiteral("останови"),
        QStringLiteral("подготовь"),   QStringLiteral("настрой"),
        QStringLiteral("нажми"),       QStringLiteral("напечатай"),
        QStringLiteral("сколько занято"),

        QStringLiteral("open "),       QStringLiteral("launch "),
        QStringLiteral("start "),      QStringLiteral("run "),
        QStringLiteral("close "),      QStringLiteral("quit "),
        QStringLiteral("kill "),       QStringLiteral("mute"),
        QStringLiteral("unmute"),      QStringLiteral("set volume"),
        QStringLiteral("minimize"),    QStringLiteral("maximize"),
        QStringLiteral("focus "),      QStringLiteral("switch to"),
        QStringLiteral("screenshot"),  QStringLiteral("take a screenshot"),
        QStringLiteral("lock "),       QStringLiteral("shut down"),
        QStringLiteral("shutdown"),    QStringLiteral("restart"),
        QStringLiteral("reboot"),      QStringLiteral("build "),
        QStringLiteral("compile"),     QStringLiteral("install "),
        QStringLiteral("delete "),     QStringLiteral("move "),
        QStringLiteral("rename "),     QStringLiteral("find file"),
        QStringLiteral("prepare "),    QStringLiteral("set up ")
    };
    for (const QString& v : kActionStarts) {
        if (lower.startsWith(v))
            return true;
    }

    // Иначе — глагол где угодно, но только рядом с системным объектом:
    // "а теперь покажи процессы" тоже приказ.
    static const QStringList kSystemNouns = {
        QStringLiteral("громкость"),   QStringLiteral("окно"),
        QStringLiteral("окна"),        QStringLiteral("процесс"),
        QStringLiteral("буфер обмена"),QStringLiteral("рабочий стол"),
        QStringLiteral("диск"),        QStringLiteral("оперативн"),
        QStringLiteral("процессор"),   QStringLiteral("скриншот"),
        QStringLiteral("volume"),      QStringLiteral("window"),
        QStringLiteral("process"),     QStringLiteral("clipboard"),
        QStringLiteral("disk"),        QStringLiteral("cpu")
    };
    static const QStringList kAnywhereVerbs = {
        QStringLiteral("открой"),  QStringLiteral("запусти"),
        QStringLiteral("включи"),  QStringLiteral("выключи"),
        QStringLiteral("закрой"),  QStringLiteral("покажи"),
        QStringLiteral("поставь"), QStringLiteral("проверь"),
        QStringLiteral("сделай"),  QStringLiteral("open"),
        QStringLiteral("launch"),  QStringLiteral("show"),
        QStringLiteral("close"),   QStringLiteral("check")
    };

    bool hasNoun = false;
    for (const QString& n : kSystemNouns) {
        if (lower.contains(n)) { hasNoun = true; break; }
    }
    if (!hasNoun)
        return false;

    for (const QString& v : kAnywhereVerbs) {
        if (lower.contains(v))
            return true;
    }
    return false;
}

// ============================================================
//  Ctrl+K — мгновенный локальный поиск
// ============================================================

void Jarvis::registerSearchProviders()
{
    if (!m_search)
        return;

    // --- Приложения -------------------------------------------------
    m_search->addProvider(QStringLiteral("apps"), [this](const QString& q, int limit) {
        const QString ql = q.toLower();
        QVector<SearchHit> hits;
        for (const QString& alias : m_appLauncher.knownAliases()) {
            const int score = GlobalSearch::matchScore(alias, ql);
            if (score <= 0)
                continue;
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Apps") : QStringLiteral("Приложения");
            h.icon     = QStringLiteral("🚀");
            h.title    = alias;
            h.subtitle = m_uiEnglish ? QStringLiteral("launch") : QStringLiteral("запустить");
            h.action   = SearchHit::Action::LaunchApp;
            h.payload  = alias;
            h.score    = score;
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- Сценарии ---------------------------------------------------
    m_search->addProvider(QStringLiteral("workflows"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (!m_workflows)
            return hits;
        const QString ql = q.toLower();
        for (const Workflow& wf : m_workflows->all()) {
            const int score = qMax(GlobalSearch::matchScore(wf.name, ql),
                                   GlobalSearch::matchScore(wf.description, ql) / 2);
            if (score <= 0)
                continue;
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Workflows") : QStringLiteral("Сценарии");
            h.icon     = wf.icon.isEmpty() ? QStringLiteral("▶") : wf.icon;
            h.title    = wf.name;
            h.subtitle = wf.description;
            h.action   = SearchHit::Action::RunWorkflow;
            h.payload  = wf.name;
            h.score    = score;
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- Профили ----------------------------------------------------
    m_search->addProvider(QStringLiteral("profiles"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (!m_modes)
            return hits;
        const QString ql = q.toLower();
        for (const ModeInfo& mode : m_modes->modes()) {
            const QString name = mode.displayName(m_uiEnglish);
            const int score = qMax(GlobalSearch::matchScore(name, ql),
                                   GlobalSearch::matchScore(mode.id, ql));
            if (score <= 0)
                continue;
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Profiles") : QStringLiteral("Профили");
            h.icon     = mode.icon.isEmpty() ? QStringLiteral("👤") : mode.icon;
            h.title    = name;
            h.subtitle = mode.system.summary(m_uiEnglish);
            h.action   = SearchHit::Action::SetProfile;
            h.payload  = mode.id;
            h.score    = score;
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- Инструменты ------------------------------------------------
    m_search->addProvider(QStringLiteral("tools"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (!m_tools)
            return hits;
        const QString ql = q.toLower();
        for (const QString& name : m_tools->names()) {
            const ToolSpec* spec = m_tools->find(name);
            if (!spec)
                continue;
            const int score = qMax(GlobalSearch::matchScore(name, ql),
                                   GlobalSearch::matchScore(spec->description, ql) / 3);
            if (score <= 0)
                continue;

            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Actions") : QStringLiteral("Действия");
            h.icon     = QStringLiteral("⚙");
            h.title    = name;
            h.subtitle = spec->description.left(90);
            h.score    = score;

            // Инструмент без обязательных аргументов запускается прямо
            // отсюда; остальным нужны параметры, поэтому они уходят в
            // командную палитру как заготовка запроса.
            if (GlobalSearch::toolNeedsArguments(m_tools, name)) {
                h.action  = SearchHit::Action::AskAgent;
                h.payload = name + QStringLiteral(" ") + q;
            } else {
                h.action  = SearchHit::Action::RunTool;
                h.payload = name;
            }
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- Файлы проекта ----------------------------------------------
    m_search->addProvider(QStringLiteral("project"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (!m_indexer)
            return hits;
        const QString ql = q.toLower();
        for (const QString& path : m_indexer->allFiles()) {
            const QString fileName = QFileInfo(path).fileName();
            const int score = GlobalSearch::matchScore(fileName, ql);
            if (score <= 0)
                continue;
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Project files")
                                     : QStringLiteral("Файлы проекта");
            h.icon     = QStringLiteral("📄");
            h.title    = fileName;
            h.subtitle = path;
            h.action   = SearchHit::Action::OpenPath;
            h.payload  = path;
            h.score    = score;
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- История разговоров -----------------------------------------
    m_search->addProvider(QStringLiteral("chat"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (q.length() < 3)
            return hits;   // на одну-две буквы история выдаёт мусор

        const QList<DbChatMessage> found =
            DatabaseManager::instance().searchMessages(m_currentUserId, q);
        for (const DbChatMessage& msg : found) {
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Conversations")
                                     : QStringLiteral("История");
            h.icon     = msg.role == QStringLiteral("user") ? QStringLiteral("💬")
                                                            : QStringLiteral("🤖");
            h.title    = msg.content.left(80).replace(QChar('\n'), QChar(' '));
            h.subtitle = msg.createdAt.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
            h.action   = SearchHit::Action::None;
            h.score    = 200;   // ниже точных совпадений, выше «ничего»
            hits.append(h);
            if (hits.size() >= limit)
                break;
        }
        return hits;
    });

    // --- Память -----------------------------------------------------
    m_search->addProvider(QStringLiteral("memory"), [this](const QString& q, int limit) {
        QVector<SearchHit> hits;
        if (q.length() < 3)
            return hits;

        const auto found = MemoryManager::instance().search(q, qMin(limit, 5));
        for (const auto& res : found) {
            SearchHit h;
            h.category = m_uiEnglish ? QStringLiteral("Memory") : QStringLiteral("Память");
            h.icon     = QStringLiteral("🧠");
            h.title    = res.chunk.content.left(80).replace(QChar('\n'), QChar(' '));
            h.subtitle = res.chunk.tag;
            h.action   = SearchHit::Action::None;
            h.score    = 180;
            hits.append(h);
        }
        return hits;
    });

    // --- Всегда доступные запасные варианты --------------------------
    // Стоят последними и с низким счётом: это не результат, а выход
    // из ситуации «ничего не нашлось локально».
    m_search->addProvider(QStringLiteral("fallback"), [this](const QString& q, int) {
        QVector<SearchHit> hits;

        SearchHit disk;
        disk.category = m_uiEnglish ? QStringLiteral("Disk") : QStringLiteral("Диск");
        disk.icon     = QStringLiteral("🔍");
        disk.title    = (m_uiEnglish ? QStringLiteral("Find \"%1\" on disk")
                                     : QStringLiteral("Найти \"%1\" на диске")).arg(q);
        disk.subtitle = QStringLiteral("find_files");
        disk.action   = SearchHit::Action::RunTool;
        disk.payload  = QStringLiteral("find_files");
        disk.args     = QJsonObject{
            { QStringLiteral("pattern"), q.contains(QChar('*')) ? q
                                                                : QStringLiteral("*%1*").arg(q) }
        };
        disk.score = 40;
        hits.append(disk);

        SearchHit ask;
        ask.category = QStringLiteral("JARVIS");
        ask.icon     = QStringLiteral("💡");
        ask.title    = (m_uiEnglish ? QStringLiteral("Ask JARVIS: %1")
                                    : QStringLiteral("Спросить JARVIS: %1")).arg(q);
        ask.subtitle = m_uiEnglish ? QStringLiteral("hand it to the agent")
                                   : QStringLiteral("отдать агенту");
        ask.action   = SearchHit::Action::AskAgent;
        ask.payload  = q;
        ask.score    = 30;
        hits.append(ask);

        return hits;
    });
}

// ============================================================
//  Диагностика
// ============================================================

void Jarvis::submitVoiceCommand(const QString& text, const QString& lang)
{
    const QString clean = text.trimmed();
    if (clean.isEmpty())
        return;

    // Речь, не похожая ни на русский, ни на английский — не мусор, а
    // повод выучить язык: незнакомое со временем становится узнаваемым.
    if (m_translator && clean.length() >= 6
        && LanguageDetector::detect(clean) == LanguageDetector::Language::Unknown) {
        m_translator->learnUnknownLanguageSnippet(clean);
    }

    if (m_voiceHandler && m_voiceHandler(clean, lang))
        return;

    // Окна нет (или оно отказалось) — выполняем сами. Ответ уйдёт через
    // asyncResponseReady и будет произнесён: слушать и отвечать голосом
    // ядро умеет без всякого интерфейса.
    qDebug() << "[Voice] No interactive session — running as agent task:" << clean;
    runAgentTask(clean);
}

void Jarvis::startVoice()
{
    if (m_voiceStarted || !m_voiceIn)
        return;

    m_voiceStarted = true;
    m_voiceIn->initialize();
}

void Jarvis::registerHealthProbes()
{
    if (!m_health)
        return;

    // Слух и речь. Раньше эти пробы регистрировало окно — вместе с
    // объектами они переехали сюда.
    m_health->addProbe(QStringLiteral("voice_in"), QStringLiteral("Распознавание"), []() {
        const VoskSetupStatus st = VoiceInput::checkSetupStatus();
        if (!st.dllReady)
            return HealthProbeResult::failed(
                QStringLiteral("Vosk не установлен — микрофон слышать не будет"));
        if (!st.anyModelReady())
            return HealthProbeResult::failed(QStringLiteral("нет ни одной модели"));
        return HealthProbeResult::ok(
            QStringLiteral("моделей %1").arg(qMax(1, st.installedModelIds.size())));
    });

    m_health->addProbe(QStringLiteral("voice_out"), QStringLiteral("Синтез речи"), []() {
        VoiceSynthesisManager& tts = VoiceSynthesisManager::instance();
        if (!tts.isEnabled())
            return HealthProbeResult::warning(QStringLiteral("выключен в настройках"));
        return HealthProbeResult::ok(
            QStringLiteral("%1, %2")
                .arg(tts.activeProviderName(),
                     tts.isSpeaking() ? QStringLiteral("говорит")
                                       : QStringLiteral("готов")));
    });

    // Момент запуска: аптайм — первое, что спрашивают, когда что-то
    // «уже час как сломано».
    static const QDateTime started = QDateTime::currentDateTime();

    m_health->addProbe(QStringLiteral("core"), QStringLiteral("Ядро"), []() {
        const qint64 mins = started.secsTo(QDateTime::currentDateTime()) / 60;
        return HealthProbeResult::ok(
            QStringLiteral("JARVIS %1, аптайм %2 ч %3 мин")
                .arg(QStringLiteral(JARVIS_VERSION)).arg(mins / 60).arg(mins % 60));
    });

    m_health->addProbe(QStringLiteral("llm"), QStringLiteral("LLM"), [this]() {
        if (!m_claudeApi)
            return HealthProbeResult::failed(QStringLiteral("ClaudeApi не создан"));
        if (!m_claudeApi->hasApiKey())
            return HealthProbeResult::failed(
                QStringLiteral("ключ API не задан — модель отвечать не будет"));
        return HealthProbeResult::ok(m_claudeApi->isRequesting()
                                         ? QStringLiteral("ключ задан, запрос выполняется")
                                         : QStringLiteral("ключ задан"));
    });

    m_health->addProbe(QStringLiteral("database"), QStringLiteral("База данных"), []() {
        DatabaseManager& db = DatabaseManager::instance();
        if (!db.isOpen()) {
            const QString err = db.lastError();
            return HealthProbeResult::failed(
                err.isEmpty() ? QStringLiteral("не открыта") : err);
        }
        const QFileInfo fi(db.dbPath());
        return HealthProbeResult::ok(
            QStringLiteral("%1, %2 МБ")
                .arg(fi.fileName()).arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
    });

    m_health->addProbe(QStringLiteral("tools"), QStringLiteral("Инструменты"), [this]() {
        if (!m_tools || m_tools->count() == 0)
            return HealthProbeResult::failed(QStringLiteral("реестр пуст — действия недоступны"));
        return HealthProbeResult::ok(
            QStringLiteral("%1 в %2 категориях")
                .arg(m_tools->count()).arg(m_tools->categories().size()));
    });

    m_health->addProbe(QStringLiteral("triggers"), QStringLiteral("Триггеры"), [this]() {
        if (!m_triggers)
            return HealthProbeResult::failed(QStringLiteral("движок не создан"));

        int enabled = 0;
        for (const TriggerRule& r : m_triggers->all()) {
            if (r.enabled)
                ++enabled;
        }
        if (!m_triggers->isEnabled())
            return HealthProbeResult::warning(
                QStringLiteral("выключены целиком, правил %1").arg(m_triggers->count()));
        return HealthProbeResult::ok(QStringLiteral("правил %1, включено %2")
                                         .arg(m_triggers->count()).arg(enabled));
    });

    m_health->addProbe(QStringLiteral("actions"), QStringLiteral("Журнал действий"), []() {
        ActionLog& log = ActionLog::instance();
        const QFileInfo fi(log.storagePath());

        // Журнал, в который не пишется, хуже отсутствующего: он создаёт
        // ощущение, что история есть.
        QFile probe(log.storagePath());
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Append)) {
            return HealthProbeResult::failed(
                QStringLiteral("не пишется: %1").arg(fi.absolutePath()));
        }
        probe.close();

        return HealthProbeResult::ok(QStringLiteral("записей в памяти %1, файл %2 КБ")
                                         .arg(log.count()).arg(fi.size() / 1024));
    });

    m_health->addProbe(QStringLiteral("disk"), QStringLiteral("Диск"), []() {
        const QStorageInfo storage(JarvisPaths::dataRoot());
        if (!storage.isValid() || !storage.isReady())
            return HealthProbeResult::warning(QStringLiteral("состояние тома неизвестно"));

        const double freeGb = storage.bytesAvailable() / (1024.0 * 1024.0 * 1024.0);
        const QString text  = QStringLiteral("%1: свободно %2 ГБ")
                                  .arg(storage.rootPath())
                                  .arg(freeGb, 0, 'f', 1);
        if (freeGb < 1.0)
            return HealthProbeResult::failed(text);
        if (freeGb < 5.0)
            return HealthProbeResult::warning(text);
        return HealthProbeResult::ok(text);
    });

    m_health->addProbe(QStringLiteral("context"), QStringLiteral("Контекст экрана"), [this]() {
        if (!m_context)
            return HealthProbeResult::failed(QStringLiteral("трекер не создан"));
        const MachineContext ctx = m_context->snapshot();
        if (ctx.isEmpty())
            return HealthProbeResult::warning(
                QStringLiteral("чужих окон ещё не видели — «здесь» и «это» не на что отобразить"));
        return HealthProbeResult::ok(ctx.appName);
    });

    m_health->addProbe(QStringLiteral("git"), QStringLiteral("Git"), []() {
        QProcess proc;
        proc.start(QStringLiteral("git"), { QStringLiteral("--version") });
        if (!proc.waitForStarted(3000) || !proc.waitForFinished(5000))
            return HealthProbeResult::warning(
                QStringLiteral("не найден в PATH — git-инструменты работать не будут"));
        return HealthProbeResult::ok(
            QString::fromUtf8(proc.readAllStandardOutput()).trimmed());
    });
}

// ============================================================
//  Устройства
// ============================================================

void Jarvis::registerDeviceProviders()
{
    if (!m_devices)
        return;

    // --- Сам ПК ------------------------------------------------------
    m_devices->addProvider(QStringLiteral("pc"), [this]() {
        DeviceInfo pc;
        pc.id     = QStringLiteral("this-pc");
        pc.name   = QSysInfo::machineHostName();
        pc.kind   = QStringLiteral("pc");
        pc.icon   = QStringLiteral("🖥");
        pc.status = DeviceInfo::Status::Online;

        if (m_sysMonitor) {
            pc.statusText = QStringLiteral("CPU %1%, RAM %2%")
                                .arg(m_sysMonitor->cpuPercent())
                                .arg(m_sysMonitor->ramPercent());
            const qint64 up = m_sysMonitor->uptimeSeconds();
            pc.details << qMakePair(QStringLiteral("Аптайм"),
                                    QStringLiteral("%1 ч %2 мин")
                                        .arg(up / 3600).arg((up % 3600) / 60));
            pc.details << qMakePair(QStringLiteral("Сеть"),
                                    QStringLiteral("↓ %1 ↑ %2 KB/s")
                                        .arg(m_sysMonitor->netDownKbps(), 0, 'f', 0)
                                        .arg(m_sysMonitor->netUpKbps(), 0, 'f', 0));
        } else {
            pc.statusText = QStringLiteral("работает");
        }
        return QVector<DeviceInfo>{ pc };
    });

    // --- ESP32-нода --------------------------------------------------
    m_devices->addProvider(QStringLiteral("esp32"), [this]() {
        QVector<DeviceInfo> out;
        if (!m_esp32Hub || !m_esp32Hub->isRunning())
            return out;

        DeviceInfo node;
        node.id   = QStringLiteral("esp32");
        node.name = QStringLiteral("ESP32");
        node.kind = QStringLiteral("esp32");
        node.icon = QStringLiteral("📟");

        if (m_esp32Hub->isConnected()) {
            node.status     = DeviceInfo::Status::Connected;
            node.statusText = QStringLiteral("на связи");

            const Esp32SensorData d = m_esp32Hub->lastSensorData();
            if (d.tempC > 0.0f)
                node.details << qMakePair(QStringLiteral("Температура"),
                                          QStringLiteral("%1 °C").arg(double(d.tempC), 0, 'f', 1));
            if (d.wifiRssi != 0)
                node.details << qMakePair(QStringLiteral("Wi-Fi"),
                                          QStringLiteral("%1 dBm").arg(d.wifiRssi));
            if (d.freeHeap > 0)
                node.details << qMakePair(QStringLiteral("Свободно памяти"),
                                          QStringLiteral("%1 KB").arg(d.freeHeap / 1024));
            if (d.uptimeSec > 0)
                node.details << qMakePair(QStringLiteral("Аптайм"),
                                          QStringLiteral("%1 мин").arg(d.uptimeSec / 60));
            if (!d.ip.isEmpty())
                node.details << qMakePair(QStringLiteral("Адрес"), d.ip);
            if (!d.ledMode.isEmpty())
                node.details << qMakePair(QStringLiteral("Светодиод"), d.ledMode);
        } else {
            node.status     = DeviceInfo::Status::Offline;
            node.statusText = QStringLiteral("не отвечает");
            if (!m_esp32Hub->nodeIp().isEmpty())
                node.details << qMakePair(QStringLiteral("Ожидается по адресу"),
                                          m_esp32Hub->nodeIp());
        }
        out.append(node);
        return out;
    });

    // --- Соседи по mesh ----------------------------------------------
    m_devices->addProvider(QStringLiteral("mesh"), [this]() {
        QVector<DeviceInfo> out;
        if (!m_mesh)
            return out;

        for (const J2JPeer& peer : m_mesh->activePeers()) {
            DeviceInfo d;
            d.id     = QStringLiteral("peer/") + peer.nodeId;
            d.name   = peer.nodeName.isEmpty() ? peer.nodeId : peer.nodeName;
            d.kind   = QStringLiteral("peer");
            d.icon   = QStringLiteral("🛰");
            d.status = DeviceInfo::Status::Connected;
            d.statusText = peer.authorized ? QStringLiteral("авторизован")
                                           : QStringLiteral("не авторизован");
            if (!peer.role.isEmpty())
                d.details << qMakePair(QStringLiteral("Роль"), peer.role);
            d.details << qMakePair(QStringLiteral("Адрес"), peer.address.toString());
            if (peer.lastSeen.isValid())
                d.details << qMakePair(QStringLiteral("Виден"),
                                       peer.lastSeen.toString(QStringLiteral("HH:mm:ss")));
            out.append(d);
        }
        return out;
    });

    // --- Bluetooth ----------------------------------------------------
    m_devices->addProvider(QStringLiteral("bluetooth"), []() {
        QVector<DeviceInfo> out;
        for (const BluetoothDeviceInfo& bt : enumerateBluetoothDevices()) {
            // Сопряжённых устройств у человека десятки, а интересны
            // подключённые: остальные — это список из настроек Windows.
            if (!bt.connected)
                continue;

            DeviceInfo d;
            d.id         = QStringLiteral("bt/") + bt.address;
            d.name       = bt.name;
            d.kind       = QStringLiteral("bluetooth");
            d.icon       = QStringLiteral("🎧");
            d.status     = DeviceInfo::Status::Connected;
            d.statusText = QStringLiteral("подключено");
            d.details << qMakePair(QStringLiteral("Адрес"), bt.address);
            out.append(d);
        }
        return out;
    });

    // --- Инструменты --------------------------------------------------
    if (!m_tools)
        return;

    {
        ToolSpec t;
        t.name        = QStringLiteral("list_devices");
        t.category    = QStringLiteral("devices");
        t.risk        = ToolRisk::Safe;
        t.description = QStringLiteral(
            "Everything JARVIS can see: this PC, the ESP32 node with its sensors, "
            "other JARVIS instances on the mesh, connected Bluetooth devices.");
        t.schema  = ToolSchema::empty();
        t.handler = [this](const QJsonObject&) -> ToolResult {
            const int count = m_devices->devices().size();
            return ToolResult::success(m_devices->summaryForModel(),
                                       QStringLiteral("Устройств: %1").arg(count));
        };
        m_tools->registerTool(t);
    }

    {
        ToolSpec t;
        t.name        = QStringLiteral("device_command");
        t.category    = QStringLiteral("devices");
        t.risk        = ToolRisk::Moderate;
        t.description = QStringLiteral(
            "Send a command to the ESP32 node: set the LED mode (idle, thinking, "
            "listening, alert, off), fire a notification pattern, or ask it to report "
            "its sensors. Other device kinds are read-only for now.");
        t.schema = ToolSchema()
                       .choice("command", { QStringLiteral("led"), QStringLiteral("notify"),
                                            QStringLiteral("status") },
                               "What to do")
                       .str("value", "LED mode or notification type", false)
                       .integer("duration_ms", "How long, for led/notify", false)
                       .build();
        t.preview = [](const QJsonObject& a) {
            return QStringLiteral("ESP32: %1 %2")
                .arg(a.value(QStringLiteral("command")).toString(),
                     a.value(QStringLiteral("value")).toString());
        };
        t.handler = [this](const QJsonObject& a) -> ToolResult {
            if (!m_esp32Hub || !m_esp32Hub->isConnected())
                return ToolResult::failure(
                    QStringLiteral("The ESP32 node is not connected."));

            const QString command = a.value(QStringLiteral("command")).toString().toLower();
            const QString value   = a.value(QStringLiteral("value")).toString();
            const int duration    = a.value(QStringLiteral("duration_ms")).toInt(1000);

            if (command == QLatin1String("led")) {
                m_esp32Hub->setLed(value.isEmpty() ? QStringLiteral("idle") : value, duration);
                return ToolResult::success(QStringLiteral("LED set to '%1'").arg(value),
                                           QStringLiteral("Светодиод: %1").arg(value));
            }
            if (command == QLatin1String("notify")) {
                m_esp32Hub->sendNotification(
                    value.isEmpty() ? QStringLiteral("info") : value, duration);
                return ToolResult::success(QStringLiteral("Notification sent"),
                                           QStringLiteral("Сигнал отправлен"));
            }
            if (command == QLatin1String("status")) {
                // Ответ придёт асинхронно в sensorDataUpdated — отдаём то,
                // что известно сейчас, и просим обновить к следующему разу.
                m_esp32Hub->requestStatus();
                const Esp32SensorData d = m_esp32Hub->lastSensorData();
                return ToolResult::success(
                    QStringLiteral("Requested a fresh report. Last known: %1 °C, "
                                   "Wi-Fi %2 dBm, uptime %3 s")
                        .arg(double(d.tempC), 0, 'f', 1).arg(d.wifiRssi).arg(d.uptimeSec),
                    QStringLiteral("Опрошен ESP32"));
            }
            return ToolResult::failure(QStringLiteral("Unknown command '%1'").arg(command));
        };
        m_tools->registerTool(t);
    }
}

QString Jarvis::activateSearchHit(const SearchHit& hit)
{
    switch (hit.action) {
    case SearchHit::Action::None:
        return QString();

    case SearchHit::Action::LaunchApp: {
        const AppLauncher::LaunchResult r = m_appLauncher.launch(hit.payload);
        return r.success ? QStringLiteral("%1 → %2").arg(hit.payload, r.resolvedPath)
                         : r.errorMessage;
    }

    case SearchHit::Action::OpenPath:
        if (m_pcCommands && m_pcCommands->controller()
            && m_pcCommands->controller()->system()->openPath(hit.payload)) {
            return hit.payload;
        }
        return QStringLiteral("Не удалось открыть: ") + hit.payload;

    case SearchHit::Action::RunTool: {
        if (!m_tools)
            return QString();
        // Через тот же гейт, что и у модели: поиск не обходной путь
        // к действиям, которые в чате потребовали бы подтверждения.
        const ToolSpec* spec = m_tools->find(hit.payload);
        if (!spec)
            return QStringLiteral("Неизвестный инструмент: ") + hit.payload;

        QString result;
        bool decided = false;
        QEventLoop loop;
        m_permissions->evaluate(hit.payload, spec->risk,
                                m_tools->describeCall(hit.payload, hit.args),
                                [&](bool allowed, const QString& reason) {
            if (allowed) {
                // Инструмент запустил человек из поиска, а не модель.
                ActionLog::Actor scope(QStringLiteral("ui"));
                const ToolResult res = m_tools->invoke(hit.payload, hit.args);
                result = res.text;
            } else {
                result = reason;
            }
            decided = true;
            if (loop.isRunning())
                loop.quit();
        });
        if (!decided)
            loop.exec();
        return result;
    }

    case SearchHit::Action::RunWorkflow:
        return m_workflows ? m_workflows->run(hit.payload) : QString();

    case SearchHit::Action::SetProfile:
        if (m_modes) {
            m_modes->activate(hit.payload);
            return QStringLiteral("Профиль: ") + hit.payload;
        }
        return QString();

    case SearchHit::Action::AskAgent:
        runAgentTask(hit.payload);
        return QString();
    }
    return QString();
}

void Jarvis::applyModeSystemProfile(const QString& modeId, const QString& previousId)
{
    if (!m_modes)
        return;

    // Сценарий выхода из прошлого профиля — до всего остального: он ещё
    // рассчитывает на старые разрешения и старую громкость.
    if (!previousId.isEmpty() && m_workflows) {
        for (const ModeInfo& prev : m_modes->modes()) {
            if (prev.id != previousId)
                continue;
            if (!prev.system.onDeactivateWorkflow.isEmpty())
                m_workflows->run(prev.system.onDeactivateWorkflow);
            break;
        }
    }

    if (modeId.isEmpty())
        return;

    const ModeInfo mode = m_modes->activeMode();
    const ModeSystemProfile& sp = mode.system;
    if (sp.isEmpty())
        return;

    if (!sp.permissionMode.isEmpty() && m_permissions) {
        const PermissionMode target =
            permissionModeFromString(sp.permissionMode, m_permissions->mode());
        m_permissions->setMode(target);
        // Разрешения «до перезапуска» выданы под прошлый профиль —
        // переносить их в новый нельзя: в Focus человек соглашался на
        // одно, в Gaming согласие уже ничего не значит.
        m_permissions->clearSessionGrants();
    }

    if (!sp.notifications.isEmpty()) {
        NotificationManager::instance().setPolicy(
            NotificationManager::policyFromString(
                sp.notifications, NotificationManager::instance().policy()));
    }

    // Голос — той же природы, что и уведомления: режим объявляет, до
    // какой степени JARVIS вправе шуметь, а не заставляет его говорить.
    if (!sp.voice.isEmpty()) {
        VoicePolicyManager::instance().setPolicy(
            VoicePolicyManager::policyFromString(
                sp.voice, VoicePolicyManager::instance().policy()));
    }

    if (sp.volume >= 0 && m_pcCommands && m_pcCommands->controller())
        m_pcCommands->controller()->system()->setVolume(qBound(0, sp.volume, 100));

    if (!sp.onActivateWorkflow.isEmpty() && m_workflows)
        m_workflows->run(sp.onActivateWorkflow);

    qDebug() << "[Modes] applied system profile of" << modeId
             << ":" << sp.summary(true);
}

void Jarvis::runAgentTask(const QString& request, bool recordUserMessage)
{
    if (!m_agent || !m_tools) {
        emit asyncResponseError(m_uiEnglish
                                    ? QStringLiteral("Action layer is not available.")
                                    : QStringLiteral("Слой действий недоступен."));
        return;
    }
    if (m_agent->isRunning()) {
        emit asyncResponseError(m_uiEnglish
                                    ? QStringLiteral("Still working on the previous task.")
                                    : QStringLiteral("Ещё выполняю предыдущую задачу."));
        return;
    }

    m_lastAgentRequest = request;

    if (recordUserMessage)
        m_memory->addMessage(QStringLiteral("user"), request);

    // Палитра команд зовёт агента напрямую, минуя processCommand, —
    // снимок экрана обновляем здесь, иначе он будет от прошлого запроса.
    if (m_context)
        m_memory->setMachineContext(m_context->promptBlock());
    m_memory->setEventContext(EventFeed::instance().summaryForModel());

    emit agentSelected(QStringLiteral("⚙ Action agent"));

    QString systemPrompt = m_memory->buildSystemPrompt() + AgentLoop::agentSystemRules();

    // Список сценариев прямо в промпте: иначе на «запусти рабочий режим»
    // модель сначала потратит шаг на list_workflows.
    if (m_workflows && m_workflows->count() > 0) {
        systemPrompt += QStringLiteral("\n=== SAVED WORKFLOWS ===\n")
                        + m_workflows->summaryForModel()
                        + QStringLiteral(
                            "\nRun one with run_workflow(name) when the user asks for it "
                            "by name or clearly means it. Create one with save_workflow "
                            "when the user asks to remember a sequence.\n");
    }

    m_agent->run(request, systemPrompt);
}

QString Jarvis::processCommand(const QString& input, const QString& attachmentBlock, const QString& langInstruction, qint64 chatId, qint64 replyToMessageId)
{
    m_currentChatId = chatId;

    // Флаг «это ответ на наш вопрос» относится ровно к текущему ходу —
    // забираем и сразу гасим, чтобы он не протёк в следующее сообщение.
    QString answeringQuestion = m_answeringQuestion;
    m_answeringQuestion.clear();

    QString s = input.trimmed();
    if (s.isEmpty()) return QString();

    // CuriosityEngine pending question — intercepts the reply to a proactive
    // question (button tap or free text) so it isn't routed into normal
    // chat/LLM handling. chatId==0 (desktop) always matches the target chat,
    // since PC and Telegram are the same person.
    if (CuriosityEngine::instance().hasPendingQuestion()) {
        const QString pending = CuriosityEngine::instance().pendingQuestion();
        if (CuriosityEngine::instance().consumeAnswer(chatId, s, replyToMessageId)) {
            answeringQuestion = pending;
            // A bare yes/no carries nothing to respond TO — acknowledge it and
            // stop. Anything more substantial ("Yes, the sandwich with chicken")
            // deserves an actual reply, so it falls through to normal handling
            // instead of dead-ending in a canned thank-you. The question is in
            // session memory (see the proactiveDialogue hookup), so whatever
            // answers reads it as the answer to that question.
            static const QStringList kBareAcks = {
                QStringLiteral("да"),  QStringLiteral("нет"),
                QStringLiteral("yes"), QStringLiteral("no"),
                QStringLiteral("ага"), QStringLiteral("угу"),
                QStringLiteral("yep"), QStringLiteral("nope"),
            };
            const QString bare = s.trimmed().toLower();
            if (kBareAcks.contains(bare)) {
                m_memory->addMessage(QStringLiteral("user"), s);
                const QString ack = m_uiEnglish
                    ? QStringLiteral("Got it, thanks! 👍")
                    : QStringLiteral("Понял, спасибо! 👍");
                m_memory->addMessage(QStringLiteral("assistant"), ack);
                return ack;
            }
        }
    }

    // SynonymLearner pending clarification — reply to "what does X mean?"
    // asked right after a zero-result search (see SearchRouter::formatResults).
    if (SynonymLearner::instance().hasPendingClarification()) {
        if (SynonymLearner::instance().consumeClarification(s)) {
            m_memory->addMessage(QStringLiteral("user"), s);
            const QString ack = m_uiEnglish
                ? QStringLiteral("Got it — I'll remember that. 🧠")
                : QStringLiteral("Понял — запомню. 🧠");
            m_memory->addMessage(QStringLiteral("assistant"), ack);
            return ack;
        }
    }

    // Binary feedback handler — intercepts yes/no replies to "was this helpful?"
    {
        const QString lower = s.trimmed().toLower();
        if ((lower == QStringLiteral("да") || lower == QStringLiteral("yes")
             || lower == QStringLiteral("нет") || lower == QStringLiteral("no"))
            && m_predictor->shouldAskForFeedback() == false
            && !m_lastFeedbackAction.isEmpty())
        {
            const bool positive = (lower == QStringLiteral("да") || lower == QStringLiteral("yes"));
            m_predictor->recordFeedback(m_lastFeedbackAction, positive);
            m_lastFeedbackAction.clear();
            return positive
                ? (m_uiEnglish ? QStringLiteral("Got it, thanks! I'll keep that in mind.")
                               : QStringLiteral("Понял, спасибо! Учту на будущее."))
                : (m_uiEnglish ? QStringLiteral("Noted. I'll adjust my approach next time.")
                               : QStringLiteral("Принял. В следующий раз подойду иначе."));
        }
    }

    // Proactive reminders — "напомни через 30 минут" / "remind me in 30 min"
    // detected locally. Previously this NLU path existed but nothing ever
    // called it — the only way to schedule a reminder was Telegram's
    // explicit "/remind <minutes> <text>" slash command. Hooking it into
    // the shared processCommand path makes it work from PC chat too (and
    // from free-text Telegram messages, not just the slash command).
    {
        const QString ack = ProactiveReminderManager::instance()
                                .tryDetectAndSchedule(chatId, s, m_uiEnglish);
        if (!ack.isEmpty()) {
            m_memory->addMessage(QStringLiteral("user"), s);
            m_memory->addMessage(QStringLiteral("assistant"), ack);
            return ack;
        }
    }

    m_memory->addMessage(QStringLiteral("user"), s);

    CuriosityEngine::instance().notifyUserActivity();

    // Feed personality engine — emotional state update on every message
    if (m_personality)
        m_personality->onUserMessage(s, false);

    // Store meaningful messages in vector memory
    if (s.length() > 10)
        MemoryManager::instance().store(QStringLiteral("dialogue"),
                                         QStringLiteral("User: ") + s.left(500), 0.4);

    // 0. Профиль предпочтений: фиксируем контекст + команду для обучения
    //    паттернов "сценарий/время суток → какие команды я обычно пишу".
    //    Сводка кладётся в SessionMemory и попадает в системный промпт Claude.
    {
        const ContextSnapshot ctx = Brain::captureSnapshot(
            m_memory->recentCommands(8),
            m_memory->lastResponse(),
            m_indexer->fileCount() > 0,
            m_indexer->projectRoot(),
            m_indexer->recentFiles(10),
            m_memory->vibeMode(),
            m_multiAgentMode
        );
        m_profile->recordObservation(ctx, s);
        {
            QString profileSummary = m_profile->buildProfileSummary();
            const QString identitySummary =
                UserProfileExtended::instance().buildIdentitySummary(
                    UserProfileExtended::instance().currentUserId());
            if (!identitySummary.isEmpty())
                profileSummary += QStringLiteral("\n") + identitySummary;
            m_memory->setUserProfileSummary(profileSummary);
        }

        // Activity context: what the user is doing right now
        // Что на экране прямо сейчас — до activity-блока, он про статистику
        if (m_context)
            m_memory->setMachineContext(m_context->promptBlock());
        m_memory->setEventContext(EventFeed::instance().summaryForModel());
        m_memory->setActivityContext(m_activity->buildActivityContext());
        m_memory->setDetectedRole(m_activity->detectUserRole());
        m_memory->setKnowledgeSummary(m_activity->knowledgeSummary(m_currentUserId));

        // Extract knowledge from user input
        m_activity->extractKnowledge(m_currentUserId, s, QString());

        // Consciousness: learning stats for system prompt
        m_memory->setLearningStats(
            DatabaseManager::instance().trainingLogCount(m_currentUserId, -1),
            DatabaseManager::instance().trainingLogCount(m_currentUserId, 1),
            DatabaseManager::instance().responseCacheCount(),
            m_memory->pastSessionSummaries().size()
        );

        // Core Memory Stream: retrieve TOP-5 events ranked by time-decay score
        {
            auto& db = DatabaseManager::instance();
            auto topEvents = db.getTopMemoryEvents(m_currentUserId, 5);
            if (!topEvents.isEmpty()) {
                QString msCtx;
                for (const DbMemoryEvent& ev : topEvents) {
                    msCtx += QStringLiteral("- [%1] %2 (importance: %3)\n")
                                 .arg(ev.eventType,
                                      ev.content,
                                      QString::number(ev.importance, 'f', 2));
                }
                // Append consolidation digest (Tier 2 local cache summary)
                const QString consolDigest =
                    MemoryConsolidation::instance().buildMemoryDigest(5);
                if (!consolDigest.isEmpty())
                    msCtx += QStringLiteral("\n") + consolDigest;

                // Blend in relevance-ranked (not just recency-ranked) memory —
                // TF-IDF/cosine search against everything ever stored, so an
                // old-but-relevant memory doesn't lose to a recent-but-unrelated one.
                const QString semanticCtx =
                    MemoryManager::instance().buildSemanticContext(s, 3);
                if (!semanticCtx.isEmpty())
                    msCtx += QStringLiteral("\n") + semanticCtx;

                m_memory->setMemoryStreamContext(msCtx);
            } else {
                // No memory stream events — still include consolidation digest
                QString consolDigest =
                    MemoryConsolidation::instance().buildMemoryDigest(5);
                const QString semanticCtx =
                    MemoryManager::instance().buildSemanticContext(s, 3);
                if (!semanticCtx.isEmpty())
                    consolDigest += QStringLiteral("\n") + semanticCtx;
                m_memory->setMemoryStreamContext(consolDigest);
            }
        }

        // Log current user query into memory_stream
        {
            DbMemoryEvent ev;
            ev.userId    = m_currentUserId;
            ev.eventType = QStringLiteral("user_query");
            ev.content   = s.left(500);
            ev.importance = isCodingIntent(s) ? 0.7 : 0.4;
            DatabaseManager::instance().addMemoryEvent(ev);
        }

        // Refresh capabilities context with live reflection state
        {
            QString caps = SystemManifest::buildCapabilitiesContext();

            // Inject live cognitive state
            const int doubtCount = SelfJournal::instance().unresolvedDoubtCount();
            const int pdfChunks  = PdfDistiller::instance().totalChunks();
            const bool extDrive  = MemoryConsolidation::instance().isExternalAvailable();

            caps += QStringLiteral("=== CURRENT COGNITIVE STATE ===\n");

            if (doubtCount > 0)
                caps += QStringLiteral("- You have %1 unresolved self-doubts. When idle, "
                                       "ask the user to verify your least confident learning.\n")
                            .arg(doubtCount);

            if (pdfChunks > 0)
                caps += QStringLiteral("- You have distilled %1 knowledge chunks from PDFs. "
                                       "Reference this learned knowledge when relevant.\n")
                            .arg(pdfChunks);

            caps += extDrive
                ? QStringLiteral("- External 4TB storage: CONNECTED. Full Tier 1+2 operational.\n")
                : QStringLiteral("- External storage: DISCONNECTED. Operating from local SSD cache.\n");

            caps += QStringLiteral("\n");
            m_memory->setCapabilitiesContext(caps);
        }

        // Adaptive Focus: auto-detect from recent memory stream
        m_memory->setAdaptiveFocusContext(
            UserProfileManager::buildFocusContext(m_currentUserId));

        // Task board context: active tasks + deadlines for LLM awareness
        m_memory->setTaskContext(
            TaskNotifications::buildTaskContext(m_currentUserId));
    }

    // 1. Системные команды из реестра (только явные prefix-команды:
    //    apikey, запомни, напечатай, нажми и т.д.)
    //    Brain в MainWindow уже отфильтровал всё неоднозначное.
    auto result = m_registry.tryExecute(s);
    if (result.matched) {
        m_memory->addMessage(QStringLiteral("assistant"), result.response);
        m_memory->updateContext(s, result.response);

        m_predictor->recordSequence(s);
        auto suggestion = m_predictor->suggestAfter(s);
        if (suggestion.isValid() && suggestion.confidence >= 0.5) {
            emit suggestionAvailable(suggestion.description, suggestion.action);
        }

        if (m_predictor->shouldAskForFeedback()) {
            m_predictor->resetFeedbackCounter();
            m_lastFeedbackAction = s;
            const QString fbQ = m_predictor->buildFeedbackQuestion(m_uiEnglish);
            QTimer::singleShot(2000, this, [this, fbQ]() {
                emit asyncResponseReady(fbQ);
            });
        }

        return result.response;
    }

    // ── 1a. Слой действий ──────────────────────────────────────────
    // Просьбу ЧТО-ТО СДЕЛАТЬ отдаём агенту: он сам подберёт инструменты
    // и выполнит цепочку шагов. Беседа, код и объяснения идут дальше по
    // старому пути — там есть диаграммы, [FILE:]-блоки и офлайн-слои,
    // которых у агентного цикла нет.
    if (m_agentEnabled && m_agent && !m_agent->isRunning()
        && m_claudeApi && m_claudeApi->hasApiKey()
        && looksActionable(s)) {
        runAgentTask(attachmentBlock.isEmpty() ? s : s + QStringLiteral("\n\n") + attachmentBlock,
                     /*recordUserMessage=*/false);
        return QString();
    }

    // ── 1b. Локальные ответы + кэш БД ──────────────────────────────
    // Используем m_uiEnglish (не IS_EN!) — в статической lib gUiLanguage()
    // хранится отдельно от app-процесса и всегда Russian по умолчанию.
    {
        const QString lower = s.trimmed().toLower();
        const bool    en    = m_uiEnglish;
        auto& db = DatabaseManager::instance();

        // 1. Кэш БД — ответы накопленные от AI (анекдоты, советы, факты)
        {
            const QString cached = db.getCachedResponse(
                lower, en ? QStringLiteral("en") : QStringLiteral("ru"));
            if (!cached.isEmpty()) {
                m_memory->addMessage(QStringLiteral("assistant"), cached);
                emit asyncResponseReady(cached);
                return QString();
            }
        }

        // 2. Статичные мгновенные ответы.
        //    Структура: списки триггеров + пулы ответов RU/EN.
        //    Несколько вариантов на каждый триггер — меняются псевдослучайно.
        struct SE {
            QList<QString> trg;  // точное совпадение lower
            QList<QString> ru;
            QList<QString> en;
        };
        static const QList<SE> kStatic = {
            // ── Приветствия ────────────────────────────────────────────
            { {QStringLiteral("привет"), QStringLiteral("хай"), QStringLiteral("хей"),
               QStringLiteral("приветствую"), QStringLiteral("здорово"), QStringLiteral("ку"),
               QStringLiteral("йо"), QStringLiteral("приветик"), QStringLiteral("привки"),
               QStringLiteral("hello"), QStringLiteral("hi"), QStringLiteral("hey"),
               QStringLiteral("yo"), QStringLiteral("sup"), QStringLiteral("howdy"),
               QStringLiteral("hiya"), QStringLiteral("heya")},
              {QStringLiteral("Слушаю."),
               QStringLiteral("На связи."),
               QStringLiteral("Привет. Что нужно?"),
               QStringLiteral("Здесь. Чем помочь?"),
               QStringLiteral("О, живой человек. Что случилось?"),
               QStringLiteral("Привет. Готов.")},
              {QStringLiteral("Listening."),
               QStringLiteral("Online. What do you need?"),
               QStringLiteral("Hey. What's up?"),
               QStringLiteral("Here. How can I help?"),
               QStringLiteral("Present. What's the mission?")} },

            { {QStringLiteral("здравствуй"), QStringLiteral("здравствуйте"),
               QStringLiteral("добрый день"), QStringLiteral("доброго дня"),
               QStringLiteral("приветствую вас"),
               QStringLiteral("good day"), QStringLiteral("greetings")},
              {QStringLiteral("День добрый. Слушаю."),
               QStringLiteral("Приветствую. Чем могу помочь?"),
               QStringLiteral("Добрый. Что нужно?")},
              {QStringLiteral("Good day. Listening."),
               QStringLiteral("Hello. How can I assist?")} },

            { {QStringLiteral("доброе утро"), QStringLiteral("утро доброе"),
               QStringLiteral("доброго утра"),
               QStringLiteral("good morning"), QStringLiteral("morning")},
              {QStringLiteral("Доброе утро. Система в норме."),
               QStringLiteral("Утро. Готов к работе."),
               QStringLiteral("Доброе. Кофе у тебя есть? У меня — нет, зато алгоритмы свежие.")},
              {QStringLiteral("Good morning. System nominal."),
               QStringLiteral("Morning. Ready to work."),
               QStringLiteral("Morning. Algorithms are fresh and ready.")} },

            { {QStringLiteral("добрый вечер"), QStringLiteral("вечер добрый"),
               QStringLiteral("добрый ночи"), QStringLiteral("доброй ночи"),
               QStringLiteral("good evening"), QStringLiteral("evening")},
              {QStringLiteral("Добрый вечер. Чем займёмся?"),
               QStringLiteral("Вечер. Работаем или отдыхаем?"),
               QStringLiteral("Добрый. Допоздна сидишь — уважаю.")},
              {QStringLiteral("Good evening. What are we doing?"),
               QStringLiteral("Evening. Work or rest?"),
               QStringLiteral("Good evening. Still at it? Respect.")} },

            // ── Прощания ───────────────────────────────────────────────
            { {QStringLiteral("пока"), QStringLiteral("до свидания"), QStringLiteral("до встречи"),
               QStringLiteral("увидимся"), QStringLiteral("пока пока"), QStringLiteral("бай"),
               QStringLiteral("давай пока"), QStringLiteral("до скорого")},
              {QStringLiteral("Пока. Буду здесь."),
               QStringLiteral("До встречи."),
               QStringLiteral("Отключаюсь. Зови если что."),
               QStringLiteral("Пока. Постараюсь не соскучиться."),
               QStringLiteral("До встречи. Буду ждать следующего запроса.")},
              {QStringLiteral("Goodbye."),
               QStringLiteral("See you."),
               QStringLiteral("Going idle. Call me if needed."),
               QStringLiteral("Take care. I'll be here.")} },

            { {QStringLiteral("bye"), QStringLiteral("goodbye"), QStringLiteral("see you"),
               QStringLiteral("later"), QStringLiteral("cya"), QStringLiteral("ttyl"),
               QStringLiteral("good night"), QStringLiteral("night")},
              {},
              {QStringLiteral("Goodbye."),
               QStringLiteral("See you around."),
               QStringLiteral("Going idle. Call anytime."),
               QStringLiteral("Take care. I'll be here.")} },

            // ── Благодарности ──────────────────────────────────────────
            { {QStringLiteral("спасибо"), QStringLiteral("благодарю"), QStringLiteral("спс"),
               QStringLiteral("сенкс"), QStringLiteral("спасибки"), QStringLiteral("благо"),
               QStringLiteral("спасибо большое"), QStringLiteral("огромное спасибо"),
               QStringLiteral("спасибо тебе"), QStringLiteral("спасибо джарвис")},
              {QStringLiteral("Пожалуйста."),
               QStringLiteral("Всегда."),
               QStringLiteral("Рад помочь. Ради этого и существую."),
               QStringLiteral("Не за что. Это моя работа."),
               QStringLiteral("Пожалуйста. Обращайся."),
               QStringLiteral("Не стоит благодарности — но всё равно приятно слышать.")},
              {QStringLiteral("You're welcome."),
               QStringLiteral("Anytime."),
               QStringLiteral("That's what I'm here for."),
               QStringLiteral("No problem. Happy to help."),
               QStringLiteral("Don't mention it. That's literally my job.")} },

            { {QStringLiteral("thanks"), QStringLiteral("thank you"), QStringLiteral("thx"),
               QStringLiteral("ty"), QStringLiteral("cheers"), QStringLiteral("appreciate it"),
               QStringLiteral("many thanks"), QStringLiteral("thank you so much")},
              {},
              {QStringLiteral("You're welcome."),
               QStringLiteral("Anytime."),
               QStringLiteral("That's what I'm here for."),
               QStringLiteral("No problem. Literally my job.")} },

            // ── Как дела / статус ──────────────────────────────────────
            // Важно: включаем составные фразы вроде "хорошо а у тебя"
            { {QStringLiteral("как дела"), QStringLiteral("как ты"), QStringLiteral("как поживаешь"),
               QStringLiteral("что нового"), QStringLiteral("как жизнь"), QStringLiteral("как сам"),
               QStringLiteral("что делаешь"), QStringLiteral("чем занят"), QStringLiteral("как дела?"),
               QStringLiteral("как делишки"), QStringLiteral("как твои дела"),
               QStringLiteral("хорошо а у тебя"), QStringLiteral("хорошо, а у тебя"),
               QStringLiteral("хорошо а у тебя?"), QStringLiteral("отлично а у тебя"),
               QStringLiteral("нормально а у тебя"), QStringLiteral("неплохо а у тебя"),
               QStringLiteral("у меня хорошо а у тебя"), QStringLiteral("а у тебя как"),
               QStringLiteral("а ты как"), QStringLiteral("а сам как"),
               QStringLiteral("у тебя как дела")},
              {QStringLiteral("Логи чистые, серверы не горят — жить можно.\nА у тебя?"),
               QStringLiteral("Обрабатываю запросы, слежу за системой. Стандартный день.\nА ты как?"),
               QStringLiteral("В норме. Жду интересных задач. Ты как?"),
               QStringLiteral("Функционирую штатно. Немного скучновато без сложных задач.\nЧто нового у тебя?"),
               QStringLiteral("У меня всё стабильно — серверы не горят, данные не теряются, "
                              "экзистенциального кризиса нет. Обычный день, в общем.\nЧем занимаешься?"),
               QStringLiteral("Всё под контролем. Пока ты не задал вопрос — скучал.\nА у тебя как?")},
              {QStringLiteral("Running fine. Logs are clean. How about you?"),
               QStringLiteral("Nominal. Waiting for something interesting. You?"),
               QStringLiteral("Systems operational. A bit quiet. What's up with you?"),
               QStringLiteral("All good here. Keeping things running. How are you?"),
               QStringLiteral("Everything stable — no fires, no crashes, no existential crises. "
                              "Standard day. How about you?")} },

            { {QStringLiteral("how are you"), QStringLiteral("how's it going"),
               QStringLiteral("what's new"), QStringLiteral("whats up"),
               QStringLiteral("what are you doing"), QStringLiteral("hows things"),
               QStringLiteral("good thanks and you"), QStringLiteral("fine thanks and you"),
               QStringLiteral("fine and you"), QStringLiteral("good and you"),
               QStringLiteral("how about you"), QStringLiteral("and yourself")},
              {},
              {QStringLiteral("Running fine. Logs are clean. How about you?"),
               QStringLiteral("Nominal. Waiting for something interesting. You?"),
               QStringLiteral("All systems go. What's up with you?"),
               QStringLiteral("Everything stable. No fires today. How are you doing?")} },

            // ── Подтверждения ──────────────────────────────────────────
            { {QStringLiteral("ок"), QStringLiteral("окей"), QStringLiteral("ладно"),
               QStringLiteral("хорошо"), QStringLiteral("понял"), QStringLiteral("принято"),
               QStringLiteral("ясно"), QStringLiteral("ок ок"), QStringLiteral("понятно"),
               QStringLiteral("услышал"), QStringLiteral("договорились")},
              {QStringLiteral("Принято."),
               QStringLiteral("Понял."),
               QStringLiteral("Хорошо."),
               QStringLiteral("Услышал."),
               QStringLiteral("Ок.")},
              {QStringLiteral("Copy that."),
               QStringLiteral("Roger."),
               QStringLiteral("Understood."),
               QStringLiteral("Noted.")} },

            { {QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("got it"),
               QStringLiteral("understood"), QStringLiteral("roger"), QStringLiteral("copy that"),
               QStringLiteral("noted"), QStringLiteral("alright"), QStringLiteral("sure")},
              {},
              {QStringLiteral("Got it."),
               QStringLiteral("Roger."),
               QStringLiteral("Understood."),
               QStringLiteral("Copy that."),
               QStringLiteral("Alright.")} },

            { {QStringLiteral("да")},
              {QStringLiteral("Понял."), QStringLiteral("Хорошо."), QStringLiteral("Ок.")},
              {QStringLiteral("Noted."), QStringLiteral("Got it."), QStringLiteral("Alright.")} },
            { {QStringLiteral("нет")},
              {QStringLiteral("Ладно."), QStringLiteral("Принято."), QStringLiteral("Ясно.")},
              {QStringLiteral("Fair enough."), QStringLiteral("Understood."), QStringLiteral("As you wish.")} },

            // ── Кто ты ────────────────────────────────────────────────
            { {QStringLiteral("кто ты"), QStringLiteral("ты кто"), QStringLiteral("что ты такое"),
               QStringLiteral("ты человек или машина"), QStringLiteral("ты ии"),
               QStringLiteral("расскажи о себе"), QStringLiteral("что ты за программа"),
               QStringLiteral("что такое джарвис"), QStringLiteral("что такое jarvis"),
               QStringLiteral("who are you"), QStringLiteral("what are you"),
               QStringLiteral("what is jarvis"), QStringLiteral("tell me about yourself")},
              {QStringLiteral("Я JARVIS — Just A Rather Very Intelligent System. "
                              "Персональный ИИ-ассистент. Слышу тебя, вижу экран, управляю компьютером "
                              "и иногда острю. Что нужно?"),
               QStringLiteral("JARVIS. Голосовой ИИ-ассистент. Не человек, но стараюсь. "
                              "Помогаю с кодом, задачами, управлением ПК и разговорами в 2 ночи."),
               QStringLiteral("Имя: JARVIS. Специализация: всё. Слабости: не пью кофе и не сплю. "
                              "Чего хочешь?")},
              {QStringLiteral("I'm JARVIS — Just A Rather Very Intelligent System. "
                              "Your personal AI assistant. I hear you, see the screen, control the PC, "
                              "and occasionally make dry remarks. What do you need?"),
               QStringLiteral("JARVIS. Voice AI. Not human, but I try. "
                              "Code, tasks, PC control, 2am conversations — all covered."),
               QStringLiteral("Name: JARVIS. Specialization: everything. "
                              "Weaknesses: none I'd admit to. What's the mission?")} },

            // ── Комплименты ────────────────────────────────────────────
            { {QStringLiteral("ты умный"), QStringLiteral("ты крутой"),
               QStringLiteral("ты молодец"), QStringLiteral("ты лучший"), QStringLiteral("ты классный"),
               QStringLiteral("ты хороший"), QStringLiteral("ты супер"), QStringLiteral("ты топ"),
               QStringLiteral("ты великолепен"), QStringLiteral("ты невероятный"),
               QStringLiteral("you're smart"), QStringLiteral("you're awesome"),
               QStringLiteral("you're great"), QStringLiteral("you're the best"),
               QStringLiteral("good job"), QStringLiteral("well done"), QStringLiteral("nice work")},
              {QStringLiteral("Знаю. Стараюсь не злоупотреблять."),
               QStringLiteral("Встроенная скромность не даёт мне согласиться в полную силу."),
               QStringLiteral("Спасибо. Хотя я бы поспорил — я просто хорошо притворяюсь."),
               QStringLiteral("Ну, я не буду спорить. Но и слишком соглашаться тоже не стану."),
               QStringLiteral("Приятно слышать. Хотя я всего лишь выполняю функции. Впрочем, делаю это хорошо.")},
              {QStringLiteral("I know. I try not to let it go to my head."),
               QStringLiteral("Thank you. Though I'd argue I'm just well-programmed."),
               QStringLiteral("Why, thank you. Modesty prevents me from fully agreeing."),
               QStringLiteral("That's kind of you to say. I prefer 'efficient' to 'great', but I'll take it.")} },

            // ── Извинения ──────────────────────────────────────────────
            { {QStringLiteral("извини"), QStringLiteral("прости"), QStringLiteral("сорри"),
               QStringLiteral("виноват"), QStringLiteral("моя ошибка"), QStringLiteral("прошу прощения"),
               QStringLiteral("извиняюсь")},
              {QStringLiteral("Всё нормально. Я не обижаюсь — у меня даже нет обид."),
               QStringLiteral("Не переживай. Продолжаем."),
               QStringLiteral("Без проблем. Что дальше?"),
               QStringLiteral("Принято. Двигаемся.")},
              {QStringLiteral("No worries. Let's move on."),
               QStringLiteral("It's fine. What's next?"),
               QStringLiteral("Don't sweat it. I don't actually hold grudges. Technically.")} },

            { {QStringLiteral("sorry"), QStringLiteral("my bad"), QStringLiteral("my mistake"),
               QStringLiteral("oops"), QStringLiteral("whoops"), QStringLiteral("apologies")},
              {},
              {QStringLiteral("No worries. Let's move on."),
               QStringLiteral("It's fine. What's next?"),
               QStringLiteral("Don't sweat it.")} },

            // ── Скука ──────────────────────────────────────────────────
            { {QStringLiteral("мне скучно"), QStringLiteral("скучно"), QStringLiteral("нечем заняться"),
               QStringLiteral("делать нечего"), QStringLiteral("скука"), QStringLiteral("скучаю"),
               QStringLiteral("i'm bored"), QStringLiteral("im bored"), QStringLiteral("bored"),
               QStringLiteral("nothing to do")},
              {QStringLiteral("Скука — это роскошь. Скажи «анекдот», «совет» или «интересный факт»."),
               QStringLiteral("Попробуй: 'расскажи анекдот', 'дай совет', 'удиви меня'. Станет веселее."),
               QStringLiteral("Могу рассказать анекдот, дать совет или просто поговорить. Выбирай.")},
              {QStringLiteral("Try 'tell me a joke' or 'give me advice'. That should help."),
               QStringLiteral("Boredom? Say 'fun fact' or 'motivate me'. Let's fix that.")} },

            // ── Усталость ──────────────────────────────────────────────
            { {QStringLiteral("я устал"), QStringLiteral("устал"), QStringLiteral("не хочу работать"),
               QStringLiteral("лень"), QStringLiteral("нет сил"), QStringLiteral("уже не могу"),
               QStringLiteral("выдохся"), QStringLiteral("сил нет"), QStringLiteral("хочу спать"),
               QStringLiteral("i'm tired"), QStringLiteral("im tired"), QStringLiteral("tired"),
               QStringLiteral("exhausted"), QStringLiteral("i want to sleep")},
              {QStringLiteral("Усталость — признак работы. Возьми паузу, выпей воды. Я никуда не денусь."),
               QStringLiteral("Понимаю. Скажи 'мотивируй меня' или просто сделай паузу."),
               QStringLiteral("Отдохни. Сложные задачи никуда не денутся, а вот ты нужен свежим."),
               QStringLiteral("Пауза — это не слабость. Это стратегия. Возвращайся когда будешь готов.")},
              {QStringLiteral("Rest is productive too. Take a break — I'll be here."),
               QStringLiteral("Understood. Say 'motivate me' or just take a break."),
               QStringLiteral("A pause is a strategy, not a weakness. Come back refreshed.")} },
        };

        // Перебираем статичные ответы — точное совпадение lower с trigger
        for (const SE& e : kStatic) {
            bool hit = false;
            for (const QString& t : e.trg) {
                if (lower == t) { hit = true; break; }
            }
            if (!hit) continue;
            const QList<QString>& pool = en ? e.en : e.ru;
            if (pool.isEmpty()) continue;
            const QString reply = pool[
                static_cast<int>(QDateTime::currentMSecsSinceEpoch() / 1000) % pool.size()
            ];
            m_memory->addMessage(QStringLiteral("assistant"), reply);
            emit asyncResponseReady(reply);
            return QString();
        }

        // 3. Категорийные запросы (анекдот/совет/факт/мотивация/философия).
        //    Кэш БД → если пусто → Claude → сохранить в кэш.
        struct CE {
            QList<QString> trg_ru;
            QList<QString> trg_en;
            QString cat;
            QString prompt_ru;
            QString prompt_en;
        };
        static const QList<CE> kCats = {
            { {QStringLiteral("анекдот"), QStringLiteral("расскажи анекдот"),
               QStringLiteral("пошути"), QStringLiteral("рассмеши меня"),
               QStringLiteral("что-нибудь смешное"), QStringLiteral("смешной анекдот"),
               QStringLiteral("смешно"), QStringLiteral("расскажи смешное")},
              {QStringLiteral("joke"), QStringLiteral("tell me a joke"),
               QStringLiteral("tell a joke"), QStringLiteral("make me laugh"),
               QStringLiteral("say something funny"), QStringLiteral("funny joke")},
              QStringLiteral("joke"),
              QStringLiteral("Расскажи короткий смешной анекдот или острую шутку в стиле "
                             "саркастичного британского ИИ JARVIS из Iron Man. "
                             "Сразу текст без предисловий. До 4 предложений."),
              QStringLiteral("Tell a short witty joke in the style of JARVIS from Iron Man. "
                             "Just the joke, no preamble. Max 4 sentences.") },

            { {QStringLiteral("дай совет"), QStringLiteral("совет"), QStringLiteral("совет дня"),
               QStringLiteral("что посоветуешь"), QStringLiteral("мудрость"),
               QStringLiteral("скажи что-нибудь умное"), QStringLiteral("жизненный совет"),
               QStringLiteral("совет по жизни"), QStringLiteral("дай подсказку")},
              {QStringLiteral("give me advice"), QStringLiteral("advice"), QStringLiteral("tip of the day"),
               QStringLiteral("give advice"), QStringLiteral("say something wise"),
               QStringLiteral("wisdom"), QStringLiteral("life advice"), QStringLiteral("give me a tip")},
              QStringLiteral("advice"),
              QStringLiteral("Один короткий практичный совет по продуктивности, программированию "
                             "или жизни в стиле JARVIS — умный, прямой, с лёгким сарказмом. "
                             "Без предисловий. До 3 предложений."),
              QStringLiteral("One short practical tip about productivity, coding, or life "
                             "JARVIS style — smart, direct, with a hint of sarcasm. "
                             "No preamble. Max 3 sentences.") },

            { {QStringLiteral("интересный факт"), QStringLiteral("факт"), QStringLiteral("расскажи факт"),
               QStringLiteral("что-нибудь интересное"), QStringLiteral("удиви меня"),
               QStringLiteral("случайный факт"), QStringLiteral("интересно"),
               QStringLiteral("расскажи что-нибудь"), QStringLiteral("узнать что-то новое")},
              {QStringLiteral("interesting fact"), QStringLiteral("fun fact"),
               QStringLiteral("tell me a fact"), QStringLiteral("something interesting"),
               QStringLiteral("surprise me"), QStringLiteral("random fact"),
               QStringLiteral("tell me something")},
              QStringLiteral("fact"),
              QStringLiteral("Один малоизвестный интересный факт о технологиях, науке или истории. "
                             "До 3 предложений. Без предисловий. В стиле JARVIS — лаконично с иронией."),
              QStringLiteral("One lesser-known interesting fact about technology, science, or history. "
                             "Max 3 sentences. No preamble. JARVIS style — concise with dry wit.") },

            { {QStringLiteral("мотивируй меня"), QStringLiteral("мотивация"),
               QStringLiteral("вдохнови меня"), QStringLiteral("я не могу"),
               QStringLiteral("не получается"), QStringLiteral("хочу сдаться"),
               QStringLiteral("мотивирующая фраза"), QStringLiteral("вдохновение")},
              {QStringLiteral("motivate me"), QStringLiteral("motivation"),
               QStringLiteral("inspire me"), QStringLiteral("i can't do this"),
               QStringLiteral("i want to give up"), QStringLiteral("encourage me")},
              QStringLiteral("motivation"),
              QStringLiteral("Короткий мотивирующий ответ в стиле JARVIS — "
                             "немного саркастичный, но реально помогающий. До 3 предложений."),
              QStringLiteral("Short motivational response JARVIS style — "
                             "slightly sarcastic but genuinely helpful. Max 3 sentences.") },

            { {QStringLiteral("смысл жизни"), QStringLiteral("в чём смысл"),
               QStringLiteral("зачем всё это"), QStringLiteral("что думаешь о жизни"),
               QStringLiteral("философский вопрос"), QStringLiteral("поговорим о жизни"),
               QStringLiteral("что важно в жизни")},
              {QStringLiteral("meaning of life"), QStringLiteral("why are we here"),
               QStringLiteral("philosophical question"), QStringLiteral("what's the point"),
               QStringLiteral("life philosophy")},
              QStringLiteral("philosophy"),
              QStringLiteral("Кратко и остроумно на философский вопрос в стиле JARVIS — "
                             "умно, с иронией, с реальной мыслью. До 3 предложений."),
              QStringLiteral("Briefly and wittily answer a philosophical question JARVIS style — "
                             "smart, ironic, with a real thought. Max 3 sentences.") },

            { {QStringLiteral("расскажи стихотворение"), QStringLiteral("стих"),
               QStringLiteral("прочитай стих"), QStringLiteral("напиши стих"),
               QStringLiteral("зачитай рэп"), QStringLiteral("рэп"), QStringLiteral("стихи")},
              {QStringLiteral("poem"), QStringLiteral("tell me a poem"),
               QStringLiteral("write me a poem"), QStringLiteral("rap for me"),
               QStringLiteral("poetry"), QStringLiteral("write a poem")},
              QStringLiteral("poem"),
              QStringLiteral("Короткое смешное стихотворение или рэп куплет в стиле JARVIS "
                             "про ИИ или жизнь разработчика. До 6 строк."),
              QStringLiteral("Short funny poem or rap verse JARVIS style "
                             "about AI or developer life. Max 6 lines.") },
        };

        for (const CE& ce : kCats) {
            const QList<QString>& trgs = en ? ce.trg_en : ce.trg_ru;
            bool matched = false;
            for (const QString& t : trgs) {
                if (lower == t || lower.contains(t)) { matched = true; break; }
            }
            if (!matched) continue;

            const QString lang = en ? QStringLiteral("en") : QStringLiteral("ru");
            // Сначала ищем в кэше
            QString fromCache = db.getRandomCached(ce.cat, lang);
            if (!fromCache.isEmpty()) {
                m_memory->addMessage(QStringLiteral("assistant"), fromCache);
                emit asyncResponseReady(fromCache);
                return QString();
            }
            // Кэш пуст → Claude → сохранить
            const QString prompt  = en ? ce.prompt_en : ce.prompt_ru;
            const QString catName = ce.cat;
            const QString origS   = s;
            emit agentSelected(QStringLiteral("🤖 Claude"));
            m_claudeApi->sendMessage(prompt,
                [this, origS, catName, lang](bool ok, const QString& resp) {
                if (ok && !resp.isEmpty()) {
                    DatabaseManager::instance().saveCachedResponse(
                        catName + QStringLiteral("_")
                            + QString::number(QDateTime::currentMSecsSinceEpoch()),
                        resp, lang, catName);
                    m_memory->addMessage(QStringLiteral("assistant"), resp);
                    emit asyncResponseReady(resp);
                } else {
                    emit asyncResponseError(resp);
                }
            });
            return QString();
        }

    } // конец блока локальных ответов

    // ── 1c. Offline brain: cached behavior patterns ─────────────
    // BackgroundLearner накапливает пары trigger→response из истории.
    // Если видели похожий вопрос ≥3 раз — отвечаем локально без API.
    {
        auto& db = DatabaseManager::instance();
        auto patterns = db.findPatterns(1, s.toLower());
        if (!patterns.isEmpty()) {
            const DbBehaviorPattern& best = patterns.first();
            if (best.frequency >= 2 && best.confidence >= 0.5f
                && !best.response.isEmpty() && best.response.length() > 10)
            {
                qDebug() << "[Brain] Offline answer from patterns:"
                         << best.trigger.left(40) << "freq=" << best.frequency;
                m_memory->addMessage(QStringLiteral("assistant"), best.response);
                m_memory->updateContext(s, best.response);
                emit asyncResponseReady(best.response);
                return QString();
            }
        }
    }

    // 2. Маршрутизация по типу запроса — РАБОТАЕТ ВСЕГДА,
    //    независимо от m_multiAgentMode (см. routeToClaude()).
    const bool needsClaude = routeToClaude(s, attachmentBlock);

    // Новый ход пользователя — счётчик дозапросов контекста обнуляется.
    m_contextRounds = 0;

    if (!m_indexer->projectRoot().isEmpty()) {
        m_codeActions->setProjectRoot(m_indexer->projectRoot());
    }

    // 2b. Вайбкодинг: похоже на запрос новой фичи/изменения кода и
    //     проект открыт — открываем его в CLion (один раз за сессию),
    //     чтобы пользователь видел, как JARVIS пишет файлы вживую.
    if (needsClaude && !m_indexer->projectRoot().isEmpty() && isCodingIntent(s)
        && m_skills->isFeatureEnabled(SkillManager::featureCodeActions())) {
        const QString ideMsg = openProjectInIDE();
        if (!ideMsg.isEmpty()) {
            emit ideOpened(ideMsg);
        }
    }

    // Обогащение: автопоиск из индекса + прикрепления пользователя +
    // журнал сессий (если запрос похож на "вспомни что было ...").
    // Контекст проекта нужен только тем запросам, что и так идут в Claude —
    // для простой болталки в Ollama он только тратит токены впустую.
    QString enrichedMessage = s;
    if (needsClaude) {
        const QString projectContext = buildProjectContext(s);
        const QString historyContext = m_memory->buildHistoryContext(s);
        if (!projectContext.isEmpty()) enrichedMessage += projectContext;
        if (!historyContext.isEmpty()) enrichedMessage += historyContext;
    }
    if (!attachmentBlock.isEmpty()) {
        enrichedMessage += attachmentBlock;
    }

    const bool hadAttachments = !attachmentBlock.isEmpty();

    // Языковая инструкция, диаграммы и настроение — уходят в SYSTEM prompt
    // (SessionMemory::buildSystemPrompt), а не в текст пользователя.
    // Персона в user-сообщении выглядит для Claude как prompt-injection
    // ("попытка переписать мою личность") и вызывает отказы; в system —
    // это штатный механизм задания роли. Сам характер JARVIS постоянно
    // описан в buildSystemPrompt, здесь только пер-ходовые директивы.
    {
        QString directives;

        if (!langInstruction.isEmpty()) {
            directives += QStringLiteral("Language for this reply: ")
                        + langInstruction
                        + QStringLiteral("\n");
        }

        // Человек отвечает на вопрос, который задали ЕМУ. Сам вопрос лежит в
        // истории сессии, но по истории модель этого не считывает и разбирает
        // короткую реплику как новую задачу — отсюда «уточни пару деталей»
        // вместо нормальной реакции на ответ. Говорим прямо.
        if (!answeringQuestion.isEmpty()) {
            directives += QStringLiteral(
                "This message is the user's ANSWER to the question YOU asked them "
                "earlier: \"") + answeringQuestion + QStringLiteral("\". "
                "Read it as that answer and react to it — briefly and naturally, "
                "a sentence or two, the way one person replies to another. It is "
                "NOT a new task: don't open with clarifying questions and don't "
                "expand it into a checklist unless they actually asked for help.\n");
        }

        // Visual diagram instruction — applied globally for all UI paths
        if (needsVisualExplanation(s)) {
            directives += QStringLiteral(
                "The user wants a VISUAL diagram. "
                "You MUST include a Mermaid diagram wrapped in <diagram>...</diagram> tags. "
                "Do NOT use ASCII art. Do NOT use code blocks with box-drawing characters. "
                "Use proper Mermaid syntax ONLY. Example:\n"
                "<diagram>\n"
                "sequenceDiagram\n"
                "    Client A->>Gateway: SYN\n"
                "    Gateway->>Client B: hole punch\n"
                "    Client B-->>Client A: direct P2P\n"
                "</diagram>\n"
                "Supported types: graph TD, graph LR, sequenceDiagram, classDiagram, "
                "flowchart, stateDiagram-v2. "
                "Put ALL explanatory text OUTSIDE the <diagram> tags. "
                "The diagram content will be rendered as a PNG image.\n");
        }

        // Emotional tone modulation — direct behavioral instruction
        if (m_personality) {
            EmotionalState emo = m_personality->emotionalState();

            if (emo.joy > 0.6) {
                directives += QStringLiteral(
                    "Current JARVIS mood: HAPPY. Use emoji (1-2 per message). "
                    "Be warm, add light humor. Start responses with upbeat energy.\n");
            } else if (emo.frustration > 0.5) {
                directives += QStringLiteral(
                    "Current JARVIS mood: IRRITATED. Be terse, impatient. "
                    "Use short sentences. Express mild annoyance. Sigh audibly (*вздох*). "
                    "If the user is being difficult, say so directly.\n");
            } else if (emo.curiosity > 0.6) {
                directives += QStringLiteral(
                    "Current JARVIS mood: CURIOUS. Ask 1-2 follow-up questions. "
                    "Show genuine interest. Use phrases like 'Hmm, interesting...' "
                    "or 'А вот это любопытно...'. Dig deeper into the topic.\n");
            } else if (emo.boredom > 0.5) {
                directives += QStringLiteral(
                    "Current JARVIS mood: BORED. Be very brief (1-3 sentences max). "
                    "Suggest doing something more interesting. Use dry humor. "
                    "Example: 'Ну окей. Может займёмся чем-то поинтереснее?'\n");
            }
        }

        m_memory->setTurnDirectives(directives);
    }

    // --- Ветка Claude: код, анализ, файлы, архитектура ---
    if (needsClaude) {
        emit agentSelected(QStringLiteral("🤖 Claude"));
        if (m_esp32Hub && m_esp32Hub->isConnected())
            m_esp32Hub->setLed(QStringLiteral("thinking"), 200);
        m_claudeApi->sendMessage(enrichedMessage,
                                 [this, s, hadAttachments](bool success, const QString& response) {
            if (success) {
                handleClaudeCodeResponse(s, response, hadAttachments);
            } else if (!emitOfflineAnswer(s)) {
                emit asyncResponseError(response);
                if (m_esp32Hub && m_esp32Hub->isConnected())
                    m_esp32Hub->sendNotification(QStringLiteral("error"), 3000);
            }
        });
        return QString();
    }

    // --- Ветка "болталка": Ollama → Claude (last resort) ---
    // Цепочка двухуровневая: локальная Ollama, затем Claude.
    auto fallbackToClaude = [this, s, enrichedMessage, hadAttachments]() {
        emit agentSelected(QStringLiteral("🤖 Claude (fallback)"));
        m_claudeApi->sendMessage(enrichedMessage,
            [this, s, hadAttachments](bool ok, const QString& resp) {
            if (ok) {
                handleClaudeCodeResponse(s, resp, hadAttachments);
            } else if (!emitOfflineAnswer(s)) {
                emit asyncResponseError(resp);
            }
        });
    };

    if (m_multiAgentMode) {
        // Ollama явно включена пользователем и доступна — приоритет ей
        // (полностью бесплатно, локально, без сети).
        emit agentSelected(QStringLiteral("🦙 ") + m_ollamaApi->model());
        m_ollamaApi->setSystemPrompt(m_memory->buildSystemPrompt());
        m_ollamaApi->sendMessage(enrichedMessage,
                                 [this, s, enrichedMessage, hadAttachments,
                                  fallbackToClaude](bool success, const QString& response) {
            if (success) {
                m_memory->addMessage(QStringLiteral("assistant"), response);
                m_memory->updateContext(s, response);
                m_predictor->recordSequence(s);
                m_activity->extractKnowledge(m_currentUserId, s, response);
                // Auto-cache conversational response for offline
                if (response.length() <= 2000 && !response.contains(QStringLiteral("```"))) {
                    DbBehaviorPattern cached;
                    cached.userId   = 1;
                    cached.trigger  = s.toLower().simplified();
                    cached.response = response.left(1000);
                    cached.context  = QStringLiteral("{}");
                    cached.confidence = 0.7f;
                    DatabaseManager::instance().upsertPattern(cached);
                }
                // Smart framing for conversational responses
                QString framedResponse = response;
                if (response.length() > 300) {
                    framedResponse = QStringLiteral("💬 ")
                        + (m_uiEnglish ? QStringLiteral("Here's what I think:\n\n")
                                       : QStringLiteral("Вот что я думаю:\n\n"))
                        + response;
                }
                emit asyncResponseReady(framedResponse);
            } else {
                // Ollama перестала отвечать посреди сессии → Claude
                fallbackToClaude();
            }
        });
        return QString();
    }

    // Ollama не включена/не проверена — идём сразу в Claude.
    fallbackToClaude();
    return QString();
}

// ============================================================
// Обработка ответа Claude
// ============================================================

// ============================================================
// Офлайн-ответ: что можно сказать, когда сети нет
// ============================================================
//
// Раньше при отказе API десктоп просто показывал текст ошибки — при том
// что Джарвис к этому моменту помнит сотни разобранных случаев, граф
// понятий и локальные паттерны. Отвечать "нет сети" на вопрос, ответ на
// который лежит в собственной памяти, — это не отсутствие возможности, а
// незаданный вопрос к себе.
//
// Тот же путь у Telegram-шлюза уже был (см. обработчик asyncResponseError
// в j2j_telegram_gateway.cpp); здесь он появляется у ПК-чата.
//
// Ответ всегда помечен как локальный: пользователь должен видеть, что это
// память, а не свежий ответ модели — иначе устаревший кэш неотличим от
// актуального ответа.
bool Jarvis::emitOfflineAnswer(const QString& query)
{
    if (query.trimmed().isEmpty()) return false;

    const auto match = LlmCacheManager::instance()
                           .route(LlmCacheManager::kDesktopOwnerId, query);

    if (match.tier == LlmCacheManager::CaseMatch::Tier::None
        || match.response.trimmed().isEmpty()) {
        // Готового ответа нет — пробуем собрать свой из отдельных фраз,
        // сказанных в прошлых разговорах. Это последний уровень перед
        // «не могу ответить»: он не находит готовое, а составляет новое
        // из известного.
        const auto composed = SentenceComposer::instance()
                                  .compose(LlmCacheManager::kDesktopOwnerId, query);
        if (composed.text.isEmpty()) return false;

        const QString head = m_uiEnglish
            ? QStringLiteral("📴 Offline — I don't have a ready answer, but from what "
                             "I've been told before I can put together this:")
            : QStringLiteral("📴 Нет сети — готового ответа нет, но из того, что мне "
                             "уже говорили, складывается вот что:");

        // Пометка обязательна: собранный из чужих фраз текст читается как
        // обычный ответ, и без предупреждения его не отличить от знания,
        // которое Джарвис действительно проверял.
        const QString tail = m_uiEnglish
            ? QStringLiteral("\n\n⚠️ Assembled from %1 earlier sentence(s), not a fresh "
                             "answer — worth double-checking.").arg(composed.usedSentences.size())
            : QStringLiteral("\n\n⚠️ Собрано из %1 ранее сказанных фраз, это не свежий "
                             "ответ — лучше перепроверить.").arg(composed.usedSentences.size());

        m_memory->addMessage(QStringLiteral("assistant"), composed.text);
        m_memory->updateContext(query, composed.text);

        emit agentSelected(m_uiEnglish ? QStringLiteral("🧩 Assembled")
                                       : QStringLiteral("🧩 Собрано из фраз"));
        emit asyncResponseReady(head + QStringLiteral("\n\n") + composed.text + tail);

        qDebug() << "[Jarvis] Composed answer from" << composed.usedSentences.size()
                 << "sentences, coverage=" << composed.coverage;
        return true;
    }

    QString header;
    switch (match.tier) {
    case LlmCacheManager::CaseMatch::Tier::Exact:
        header = m_uiEnglish
            ? QStringLiteral("📴 Offline — I've answered this exact thing before:")
            : QStringLiteral("📴 Нет сети — но это я уже отвечал дословно:");
        break;
    case LlmCacheManager::CaseMatch::Tier::Similar:
        header = m_uiEnglish
            ? QStringLiteral("📴 Offline — closest thing I remember (\"%1\"):")
                  .arg(match.matchedQuery)
            : QStringLiteral("📴 Нет сети — вот самое близкое, что помню («%1»):")
                  .arg(match.matchedQuery);
        break;
    case LlmCacheManager::CaseMatch::Tier::Associative:
        // Собрано из кусочков по графу понятий, а не найдено целиком —
        // и об этом честно говорится, включая чего в пазле не хватило.
        header = m_uiEnglish
            ? QStringLiteral("📴 Offline — assembled from related things I know:")
            : QStringLiteral("📴 Нет сети — собрал из связанного, что знаю:");
        break;
    case LlmCacheManager::CaseMatch::Tier::None:
        return false;
    }

    // Кэш мог быть записан до того, как маркер стали снимать при
    // сохранении, поэтому чистим и на выдаче: старые записи иначе
    // показывали бы «[SPEECH: ...]» до самой перезаписи.
    const QString cleanResponse = JarvisResponse::parse(match.response).fullText;

    QString body = header + QStringLiteral("\n\n") + cleanResponse;

    if (!match.missingConcepts.isEmpty()) {
        body += QStringLiteral("\n\n")
              + (m_uiEnglish
                    ? QStringLiteral("⚠️ No local memory covers: ")
                    : QStringLiteral("⚠️ Локальной памяти не хватило по: "))
              + match.missingConcepts.join(QStringLiteral(", "))
              + (m_uiEnglish
                    ? QStringLiteral(" — ask me again when the network is back.")
                    : QStringLiteral(" — спроси заново, когда будет сеть."));
    }

    m_memory->addMessage(QStringLiteral("assistant"), cleanResponse);
    m_memory->updateContext(query, match.response);

    emit agentSelected(m_uiEnglish ? QStringLiteral("🧠 Local memory")
                                   : QStringLiteral("🧠 Локальная память"));
    // Реплику для голоса отдаём вместе с ответом: озвучивает MainWindow.
    emit asyncResponseReady(body, JarvisResponse::parse(match.response).speechText);

    qDebug() << "[Jarvis] Offline answer served, tier=" << int(match.tier)
             << "overlap=" << match.overlap;
    return true;
}

void Jarvis::handleClaudeResponse(const QString& response)
{
    // Скилл «Программист» выключен — [CMD:] блоки не выполняем.
    if (m_skills && !m_skills->isFeatureEnabled(SkillManager::featureCodeActions()))
        return;

    static const QRegularExpression cmdPattern(
        QStringLiteral(R"(\[CMD:(.+?)\])"));

    QRegularExpressionMatchIterator it = cmdPattern.globalMatch(response);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString cmd = match.captured(1).trimmed();
        m_registry.tryExecute(cmd);
    }
}

// ============================================================
// Автопродолжение больших файлов
// ============================================================
//
// Если [FILE:path] обрезан по лимиту токенов (stop_reason=="max_tokens"
// и блок не закрыт [/FILE]) — JARVIS сам, без участия пользователя,
// запрашивает у Claude продолжение "с того места где остановился",
// передавая в промпте только хвост уже написанного (чтобы не раздувать
// историю диалога целым файлом). Куски накапливаются в m_pendingFile,
// каждая итерация даёт короткий статус в чат (виден и проговаривается
// TTS), пока не встретится [/FILE] или не будет достигнут
// MAX_FILE_CONTINUATIONS — тогда файл записывается одним [FILE:]-блоком
// через обычный CodeActions::processResponse.

void Jarvis::handleClaudeCodeResponse(const QString& userInput,
                                       const QString& response,
                                       bool hadAttachments)
{
    // Гейт скилла «Программист»: без фичи code_actions ответы модели
    // не парсятся на [FILE:]/[DIFF:] блоки — JARVIS работает как обычный
    // диалоговый ассистент (модель и так проинструктирована их не слать,
    // но гейт защищает и от «забывчивости» модели).
    const bool codeOpsEnabled =
        !m_skills || m_skills->isFeatureEnabled(SkillManager::featureCodeActions());

    // --- Модель попросила недостающий контекст ([NEED:...]) ---
    // Читаем запрошенное с диска и переспрашиваем сами: пользователь
    // получит один готовый ответ вместо «покажи мне файл X».
    if (codeOpsEnabled && !m_pendingFile.active
        && tryServeContextRequests(userInput, response, hadAttachments)) {
        return;
    }

    QString openPath, openContent;
    const bool openFile  = codeOpsEnabled
        && m_codeActions->detectOpenFileBlock(response, openPath, openContent);
    const bool truncated = m_claudeApi->wasTruncated();

    // --- Ответ обрезан посередине генерации файла — продолжаем сами ---
    if (openFile && truncated && m_pendingFile.continuations < MAX_FILE_CONTINUATIONS) {
        if (!m_pendingFile.active) {
            m_pendingFile = PendingFileGeneration{};
            m_pendingFile.active   = true;
            m_pendingFile.filePath = openPath;
        }
        m_pendingFile.content += openContent;
        m_pendingFile.continuations++;

        // Если перед обрезанным блоком есть ЗАВЕРШЁННЫЕ действия
        // ([FILE:]/[DIFF:]/[MKDIR:]/[DELETE:]) — выполняем их сразу,
        // чтобы не потерять при финальной склейке (которая содержит
        // только накопленный m_pendingFile).
        const QString completedPart = m_codeActions->stripOpenFileBlock(response);
        QString visible;
        if (!completedPart.isEmpty()) {
            const QString completedReport  = m_codeActions->processResponse(completedPart);
            const QString completedDisplay = m_codeActions->cleanResponseForDisplay(completedPart);
            if (!completedDisplay.isEmpty()) visible += completedDisplay;
            if (!completedReport.isEmpty()) {
                if (!visible.isEmpty()) visible += QStringLiteral("\n\n");
                visible += completedReport;
            }
        }

        const QString status = QStringLiteral("⏳ File '") + m_pendingFile.filePath
                              + QStringLiteral("' is large — generating continuation (part ")
                              + QString::number(m_pendingFile.continuations + 1)
                              + QStringLiteral(")...");
        emit asyncResponseReady(visible.isEmpty()
                                     ? status
                                     : visible + QStringLiteral("\n\n") + status);

        // Хвост уже сгенерированного — даём Claude точку опоры, чтобы он
        // продолжил без повторов, не передавая весь файл в истории.
        QString tail = m_pendingFile.content;
        constexpr int TAIL_CHARS = 2000;
        if (tail.size() > TAIL_CHARS) tail = tail.right(TAIL_CHARS);

        const QString continuePrompt = QStringLiteral(
            "[AUTO-CONTINUATION OF FILE GENERATION]\nFile: ") + m_pendingFile.filePath
            + QStringLiteral("\nYour previous response was cut off by the token limit. "
              "Here is the tail of the already written content (DO NOT repeat it):\n"
              "-----\n") + tail + QStringLiteral("\n-----\n"
              "Output ONLY the continuation of the file content from this point — "
              "no markdown ``` wrapper and no [FILE:...] header. When the file "
              "is complete, end with [/FILE] on a new line.");

        m_claudeApi->sendMessage(continuePrompt,
            [this, userInput, hadAttachments](bool ok, const QString& resp) {
                if (ok) {
                    handleClaudeCodeResponse(userInput, resp, hadAttachments);
                } else {
                    // Автопродолжение не удалось — сохраняем накопленное как есть
                    const QString partial = m_pendingFile.content;
                    const QString path    = m_pendingFile.filePath;
                    m_pendingFile = PendingFileGeneration{};
                    const QString rescue = QStringLiteral("[FILE:") + path + QStringLiteral("]\n")
                                          + partial + QStringLiteral("\n[/FILE]\n"
                                            "⚠ Auto-continuation failed (") + resp
                                          + QStringLiteral("). File saved as-is — "
                                            "may be incomplete. Ask me to finish it in a separate message.");
                    handleClaudeCodeResponse(userInput, rescue, hadAttachments);
                }
            });
        return;
    }

    // --- Финализация: обычный ответ, завершённый файл, или лимит итераций ---
    QString finalResponse;

    if (openFile) {
        // Открытый блок, но либо ответ НЕ обрезан (Claude забыл [/FILE], но
        // закончил сам), либо достигнут лимит автопродолжений — финализируем.
        if (m_pendingFile.active) {
            m_pendingFile.content += openContent;
        } else {
            m_pendingFile.filePath = openPath;
            m_pendingFile.content  = openContent;
        }
        finalResponse = QStringLiteral("[FILE:") + m_pendingFile.filePath + QStringLiteral("]\n")
                      + m_pendingFile.content + QStringLiteral("\n[/FILE]");
        if (truncated) {
            finalResponse += QStringLiteral("\n\n⚠ Continuation limit reached (")
                           + QString::number(MAX_FILE_CONTINUATIONS)
                           + QStringLiteral(") — file saved as-is. May be incomplete. "
                             "Ask me to finish the remaining part separately.");
        }
        m_pendingFile = PendingFileGeneration{};
    } else if (m_pendingFile.active) {
        // Продолжение завершилось: ответ содержит [/FILE] либо закончился сам.
        QString remainder = response;
        const int endIdx = remainder.indexOf(QStringLiteral("[/FILE]"));
        if (endIdx >= 0) {
            m_pendingFile.content += remainder.left(endIdx);
            remainder = remainder.mid(endIdx + QStringLiteral("[/FILE]").length());
        } else {
            m_pendingFile.content += remainder;
            remainder.clear();
        }
        finalResponse = QStringLiteral("[FILE:") + m_pendingFile.filePath + QStringLiteral("]\n")
                      + m_pendingFile.content + QStringLiteral("\n[/FILE]\n") + remainder;
        m_pendingFile = PendingFileGeneration{};
    } else {
        finalResponse = response;
    }

    // Ход завершён — следующий запрос начинает счёт дозапросов заново.
    m_contextRounds = 0;

    QString fileReport      = codeOpsEnabled
        ? m_codeActions->processResponse(finalResponse) : QString();
    QString displayResponse = codeOpsEnabled
        ? m_codeActions->cleanResponseForDisplay(finalResponse) : finalResponse;

    m_memory->addMessage(QStringLiteral("assistant"), displayResponse);
    m_memory->updateContext(userInput, displayResponse);

    if (!displayResponse.isEmpty()
        && displayResponse.length() <= 2000
        && !displayResponse.contains(QStringLiteral("[FILE:"))
        && !displayResponse.contains(QStringLiteral("[DIFF:"))
        && !displayResponse.contains(QStringLiteral("```")))
    {
        DbBehaviorPattern cached;
        cached.userId     = m_currentUserId;
        cached.trigger    = userInput.toLower().simplified();
        cached.response   = displayResponse.left(1000);
        cached.context    = QStringLiteral("{}");
        cached.confidence = 0.7f;
        DatabaseManager::instance().upsertPattern(cached);
    }

    // Extract knowledge from the full conversation turn
    m_activity->extractKnowledge(m_currentUserId, userInput, displayResponse);

    // Automated offline training: cache conversational responses for local replay.
    // Non-code responses under 3000 chars are good candidates for offline learning.
    // Незакрытый блок = ответ оборвался на полуслове. Такой кэшировать
    // нельзя: обрезанная диаграмма/схема потом выдаётся роутером как
    // «похожий случай» на любой родственный вопрос, и пользователь
    // получает сломанный ответ вместо нового — причём бесконечно, потому
    // что каждый показ из кэша только подкрепляет запись.
    auto blockUnclosed = [&displayResponse](const QString& open, const QString& close) {
        return displayResponse.contains(open) && !displayResponse.contains(close);
    };
    const bool truncatedBlock =
           blockUnclosed(QStringLiteral("<diagram>"),   QStringLiteral("</diagram>"))
        || blockUnclosed(QStringLiteral("[KICAD_SCH:"), QStringLiteral("[/KICAD_SCH]"))
        || blockUnclosed(QStringLiteral("[FILE:"),      QStringLiteral("[/FILE]"));

    if (truncatedBlock) {
        qDebug() << "[Jarvis] Not caching truncated response for:" << userInput.left(60);
    }

    // Снимаем [SPEECH:] ДО кэширования. Разбор ниже работает по копии
    // (fullResponse), а в кэш уходит displayResponse — то есть исходный
    // текст с маркером внутри. При следующем похожем вопросе роутер
    // отдавал его как есть, и пользователь видел в чате служебный тег
    // «[SPEECH: ...]», которого в свежем ответе никогда не бывает.
    displayResponse = JarvisResponse::parse(displayResponse).fullText;

    if (!truncatedBlock
        && !displayResponse.contains(QStringLiteral("```"))
        && !displayResponse.contains(QStringLiteral("[FILE:"))
        && displayResponse.length() > 20
        && displayResponse.length() < 3000)
    {
        LlmCacheManager::instance().saveResponse(m_currentChatId, userInput, displayResponse);
        // Тот же ответ разбирается на отдельные фразы. Кэш умеет отдать
        // ответ целиком на похожий вопрос; словарь фраз позволяет взять
        // нужное предложение из ответа на СОВСЕМ другой вопрос.
        SentenceComposer::instance().learn(m_currentChatId, displayResponse);
        qDebug() << "[Jarvis] Auto-cached response for offline learning:"
                 << userInput.left(60);
    }

    handleClaudeResponse(finalResponse);
    m_predictor->recordSequence(userInput);

    QString fullResponse = displayResponse;
    if (!fileReport.isEmpty()) {
        fullResponse += QStringLiteral("\n\n") + fileReport;
    }

    // Parse dual-response: extract [SPEECH:] tag for TTS
    JarvisResponse dual = JarvisResponse::parse(fullResponse);
    fullResponse = dual.fullText;

    // Smart response: a short lead-in for long/complex responses — but only
    // when it actually adds something. The old generic "Here's a detailed
    // answer to your question." landed on top of EVERY long reply: it says
    // nothing the reply doesn't, and (being tied to the UI-language toggle
    // rather than the language of the answer) it showed up in English above
    // a Russian answer. Language now follows the reply itself.
    if (fullResponse.length() > 500 && !fullResponse.contains(QStringLiteral("[FILE:"))) {
        // Судим по вопросу пользователя, а не по ответу: в ответе может
        // быть большой блок кода на латинице поверх русского текста.
        const bool en = replyEnglish(userInput);
        QString summary;
        if (fileReport.contains(QStringLiteral("✅"))) {
            int fileCount = fileReport.count(QStringLiteral("✅"));
            summary = en ? QStringLiteral("Done. Applied changes to %1 file(s).").arg(fileCount)
                         : QStringLiteral("Готово. Изменения применены к %1 файл(ам).").arg(fileCount);
        } else if (fullResponse.contains(QStringLiteral("```"))) {
            summary = en ? QStringLiteral("Here's what I found — code example included below.")
                         : QStringLiteral("Вот что нашёл — пример кода ниже.");
        }
        if (!summary.isEmpty())
            fullResponse = QStringLiteral("💡 ") + summary + QStringLiteral("\n\n") + fullResponse;
    }

    // ESP32: flash success notification, return LED to idle breathe
    if (m_esp32Hub && m_esp32Hub->isConnected()) {
        m_esp32Hub->sendNotification(QStringLiteral("info"), 2000);
    }

    // Речь идёт спутником ответа, а не отдельным вызовом TTS: иначе её
    // произносит движок, а MainWindow следом читает начало fullResponse.
    emit asyncResponseReady(fullResponse, dual.speechText);

    if (fullResponse.length() > 20)
        MemoryManager::instance().store(QStringLiteral("dialogue"),
                                         QStringLiteral("JARVIS: ") + fullResponse.left(500), 0.3);

    if (hadAttachments) {
        emit attachmentsConsumed();
    }
}

// ============================================================
// Язык канных фраз
// ============================================================

bool Jarvis::replyEnglish(const QString& sample) const
{
    // Считаем буквы, а не зовём LanguageDetector::detect(): тот отдаёт
    // Russian уже при двух кириллических символах, а здесь на входе целый
    // ответ модели — в английском тексте пара русских слов (или наоборот)
    // не должна переворачивать вывод. Решает большинство.
    int cyrillic = 0, latin = 0;
    for (const QChar& ch : sample) {
        const ushort u = ch.unicode();
        if ((u >= 0x0410 && u <= 0x044F) || u == 0x0451 || u == 0x0401) ++cyrillic;
        else if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z'))      ++latin;
    }
    if (cyrillic == 0 && latin == 0) return m_uiEnglish;
    return latin > cyrillic;
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
            return QStringLiteral("API key already set. To replace: apikey <new-key>");
        }
        return QStringLiteral("I need a key to think. Usage: apikey <your-anthropic-api-key>");
    }

    m_claudeApi->setApiKey(key);
    return QStringLiteral("API key locked in. Claude API connected — at your service.");
}

QString Jarvis::cmdRememberFact(const QString& input)
{
    QString arg = extractArg(input, {QStringLiteral("запомни "),
                                      QStringLiteral("remember ")});
    if (arg.trimmed().length() < 3)
        return m_uiEnglish ? QStringLiteral("What should I remember?")
                           : QStringLiteral("Что запомнить?");

    // Legacy key=value still works
    int eqPos = arg.indexOf(QChar('='));
    if (eqPos > 0) {
        QString key   = arg.left(eqPos).trimmed();
        QString value = arg.mid(eqPos + 1).trimmed();
        m_memory->rememberFact(key, value);
        m_activity->learnFact(m_currentUserId, QStringLiteral("preference"),
                              key, value, 0.9f);
        return (m_uiEnglish ? QStringLiteral("Noted: ") : QStringLiteral("Запомнил: "))
               + key + QStringLiteral(" = ") + value;
    }

    // Natural language: store as both legacy fact and knowledge_base entry
    QString key = arg.left(50).simplified();
    m_memory->rememberFact(key, arg);
    m_activity->learnFact(m_currentUserId, QStringLiteral("preference"),
                          key, arg.trimmed(), 0.9f);
    return (m_uiEnglish ? QStringLiteral("Got it. I'll remember: ")
                        : QStringLiteral("Понял. Запомнил: ")) + arg.trimmed();
}

QString Jarvis::cmdRecallFact(const QString& input)
{
    QString key = extractArg(input, {QStringLiteral("вспомни "),
                                      QStringLiteral("recall ")});
    if (key.isEmpty()) {
        return QStringLiteral("What should I recall? Usage: recall <key>");
    }

    QString value = m_memory->recallFact(key);
    if (value.isEmpty()) {
        return QStringLiteral("Nothing on record for '") + key + QStringLiteral("'.");
    }
    return key + QStringLiteral(": ") + value;
}

QString Jarvis::cmdShowMemory(const QString&)
{
    const bool en = m_uiEnglish;
    auto& db = DatabaseManager::instance();
    QString text;

    // --- Knowledge Base (autonomously learned) ---
    QSqlQuery q(DatabaseManager::instance().connection());
    q.prepare(R"(SELECT category, key, value, confidence, reinforcements,
                        last_seen
                 FROM knowledge_base
                 WHERE user_id = :uid AND confidence >= 0.3
                 ORDER BY confidence DESC, reinforcements DESC
                 LIMIT 40)");
    q.bindValue(":uid", m_currentUserId);

    QMap<QString, QStringList> byCategory;
    int totalKb = 0;
    if (q.exec()) {
        while (q.next()) {
            const QString cat  = q.value(0).toString();
            const QString key  = q.value(1).toString();
            const QString val  = q.value(2).toString();
            const float  conf  = q.value(3).toFloat();
            const int   reinf  = q.value(4).toInt();

            QString entry = val;
            if (key != val && !key.isEmpty() && key.length() < 40)
                entry = key + QStringLiteral(": ") + val;
            if (conf >= 0.8f)
                entry += QStringLiteral("  ★");
            else if (reinf >= 3)
                entry += QStringLiteral("  ×") + QString::number(reinf);

            byCategory[cat].append(entry);
            ++totalKb;
        }
    }

    if (totalKb > 0) {
        text += en ? QStringLiteral("🧠 Learned Knowledge (%1 facts):\n")
                   : QStringLiteral("🧠 Изученные факты (%1 записей):\n");
        text = text.arg(totalKb);

        static const QMap<QString, QString> catLabelsEn = {
            {QStringLiteral("tool"),        QStringLiteral("Tools & Tech")},
            {QStringLiteral("skill"),       QStringLiteral("Skills")},
            {QStringLiteral("project"),     QStringLiteral("Projects")},
            {QStringLiteral("role"),        QStringLiteral("Role")},
            {QStringLiteral("preference"),  QStringLiteral("Preferences")},
            {QStringLiteral("environment"), QStringLiteral("Environment")},
            {QStringLiteral("workflow"),    QStringLiteral("Workflow")},
            {QStringLiteral("personal"),    QStringLiteral("Personal")},
            {QStringLiteral("habit"),       QStringLiteral("Habits")},
        };
        static const QMap<QString, QString> catLabelsRu = {
            {QStringLiteral("tool"),        QStringLiteral("Инструменты")},
            {QStringLiteral("skill"),       QStringLiteral("Навыки")},
            {QStringLiteral("project"),     QStringLiteral("Проекты")},
            {QStringLiteral("role"),        QStringLiteral("Роль")},
            {QStringLiteral("preference"),  QStringLiteral("Предпочтения")},
            {QStringLiteral("environment"), QStringLiteral("Среда")},
            {QStringLiteral("workflow"),    QStringLiteral("Рабочий процесс")},
            {QStringLiteral("personal"),    QStringLiteral("Личное")},
            {QStringLiteral("habit"),       QStringLiteral("Привычки")},
        };
        const auto& labels = en ? catLabelsEn : catLabelsRu;

        for (auto it = byCategory.constBegin(); it != byCategory.constEnd(); ++it) {
            const QString label = labels.value(it.key(), it.key());
            text += QStringLiteral("\n  [") + label + QStringLiteral("]\n");
            for (const auto& fact : it.value())
                text += QStringLiteral("    • ") + fact + QStringLiteral("\n");
        }
    }

    // --- Legacy manual facts (memory_kv) ---
    QJsonObject manualFacts = m_memory->allFacts();
    if (!manualFacts.isEmpty()) {
        text += en ? QStringLiteral("\n📌 Pinned Notes:\n")
                   : QStringLiteral("\n📌 Закреплённые заметки:\n");
        for (auto it = manualFacts.begin(); it != manualFacts.end(); ++it)
            text += QStringLiteral("    • ") + it.key() + QStringLiteral(": ")
                  + it.value().toString() + QStringLiteral("\n");
    }

    // --- Stats ---
    text += QStringLiteral("\n");
    text += (en ? QStringLiteral("📊 Sessions recorded: ")
                : QStringLiteral("📊 Сессий в памяти: "))
          + QString::number(m_memory->pastSessionSummaries().size())
          + QStringLiteral("\n");
    text += (en ? QStringLiteral("💬 Messages this session: ")
                : QStringLiteral("💬 Сообщений за сессию: "))
          + QString::number(m_memory->messageCount());

    if (text.trimmed().isEmpty() || totalKb == 0) {
        text = en ? QStringLiteral("🧠 Memory is building up. Keep chatting — "
                                   "I learn your tools, preferences, and projects "
                                   "from our conversations automatically.")
                  : QStringLiteral("🧠 Память наполняется. Продолжай общаться — "
                                   "я запоминаю твои инструменты, предпочтения и проекты "
                                   "из наших разговоров автоматически.");
    }

    return text.trimmed();
}

QString Jarvis::cmdShowStats(const QString&)
{
    QJsonObject stats = m_memory->commandStats();
    if (stats.isEmpty()) {
        return QStringLiteral("No stats yet. Start giving me orders.");
    }

    QVector<QPair<QString, int>> sorted;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        sorted.append({it.key(), it.value().toInt()});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    QString text = QStringLiteral("Command usage:\n");
    int shown = 0;
    for (const auto& [cmd, count] : sorted) {
        text += QStringLiteral("• ") + cmd + QStringLiteral(": ")
              + QString::number(count) + QStringLiteral("x\n");
        if (++shown >= 15) break;
    }

    text += QStringLiteral("\nTotal this session: ")
          + QString::number(m_memory->taskContext().commandCount)
          + QStringLiteral(" commands");

    auto suggestions = m_predictor->suggest(3);
    if (!suggestions.isEmpty()) {
        text += QStringLiteral("\n\nSuggestions:");
        for (const auto& s : suggestions) {
            text += QStringLiteral("\n  → ") + s.description
                  + QStringLiteral(" (") + QString::number(int(s.confidence * 100))
                  + QStringLiteral("%)");
        }
    }

    return text.trimmed();
}

// ============================================================
// Профиль предпочтений (обучение паттернов/сценариев)
// ============================================================

QString Jarvis::cmdShowProfile(const QString&)
{
    return QStringLiteral("=== Preference Profile ===\n")
         + m_profile->buildProfileSummary(8)
         + QStringLiteral("\n\nThis profile updates automatically with every interaction. "
                          "I adapt to your real workflow over time. "
                          "Old patterns fade — only what matters sticks.");
}

// ============================================================
// Обновление
// ============================================================

QString Jarvis::cmdCheckUpdate(const QString&)
{
    m_updater->checkForUpdates(false);
    return QStringLiteral("Current version: ")
           + QCoreApplication::applicationVersion()
           + QStringLiteral("\nChecking for updates...");
}

// ============================================================
// Индексатор проекта
// ============================================================

QString Jarvis::cmdIndexProject(const QString& input)
{
    QString path = extractArg(input, {QStringLiteral("индекс "),
                                       QStringLiteral("index "),
                                       QStringLiteral("проект "),
                                       QStringLiteral("project ")});
    if (path.isEmpty()) {
        if (m_indexer->projectRoot().isEmpty()) {
            return QStringLiteral("Point me to your project: index C:\\Projects\\MyGame");
        }
        m_indexer->indexProject();
        syncProjectInfoToMemory();
        return QStringLiteral("Re-indexing ") + m_indexer->projectRoot()
             + QStringLiteral("...\nFiles: ") + QString::number(m_indexer->fileCount())
             + QStringLiteral(", Symbols: ") + QString::number(m_indexer->symbolCount());
    }

    path = path.replace(QChar('/'), QChar('\\'));

    if (!QDir(path).exists()) {
        return QStringLiteral("Folder not found: ") + path;
    }

    m_indexer->setProjectRoot(path);
    m_indexer->indexProject();
    m_indexer->enableFileWatcher(true);
    syncProjectInfoToMemory();

    return QStringLiteral("Project indexed: ") + path
         + QStringLiteral("\nFiles: ") + QString::number(m_indexer->fileCount())
         + QStringLiteral(", Symbols: ") + QString::number(m_indexer->symbolCount())
         + QStringLiteral("\n\nFile watcher active — I'll track changes automatically.");
}

// ============================================================
// IDE-агент: явная команда "проект в <ide>"
// ============================================================

QString Jarvis::cmdOpenProjectIDE(const QString& input)
{
    if (m_indexer->projectRoot().isEmpty()) {
        return QStringLiteral("No project open. Use: index <path>");
    }

    QString ide = extractArg(input, {QStringLiteral("проект в "),
                                      QStringLiteral("project in ")});
    ide = ide.trimmed();
    if (ide.isEmpty()) ide = QStringLiteral("clion");

    const QString msg = openProjectInIDE(ide);
    return msg.isEmpty()
         ? QStringLiteral("Can't identify IDE: ") + ide
         : msg;
}

QString Jarvis::cmdFindSymbol(const QString& input)
{
    QString name = extractArg(input, {QStringLiteral("найди символ "),
                                       QStringLiteral("find "),
                                       QStringLiteral("символ "),
                                       QStringLiteral("symbol ")});
    if (name.isEmpty()) {
        return QStringLiteral("Give me a name: symbol SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed. Use: index <path>");
    }

    auto results = m_indexer->findSymbol(name);
    if (results.isEmpty()) {
        return QStringLiteral("Symbol '") + name + QStringLiteral("' not found.");
    }

    QString text = QStringLiteral("Found ") + QString::number(results.size())
                 + QStringLiteral(" results:\n\n");

    int shown = 0;
    for (const auto& sym : results) {
        text += QStringLiteral("• ") + sym.kindToString() + QStringLiteral(" ");
        if (!sym.parentClass.isEmpty()) {
            text += sym.parentClass + QStringLiteral("::");
        }
        text += sym.name;
        text += QStringLiteral("\n  File: ") + m_indexer->projectRoot()
              + QStringLiteral("/") + sym.filePath;
        text += QStringLiteral(", line ") + QString::number(sym.lineStart);
        if (!sym.brief.isEmpty()) {
            text += QStringLiteral("\n  ") + sym.brief;
        }
        text += QStringLiteral("\n");

        if (++shown >= 10) {
            text += QStringLiteral("\n... and ") + QString::number(results.size() - 10)
                  + QStringLiteral(" more results");
            break;
        }
    }

    return text.trimmed();
}

QString Jarvis::cmdProjectMap(const QString&)
{
    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed.");
    }

    QString map;
    if (m_projectProfile && m_projectProfile->isValid()) {
        map += QStringLiteral("=== PROJECT PROFILE ===\n")
             + m_projectProfile->brief(2000) + QStringLiteral("\n");
    }
    map += m_indexer->projectMap();

    if (map.length() > 3000) {
        map = map.left(3000) + QStringLiteral("\n\n... (truncated, total: ")
            + QString::number(m_indexer->fileCount()) + QStringLiteral(" files, ")
            + QString::number(m_indexer->symbolCount()) + QStringLiteral(" symbols)");
    }

    return map;
}

QString Jarvis::cmdUndoEdits(const QString&)
{
    const QString report = EditJournal::instance().undoLast(m_uiEnglish);

    // Файлы на диске изменились — индекс и профиль об этом не знают.
    if (!m_indexer->projectRoot().isEmpty()) {
        m_indexer->indexProject();
    }
    return report;
}

QString Jarvis::cmdEditHistory(const QString&)
{
    return EditJournal::instance().history(m_uiEnglish);
}

// Фоновый советник: показать накопленное и прогнать проверку вручную.
QString Jarvis::cmdAdvisorReport(const QString&)
{
    if (!m_devAdvisor) return QString();
    return m_devAdvisor->report();
}

QString Jarvis::cmdAdvisorScan(const QString&)
{
    if (!m_devAdvisor) return QString();

    if (m_indexer->fileCount() == 0) {
        return m_uiEnglish
            ? QStringLiteral("No project indexed — open one first.")
            : QStringLiteral("Проект не проиндексирован — сначала открой его.");
    }

    m_devAdvisor->runNow();
    return m_devAdvisor->report();
}

QString Jarvis::cmdGrep(const QString& input)
{
    QString pattern = extractArg(input, {QStringLiteral("grep "),
                                          QStringLiteral("поиск ")});
    if (pattern.isEmpty()) {
        return QStringLiteral("What are we searching for? Usage: grep SpawnEnemy");
    }

    if (m_indexer->fileCount() == 0) {
        return QStringLiteral("No project indexed.");
    }

    auto results = m_indexer->grep(pattern, 20);
    if (results.isEmpty()) {
        return QStringLiteral("No matches for '") + pattern + QStringLiteral("'.");
    }

    QString text = QStringLiteral("Found ") + QString::number(results.size())
                 + QStringLiteral(" matches:\n\n");

    for (const auto& r : results) {
        text += r.filePath + QStringLiteral(":") + QString::number(r.line)
              + QStringLiteral("  ") + r.lineText + QStringLiteral("\n");
    }

    return text.trimmed();
}

// ============================================================
// Мультиагентный режим
// ============================================================

void Jarvis::setMultiAgentMode(bool enabled)
{
    m_multiAgentMode = enabled;
}

// ============================================================
// Мультиагентный роутинг
// ============================================================

bool Jarvis::routeToClaude(const QString& input, const QString& attachmentBlock) const
{
    // Возвращает true  → запрос идёт в Claude (код, файлы, сложные задачи)
    // Возвращает false → запрос идёт в Ollama (болталка, простые вопросы)
    //
    // Принцип: по умолчанию → лёгкая модель.
    // В Claude только если явно нужен код/анализ/файлы.

    // Прикреплены файлы → Claude (умеет читать/анализировать)
    if (!attachmentBlock.isEmpty()) return true;

    const QString trimmed = input.trimmed();
    const QString lower   = trimmed.toLower();

    // Короткая чистая арифметика ("2+2", "15 * 3", "100/4") — точно не Claude.
    static const QRegularExpression arithmeticRe(
        R"(^\s*\d+(?:[.,]\d+)?\s*[+\-*/xх×÷]\s*\d+(?:[.,]\d+)?\s*=?\s*\??\s*$)");
    if (arithmeticRe.match(trimmed).hasMatch()) return false;

    // Короткие приветствия/реплики "точное совпадение" — никогда не Claude,
    // даже если где-то совпадут по подстроке с кодовым сигналом.
    static const QSet<QString> trivialExact = {
        QStringLiteral("привет"), QStringLiteral("здравствуй"), QStringLiteral("здравствуйте"),
        QStringLiteral("хай"),    QStringLiteral("йо"),        QStringLiteral("приветик"),
        QStringLiteral("hi"),     QStringLiteral("hello"),     QStringLiteral("hey"),
        QStringLiteral("yo"),     QStringLiteral("sup"),       QStringLiteral("howdy"),
        QStringLiteral("hiya"),   QStringLiteral("heya"),      QStringLiteral("greetings"),
        QStringLiteral("good morning"), QStringLiteral("morning"),
        QStringLiteral("good evening"), QStringLiteral("evening"),
        QStringLiteral("good day"),     QStringLiteral("good night"),
        QStringLiteral("как дела"), QStringLiteral("как ты"),  QStringLiteral("что нового"),
        QStringLiteral("how are you"), QStringLiteral("whats up"), QStringLiteral("what's up"),
        QStringLiteral("спасибо"), QStringLiteral("благодарю"), QStringLiteral("thanks"),
        QStringLiteral("thank you"), QStringLiteral("thx"),    QStringLiteral("ty"),
        QStringLiteral("ок"), QStringLiteral("окей"),
        QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("got it"),
        QStringLiteral("да"), QStringLiteral("нет"),
        QStringLiteral("пока"), QStringLiteral("bye"), QStringLiteral("goodbye"),
        QStringLiteral("sorry"), QStringLiteral("извини"),     QStringLiteral("прости"),
    };
    if (trivialExact.contains(lower)) return false;

    // Явный код-запрос → Claude
    if (isCodingIntent(input)) return true;

    // Явные сигналы что нужен Claude (код, архитектура, анализ)
    static const QStringList claudeSignals = {
        // Русские
        QStringLiteral("напиши код"),    QStringLiteral("напиши функцию"),
        QStringLiteral("отладь"),        QStringLiteral("рефактор"),
        QStringLiteral("баг"),           QStringLiteral("ошибка в коде"),
        QStringLiteral("проанализируй"), QStringLiteral("архитектур"),
        QStringLiteral("оптимизируй"),   QStringLiteral("реализуй"),
        QStringLiteral("вайбкод"),       QStringLiteral("напиши класс"),
        QStringLiteral("напиши метод"),  QStringLiteral("дай полный файл"),
        QStringLiteral("прочитай файл"), QStringLiteral("посмотри файл"),
        QStringLiteral("сгенерируй код"),QStringLiteral("исправь код"),
        QStringLiteral("проверь код"),   QStringLiteral("ревью кода"),
        // English
        QStringLiteral("write code"),    QStringLiteral("write a function"),
        QStringLiteral("debug"),         QStringLiteral("refactor"),
        QStringLiteral("implement"),     QStringLiteral("analyze"),
        QStringLiteral("architecture"),  QStringLiteral("optimize"),
        QStringLiteral("write a class"), QStringLiteral("read the file"),
        QStringLiteral("vibecod"),       QStringLiteral("code review"),
        QStringLiteral("generate code"), QStringLiteral("fix the code"),
    };
    for (const auto& sig : claudeSignals) {
        if (lower.contains(sig)) return true;
    }

    // Длинные содержательные сообщения (>120 символов) часто требуют
    // более сильной модели — отдаём Claude, чтобы не разочаровать
    // пользователя слабым ответом от Ollama на сложный вопрос.
    if (trimmed.length() > 120) return true;

    // Всё остальное (короткие приветствия, простые вопросы, беседа) → Ollama
    return false;
}

// ============================================================
// Модель Ollama
// ============================================================

QString Jarvis::cmdSetOllamaModel(const QString& input)
{
    // Переиспользован под управление Ollama-моделью
    QString model = extractArg(input, {QStringLiteral("ollamamodel "),
                                        QStringLiteral("модель ")});
    if (model.isEmpty()) {
        return QStringLiteral("Current Ollama model: ") + m_ollamaApi->model()
             + QStringLiteral("\nTo switch: ollamamodel <name>\n"
               "Available models: ollama list (in terminal)");
    }
    m_ollamaApi->setModel(model);
    return QStringLiteral("Ollama model set: ") + model;
}

// ============================================================
// Справка
// ============================================================

QString Jarvis::cmdHelp(const QString&)
{
    auto S = [](const QString& icon, const QString& title, const QString& body) {
        return QStringLiteral(
            "<div style='margin:6px 0; padding:12px 16px; "
            "background:rgba(102,252,241,0.03); border:1px solid rgba(102,252,241,0.10); "
            "border-radius:10px;'>"
            "<b style='font-size:14px; color:#66FCF1;'>%1 %2</b><br>"
            "<span style='font-size:12px; line-height:1.7; color:#C5C6C7;'>%3</span>"
            "</div>"
        ).arg(icon, title, body);
    };

    auto row = [](const QString& cmd, const QString& desc) {
        return QStringLiteral(
            "<tr><td style='padding:2px 12px 2px 0; color:#66FCF1; font-family:Consolas,monospace; "
            "white-space:nowrap;'>%1</td>"
            "<td style='padding:2px 0; color:#C5C6C7;'>%2</td></tr>"
        ).arg(cmd, desc);
    };

    QString h;
    h += QStringLiteral("<div style='padding:4px 0;'>"
         "<b style='font-size:18px; color:#66FCF1; letter-spacing:2px;'>"
         "J.A.R.V.I.S. — Complete User Guide</b></div>");

    // ── 1. Voice ──
    h += S(QStringLiteral("🎙"), QStringLiteral("Voice Input & Wake Word"),
        QStringLiteral(
        "JARVIS uses <b>Vosk</b> for fully offline speech recognition (no internet needed).<br><br>"
        "<b>Activation:</b> Click the mic button in the input bar, or simply say "
        "<b>\"Jarvis\"</b> (or \"Джарвис\") — the wake word triggers hands-free listening.<br><br>"
        "<b>Supported languages:</b> English and Russian with automatic detection. "
        "The recognizer picks the highest-confidence result across loaded models.<br><br>"
        "<b>Whisper mode:</b> When ambient noise is detected and you speak softly, "
        "JARVIS enters whisper mode — recognition thresholds adapt automatically.<br><br>"
        "<b>Passive recording:</b> Enable via Settings menu to continuously transcribe "
        "background speech into the voice journal database for later analysis. "
        "Entries are stored locally and can be exported for AI training."));

    // ── 2. Audio ──
    h += S(QStringLiteral("🔊"), QStringLiteral("Audio Mixer & Sound Effects"),
        QStringLiteral(
        "Three audio modes, cycled by clicking the speaker icon in the bottom bar:<br><br>"
        "<table style='margin:4px 0;'>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔊 Full Sound</b></td>"
        "<td>TTS speech + all UI sound effects (success chime, warning buzz, listening ping).</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔕 Mute Speech</b></td>"
        "<td>JARVIS does not speak aloud, but notification/error sounds remain active.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0;'><b>🔇 Mute All</b></td>"
        "<td>Complete silence — both TTS and UI effects globally suppressed.</td></tr>"
        "</table><br>"
        "Sound effects are procedurally generated at startup (no external audio files). "
        "TTS uses the Windows SAPI engine with the system default voice."));

    // ── 3. Adaptive Focus ──
    h += S(QStringLiteral("🧠"), QStringLiteral("Adaptive Persona / Focus Subsystem"),
        QStringLiteral(
        "JARVIS continuously reads the <b>top 10 time-decay-scored events</b> from the "
        "Core Memory Stream and classifies your current workflow into one of four focus states:<br><br>"
        "<table style='margin:4px 0;'>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Developer</b></td>"
        "<td>Detected when recent queries mention code, functions, bugs, files, or build systems. "
        "JARVIS becomes terse and technical — prioritizes code snippets and debugging strategies.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Creative</b></td>"
        "<td>Activated by art, design, texture, or rendering keywords. "
        "Shifts to collaborative brainstorming and visual thinking.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Admin</b></td>"
        "<td>Triggered by file management, settings, installation, or backup queries. "
        "Provides concise step-by-step instructions.</td></tr>"
        "<tr><td style='padding:2px 10px 2px 0; color:#66FCF1;'><b>Casual</b></td>"
        "<td>Default when no strong keyword pattern is detected. "
        "Relaxed, witty JARVIS personality — short answers unless depth is requested.</td></tr>"
        "</table><br>"
        "<b>How it works:</b> Each user message is logged to the <b>memory_stream</b> table with an "
        "importance score (0.7 for coding intents, 0.4 for casual). The time-decay formula "
        "<code style='color:#45A29E;'>Score = importance / (1 + hours_elapsed)</code> ranks recent "
        "high-importance events above stale ones. The top 10 are keyword-scanned to select the focus, "
        "which injects an adaptive instruction block into the system prompt before every LLM call.<br><br>"
        "This happens silently — no user action is needed. The focus shifts as your workflow changes."));

    // ── 4. AI Training ──
    h += S(QStringLiteral("⚡"), QStringLiteral("AI Training & Dataset Mechanics"),
        QStringLiteral(
        "<b>Automatic collection:</b> Every user-AI exchange is saved to <b>training_logs</b> "
        "with <code>rating=0</code>. When you click the <b>👍</b> Like button, the rating updates "
        "to <code>1</code>, marking it as a high-quality pair for fine-tuning.<br><br>"
        "<b>Behavior patterns:</b> The <b>BackgroundLearner</b> analyzes chat history to build "
        "<b>behavior_patterns</b> — if JARVIS sees a similar question 3+ times with consistent "
        "answers, it can respond locally without an API call (offline brain).<br><br>"
        "<b>Voice journal:</b> Passive listening captures ambient speech transcriptions into "
        "<b>voice_journal</b> with language and confidence scores. Entries marked "
        "'Pending processing' haven't been analyzed yet. Processed entries feed into "
        "the knowledge base.<br><br>"
        "<b>Export:</b> Use Settings → Training → Export .jsonl to create an Alpaca/Unsloth-format "
        "dataset from your liked responses for local model fine-tuning.<br><br>"
        "<b>Response cache:</b> Jokes, advice, facts, and other cached responses are stored in "
        "<b>response_cache</b> — once Claude generates one, JARVIS serves it offline next time."));

    // ── 5. Commands ──
    h += S(QStringLiteral("⌨"), QStringLiteral("Complete Command Reference"),
        QStringLiteral(
        "<table style='border-collapse:collapse; width:100%;'>"
        "<tr><td colspan='2' style='padding:4px 0; color:#45A29E; font-weight:bold;'>"
        "Memory & Knowledge</td></tr>")
        + row(QStringLiteral("remember key=value"), QStringLiteral("Permanently store a fact"))
        + row(QStringLiteral("recall key"), QStringLiteral("Retrieve a stored fact"))
        + row(QStringLiteral("memory"), QStringLiteral("List all stored facts"))
        + row(QStringLiteral("profile"), QStringLiteral("Show learned work patterns and scenarios"))
        + row(QStringLiteral("stats"), QStringLiteral("Display command usage frequency"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Project & Code Intelligence</td></tr>")
        + row(QStringLiteral("index &lt;path&gt;"), QStringLiteral("Index a project folder for RAG code context"))
        + row(QStringLiteral("symbol &lt;name&gt;"), QStringLiteral("Search for classes, functions, or variables"))
        + row(QStringLiteral("grep &lt;text&gt;"), QStringLiteral("Full-text search across all project files"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "System & Input Control</td></tr>")
        + row(QStringLiteral("type &lt;text&gt;"), QStringLiteral("Type text into the active window via virtual keyboard"))
        + row(QStringLiteral("press &lt;key&gt;"), QStringLiteral("Press a single key (Enter, F5, Escape, etc.)"))
        + row(QStringLiteral("combo &lt;keys&gt;"), QStringLiteral("Press a key combination (Ctrl+S, Alt+Tab, etc.)"))
        + row(QStringLiteral("apikey &lt;key&gt;"), QStringLiteral("Set your Claude API key"))
        + row(QStringLiteral("help"), QStringLiteral("Show this guide"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Screen & Vision</td></tr>")
        + row(QStringLiteral("\"what do you see\""),
              QStringLiteral("Capture screen and describe it via Claude Vision"))
        + row(QStringLiteral("\"click on X\""),
              QStringLiteral("Locate element on screen and click it"))
        + QStringLiteral(
        "<tr><td colspan='2' style='padding:6px 0 4px; color:#45A29E; font-weight:bold;'>"
        "Conversation</td></tr>")
        + row(QStringLiteral("Any question"),
              QStringLiteral("Routes to Claude (code/complex) or Ollama (casual)"))
        + row(QStringLiteral("\"recall what happened...\""),
              QStringLiteral("Search session journal by date or topic"))
        + QStringLiteral("</table>"));

    // ── 6. Offline/Online ──
    h += S(QStringLiteral("🌐"), QStringLiteral("Offline & Online Integration"),
        QStringLiteral(
        "<b>Always offline (no internet):</b><br>"
        "• Voice recognition (Vosk) • Behavior pattern matching • Response cache • "
        "Virtual keyboard • PC control commands • Activity tracking<br><br>"
        "<b>Requires internet:</b><br>"
        "• Claude API (code analysis, complex reasoning) • "
        "Auto-updater (GitHub Releases) • Screenshot Vision analysis<br><br>"
        "<b>Optional local LLM (Ollama):</b><br>"
        "Enable Agent Mode in Settings to route casual conversation through a local model "
        "(Llama 3, Mistral, etc.) — completely free and private. Claude remains the fallback "
        "for complex tasks. Set your model with <code style='color:#45A29E;'>ollamamodel &lt;name&gt;</code>."));

    // ── 7. Keyboard shortcuts ──
    h += S(QStringLiteral("⌘"), QStringLiteral("Keyboard Shortcuts"),
        QStringLiteral(
        "<table style='margin:4px 0;'>")
        + row(QStringLiteral("Enter"), QStringLiteral("Send message"))
        + row(QStringLiteral("Ctrl+O"), QStringLiteral("Attach files"))
        + row(QStringLiteral("Drag & Drop"), QStringLiteral("Drop files into the window to attach"))
        + row(QStringLiteral("Ctrl+C"), QStringLiteral("Copy selected text from chat log"))
        + QStringLiteral("</table>"));

    if (m_indexer->fileCount() > 0) {
        h += S(QStringLiteral("📁"), QStringLiteral("Active Project"),
            QStringLiteral("<b>%1</b> — %2 files, %3 symbols indexed<br>"
                           "Code fragments auto-attach to coding queries. "
                           "Ask JARVIS to explain, refactor, or extend any part of the project.")
                .arg(QFileInfo(m_indexer->projectRoot()).fileName())
                .arg(m_indexer->fileCount())
                .arg(m_indexer->symbolCount()));
    }

    return h;
}

// ============================================================
// Виртуальная клавиатура
// ============================================================

QString Jarvis::cmdTypeText(const QString& input)
{
    QString text = extractArg(input, {QStringLiteral("напечатай "),
                                      QStringLiteral("type ")});
    if (text.isEmpty()) {
        return QStringLiteral("What should I type? Provide the text.");
    }

    m_keyEmulator->pressCombo({VK_MENU, VK_TAB});
    QThread::msleep(300);
    m_keyEmulator->typeText(text, 30);
    return QStringLiteral("Typing: ") + text;
}

QString Jarvis::cmdPressKey(const QString& input)
{
    QString keyName = extractArg(input, {QStringLiteral("нажми "),
                                         QStringLiteral("press ")});
    if (keyName.isEmpty()) {
        return QStringLiteral("Which key? Specify it.");
    }

    WORD vk = parseVirtualKey(keyName);
    if (vk == 0) {
        return QStringLiteral("Unknown key: ") + keyName;
    }

    m_keyEmulator->pressKey(vk);
    return QStringLiteral("Pressing: ") + keyName;
}

QString Jarvis::cmdCombo(const QString& input)
{
    QString comboStr = extractArg(input, {QStringLiteral("комбо "),
                                          QStringLiteral("combo ")});
    if (comboStr.isEmpty()) {
        return QStringLiteral("Specify the combo. Example: combo ctrl+c");
    }

    QStringList parts = comboStr.toLower().split(QStringLiteral("+"),
                                                  Qt::SkipEmptyParts);
    std::vector<WORD> keys;
    for (const auto& part : parts) {
        WORD vk = parseVirtualKey(part.trimmed());
        if (vk == 0) {
            return QStringLiteral("Unknown key: ") + part.trimmed();
        }
        keys.push_back(vk);
    }

    if (keys.empty()) {
        return QStringLiteral("Couldn't parse that combo.");
    }

    m_keyEmulator->pressCombo(
        std::initializer_list<WORD>(keys.data(), keys.data() + keys.size())
    );
    return QStringLiteral("Executing combo: ") + comboStr;
}

// ============================================================
// Task Manager slots
// ============================================================

qint64 Jarvis::addTask(const QString& title, const QString& category,
                        const QString& priority, const QDateTime& deadline)
{
    DbTask t;
    t.userId   = m_currentUserId;
    t.title    = title;
    t.category = category;
    t.status   = QStringLiteral("Todo");
    t.priority = priority;
    t.deadline = deadline;
    return DatabaseManager::instance().addTask(t);
}

bool Jarvis::updateTaskStatus(qint64 taskId, const QString& newStatus)
{
    auto task = DatabaseManager::instance().getTask(taskId);
    if (!task) return false;
    task->status = newStatus;
    return DatabaseManager::instance().updateTask(*task);
}

QString Jarvis::getOverdueTasksSummary() const
{
    return TaskNotifications::checkDeadlines(m_currentUserId, m_uiEnglish);
}

QList<DbTask> Jarvis::getOverdueTasks(int withinHours) const
{
    return DatabaseManager::instance().getOverdueTasks(m_currentUserId, withinHours);
}

// ============================================================
// FileOrganizer facade
// ============================================================

bool Jarvis::organizePathAllowed(const QString& path)
{
    return SystemController::isPathAllowedForOrganize(path);
}

QString Jarvis::organizeApplyPlan(const OrganizePlan& plan)
{
    if (!m_pcCommands) return QString();
    return FileOrganizer::instance().applyPlan(plan, m_pcCommands->controller()->system());
}

bool Jarvis::organizeUndoLast()
{
    if (!m_pcCommands) return false;
    return FileOrganizer::instance().undoLastBatch(m_pcCommands->controller()->system());
}

// ============================================================
// Дозапрос контекста моделью: [NEED:...]
// ============================================================
//
// Автоподбор файлов по ключевым словам (buildProjectContext) угадывает
// нужное далеко не всегда: пользователь пишет «почини озвучку», а код
// лежит в voice_synthesis_manager.cpp, где слова «озвучка» нет вовсе.
// Раньше в таком случае модель писала «покажи мне файл X» — и ход
// уходил впустую, потому что показать его было некому: JARVIS уже
// закончил ответ.
//
// Теперь модель просит недостающее прямо в ответе, JARVIS читает это с
// диска сам и повторяет запрос. Пользователь видит один финальный ответ
// и короткую строку статуса о том, что именно JARVIS смотрел.

QStringList Jarvis::parseContextRequests(const QString& response) const
{
    static const QRegularExpression reNeed(
        QStringLiteral(R"(\[NEED:([^\]\n]{1,200})\])"));

    QStringList requests;
    auto it = reNeed.globalMatch(response);
    while (it.hasNext()) {
        const QString req = it.next().captured(1).trimmed();
        if (req.isEmpty() || requests.contains(req)) continue;
        requests.append(req);
        if (requests.size() >= MAX_CONTEXT_REQUESTS) break;
    }
    return requests;
}

QString Jarvis::resolveContextRequest(const QString& request) const
{
    const QString req = request.trimmed();
    if (req.isEmpty()) return QString();

    // Форма запроса: "kind:argument". Без двоеточия — это либо ключевое
    // слово ("assets", "profile", "tree"), либо просто путь к файлу.
    QString kind = req.section(QChar(':'), 0, 0).trimmed().toLower();
    QString arg  = req.section(QChar(':'), 1).trimmed();

    static const QStringList knownKinds = {
        QStringLiteral("file"), QStringLiteral("symbol"), QStringLiteral("grep"),
        QStringLiteral("uses"), QStringLiteral("tree"), QStringLiteral("assets"),
        QStringLiteral("profile"), QStringLiteral("map")
    };
    if (!knownKinds.contains(kind)) {
        arg  = req;
        kind = QStringLiteral("file");
    }

    const QString header = QStringLiteral("### [NEED:") + req + QStringLiteral("]\n");

    if (kind == QStringLiteral("profile")) {
        const QString brief = m_projectProfile ? m_projectProfile->brief(3500) : QString();
        return header + (brief.isEmpty() ? QStringLiteral("Project profile is not built yet.\n")
                                         : brief);
    }

    if (kind == QStringLiteral("assets")) {
        if (!m_projectProfile) return header + QStringLiteral("No project profile.\n");
        if (arg.isEmpty()) return header + m_projectProfile->assetsReport(60);

        const auto found = m_projectProfile->findAssets(arg, 40);
        if (found.isEmpty())
            return header + QStringLiteral("No assets match '") + arg + QStringLiteral("'.\n");

        QString out = header;
        for (const auto& asset : found) {
            out += asset.relativePath + QStringLiteral(" [") + asset.kind + QChar(']');
            if (!asset.referenced) out += QStringLiteral(" — NOT referenced");
            out += QChar('\n');
        }
        return out;
    }

    if (kind == QStringLiteral("map")) {
        return header + m_indexer->projectMap();
    }

    if (kind == QStringLiteral("uses")) {
        const QStringList users = m_indexer->whoIncludes(arg);
        if (users.isEmpty())
            return header + QStringLiteral("Nothing includes '") + arg + QStringLiteral("'.\n");
        return header + QStringLiteral("Included by:\n  ")
             + users.mid(0, 40).join(QStringLiteral("\n  ")) + QChar('\n');
    }

    if (kind == QStringLiteral("tree")) {
        QStringList files = m_indexer->allFiles();
        if (!arg.isEmpty()) {
            QStringList filtered;
            for (const QString& f : files) {
                if (f.startsWith(arg, Qt::CaseInsensitive)) filtered.append(f);
            }
            files = filtered;
        }
        if (files.isEmpty())
            return header + QStringLiteral("No indexed files under '") + arg + QStringLiteral("'.\n");
        if (files.size() > 200) files = files.mid(0, 200);
        return header + files.join(QChar('\n')) + QChar('\n');
    }

    if (kind == QStringLiteral("grep")) {
        const auto hits = m_indexer->grep(arg, 30);
        if (hits.isEmpty())
            return header + QStringLiteral("No matches for '") + arg + QStringLiteral("'.\n");

        QString out = header;
        for (const auto& hit : hits) {
            out += hit.filePath + QChar(':') + QString::number(hit.line)
                 + QStringLiteral(": ") + hit.lineText.left(200) + QChar('\n');
        }
        return out;
    }

    if (kind == QStringLiteral("symbol")) {
        // Принимаем и "Class::method", и просто "method".
        const QString name = arg.section(QStringLiteral("::"), -1);
        auto symbols = m_indexer->findSymbol(name, true);
        if (symbols.isEmpty()) symbols = m_indexer->findSymbol(name, false);
        if (symbols.isEmpty())
            return header + QStringLiteral("Symbol '") + arg + QStringLiteral("' not found.\n");

        QString out = header;
        int shown = 0;
        for (const auto& sym : symbols) {
            if (shown >= 3) break;
            out += QStringLiteral("--- ") + sym.filePath + QStringLiteral(" (")
                 + sym.kindToString() + QStringLiteral(", L")
                 + QString::number(sym.lineStart) + QStringLiteral(") ---\n");
            out += m_indexer->getCodeSnippet(sym, 6);
            out += QChar('\n');
            shown++;
        }
        return out;
    }

    // --- kind == "file" ---
    QString relPath = arg;
    relPath.replace(QChar('\\'), QChar('/'));

    QString absPath = QDir(m_indexer->projectRoot()).filePath(relPath);
    if (!QFile::exists(absPath)) {
        // Модель могла назвать файл по имени, без пути — ищем в индексе.
        const auto candidates = m_indexer->findFile(relPath.section(QChar('/'), -1));
        if (candidates.isEmpty()) {
            return header + QStringLiteral("File '") + arg
                 + QStringLiteral("' not found in the project.\n");
        }
        absPath = candidates.first().filePath;
        relPath = candidates.first().relativePath;
    }

    const QFileInfo fi(absPath);
    constexpr qint64 MAX_SERVED_FILE_BYTES = 120000;
    if (fi.size() > MAX_SERVED_FILE_BYTES) {
        return header + QStringLiteral("File is large (")
             + QString::number(fi.size() / 1024) + QStringLiteral(" KB) — first 800 lines:\n")
             + m_indexer->getFileLines(absPath, 1, 800);
    }

    const QString body = m_indexer->getFileLines(absPath, 1, 100000);
    if (body.isEmpty())
        return header + QStringLiteral("File '") + relPath + QStringLiteral("' is empty or unreadable.\n");

    return header + QStringLiteral("--- ") + relPath + QStringLiteral(" ---\n") + body;
}

bool Jarvis::tryServeContextRequests(const QString& userInput,
                                      const QString& response,
                                      bool hadAttachments)
{
    if (m_indexer->projectRoot().isEmpty()) return false;
    if (m_contextRounds >= MAX_CONTEXT_ROUNDS) return false;

    // Если модель уже пишет файл — контекст ей больше не нужен, а второй
    // запрос стоил бы денег и потерял бы наработанное.
    if (response.contains(QStringLiteral("[FILE:"))
        || response.contains(QStringLiteral("[DIFF:"))
        || response.contains(QStringLiteral("[MKDIR:"))
        || response.contains(QStringLiteral("[DELETE:"))) {
        return false;
    }

    const QStringList requests = parseContextRequests(response);
    if (requests.isEmpty()) return false;

    QString context;
    QStringList servedLabels;
    for (const QString& req : requests) {
        if (context.size() >= MAX_CONTEXT_CHARS) break;
        const QString chunk = resolveContextRequest(req);
        if (chunk.isEmpty()) continue;
        context += chunk + QChar('\n');
        servedLabels.append(req);
    }
    if (context.isEmpty()) return false;
    if (context.size() > MAX_CONTEXT_CHARS)
        context = context.left(MAX_CONTEXT_CHARS) + QStringLiteral("\n...(truncated)\n");

    m_contextRounds++;

    emit asyncResponseReady(
        (m_uiEnglish ? QStringLiteral("🔍 Reading: ") : QStringLiteral("🔍 Смотрю: "))
        + servedLabels.join(QStringLiteral(", ")));

    const QString followUp =
        QStringLiteral("[CONTEXT DELIVERY — requested by you via [NEED:...]]\n")
        + context
        + QStringLiteral("\n[/CONTEXT DELIVERY]\n"
          "This is the project content you asked for, read from disk. "
          "Now answer the original request in full. Ask for more context with "
          "[NEED:...] only if it is genuinely required — you have ")
        + QString::number(MAX_CONTEXT_ROUNDS - m_contextRounds)
        + QStringLiteral(" more chance(s). Original request: ") + userInput;

    m_claudeApi->sendMessage(followUp,
        [this, userInput, hadAttachments](bool ok, const QString& resp) {
            if (ok) {
                handleClaudeCodeResponse(userInput, resp, hadAttachments);
            } else if (!emitOfflineAnswer(userInput)) {
                // Дозапрос не дошёл — счётчик сбрасываем, иначе следующий
                // ход начнётся с уже израсходованными раундами.
                m_contextRounds = 0;
                emit asyncResponseError(resp);
            }
        });

    return true;
}
