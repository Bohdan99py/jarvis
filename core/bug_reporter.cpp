// =============================================================================
// bug_reporter.cpp — Баг-репортер через Telegram Bot API
// =============================================================================
//
// Как настроить бота:
//   1. Напишите @BotFather в Telegram → /newbot → получите токен
//   2. Напишите боту что-нибудь (чтобы он мог слать вам сообщения)
//   3. Узнайте свой chat_id: https://api.telegram.org/bot<TOKEN>/getUpdates
//   4. Впишите TOKEN и CHAT_ID в embedded_key.h или в QSettings
//
// API: https://api.telegram.org/bot<TOKEN>/sendMessage
//      https://api.telegram.org/bot<TOKEN>/sendPhoto
// =============================================================================

#include "bug_reporter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include "lang.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QApplication>
#include <QScreen>
#include <QBuffer>
#include <QUrl>
#include <QSettings>
#include <QDateTime>
#include <QSysInfo>
#include <QTimer>
#include <QMessageBox>

// =============================================================================
// Чтобы не светить токен в исходниках — храним в QSettings.
// При первом запуске — пусто, пользователь ничего не видит.
// Разработчик вписывает токен в embedded_key.h через GitHub Secrets.



// =============================================================================
// Конструктор и UI
// =============================================================================

BugReporter::BugReporter(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(IS_EN ? QStringLiteral("Report a Bug — J.A.R.V.I.S.")
                         : QStringLiteral("Сообщить о баге — J.A.R.V.I.S."));
    setMinimumSize(480, 400);
    setModal(true);
    setupUi();
}

void BugReporter::setupUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0a1018; color: #c8e0f0; }"
        "QTextEdit { background-color: #0c1828; color: #c8e0f0; "
        "           border: 1px solid #1a3050; border-radius: 4px; padding: 6px; }"
        "QLabel { color: #80b4d0; }"
        "QCheckBox { color: #80b4d0; }"
        "QPushButton { background-color: #0f2438; color: #00d4ff; "
        "              border: 1px solid #1a5070; border-radius: 4px; "
        "              padding: 6px 14px; font-size: 12px; }"
        "QPushButton:hover { background-color: #1a3a5c; }"
        "QPushButton:disabled { color: #3a5a70; border-color: #1a3050; }"
        "QPushButton#sendBtn { background-color: #003a5c; color: #00d4ff; "
        "                      font-weight: bold; padding: 8px 20px; }"
        "QPushButton#sendBtn:hover { background-color: #005080; }"
        "QProgressBar { background-color: #0c1828; border: 1px solid #1a3050; "
        "               border-radius: 3px; height: 6px; }"
        "QProgressBar::chunk { background-color: #00d4ff; border-radius: 3px; }"
    ));

    auto* vbox = new QVBoxLayout(this);
    vbox->setSpacing(10);
    vbox->setContentsMargins(16, 14, 16, 14);

    // Заголовок
    auto* titleLbl = new QLabel(
        IS_EN ? QStringLiteral("<b>Found a bug? Tell us what happened.</b>")
              : QStringLiteral("<b>Нашли баг? Расскажите что произошло.</b>"), this);
    titleLbl->setStyleSheet(QStringLiteral("color: #00d4ff; font-size: 13px;"));
    vbox->addWidget(titleLbl);

    // Описание
    auto* descLbl = new QLabel(
        IS_EN ? QStringLiteral("Describe the bug (what you did, what you expected, what actually happened):")
              : QStringLiteral("Опишите баг (что делали, что ожидали, что произошло):"), this);
    vbox->addWidget(descLbl);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setPlaceholderText(
        IS_EN ? QStringLiteral("e.g.: I said \"open Chrome\" but JARVIS opened Notepad instead...")
              : QStringLiteral("Например: Написал \"открой Chrome\" — но открылся блокнот..."));
    m_textEdit->setMinimumHeight(120);
    vbox->addWidget(m_textEdit);

    // Прикрепление
    auto* attachRow = new QHBoxLayout();
    m_attachBtn = new QPushButton(
        IS_EN ? QStringLiteral("📎 Attach image") : QStringLiteral("📎 Прикрепить фото"), this);
    m_screenshotBtn = new QPushButton(
        IS_EN ? QStringLiteral("📸 Take screenshot") : QStringLiteral("📸 Скриншот сейчас"), this);
    m_clearBtn = new QPushButton(QStringLiteral("✕"), this);
    m_clearBtn->setFixedWidth(30);
    m_clearBtn->setToolTip(IS_EN ? QStringLiteral("Remove attachment") : QStringLiteral("Убрать вложение"));
    m_clearBtn->setVisible(false);

    attachRow->addWidget(m_attachBtn);
    attachRow->addWidget(m_screenshotBtn);
    attachRow->addWidget(m_clearBtn);
    attachRow->addStretch(1);
    vbox->addLayout(attachRow);

    // Превью прикреплённого изображения
    m_previewLbl = new QLabel(this);
    m_previewLbl->setFixedHeight(90);
    m_previewLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_previewLbl->setStyleSheet(
        QStringLiteral("border: 1px solid #1a3050; border-radius: 4px; "
                       "background: #080e18; padding: 4px;"));
    m_previewLbl->setText(
        IS_EN ? QStringLiteral("No image attached")
              : QStringLiteral("Изображение не прикреплено"));
    m_previewLbl->setVisible(false);
    vbox->addWidget(m_previewLbl);

    // Прогресс-бар
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0); // indeterminate
    m_progress->setVisible(false);
    vbox->addWidget(m_progress);

    // Статус
    m_statusLbl = new QLabel(this);
    m_statusLbl->setAlignment(Qt::AlignCenter);
    m_statusLbl->setVisible(false);
    vbox->addWidget(m_statusLbl);

    // Примечание
    auto* noteLbl = new QLabel(
        IS_EN ? QStringLiteral("ℹ Report is sent privately to the developer via Telegram. "
                               "No personal data is collected automatically.")
              : QStringLiteral("ℹ Репорт отправляется лично разработчику в Telegram. "
                               "Личные данные автоматически не собираются."), this);
    noteLbl->setStyleSheet(QStringLiteral("color: #3a6a90; font-size: 11px;"));
    noteLbl->setWordWrap(true);
    vbox->addWidget(noteLbl);

    // Кнопки
    auto* btnRow = new QHBoxLayout();
    auto* cancelBtn = new QPushButton(
        IS_EN ? QStringLiteral("Cancel") : QStringLiteral("Отмена"), this);
    m_sendBtn = new QPushButton(
        IS_EN ? QStringLiteral("🚀 Send Report") : QStringLiteral("🚀 Отправить"), this);
    m_sendBtn->setObjectName(QStringLiteral("sendBtn"));

    btnRow->addStretch(1);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_sendBtn);
    vbox->addLayout(btnRow);

    connect(m_attachBtn,     &QPushButton::clicked, this, &BugReporter::onAttachScreenshot);
    connect(m_screenshotBtn, &QPushButton::clicked, this, &BugReporter::onTakeScreenshot);
    connect(m_clearBtn,      &QPushButton::clicked, this, &BugReporter::onClearAttachment);
    connect(m_sendBtn,       &QPushButton::clicked, this, &BugReporter::onSend);
    connect(cancelBtn,       &QPushButton::clicked, this, &QDialog::reject);
}

// =============================================================================
// Прикрепление изображения
// =============================================================================

void BugReporter::onAttachScreenshot()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        IS_EN ? QStringLiteral("Select image") : QStringLiteral("Выберите изображение"),
        QDir::homePath(),
        IS_EN ? QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")
              : QStringLiteral("Изображения (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));

    if (path.isEmpty()) return;

    m_attachment = QPixmap(path);
    if (m_attachment.isNull()) return;

    m_previewLbl->setPixmap(
        m_attachment.scaled(m_previewLbl->width() - 8, 82,
                            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_previewLbl->setVisible(true);
    m_clearBtn->setVisible(true);
}

void BugReporter::onTakeScreenshot()
{
    // Прячем диалог на секунду, делаем скриншот, показываем обратно
    hide();
    QTimer::singleShot(600, this, [this]() {
        if (QScreen* screen = QApplication::primaryScreen()) {
            m_attachment = screen->grabWindow(0);
            m_previewLbl->setPixmap(
                m_attachment.scaled(m_previewLbl->width() - 8, 82,
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_previewLbl->setVisible(true);
            m_clearBtn->setVisible(true);
        }
        show();
        raise();
    });
}

void BugReporter::onClearAttachment()
{
    m_attachment = QPixmap();
    m_previewLbl->setText(
        IS_EN ? QStringLiteral("No image attached")
              : QStringLiteral("Изображение не прикреплено"));
    m_previewLbl->setVisible(false);
    m_clearBtn->setVisible(false);
}

// =============================================================================
// Отправка
// =============================================================================

void BugReporter::onSend()
{
    const QString text = m_textEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this,
            IS_EN ? QStringLiteral("Empty report") : QStringLiteral("Пустой репорт"),
            IS_EN ? QStringLiteral("Please describe the bug before sending.")
                  : QStringLiteral("Пожалуйста, опишите баг перед отправкой."));
        return;
    }

    // Открываем GitHub Issues с предзаполненным описанием
    const QString ghUrl = QStringLiteral(
        "https://github.com/Bohdan99py/jarvis/issues/new?title=Bug+Report&body=")
        + QString::fromUtf8(QUrl::toPercentEncoding(text));
    ShellExecuteW(nullptr, L"open",
                  reinterpret_cast<LPCWSTR>(ghUrl.utf16()),
                  nullptr, nullptr, SW_SHOWNORMAL);

    m_statusLbl->setStyleSheet(QStringLiteral("color: #4a9a6a; font-weight: bold;"));
    m_statusLbl->setText(IS_EN
        ? QStringLiteral("✅ Opened GitHub Issues in your browser.")
        : QStringLiteral("✅ Открыли GitHub Issues в браузере."));
    m_statusLbl->setVisible(true);
    QTimer::singleShot(2000, this, &QDialog::accept);
}



void BugReporter::showDialog(QWidget* parent)
{
    auto* dlg = new BugReporter(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}