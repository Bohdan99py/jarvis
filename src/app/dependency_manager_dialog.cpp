// ============================================================
// dependency_manager_dialog.cpp — Component Manager UI
// ============================================================

#include "dependency_manager_dialog.h"
#include "jarvis_paths.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QScrollArea>
#include <QUrl>
#include <QDebug>

static const QString kCardStyle = QStringLiteral(
    "ComponentCard { background: rgba(14,18,30,200); "
    "border: 1px solid rgba(0,212,255,0.15); border-radius: 8px; }");

static const QString kBtnInstall = QStringLiteral(
    "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "stop:0 #00d4ff, stop:1 #7c4dff); color: white; font-weight: bold; "
    "border: none; border-radius: 6px; padding: 6px 16px; font-size: 12px; }"
    "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "stop:0 #33e0ff, stop:1 #9b6dff); }");

static const QString kBtnFolder = QStringLiteral(
    "QPushButton { background: rgba(102,252,241,0.08); color: #66FCF1; "
    "border: 1px solid rgba(102,252,241,0.2); border-radius: 4px; "
    "padding: 4px 10px; font-size: 11px; }"
    "QPushButton:hover { background: rgba(102,252,241,0.2); }");

// ============================================================
//  ComponentCard
// ============================================================

ComponentCard::ComponentCard(const ComponentInfo& info, QWidget* parent)
    : QFrame(parent), m_info(info)
{
    setObjectName(QStringLiteral("ComponentCard"));
    setStyleSheet(kCardStyle);
    setMinimumHeight(80);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(12);

    // Left: info
    auto* infoLayout = new QVBoxLayout();
    auto* nameLabel = new QLabel(QStringLiteral("<b style='color:#66FCF1;font-size:14px;'>") + info.name + QStringLiteral("</b>"), this);
    nameLabel->setTextFormat(Qt::RichText);
    infoLayout->addWidget(nameLabel);

    auto* descLabel = new QLabel(info.description, this);
    descLabel->setStyleSheet(QStringLiteral("color: #8892a4; font-size: 11px; background: transparent;"));
    descLabel->setWordWrap(true);
    infoLayout->addWidget(descLabel);

    auto* detailRow = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: bold; background: transparent;"));
    detailRow->addWidget(m_statusLabel);

    m_sizeLabel = new QLabel(info.sizeHuman, this);
    m_sizeLabel->setStyleSheet(QStringLiteral("color: #556; font-size: 11px; background: transparent;"));
    detailRow->addWidget(m_sizeLabel);
    detailRow->addStretch();
    infoLayout->addLayout(detailRow);

    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(6);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(0,0,0,0.3); border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #00d4ff; border-radius: 3px; }"));
    m_progress->setVisible(false);
    infoLayout->addWidget(m_progress);

    layout->addLayout(infoLayout, 1);

    // Right: buttons
    auto* btnLayout = new QVBoxLayout();
    btnLayout->setSpacing(6);

    m_actionBtn = new QPushButton(this);
    m_actionBtn->setStyleSheet(kBtnInstall);
    m_actionBtn->setFixedWidth(100);
    connect(m_actionBtn, &QPushButton::clicked, this, [this]() {
        emit installRequested(m_info.id);
    });
    btnLayout->addWidget(m_actionBtn);

    m_folderBtn = new QPushButton(QStringLiteral("Open folder"), this);
    m_folderBtn->setStyleSheet(kBtnFolder);
    m_folderBtn->setFixedWidth(100);
    connect(m_folderBtn, &QPushButton::clicked, this, [this]() {
        emit openFolderRequested(m_info.installPath);
    });
    btnLayout->addWidget(m_folderBtn);

    layout->addLayout(btnLayout);

    refresh();
}

void ComponentCard::refresh()
{
    m_info.installed = QFileInfo::exists(m_info.installPath + QStringLiteral("/") + m_info.checkFile);
    setInstalled(m_info.installed);
}

void ComponentCard::setProgress(int percent)
{
    m_progress->setValue(percent);
}

void ComponentCard::setDownloading(bool active)
{
    m_progress->setVisible(active);
    m_actionBtn->setEnabled(!active);
    if (active) m_actionBtn->setText(QStringLiteral("..."));
}

void ComponentCard::setInstalled(bool ok)
{
    m_info.installed = ok;
    if (ok) {
        m_statusLabel->setText(QStringLiteral("Installed"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #66FCF1; font-size: 11px; font-weight: bold; background: transparent;"));
        m_actionBtn->setText(QStringLiteral("Reinstall"));
    } else {
        m_statusLabel->setText(QStringLiteral("Not installed"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: #ff5050; font-size: 11px; font-weight: bold; background: transparent;"));
        m_actionBtn->setText(QStringLiteral("Install"));
    }
}

// ============================================================
//  DependencyManagerDialog
// ============================================================

DependencyManagerDialog::DependencyManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("J.A.R.V.I.S. — Component Manager"));
    setMinimumSize(560, 480);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral(
        "QDialog { background: rgba(8,10,18,250); }"
        "QLabel { color: #c0c8d8; }"));

    m_network = new QNetworkAccessManager(this);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Component Manager"), this);
    title->setStyleSheet(QStringLiteral(
        "color: #00d4ff; font-size: 18px; font-weight: bold; letter-spacing: 2px;"));
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Install optional components to unlock features.\n"
        "All downloads are from official sources."), this);
    subtitle->setStyleSheet(QStringLiteral("color: #556; font-size: 11px;"));
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"));

    auto* container = new QWidget(this);
    m_layout = new QVBoxLayout(container);
    m_layout->setSpacing(8);
    m_layout->setContentsMargins(0, 0, 0, 0);

    const auto components = buildComponentList();
    for (const auto& comp : components) {
        auto* card = new ComponentCard(comp, container);
        connect(card, &ComponentCard::installRequested,
                this, &DependencyManagerDialog::onInstallComponent);
        connect(card, &ComponentCard::openFolderRequested,
                this, [](const QString& path) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        m_cards[comp.id] = card;
        m_layout->addWidget(card);
    }

    m_layout->addStretch();
    scroll->setWidget(container);
    mainLayout->addWidget(scroll, 1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setStyleSheet(kBtnFolder);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignRight);
}

QList<ComponentInfo> DependencyManagerDialog::buildComponentList() const
{
    const QString redist = QCoreApplication::applicationDirPath() + QStringLiteral("/../redist");

    return {
        {
            QStringLiteral("opencv"),
            QStringLiteral("OpenCV 4.10.0"),
            QStringLiteral("Face detection, owner recognition, security camera, motion detection."),
            QStringLiteral("https://github.com/opencv/opencv/releases/download/4.10.0/opencv-4.10.0-windows.exe"),
            redist + QStringLiteral("/opencv"),
            QStringLiteral("build/include/opencv2/core.hpp"),
            QStringLiteral("~174 MB"),
        },
        {
            QStringLiteral("vosk_en"),
            QStringLiteral("Vosk English Model"),
            QStringLiteral("Offline speech recognition — English (small, ~40 MB)."),
            QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"),
            JarvisPaths::subPath(QStringLiteral("vosk_models")),
            QStringLiteral("vosk-model-small-en-us-0.15/conf/model.conf"),
            QStringLiteral("~40 MB"),
        },
        {
            QStringLiteral("vosk_ru"),
            QStringLiteral("Vosk Russian Model"),
            QStringLiteral("Offline speech recognition — Russian (small, ~45 MB)."),
            QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip"),
            JarvisPaths::subPath(QStringLiteral("vosk_models")),
            QStringLiteral("vosk-model-small-ru-0.22/conf/model.conf"),
            QStringLiteral("~45 MB"),
        },
        {
            QStringLiteral("mermaid"),
            QStringLiteral("Mermaid CLI (mmdc)"),
            QStringLiteral("Renders Mermaid diagrams as SVG/PNG. Requires Node.js."),
            QStringLiteral("npm://global/@mermaid-js/mermaid-cli"),
            QStringLiteral("C:/"),
            QStringLiteral(""),
            QStringLiteral("~30 MB"),
        },
        {
            QStringLiteral("cascade"),
            QStringLiteral("Haar Cascade (face detection)"),
            QStringLiteral("XML model for frontal face detection. Required for security camera."),
            QStringLiteral("https://raw.githubusercontent.com/opencv/opencv/4.x/data/haarcascades/haarcascade_frontalface_default.xml"),
            JarvisPaths::subPath(QStringLiteral("security")),
            QStringLiteral("haarcascade_frontalface_default.xml"),
            QStringLiteral("~900 KB"),
        },
    };
}

void DependencyManagerDialog::onInstallComponent(const QString& id)
{
    auto* card = m_cards.value(id);
    if (!card) return;

    if (id == QStringLiteral("opencv"))       { installOpenCV(card); return; }
    if (id == QStringLiteral("mermaid"))      { installMermaidCli(card); return; }
    if (id.startsWith(QStringLiteral("vosk"))){ installVosk(card); return; }

    if (id == QStringLiteral("cascade")) {
        downloadFile(card->info().downloadUrl,
                     card->info().installPath + QStringLiteral("/haarcascade_frontalface_default.xml"),
                     card, [card](bool ok) { card->setInstalled(ok); });
        return;
    }
}

void DependencyManagerDialog::downloadFile(const QString& url,
                                            const QString& dest,
                                            ComponentCard* card,
                                            std::function<void(bool)> onComplete)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    card->setDownloading(true);
    card->setProgress(0);

    QNetworkRequest req(QUrl(url));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_network->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [card](qint64 received, qint64 total) {
        if (total > 0)
            card->setProgress(static_cast<int>(received * 100 / total));
    });

    connect(reply, &QNetworkReply::finished, this,
            [reply, dest, card, onComplete]() {
        reply->deleteLater();
        card->setDownloading(false);

        if (reply->error() != QNetworkReply::NoError) {
            card->setProgress(0);
            if (onComplete) onComplete(false);
            return;
        }

        QFile file(dest);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            card->setProgress(100);
            if (onComplete) onComplete(true);
        } else {
            if (onComplete) onComplete(false);
        }
    });
}

void DependencyManagerDialog::installOpenCV(ComponentCard* card)
{
    const QString exePath = card->info().installPath + QStringLiteral("/../opencv-4.10.0-windows.exe");

    if (QFileInfo::exists(exePath)) {
        // Already downloaded — just extract
        card->setDownloading(true);
        card->setProgress(50);

        QProcess* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [card, proc](int exitCode, QProcess::ExitStatus) {
            proc->deleteLater();
            card->setDownloading(false);
            card->setProgress(100);
            card->refresh();
            if (exitCode != 0)
                QMessageBox::warning(card, QStringLiteral("OpenCV"),
                    QStringLiteral("Extraction may have failed. Check redist/opencv/."));
        });
        proc->start(QStringLiteral("powershell.exe"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
            QStringLiteral("Start-Process -FilePath '%1' -ArgumentList '-o\"%2\" -y' -WindowStyle Hidden -Wait")
                .arg(exePath, QFileInfo(exePath).absolutePath())
        });
        return;
    }

    // Download then extract
    downloadFile(card->info().downloadUrl, exePath, card,
                 [this, card, exePath](bool ok) {
        if (!ok) return;
        // Chain: extract after download
        installOpenCV(card);
    });
}

void DependencyManagerDialog::installVosk(ComponentCard* card)
{
    const QString zipPath = card->info().installPath + QStringLiteral("/model.zip");

    downloadFile(card->info().downloadUrl, zipPath, card,
                 [card, zipPath](bool ok) {
        if (!ok) return;

        QProcess* proc = new QProcess();
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                proc, [card, proc, zipPath](int, QProcess::ExitStatus) {
            proc->deleteLater();
            QFile::remove(zipPath);
            card->refresh();
        });
        proc->start(QStringLiteral("powershell.exe"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
            QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(zipPath, card->info().installPath)
        });
    });
}

void DependencyManagerDialog::installMermaidCli(ComponentCard* card)
{
    card->setDownloading(true);

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [card, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        card->setDownloading(false);

        if (exitCode == 0) {
            card->setInstalled(true);
            card->setProgress(100);
        } else {
            QMessageBox::warning(card, QStringLiteral("Mermaid CLI"),
                QStringLiteral("npm install failed. Make sure Node.js is installed.\n"
                               "Manual: npm install -g @mermaid-js/mermaid-cli"));
        }
    });
    proc->start(QStringLiteral("npm"), {
        QStringLiteral("install"), QStringLiteral("-g"),
        QStringLiteral("@mermaid-js/mermaid-cli")
    });
}
