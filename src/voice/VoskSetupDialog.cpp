// ============================================================
// VoskSetupDialog.cpp — J.A.R.V.I.S.
// Диалог первого запуска + менеджер моделей для Settings
// ============================================================

#include "VoskSetupDialog.h"
#include "lang.h"

#include <QApplication>
#include <QStackedWidget>
#include <QScrollBar>
#include <QSizePolicy>
#include <QScrollArea>
#include <QSpacerItem>
#include <QMessageBox>
#include <QDir>
#include <QStyle>

// ============================================================
//  Helpers — общие стили
// ============================================================

static const QString kCardBase = QStringLiteral(R"(
    QFrame {
        background: #1a1a2e;
        border: 1px solid #2a2a4a;
        border-radius: 8px;
        padding: 4px;
    }
)");

static const QString kCardSelected = QStringLiteral(R"(
    QFrame {
        background: #0d2137;
        border: 2px solid #00b4d8;
        border-radius: 8px;
        padding: 4px;
    }
)");

static const QString kCardInstalled = QStringLiteral(R"(
    QFrame {
        background: #0d2d1a;
        border: 2px solid #2ecc71;
        border-radius: 8px;
        padding: 4px;
    }
)");

static const QString kBtnPrimary = QStringLiteral(R"(
    QPushButton {
        background: #00b4d8;
        color: #000;
        border: none;
        border-radius: 6px;
        padding: 8px 18px;
        font-weight: bold;
        font-size: 13px;
    }
    QPushButton:hover { background: #48cae4; }
    QPushButton:disabled { background: #333; color: #666; }
)");

static const QString kBtnDanger = QStringLiteral(R"(
    QPushButton {
        background: #c0392b;
        color: #fff;
        border: none;
        border-radius: 6px;
        padding: 6px 12px;
        font-size: 12px;
    }
    QPushButton:hover { background: #e74c3c; }
)");

static const QString kBtnSecondary = QStringLiteral(R"(
    QPushButton {
        background: #2c3e50;
        color: #ecf0f1;
        border: 1px solid #34495e;
        border-radius: 6px;
        padding: 6px 12px;
        font-size: 12px;
    }
    QPushButton:hover { background: #34495e; }
)");

// ============================================================
//  ModelCard
// ============================================================

ModelCard::ModelCard(const VoskModelInfo& info,
                     const QString& installDir,
                     bool isEnglish,
                     QWidget* parent)
    : QFrame(parent), m_info(info), m_isEn(isEnglish)
{
    buildUi();
    refresh(installDir);
}

void ModelCard::buildUi()
{
    setFrameShape(QFrame::StyledPanel);
    setMinimumHeight(88);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(kCardBase);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(12);

    // Чекбокс (только для диалога первого запуска)
    m_check = new QCheckBox(this);
    m_check->setFixedSize(20, 20);
    m_check->setStyleSheet(QStringLiteral(
        "QCheckBox::indicator { width:18px; height:18px; }"
        "QCheckBox::indicator:unchecked { border:2px solid #555; border-radius:4px; background:#1a1a2e; }"
        "QCheckBox::indicator:checked   { border:2px solid #00b4d8; border-radius:4px; background:#00b4d8; }"
    ));
    connect(m_check, &QCheckBox::toggled, this, [this](bool c) {
        updateStyle();
        emit checkChanged(m_info.id, c);
    });
    root->addWidget(m_check, 0, Qt::AlignVCenter);

    // Текстовый блок
    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);

    m_nameLabel = new QLabel(m_info.displayName, this);
    m_nameLabel->setStyleSheet(QStringLiteral("color:#ecf0f1; font-size:14px; font-weight:bold; border:none; background:transparent;"));

    m_descLabel = new QLabel(m_info.description, this);
    m_descLabel->setStyleSheet(QStringLiteral("color:#95a5a6; font-size:11px; border:none; background:transparent;"));
    m_descLabel->setWordWrap(true);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#f39c12; font-size:11px; border:none; background:transparent;"));

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#2c3e50; border-radius:2px; }"
        "QProgressBar::chunk { background:#00b4d8; border-radius:2px; }"
    ));
    m_progressBar->hide();

    textCol->addWidget(m_nameLabel);
    textCol->addWidget(m_descLabel);
    textCol->addWidget(m_statusLabel);
    textCol->addWidget(m_progressBar);
    root->addLayout(textCol, 1);

    // Кнопки действий
    auto* btnCol = new QVBoxLayout();
    btnCol->setSpacing(4);

    m_downloadBtn = new QPushButton(QStringLiteral("Скачать"), this);
    m_downloadBtn->setStyleSheet(kBtnSecondary);
    m_downloadBtn->setFixedWidth(90);
    connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
        emit downloadRequested(m_info.id);
    });

    m_deleteBtn = new QPushButton(QStringLiteral("Удалить"), this);
    m_deleteBtn->setStyleSheet(kBtnDanger);
    m_deleteBtn->setFixedWidth(90);
    connect(m_deleteBtn, &QPushButton::clicked, this, [this] {
        emit deleteRequested(m_info.id);
    });

    btnCol->addWidget(m_downloadBtn);
    btnCol->addWidget(m_deleteBtn);
    btnCol->addStretch();
    root->addLayout(btnCol);

    // Рекомендованная — чекнута по умолчанию
    if (m_info.recommended) {
        m_check->setChecked(true);
    }
}

void ModelCard::refresh(const QString& installDir)
{
    m_installed = m_info.isInstalled(installDir);
    setInstalled(m_installed);
}

void ModelCard::setInstalled(bool installed)
{
    m_installed = installed;
    if (installed) {
        m_statusLabel->setText(QStringLiteral("✅ Установлена"));
        m_statusLabel->setStyleSheet(QStringLiteral(
            "color:#2ecc71; font-size:11px; border:none; background:transparent;"));
        m_downloadBtn->hide();
        m_deleteBtn->show();
        m_check->setChecked(true);
        m_check->setEnabled(false);
        setStyleSheet(kCardInstalled);
    } else {
        m_statusLabel->setText(QStringLiteral("Не установлена"));
        m_statusLabel->setStyleSheet(QStringLiteral(
            "color:#7f8c8d; font-size:11px; border:none; background:transparent;"));
        m_downloadBtn->show();
        m_deleteBtn->hide();
        m_check->setEnabled(true);
        updateStyle();
    }
}

void ModelCard::setDownloading(bool active)
{
    m_downloading = active;
    if (active) {
        m_progressBar->show();
        m_progressBar->setValue(0);
        m_downloadBtn->setEnabled(false);
        m_downloadBtn->setText(QStringLiteral("..."));
        m_statusLabel->setText(QStringLiteral("⬇ Скачиваем..."));
        m_statusLabel->setStyleSheet(QStringLiteral(
            "color:#3498db; font-size:11px; border:none; background:transparent;"));
    } else {
        m_progressBar->hide();
        m_downloadBtn->setEnabled(true);
        m_downloadBtn->setText(QStringLiteral("Скачать"));
    }
}

void ModelCard::setDownloadProgress(int percent)
{
    m_progressBar->setValue(percent);
    m_statusLabel->setText(QStringLiteral("⬇ Скачиваем... %1%").arg(percent));
}

bool ModelCard::isChecked() const { return m_check->isChecked(); }
void ModelCard::setChecked(bool v) { m_check->setChecked(v); }

void ModelCard::updateStyle()
{
    if (!m_installed) {
        setStyleSheet(m_check->isChecked() ? kCardSelected : kCardBase);
    }
}

// ============================================================
//  VoskSetupDialog
// ============================================================

VoskSetupDialog::VoskSetupDialog(VoiceInput* voiceInput, QWidget* parent)
    : QDialog(parent), m_voiceInput(voiceInput)
{
    setWindowTitle(IS_EN
        ? QStringLiteral("JARVIS — Voice Input Setup")
        : QStringLiteral("JARVIS — Настройка голосового ввода"));
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    setMinimumSize(600, 500);
    setModal(true);
    setStyleSheet(QStringLiteral("QDialog { background:#0a0a1a; color:#ecf0f1; }"));

    buildUi();

    connect(m_voiceInput, &VoiceInput::setupProgress,       this, &VoskSetupDialog::onProgress);
    connect(m_voiceInput, &VoiceInput::setupComponentReady, this, &VoskSetupDialog::onComponentReady);
    connect(m_voiceInput, &VoiceInput::setupLogMessage,     this, &VoskSetupDialog::onLogMessage);
    connect(m_voiceInput, &VoiceInput::setupFinished,       this, &VoskSetupDialog::onSetupFinished);
}

void VoskSetupDialog::buildUi()
{
    m_stack = new QStackedWidget(this);
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(m_stack);

    // ---- Страница 1: выбор моделей ----
    m_selectionPage = new QWidget();
    auto* selLayout = new QVBoxLayout(m_selectionPage);
    selLayout->setContentsMargins(24, 20, 24, 20);
    selLayout->setSpacing(12);

    auto* titleLabel = new QLabel(
        IS_EN ? QStringLiteral("🎤 Select Voice Models")
               : QStringLiteral("🎤 Выберите голосовые модели"),
        m_selectionPage);
    titleLabel->setStyleSheet(QStringLiteral(
        "color:#00b4d8; font-size:20px; font-weight:bold; background:transparent;"));
    selLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(
        IS_EN ? QStringLiteral("Models run fully offline — internet is only needed for the initial download.\n"
                                "Audio never leaves your computer and is never saved to disk.")
               : QStringLiteral("Модели работают полностью локально — интернет нужен только для скачивания.\n"
                                "Аудио никуда не отправляется и не сохраняется на диск."),
        m_selectionPage);
    subtitleLabel->setStyleSheet(QStringLiteral("color:#7f8c8d; font-size:12px; background:transparent;"));
    subtitleLabel->setWordWrap(true);
    selLayout->addWidget(subtitleLabel);

    selLayout->addSpacing(8);

    auto* scroll = new QScrollArea(m_selectionPage);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border:none; background:transparent; }"
        "QScrollBar:vertical { background:#111; width:6px; }"
        "QScrollBar::handle:vertical { background:#333; border-radius:3px; }"
    ));

    auto* cardsWidget = new QWidget();
    auto* cardsLayout = new QVBoxLayout(cardsWidget);
    cardsLayout->setSpacing(8);

    QString installDir = VoiceInput::voskInstallDir();
    for (const auto& info : VoskModels::catalog()) {
        auto* card = new ModelCard(info, installDir, cardsWidget);
        m_cards.push_back(card);
        cardsLayout->addWidget(card);
    }
    cardsLayout->addStretch();
    scroll->setWidget(cardsWidget);
    selLayout->addWidget(scroll, 1);

    m_startBtn = new QPushButton(
        IS_EN ? QStringLiteral("Install Selected Models")
               : QStringLiteral("Установить выбранные модели"),
        m_selectionPage);
    m_startBtn->setStyleSheet(kBtnPrimary);
    m_startBtn->setMinimumHeight(42);
    connect(m_startBtn, &QPushButton::clicked, this, &VoskSetupDialog::onStartClicked);
    selLayout->addWidget(m_startBtn);

    auto* skipLabel = new QLabel(
        IS_EN ? QStringLiteral("<a href='skip' style='color:#555;'>Skip — configure later in Settings</a>")
               : QStringLiteral("<a href='skip' style='color:#555;'>Пропустить — настроить позже в Settings</a>"),
        m_selectionPage);
    skipLabel->setAlignment(Qt::AlignCenter);
    skipLabel->setOpenExternalLinks(false);
    connect(skipLabel, &QLabel::linkActivated, this, [this](const QString&) {
        VoiceInput::markFirstRunComplete();
        reject();
    });
    selLayout->addWidget(skipLabel);

    // ---- Страница 2: прогресс ----
    m_progressPage = new QWidget();
    auto* progLayout = new QVBoxLayout(m_progressPage);
    progLayout->setContentsMargins(24, 20, 24, 20);
    progLayout->setSpacing(12);

    auto* progTitle = new QLabel(
        IS_EN ? QStringLiteral("⬇ Installing models...")
               : QStringLiteral("⬇ Устанавливаем модели..."),
        m_progressPage);
    progTitle->setStyleSheet(QStringLiteral(
        "color:#00b4d8; font-size:18px; font-weight:bold; background:transparent;"));
    progLayout->addWidget(progTitle);

    m_currentTask = new QLabel(m_progressPage);
    m_currentTask->setStyleSheet(QStringLiteral("color:#ecf0f1; font-size:13px; background:transparent;"));
    progLayout->addWidget(m_currentTask);

    m_itemProgress = new QProgressBar(m_progressPage);
    m_itemProgress->setFixedHeight(8);
    m_itemProgress->setTextVisible(false);
    m_itemProgress->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#2c3e50; border-radius:4px; }"
        "QProgressBar::chunk { background:#00b4d8; border-radius:4px; }"
    ));
    progLayout->addWidget(m_itemProgress);

    m_totalProgress = new QProgressBar(m_progressPage);
    m_totalProgress->setFixedHeight(6);
    m_totalProgress->setTextVisible(false);
    m_totalProgress->setStyleSheet(QStringLiteral(
        "QProgressBar { background:#1a1a2e; border-radius:3px; }"
        "QProgressBar::chunk { background:#2ecc71; border-radius:3px; }"
    ));
    progLayout->addWidget(m_totalProgress);

    auto* logLabel = new QLabel(
        IS_EN ? QStringLiteral("Install log:")
               : QStringLiteral("Лог установки:"),
        m_progressPage);
    logLabel->setStyleSheet(QStringLiteral("color:#7f8c8d; font-size:11px; background:transparent;"));
    progLayout->addWidget(logLabel);

    m_logView = new QTextEdit(m_progressPage);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(QStringLiteral(
        "QTextEdit { background:#0d0d1a; color:#95a5a6; font-family:Consolas,monospace; "
        "font-size:11px; border:1px solid #1a1a3a; border-radius:4px; }"
    ));
    progLayout->addWidget(m_logView, 1);

    // ---- Страница 3: результат ----
    m_resultPage = new QWidget();
    auto* resLayout = new QVBoxLayout(m_resultPage);
    resLayout->setContentsMargins(24, 40, 24, 24);
    resLayout->setSpacing(16);
    resLayout->setAlignment(Qt::AlignCenter);

    m_resultIcon = new QLabel(m_resultPage);
    m_resultIcon->setAlignment(Qt::AlignCenter);
    m_resultIcon->setStyleSheet(QStringLiteral("font-size:48px; background:transparent;"));
    resLayout->addWidget(m_resultIcon);

    m_resultText = new QLabel(m_resultPage);
    m_resultText->setAlignment(Qt::AlignCenter);
    m_resultText->setWordWrap(true);
    m_resultText->setStyleSheet(QStringLiteral("color:#ecf0f1; font-size:14px; background:transparent;"));
    resLayout->addWidget(m_resultText);

    m_finishBtn = new QPushButton(
        IS_EN ? QStringLiteral("Done") : QStringLiteral("Готово"),
        m_resultPage);
    m_finishBtn->setStyleSheet(kBtnPrimary);
    m_finishBtn->setMinimumHeight(42);
    m_finishBtn->setMaximumWidth(200);
    connect(m_finishBtn, &QPushButton::clicked, this, [this] {
        emit setupCompleted();
        accept();
    });
    resLayout->addWidget(m_finishBtn, 0, Qt::AlignCenter);

    m_stack->addWidget(m_selectionPage);
    m_stack->addWidget(m_progressPage);
    m_stack->addWidget(m_resultPage);
    m_stack->setCurrentWidget(m_selectionPage);
}

QStringList VoskSetupDialog::selectedModelIds() const
{
    QStringList ids;
    for (auto* card : m_cards) {
        if (card->isChecked()) ids << card->modelInfo().id;
    }
    return ids;
}

void VoskSetupDialog::onStartClicked()
{
    m_selectedIds = selectedModelIds();
    if (m_selectedIds.isEmpty()) {
        QMessageBox::information(this,
            QStringLiteral("Выбор моделей"),
            QStringLiteral("Выберите хотя бы одну модель для установки."));
        return;
    }

    m_totalComponents = m_selectedIds.size() + 1; // +1 за DLL
    m_doneComponents  = 0;
    m_totalProgress->setMaximum(m_totalComponents);
    m_totalProgress->setValue(0);

    switchToProgress();

    // Запускаем установку через VoiceInput
    m_voiceInput->startSetup(m_selectedIds);
    emit setupStarted(m_selectedIds);
}

void VoskSetupDialog::onProgress(const QString& component, int percent, qint64 total)
{
    Q_UNUSED(total)
    m_currentTask->setText(QStringLiteral("Скачиваем: %1  (%2%)").arg(component).arg(percent));
    m_itemProgress->setValue(percent);
}

void VoskSetupDialog::onComponentReady(const QString& component)
{
    ++m_doneComponents;
    m_totalProgress->setValue(m_doneComponents);
    m_logView->append(QStringLiteral("✅ %1").arg(component));
}

void VoskSetupDialog::onLogMessage(const QString& msg)
{
    m_logView->append(msg);
    // Auto-scroll
    auto* sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void VoskSetupDialog::onSetupFinished(bool success, const QString& error)
{
    if (success) {
        switchToSuccess();
    } else {
        switchToError(error);
    }
}

void VoskSetupDialog::switchToProgress()
{
    m_stack->setCurrentWidget(m_progressPage);
    m_logView->clear();
    m_itemProgress->setValue(0);
}

void VoskSetupDialog::switchToSuccess()
{
    m_resultIcon->setText(QStringLiteral("✅"));
    m_resultText->setText(
        QStringLiteral("Голосовые модели успешно установлены!\n\n"
                       "Скажите «Джарвис» чтобы начать.\n"
                       "Запись активируется только при обнаружении голоса."));
    m_stack->setCurrentWidget(m_resultPage);
    VoiceInput::markFirstRunComplete();
}

void VoskSetupDialog::switchToError(const QString& err)
{
    m_resultIcon->setText(QStringLiteral("❌"));
    m_resultText->setText(
        QStringLiteral("Ошибка установки:\n\n%1\n\n"
                       "Проверьте подключение к интернету и попробуйте снова в Settings → Голос.").arg(err));
    m_finishBtn->setText(QStringLiteral("Закрыть"));
    disconnect(m_finishBtn, nullptr, nullptr, nullptr);
    connect(m_finishBtn, &QPushButton::clicked, this, &QDialog::reject);
    m_stack->setCurrentWidget(m_resultPage);
}

// ============================================================
//  VoskModelManagerWidget (Settings panel)
// ============================================================

VoskModelManagerWidget::VoskModelManagerWidget(VoiceInput* voiceInput, QWidget* parent)
    : QWidget(parent), m_voiceInput(voiceInput)
{
    setStyleSheet(QStringLiteral("background:transparent;"));
    buildUi();

    connect(m_voiceInput, &VoiceInput::modelDownloadProgress, this,
            &VoskModelManagerWidget::onDownloadProgress);
    connect(m_voiceInput, &VoiceInput::modelDownloadFinished, this,
            &VoskModelManagerWidget::onDownloadFinished);
}

void VoskModelManagerWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // Заголовок
    auto* headerRow = new QHBoxLayout();
    auto* title = new QLabel(
        IS_EN ? QStringLiteral("🎤 Voice Models")
               : QStringLiteral("🎤 Голосовые модели"), this);
    title->setStyleSheet(QStringLiteral(
        "color:#ecf0f1; font-size:15px; font-weight:bold; background:transparent;"));
    headerRow->addWidget(title);
    headerRow->addStretch();

    m_diskUsageLabel = new QLabel(this);
    m_diskUsageLabel->setStyleSheet(QStringLiteral(
        "color:#7f8c8d; font-size:11px; background:transparent;"));
    headerRow->addWidget(m_diskUsageLabel);
    root->addLayout(headerRow);

    // Заметка о приватности
    m_privacyNote = new QLabel(
        IS_EN
            ? QStringLiteral("🔒 All models run locally. Audio never leaves your computer. "
                             "Recording activates only when voice is detected (VAD).")
            : QStringLiteral("🔒 Все модели работают локально. Аудио не покидает ваш компьютер. "
                             "Запись активируется только при обнаружении голоса (VAD)."),
        this);
    m_privacyNote->setStyleSheet(QStringLiteral(
        "color:#2ecc71; font-size:11px; background:#0d2d1a; "
        "border:1px solid #27ae60; border-radius:6px; padding:8px;"));
    m_privacyNote->setWordWrap(true);
    root->addWidget(m_privacyNote);

    // Карточки моделей
    QString installDir = VoiceInput::voskInstallDir();
    for (const auto& info : VoskModels::catalog()) {
        auto* card = new ModelCard(info, installDir, this);
        connect(card, &ModelCard::downloadRequested,
                this, &VoskModelManagerWidget::onDownloadRequested);
        connect(card, &ModelCard::deleteRequested,
                this, &VoskModelManagerWidget::onDeleteRequested);
        m_cards.push_back(card);
        root->addWidget(card);
    }

    root->addStretch();
    m_diskUsageLabel->setText(updateDiskUsage());
}

void VoskModelManagerWidget::refresh()
{
    QString installDir = VoiceInput::voskInstallDir();
    for (auto* card : m_cards) {
        card->refresh(installDir);
    }
    m_diskUsageLabel->setText(updateDiskUsage());
}

QString VoskModelManagerWidget::updateDiskUsage() const
{
    QString installDir = VoiceInput::voskInstallDir();
    qint64 total = 0;
    for (const auto& info : VoskModels::catalog()) {
        if (info.isInstalled(installDir))
            total += info.sizeBytes;
    }
    if (total == 0)
        return IS_EN ? QStringLiteral("No models installed")
                      : QStringLiteral("Модели не установлены");
    return IS_EN
        ? QStringLiteral("Disk usage: ~%1").arg(VoskModels::formatSize(total))
        : QStringLiteral("Используется на диске: ~%1").arg(VoskModels::formatSize(total));
}

void VoskModelManagerWidget::onDownloadRequested(const QString& modelId)
{
    // Найти карточку и показать прогресс
    for (auto* card : m_cards) {
        if (card->modelInfo().id == modelId) {
            card->setDownloading(true);
            break;
        }
    }
    m_voiceInput->downloadModel(modelId);
}

void VoskModelManagerWidget::onDeleteRequested(const QString& modelId)
{
    auto info = VoskModels::findById(modelId);
    int ret = QMessageBox::question(this,
        IS_EN ? QStringLiteral("Delete Model")
               : QStringLiteral("Удалить модель"),
        IS_EN ? QStringLiteral("Delete model \"%1\"?\nIt will be removed from disk.")
                     .arg(info.displayName)
               : QStringLiteral("Удалить модель «%1»?\nОна будет удалена с диска.")
                     .arg(info.displayName),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (ret != QMessageBox::Yes) return;

    bool ok = m_voiceInput->deleteModel(modelId);
    if (ok) {
        refresh();
        emit modelsChanged();
    } else {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("Error") : QStringLiteral("Ошибка"),
            IS_EN ? QStringLiteral("Could not delete the model. It may be in use.")
                   : QStringLiteral("Не удалось удалить модель. Возможно, она используется."));
    }
}

void VoskModelManagerWidget::onDownloadProgress(const QString& modelId, int percent, qint64 /*total*/)
{
    for (auto* card : m_cards) {
        if (card->modelInfo().id == modelId) {
            card->setDownloadProgress(percent);
            break;
        }
    }
}

void VoskModelManagerWidget::onDownloadFinished(const QString& modelId, bool success)
{
    for (auto* card : m_cards) {
        if (card->modelInfo().id == modelId) {
            card->setDownloading(false);
            if (success) {
                card->setInstalled(true);
                m_diskUsageLabel->setText(updateDiskUsage());
                emit modelsChanged();
            }
            break;
        }
    }
}