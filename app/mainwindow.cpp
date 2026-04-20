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
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("Доступно обновление v") + newVersion + QStringLiteral("!"),
                  Theme::LogColors::system);

        // Показываем диалог
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            QStringLiteral("Обновление J.A.R.V.I.S."),
            QStringLiteral("Доступна новая версия v%1.\n\n%2\n\nУстановить обновление?")
                .arg(newVersion, releaseNotes.left(300)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );

        if (reply == QMessageBox::Yes) {
            appendLog(QStringLiteral("СИСТЕМА"),
                      QStringLiteral("Скачиваю обновление..."),
                      Theme::LogColors::system);
            m_jarvis->autoUpdater()->downloadAndInstall(downloadUrl);
        }
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
        m_status->setText(QStringLiteral("Обновление: %1%").arg(percent));
    });

    connect(updater, &AutoUpdater::downloadFinished,
            this, [this](const QString& path) {
        Q_UNUSED(path)
        appendLog(QStringLiteral("СИСТЕМА"),
                  QStringLiteral("Обновление скачано. Запускаю установщик..."),
                  Theme::LogColors::system);
    });

    connect(updater, &AutoUpdater::updateError,
            this, [this](const QString& error) {
        appendLog(QStringLiteral("ОШИБКА"), error, Theme::LogColors::error);
    });

    buildUI();
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

    // Проверяем наличие API-ключа
    if (m_jarvis->claudeApi()->hasApiKey()) {
        appendLog(QStringLiteral("JARVIS"),
                  QStringLiteral("Claude API подключён. Свободный диалог доступен."),
                  Theme::LogColors::system);
    } else {
        appendLog(QStringLiteral("JARVIS"),
                  QStringLiteral("Введите команду или «помощь». "
                                 "Для свободного диалога: apikey <ваш-ключ>"),
                  Theme::LogColors::jarvis);
    }

    m_input->setFocus();

    // Пульсация индикатора при TTS
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

    appendLog(QStringLiteral("ВЫ"), text, Theme::LogColors::user);

    QString response = m_jarvis->processCommand(text);

    if (!response.isEmpty()) {
        // Синхронный ответ (локальная команда)
        appendLog(QStringLiteral("JARVIS"), response, Theme::LogColors::jarvis);
        m_jarvis->speakAsync(response);
    }
    // Если response пустой — ответ придёт через onAsyncResponse

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

    // Озвучиваем только короткие ответы (до 200 символов)
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
    m_suggestionText->setText(QStringLiteral("💡 ") + description);
    m_suggestionBar->setVisible(true);
}

// ============================================================
// Thinking state (при запросе к API)
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
    setMinimumSize(580, 500);
    resize(640, 600);

    if (auto* scr = QApplication::primaryScreen()) {
        QRect g = scr->availableGeometry();
        move((g.width() - width()) / 2, (g.height() - height()) / 2);
    }

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(16, 12, 16, 12);
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

    // Принять предложение
    connect(m_suggestionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_pendingSuggestionAction.isEmpty()) {
            m_input->setText(m_pendingSuggestionAction);
            onSend();
        }
        m_suggestionBar->setVisible(false);
    });

    // Отклонить предложение
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
    auto* bSpacer = new QWidget(this);
    bSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* clearBtn = new QPushButton(QStringLiteral("Очистить лог"), this);
    clearBtn->setObjectName(QStringLiteral("clearBtn"));
    clearBtn->setFixedWidth(120);

    auto* kbBtn = new QPushButton(QStringLiteral("⌨"), this);
    kbBtn->setObjectName(QStringLiteral("kbToggleBtn"));
    kbBtn->setFixedWidth(40);
    kbBtn->setToolTip(QStringLiteral("Виртуальная клавиатура"));

    bottomBar->addWidget(bSpacer);
    bottomBar->addWidget(clearBtn);
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

    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_log->clear();
        m_input->setFocus();
    });

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
