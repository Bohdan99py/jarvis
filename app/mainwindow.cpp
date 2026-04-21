// -------------------------------------------------------
// mainwindow.cpp — Главное окно J.A.R.V.I.S.
// -------------------------------------------------------

#include "mainwindow.h"
#include "jarvis.h"
#include "theme.h"
#include "virtual_keyboard.h"
#include "claude_api.h"
#include "auto_updater.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QScreen>
#include <QTime>
#include <QFont>
#include <QPropertyAnimation>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>

// ============================================================
// Конструктор
// ============================================================

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_jarvis = new Jarvis(this);

    // TTS
    connect(m_jarvis, &Jarvis::speakingChanged,
            this, &MainWindow::onSpeakingChanged);

    // Виртуальная клавиатура
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingStarted,
            this, &MainWindow::onTypingStarted);
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingProgress,
            this, &MainWindow::onTypingProgress);
    connect(m_jarvis->keyEmulator(), &KeyEmulator::typingFinished,
            this, &MainWindow::onTypingFinished);

    // Мозги: асинхронные ответы Claude API
    connect(m_jarvis, &Jarvis::asyncResponseReady,
            this, &MainWindow::onAsyncResponse);
    connect(m_jarvis, &Jarvis::asyncResponseError,
            this, &MainWindow::onAsyncError);

    // Предложения действий
    connect(m_jarvis, &Jarvis::suggestionAvailable,
            this, &MainWindow::onSuggestion);

    // Индикатор «Думаю...» при запросе к API
    connect(m_jarvis->claudeApi(), &ClaudeApi::requestStarted,
            this, [this]() { setThinkingState(true); });
    connect(m_jarvis->claudeApi(), &ClaudeApi::requestFinished,
            this, [this]() { setThinkingState(false); });

    // --- Автообновление ---
    auto* updater = m_jarvis->autoUpdater();

    connect(updater, &AutoUpdater::updateAvailable,
            this, [this](const QString& newVersion, const QString& releaseNotes,
                         const QUrl& downloadUrl) {
        Q_UNUSED(releaseNotes)
        Q_UNUSED(downloadUrl)
        // Показываем панель обновления (не диалог — менее навязчиво)
        showUpdateBar(newVersion);
    });

    connect(updater, &AutoUpdater::noUpdateAvailable,
            this, [this]() {
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("У вас последняя версия (")
                  + QCoreApplication::applicationVersion() + QStringLiteral(")."),
                  Theme::LogColors::system);
    });

    connect(updater, &AutoUpdater::downloadProgress,
            this, [this](int percent) {
        m_status->setText(QStringLiteral("Скачивание: %1%").arg(percent));
        if (m_updateProgress) {
            m_updateProgress->setValue(percent);
            m_updateProgress->setVisible(true);
        }
    });

    connect(updater, &AutoUpdater::downloadFinished,
            this, [this](const QString& path) {
        Q_UNUSED(path)
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("Обновление скачано. Запускаю установщик..."),
                  Theme::LogColors::system);
        hideUpdateBar();
    });

    connect(updater, &AutoUpdater::updateError,
            this, [this](const QString& error) {
        appendLog(QStringLiteral("ОШИБКА"), error, Theme::LogColors::error);
    });

    buildUI();
    buildMenuBar();
    qApp->setStyleSheet(Theme::globalStyleSheet());

    // Приветствие
    int hour = QTime::currentTime().hour();
    QString g;
    if      (hour < 6)  g = QStringLiteral("Доброй ночи");
    else if (hour < 12) g = QStringLiteral("Доброе утро");
    else if (hour < 18) g = QStringLiteral("Добрый день");
    else                g = QStringLiteral("Добрый вечер");

    appendLog(QStringLiteral("JARVIS"),
              g + QStringLiteral("! Система готова. v")
              + QCoreApplication::applicationVersion(),
              Theme::LogColors::jarvis);

    if (m_jarvis->claudeApi()->hasApiKey()) {
        appendLog(QStringLiteral("JARVIS"),
                  QStringLiteral("Claude API подключён. Свободный диалог и вайбкодинг доступны."),
                  Theme::LogColors::system);
    } else {
        appendLog(QStringLiteral("JARVIS"),
                  QStringLiteral("Введите команду или «помощь». "
                                 "Для AI-режима: apikey <ваш-ключ>"),
                  Theme::LogColors::jarvis);
    }

    m_input->setFocus();

    // Пульсация
    m_pulseTimer = new QTimer(this);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulse = !m_pulse;
        if (m_jarvis->isSpeaking()) {
            m_dot->setStyleSheet(m_pulse
                ? QStringLiteral("color: #00ff88; font-size: 18px;")
                : QStringLiteral("color: #005533; font-size: 18px;"));
        }
    });
    m_pulseTimer->start(400);

    // Проверяем обновления при старте (тихо)
    m_jarvis->autoUpdater()->checkForUpdates(true);
}

// ============================================================
// Меню
// ============================================================

void MainWindow::buildMenuBar()
{
    auto* menuBar = this->menuBar();
    menuBar->setStyleSheet(
        QStringLiteral("QMenuBar { background: #0a1018; color: #96c8e6; font-size: 12px; }"
                       "QMenuBar::item:selected { background: #1a3050; }"
                       "QMenu { background: #0c1828; color: #96c8e6; border: 1px solid #1a3050; }"
                       "QMenu::item:selected { background: #00243d; color: #00d4ff; }"));

    // --- Файл ---
    auto* fileMenu = menuBar->addMenu(QStringLiteral("Файл"));

    auto* actClear = fileMenu->addAction(QStringLiteral("Очистить лог"));
    connect(actClear, &QAction::triggered, this, [this]() { m_log->clear(); });

    fileMenu->addSeparator();

    auto* actExit = fileMenu->addAction(QStringLiteral("Выход"));
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // --- Настройки ---
    auto* settingsMenu = menuBar->addMenu(QStringLiteral("Настройки"));

    auto* actApiKey = settingsMenu->addAction(QStringLiteral("API-ключ..."));
    connect(actApiKey, &QAction::triggered, this, [this]() {
        bool ok;
        QString key = QInputDialog::getText(this,
            QStringLiteral("API-ключ Claude"),
            QStringLiteral("Введите ваш Anthropic API-ключ:"),
            QLineEdit::Password,
            QString(), &ok);
        if (ok && !key.trimmed().isEmpty()) {
            m_jarvis->claudeApi()->setApiKey(key.trimmed());
            appendLog(QStringLiteral("СИСТЕМА"),
                      QStringLiteral("API-ключ сохранён. Claude API подключён."),
                      Theme::LogColors::system);
        }
    });

    auto* actVibe = settingsMenu->addAction(QStringLiteral("Вайбкодинг режим"));
    actVibe->setCheckable(true);
    actVibe->setChecked(m_vibeCodingMode);
    connect(actVibe, &QAction::toggled, this, [this](bool checked) {
        m_vibeCodingMode = checked;
        if (checked) {
            m_input->setPlaceholderText(QStringLiteral("Опиши что хочешь создать..."));
            appendLog(QStringLiteral("JARVIS"),
                      QStringLiteral("Вайбкодинг включён. Опишите приложение/скрипт/страницу — "
                                     "я напишу код. Укажите язык (Python, HTML, C++, JS...)"),
                      Theme::LogColors::system);
        } else {
            m_input->setPlaceholderText(QStringLiteral("Введите команду или вопрос..."));
            appendLog(QStringLiteral("JARVIS"),
                      QStringLiteral("Вайбкодинг выключен. Обычный режим."),
                      Theme::LogColors::system);
        }
    });

    settingsMenu->addSeparator();

    auto* actKeyboard = settingsMenu->addAction(QStringLiteral("Виртуальная клавиатура"));
    connect(actKeyboard, &QAction::triggered, this, &MainWindow::toggleKeyboard);

    // --- Обновление ---
    auto* updateMenu = menuBar->addMenu(QStringLiteral("Обновление"));

    auto* actCheck = updateMenu->addAction(QStringLiteral("Проверить обновления"));
    connect(actCheck, &QAction::triggered, this, [this]() {
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("Проверяю обновления..."),
                  Theme::LogColors::system);
        m_jarvis->autoUpdater()->checkForUpdates(false);
    });

    auto* actDownload = updateMenu->addAction(QStringLiteral("Скачать обновление"));
    connect(actDownload, &QAction::triggered, this, [this]() {
        auto* upd = m_jarvis->autoUpdater();
        if (upd->hasPendingUpdate()) {
            appendLog(QStringLiteral("СИСТЕМА"),
                      QStringLiteral("Скачиваю v") + upd->pendingVersion() + QStringLiteral("..."),
                      Theme::LogColors::system);
            upd->downloadPendingUpdate();
        } else {
            appendLog(QStringLiteral("СИСТЕМА"),
                      QStringLiteral("Нет доступных обновлений. Проверьте сначала."),
                      Theme::LogColors::system);
            upd->checkForUpdates(false);
        }
    });

    updateMenu->addSeparator();

    auto* actReleases = updateMenu->addAction(QStringLiteral("Открыть страницу релизов"));
    connect(actReleases, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Bohdan99py/jarvis/releases")));
    });

    // --- Помощь ---
    auto* helpMenu = menuBar->addMenu(QStringLiteral("Помощь"));

    auto* actAbout = helpMenu->addAction(QStringLiteral("О программе"));
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("J.A.R.V.I.S."),
            QStringLiteral("J.A.R.V.I.S. — Personal AI Assistant\n\n"
                           "Версия: v%1\n"
                           "Движок: Claude API (Anthropic)\n"
                           "Автор: Bohdan99py\n\n"
                           "github.com/Bohdan99py/jarvis")
                .arg(QCoreApplication::applicationVersion()));
    });

    auto* actHelp = helpMenu->addAction(QStringLiteral("Список команд"));
    connect(actHelp, &QAction::triggered, this, [this]() {
        m_input->setText(QStringLiteral("помощь"));
        onSend();
    });
}

// ============================================================
// Events
// ============================================================

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_kbVisible) {
            toggleKeyboard();
        } else {
            m_input->setFocus();
            m_input->selectAll();
        }
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// ============================================================
// Slots: ввод
// ============================================================

void MainWindow::onSend()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;
    m_input->clear();

    // Вайбкодинг: добавляем системный префикс к запросу
    if (m_vibeCodingMode && m_jarvis->claudeApi()->hasApiKey()) {
        // Проверяем, не является ли это локальной командой
        if (m_jarvis->claudeApi()->shouldUseApi(text)) {
            text = QStringLiteral(
                "Ты — вайбкодинг-ассистент. Пользователь описывает что хочет создать. "
                "Напиши полный, готовый к запуску код. Не объясняй — только код. "
                "Если нужно несколько файлов — раздели их комментариями с именами файлов.\n\n"
                "Запрос: ") + text;
        }
    }

    appendLog(QStringLiteral("ВЫ"), m_input->placeholderText().isEmpty() ? text : text.left(200),
              Theme::LogColors::user);

    QString response = m_jarvis->processCommand(text);

    if (!response.isEmpty()) {
        appendLog(QStringLiteral("JARVIS"), response, Theme::LogColors::jarvis);
        m_jarvis->speakAsync(response);
    }

    m_input->setFocus();
}

// ============================================================
// Slots: TTS
// ============================================================

void MainWindow::onSpeakingChanged(bool speaking)
{
    if (speaking) {
        m_dot->setStyleSheet(QStringLiteral("color: #00ff88; font-size: 18px;"));
        m_status->setText(QStringLiteral("Говорю..."));
        m_status->setStyleSheet(QStringLiteral("color: #00ff88; font-size: 12px;"));
    } else {
        m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
        m_status->setText(QStringLiteral("В сети"));
        m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
    }
}

// ============================================================
// Slots: виртуальная клавиатура
// ============================================================

void MainWindow::onTypingStarted()
{
    m_dot->setStyleSheet(QStringLiteral("color: #ffaa00; font-size: 18px;"));
    m_status->setText(QStringLiteral("Печатаю..."));
    m_status->setStyleSheet(QStringLiteral("color: #ffaa00; font-size: 12px;"));
}

void MainWindow::onTypingProgress(int current, int total)
{
    m_status->setText(QStringLiteral("Печатаю... %1/%2").arg(current).arg(total));
}

void MainWindow::onTypingFinished()
{
    m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
    m_status->setText(QStringLiteral("В сети"));
    m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
}

// ============================================================
// Slots: Claude API (мозги)
// ============================================================

void MainWindow::onAsyncResponse(const QString& response)
{
    appendLog(QStringLiteral("JARVIS"), response, Theme::LogColors::jarvis);

    if (response.length() <= 200) {
        m_jarvis->speakAsync(response);
    }
}

void MainWindow::onAsyncError(const QString& error)
{
    appendLog(QStringLiteral("ОШИБКА"), error, Theme::LogColors::error);
}

void MainWindow::onSuggestion(const QString& description, const QString& action)
{
    m_pendingSuggestionAction = action;
    m_suggestionText->setText(QStringLiteral("→ ") + description);
    m_suggestionBar->setVisible(true);
}

// ============================================================
// Thinking state
// ============================================================

void MainWindow::setThinkingState(bool thinking)
{
    if (thinking) {
        m_dot->setStyleSheet(QStringLiteral("color: #aa66ff; font-size: 18px;"));
        m_status->setText(QStringLiteral("Думаю..."));
        m_status->setStyleSheet(QStringLiteral("color: #aa66ff; font-size: 12px;"));
        m_input->setEnabled(false);
    } else {
        m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));
        m_status->setText(QStringLiteral("В сети"));
        m_status->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 12px;"));
        m_input->setEnabled(true);
        m_input->setFocus();
    }
}

// ============================================================
// Панель обновления
// ============================================================

void MainWindow::showUpdateBar(const QString& version)
{
    m_updateLabel->setText(QStringLiteral("Доступно обновление v%1").arg(version));
    m_updateProgress->setValue(0);
    m_updateProgress->setVisible(false);
    m_updateBtn->setVisible(true);
    m_updateBar->setVisible(true);

    appendLog(QStringLiteral("СИСТЕМА"),
              QStringLiteral("Доступно обновление v") + version
              + QStringLiteral(". Нажмите кнопку «Обновить» или Обновление → Скачать обновление."),
              Theme::LogColors::system);
}

void MainWindow::hideUpdateBar()
{
    m_updateBar->setVisible(false);
}

// ============================================================
// Клавиатура: показать/скрыть
// ============================================================

void MainWindow::toggleKeyboard()
{
    m_kbVisible = !m_kbVisible;

    if (m_kbAnim) {
        m_kbAnim->stop();
        m_kbAnim->deleteLater();
    }

    m_kbAnim = new QPropertyAnimation(m_kbContainer, "maximumHeight", this);
    m_kbAnim->setDuration(250);
    m_kbAnim->setEasingCurve(QEasingCurve::InOutQuad);

    if (m_kbVisible) {
        m_kbContainer->setVisible(true);
        m_kbAnim->setStartValue(0);
        m_kbAnim->setEndValue(220);
    } else {
        m_kbAnim->setStartValue(m_kbContainer->height());
        m_kbAnim->setEndValue(0);
        connect(m_kbAnim, &QPropertyAnimation::finished, this, [this]() {
            m_kbContainer->setVisible(false);
        });
    }

    m_kbAnim->start(QAbstractAnimation::DeleteWhenStopped);
    m_kbAnim = nullptr;
}

// ============================================================
// UI
// ============================================================

void MainWindow::buildUI()
{
    setWindowTitle(QStringLiteral("J.A.R.V.I.S. — Personal Assistant v")
                   + QCoreApplication::applicationVersion());
    setMinimumSize(620, 540);
    resize(700, 650);

    if (auto* scr = QApplication::primaryScreen()) {
        QRect g = scr->availableGeometry();
        move((g.width() - width()) / 2, (g.height() - height()) / 2);
    }

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(16, 8, 16, 12);
    vbox->setSpacing(8);

    // === Заголовок ===
    auto* topBar = new QHBoxLayout();

    auto* title = new QLabel(QStringLiteral("⬡  J.A.R.V.I.S."), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_dot = new QLabel(QStringLiteral("●"), this);
    m_dot->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 18px;"));

    m_status = new QLabel(QStringLiteral("В сети"), this);
    m_status->setObjectName(QStringLiteral("statusText"));

    topBar->addWidget(title);
    topBar->addWidget(spacer);
    topBar->addWidget(m_dot);
    topBar->addWidget(m_status);
    vbox->addLayout(topBar);

    // === Разделитель ===
    auto* sep = new QLabel(this);
    sep->setObjectName(QStringLiteral("separator"));
    sep->setFixedHeight(1);
    vbox->addWidget(sep);

    // === Панель обновления (скрыта по умолчанию) ===
    m_updateBar = new QWidget(this);
    m_updateBar->setVisible(false);
    m_updateBar->setStyleSheet(
        QStringLiteral("background-color: #0c2018; border: 1px solid #00553a; "
                       "border-radius: 5px; padding: 4px 8px;"));

    auto* updateLayout = new QHBoxLayout(m_updateBar);
    updateLayout->setContentsMargins(10, 6, 10, 6);
    updateLayout->setSpacing(10);

    m_updateLabel = new QLabel(this);
    m_updateLabel->setStyleSheet(
        QStringLiteral("color: #00ff88; font-size: 13px; font-weight: bold; "
                       "border: none; background: transparent;"));

    m_updateProgress = new QProgressBar(this);
    m_updateProgress->setFixedWidth(120);
    m_updateProgress->setFixedHeight(16);
    m_updateProgress->setVisible(false);
    m_updateProgress->setStyleSheet(
        QStringLiteral("QProgressBar { background: #0a1018; border: 1px solid #1a3050; "
                       "border-radius: 3px; text-align: center; color: #96c8e6; font-size: 10px; }"
                       "QProgressBar::chunk { background: #00ff88; border-radius: 2px; }"));

    m_updateBtn = new QPushButton(QStringLiteral("Обновить"), this);
    m_updateBtn->setFixedWidth(90);
    m_updateBtn->setStyleSheet(
        QStringLiteral("background-color: #003d2a; color: #00ff88; border: 1px solid #00663a; "
                       "border-radius: 4px; padding: 4px 12px; font-size: 12px; font-weight: bold;"));

    m_updateDismiss = new QPushButton(QStringLiteral("✕"), this);
    m_updateDismiss->setFixedWidth(28);
    m_updateDismiss->setStyleSheet(
        QStringLiteral("background-color: transparent; color: #3a5a70; border: none; "
                       "font-size: 14px;"));

    updateLayout->addWidget(m_updateLabel, 1);
    updateLayout->addWidget(m_updateProgress);
    updateLayout->addWidget(m_updateBtn);
    updateLayout->addWidget(m_updateDismiss);

    vbox->addWidget(m_updateBar);

    // Кнопка "Обновить"
    connect(m_updateBtn, &QPushButton::clicked, this, [this]() {
        m_updateBtn->setVisible(false);
        m_updateProgress->setVisible(true);
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("Скачиваю обновление..."),
                  Theme::LogColors::system);
        m_jarvis->autoUpdater()->downloadPendingUpdate();
    });

    // Закрыть панель
    connect(m_updateDismiss, &QPushButton::clicked, this, [this]() {
        hideUpdateBar();
    });

    // === Лог ===
    m_log = new QTextEdit(this);
    m_log->setObjectName(QStringLiteral("logArea"));
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    vbox->addWidget(m_log, 1);

    // === Панель предложений (скрыта по умолчанию) ===
    m_suggestionBar = new QWidget(this);
    m_suggestionBar->setVisible(false);
    m_suggestionBar->setStyleSheet(
        QStringLiteral("background-color: #0c1828; border: 1px solid #1a3050; "
                       "border-radius: 4px; padding: 4px 8px;"));

    auto* sugLayout = new QHBoxLayout(m_suggestionBar);
    sugLayout->setContentsMargins(8, 4, 8, 4);
    sugLayout->setSpacing(8);

    m_suggestionText = new QLabel(this);
    m_suggestionText->setStyleSheet(
        QStringLiteral("color: #ffcc00; font-size: 12px; border: none; background: transparent;"));

    m_suggestionBtn = new QPushButton(QStringLiteral("Да"), this);
    m_suggestionBtn->setFixedWidth(50);
    m_suggestionBtn->setStyleSheet(
        QStringLiteral("background-color: #00243d; color: #00d4ff; border: 1px solid #00587a; "
                       "border-radius: 3px; padding: 3px 8px; font-size: 11px;"));

    auto* sugDismiss = new QPushButton(QStringLiteral("✕"), this);
    sugDismiss->setFixedWidth(28);
    sugDismiss->setStyleSheet(
        QStringLiteral("background-color: transparent; color: #3a5a70; border: none; "
                       "font-size: 14px;"));

    sugLayout->addWidget(m_suggestionText, 1);
    sugLayout->addWidget(m_suggestionBtn);
    sugLayout->addWidget(sugDismiss);

    vbox->addWidget(m_suggestionBar);

    connect(m_suggestionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_pendingSuggestionAction.isEmpty()) {
            m_input->setText(m_pendingSuggestionAction);
            onSend();
        }
        m_suggestionBar->setVisible(false);
    });
    connect(sugDismiss, &QPushButton::clicked, this, [this]() {
        m_suggestionBar->setVisible(false);
    });

    // === Ввод + кнопка ===
    auto* inputBar = new QHBoxLayout();
    inputBar->setSpacing(8);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("inputField"));
    m_input->setPlaceholderText(QStringLiteral("Введите команду или вопрос..."));

    auto* sendBtn = new QPushButton(QStringLiteral("▶"), this);
    sendBtn->setObjectName(QStringLiteral("sendBtn"));
    sendBtn->setFixedWidth(50);
    sendBtn->setToolTip(QStringLiteral("Отправить (Enter)"));

    inputBar->addWidget(m_input, 1);
    inputBar->addWidget(sendBtn);
    vbox->addLayout(inputBar);

    // === Нижняя панель ===
    auto* bottomBar = new QHBoxLayout();

    // Индикатор режима
    auto* modeLabel = new QLabel(this);
    modeLabel->setObjectName(QStringLiteral("modeLabel"));
    modeLabel->setStyleSheet(
        QStringLiteral("color: #2a4a60; font-size: 11px; border: none; background: transparent;"));
    modeLabel->setText(QStringLiteral("v") + QCoreApplication::applicationVersion());

    auto* bSpacer = new QWidget(this);
    bSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* kbBtn = new QPushButton(QStringLiteral("⌨"), this);
    kbBtn->setObjectName(QStringLiteral("kbToggleBtn"));
    kbBtn->setFixedWidth(40);
    kbBtn->setToolTip(QStringLiteral("Виртуальная клавиатура"));

    bottomBar->addWidget(modeLabel);
    bottomBar->addWidget(bSpacer);
    bottomBar->addWidget(kbBtn);
    vbox->addLayout(bottomBar);

    // === Виртуальная клавиатура (скрыта) ===
    m_kbContainer = new QWidget(this);
    m_kbContainer->setMaximumHeight(0);
    m_kbContainer->setVisible(false);

    auto* kbLayout = new QVBoxLayout(m_kbContainer);
    kbLayout->setContentsMargins(0, 4, 0, 0);

    m_keyboard = new VirtualKeyboardWidget(m_kbContainer);
    kbLayout->addWidget(m_keyboard);

    vbox->addWidget(m_kbContainer);

    // === Подключения ===
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSend);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSend);
    connect(kbBtn, &QPushButton::clicked, this, &MainWindow::toggleKeyboard);

    connect(m_keyboard, &VirtualKeyboardWidget::charPressed, this, [this](const QString& ch) {
        m_input->insert(ch);
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::backspacePressed, this, [this]() {
        m_input->backspace();
        m_input->setFocus();
    });
    connect(m_keyboard, &VirtualKeyboardWidget::enterPressed, this, &MainWindow::onSend);
}

// ============================================================
// Лог
// ============================================================

void MainWindow::appendLog(const QString& who, const QString& text, const QString& color)
{
    QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QString html = QStringLiteral(
        "<div style='margin-bottom:6px;'>"
        "<span style='color:%1;'>[%2]</span> "
        "<span style='color:%3;font-weight:bold;'>%4:</span> "
        "<span style='color:%5;'>%6</span></div>"
    ).arg(Theme::LogColors::timestamp, time, color, who, color,
          text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));

    m_log->append(html);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}
