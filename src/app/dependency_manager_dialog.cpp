// ============================================================
// dependency_manager_dialog.cpp — Component Manager UI
// ============================================================

#include "dependency_manager_dialog.h"
#include "jarvis_paths.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QScrollArea>
#include <QTextEdit>
#include <QUrl>
#include <QDebug>

// ── Styles ──────────────────────────────────────────────────

static const QString kCardStyle = QStringLiteral(
    "ComponentCard { background: rgba(14,18,30,200); "
    "border: 1px solid rgba(0,212,255,0.15); border-radius: 8px; }");

static const QString kBtnInstall = QStringLiteral(
    "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "stop:0 #00d4ff,stop:1 #7c4dff); color:white; font-weight:bold; "
    "border:none; border-radius:6px; padding:6px 14px; font-size:12px; }"
    "QPushButton:hover { filter:brightness(1.2); }");

static const QString kBtnSec = QStringLiteral(
    "QPushButton { background:rgba(102,252,241,0.08); color:#66FCF1; "
    "border:1px solid rgba(102,252,241,0.2); border-radius:4px; "
    "padding:4px 10px; font-size:11px; }"
    "QPushButton:hover { background:rgba(102,252,241,0.2); }");

// ============================================================
//  Helpers
// ============================================================

QString DependencyManagerDialog::tempDownloadDir()
{
    const QString dir = JarvisPaths::subPath(QStringLiteral("downloads"));
    QDir().mkpath(dir);
    return dir;
}

// ============================================================
//  ComponentCard
// ============================================================

ComponentCard::ComponentCard(const ComponentInfo& info, QWidget* parent)
    : QFrame(parent), m_info(info)
{
    setObjectName(QStringLiteral("ComponentCard"));
    setStyleSheet(kCardStyle);
    setMinimumHeight(90);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(4);

    // Header row: name + buttons
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(10);

    auto* nameLabel = new QLabel(
        QStringLiteral("<b style='color:#66FCF1;font-size:13px;'>") +
        info.name + QStringLiteral("</b>") +
        (info.type == ComponentType::BuildTime
            ? QStringLiteral(" <span style='color:#ff9900;font-size:10px;'>[requires rebuild]</span>")
            : QString()),
        this);
    nameLabel->setTextFormat(Qt::RichText);
    headerRow->addWidget(nameLabel, 1);

    m_howToBtn = new QPushButton(QStringLiteral("? How to"), this);
    m_howToBtn->setStyleSheet(kBtnSec);
    m_howToBtn->setFixedHeight(26);
    connect(m_howToBtn, &QPushButton::clicked, this,
            [this]() { emit howToRequested(m_info.id); });
    headerRow->addWidget(m_howToBtn);

    m_folderBtn = new QPushButton(QStringLiteral("📁"), this);
    m_folderBtn->setStyleSheet(kBtnSec);
    m_folderBtn->setFixedSize(30, 26);
    m_folderBtn->setToolTip(m_info.installPath);
    connect(m_folderBtn, &QPushButton::clicked, this,
            [this]() { emit openFolderRequested(m_info.installPath); });
    headerRow->addWidget(m_folderBtn);

    m_actionBtn = new QPushButton(this);
    m_actionBtn->setStyleSheet(kBtnInstall);
    m_actionBtn->setFixedHeight(28);
    m_actionBtn->setMinimumWidth(90);
    connect(m_actionBtn, &QPushButton::clicked, this,
            [this]() { emit installRequested(m_info.id); });
    headerRow->addWidget(m_actionBtn);

    root->addLayout(headerRow);

    // Description
    auto* descLabel = new QLabel(info.description, this);
    descLabel->setStyleSheet(QStringLiteral(
        "color:#8892a4; font-size:11px; background:transparent;"));
    descLabel->setWordWrap(true);
    root->addWidget(descLabel);

    // Status + path row
    auto* statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "font-size:11px; font-weight:bold; background:transparent;"));
    statusRow->addWidget(m_statusLabel);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setStyleSheet(QStringLiteral(
        "color:#445; font-size:10px; background:transparent;"));
    m_pathLabel->setTextFormat(Qt::PlainText);
    m_pathLabel->setWordWrap(false);
    m_pathLabel->setText(info.installPath + QStringLiteral("/") + info.checkFile);
    statusRow->addWidget(m_pathLabel, 1);

    auto* sizeLabel = new QLabel(info.sizeHuman, this);
    sizeLabel->setStyleSheet(QStringLiteral(
        "color:#445; font-size:11px; background:transparent;"));
    statusRow->addWidget(sizeLabel);

    root->addLayout(statusRow);

    // Progress
    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(5);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar{background:rgba(0,0,0,0.3);border:none;border-radius:3px;}"
        "QProgressBar::chunk{background:#00d4ff;border-radius:3px;}"));
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    refresh();
}

void ComponentCard::refresh()
{
    const QString fullPath = m_info.installPath + QStringLiteral("/") + m_info.checkFile;
    m_info.installed = m_info.checkFile.isEmpty()
        ? false
        : QFileInfo::exists(fullPath);
    setInstalled(m_info.installed);
}

void ComponentCard::setProgress(int percent)
{
    m_progress->setVisible(true);
    m_progress->setValue(percent);
}

void ComponentCard::setStatus(const QString& text, const QString& color)
{
    m_statusLabel->setStyleSheet(
        QStringLiteral("font-size:11px;font-weight:bold;background:transparent;color:")
        + color + QStringLiteral(";"));
    m_statusLabel->setText(text);
}

void ComponentCard::setDownloading(bool active, const QString& label)
{
    m_progress->setVisible(active);
    m_actionBtn->setEnabled(!active);
    if (active) m_actionBtn->setText(label.isEmpty() ? QStringLiteral("...") : label);
}

void ComponentCard::setInstallDisabled(const QString& reasonLabel)
{
    m_actionBtn->setEnabled(false);
    m_actionBtn->setText(reasonLabel);
    setStatus(QStringLiteral("⊘ Not available in this copy"), QStringLiteral("#8892a4"));
}

void ComponentCard::setInstalled(bool ok)
{
    m_info.installed = ok;
    if (ok) {
        if (m_info.type == ComponentType::BuildTime) {
            setStatus(QStringLiteral("✓ Files ready — rebuild JARVIS to activate"),
                      QStringLiteral("#ff9900"));
        } else {
            setStatus(QStringLiteral("✓ Installed"), QStringLiteral("#66FCF1"));
        }
        m_actionBtn->setText(QStringLiteral("Reinstall"));
    } else {
        setStatus(QStringLiteral("✗ Not installed"), QStringLiteral("#ff5050"));
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
    setMinimumSize(620, 520);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral(
        "QDialog{background:rgba(8,10,18,250);}"
        "QLabel{color:#c0c8d8;}"));

    m_network = new QNetworkAccessManager(this);
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20,16,20,16);
    mainLayout->setSpacing(10);

    // Header
    auto* title = new QLabel(QStringLiteral("Component Manager"), this);
    title->setStyleSheet(QStringLiteral(
        "color:#00d4ff;font-size:18px;font-weight:bold;letter-spacing:2px;"));
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    auto* sub = new QLabel(
        QStringLiteral("All components work immediately after install — no rebuild needed"), this);
    sub->setStyleSheet(QStringLiteral("color:#556;font-size:11px;"));
    sub->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(sub);

    // Cards
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{border:none;background:transparent;}"));

    auto* container = new QWidget(this);
    m_layout = new QVBoxLayout(container);
    m_layout->setSpacing(8);
    m_layout->setContentsMargins(0,0,4,0);

    for (const auto& comp : buildComponentList()) {
        auto* card = new ComponentCard(comp, container);
        connect(card, &ComponentCard::installRequested,
                this, &DependencyManagerDialog::onInstallComponent);
        connect(card, &ComponentCard::howToRequested,
                this, &DependencyManagerDialog::onHowTo);
        connect(card, &ComponentCard::openFolderRequested,
                this, [](const QString& path) {
            QString p = path;
            if (!QDir(p).exists()) p = QFileInfo(p).absolutePath();
            if (!QDir(p).exists()) QDir().mkpath(p);
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
        });
        m_cards[comp.id] = card;
        m_layout->addWidget(card);
    }
    m_layout->addStretch();
    scroll->setWidget(container);
    mainLayout->addWidget(scroll, 1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setStyleSheet(kBtnSec);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignRight);
}

QList<ComponentInfo> DependencyManagerDialog::buildComponentList() const
{
    const QString appDir = QCoreApplication::applicationDirPath();

    return {
        // ── OpenCV — RUNTIME (delay-loaded DLL) ──────────────
        {
            QStringLiteral("opencv"),
            QStringLiteral("OpenCV 4.10.0"),
            QStringLiteral("Face detection, owner recognition, security camera. "
                           "Downloads ~174 MB SDK, extracts only the needed DLL (~62 MB) next to JARVIS."),
            QStringLiteral("https://github.com/opencv/opencv/releases/download/4.10.0/opencv-4.10.0-windows.exe"),
            appDir,
            QStringLiteral("opencv_world4100.dll"),
            QStringLiteral("~174 MB download → ~62 MB installed"),
            ComponentType::Runtime,
        },
        // ── Haar Cascade — RUNTIME ────────────────────────────
        {
            QStringLiteral("cascade"),
            QStringLiteral("Haar Cascade (face detection XML)"),
            QStringLiteral("Required by OpenCV for detecting faces. Works without rebuild."),
            QStringLiteral("https://raw.githubusercontent.com/opencv/opencv/4.x/data/haarcascades/haarcascade_frontalface_default.xml"),
            JarvisPaths::subPath(QStringLiteral("security")),
            QStringLiteral("haarcascade_frontalface_default.xml"),
            QStringLiteral("~900 KB"),
            ComponentType::Runtime,
        },
        // ── Vosk EN — RUNTIME ────────────────────────────────
        {
            QStringLiteral("vosk_en"),
            QStringLiteral("Vosk English Speech Model"),
            QStringLiteral("Offline voice recognition — English. ~40 MB."),
            QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"),
            JarvisPaths::subPath(QStringLiteral("vosk_models")),
            QStringLiteral("vosk-model-small-en-us-0.15/conf/model.conf"),
            QStringLiteral("~40 MB"),
            ComponentType::Runtime,
        },
        // ── Vosk RU — RUNTIME ────────────────────────────────
        {
            QStringLiteral("vosk_ru"),
            QStringLiteral("Vosk Russian Speech Model"),
            QStringLiteral("Offline voice recognition — Russian. ~45 MB."),
            QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip"),
            JarvisPaths::subPath(QStringLiteral("vosk_models")),
            QStringLiteral("vosk-model-small-ru-0.22/conf/model.conf"),
            QStringLiteral("~45 MB"),
            ComponentType::Runtime,
        },
        // ── Mermaid CLI — RUNTIME ────────────────────────────
        {
            QStringLiteral("mermaid"),
            QStringLiteral("Mermaid CLI (mmdc)"),
            QStringLiteral("Renders Mermaid diagrams as SVG. Requires Node.js installed first."),
            QStringLiteral("npm://global/@mermaid-js/mermaid-cli"),
            QStringLiteral("C:/"),
            QStringLiteral(""),  // checked via 'where mmdc'
            QStringLiteral("~30 MB"),
            ComponentType::Runtime,
        },
    };
}

// ============================================================
//  Install handlers
// ============================================================

void DependencyManagerDialog::onInstallComponent(const QString& id)
{
    auto* card = m_cards.value(id);
    if (!card) return;

    if (id == QStringLiteral("opencv"))      { installOpenCV(card);    return; }
    if (id == QStringLiteral("mermaid"))     { installMermaidCli(card);return; }
    if (id.startsWith(QStringLiteral("vosk"))){ installVosk(card);     return; }

    // Generic: direct file download (cascade etc.)
    const QString dest = card->info().installPath
                       + QStringLiteral("/") + card->info().checkFile;
    downloadFile(card->info().downloadUrl, dest, card,
                 [card](bool ok) { card->setInstalled(ok); });
}

// ============================================================
//  How-To instructions
// ============================================================

void DependencyManagerDialog::onHowTo(const QString& id)
{
    auto* card = m_cards.value(id);
    const QString installPath = card ? card->info().installPath : QString();

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("How to install"));
    dlg->setMinimumSize(560, 420);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setStyleSheet(QStringLiteral(
        "QDialog{background:rgba(8,10,18,250);}"
        "QLabel,QTextEdit{color:#c0c8d8;background:transparent;}"));

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(20,16,20,16);

    auto* txt = new QTextEdit(dlg);
    txt->setReadOnly(true);
    txt->setStyleSheet(QStringLiteral(
        "QTextEdit{background:rgba(14,18,30,200);border:1px solid rgba(0,212,255,0.15);"
        "border-radius:6px;color:#c0c8d8;font-size:12px;font-family:'Consolas';}"));

    if (id == QStringLiteral("opencv")) {
        txt->setMarkdown(QStringLiteral(
            "# OpenCV 4.10.0 — Installation Guide\n\n"
            "**Type:** Runtime — works immediately after install, no rebuild needed\n\n"
            "## Auto-install\n"
            "Click **Install** — it will:\n"
            "1. Download the OpenCV SDK (~174 MB)\n"
            "2. Extract only the needed DLL (~62 MB) next to JARVIS\n"
            "3. Clean up the rest automatically\n\n"
            "After install, the security camera is ready — "
            "open the Camera menu to enroll your face.\n\n"
            "## Manual install\n"
            "1. Download: https://github.com/opencv/opencv/releases/tag/4.10.0\n"
            "2. Run the .exe installer, extract anywhere\n"
            "3. Copy these files next to `jarvis.exe`:\n"
            "   - `opencv\\build\\x64\\vc16\\bin\\opencv_world4100.dll`\n"
            "   - `opencv\\build\\x64\\vc16\\bin\\opencv_videoio_msmf4100_64.dll`\n"
            "   - `opencv\\build\\etc\\haarcascades\\haarcascade_frontalface_default.xml`\n\n"
            "## Files installed to:\n"
            "```\n") + installPath + QStringLiteral("\n```\n"
        ));
    } else if (id.startsWith(QStringLiteral("vosk"))) {
        txt->setMarkdown(QStringLiteral(
            "# Vosk Speech Model — Installation Guide\n\n"
            "**Type:** Runtime — works immediately, no rebuild needed\n\n"
            "## Auto-install\n"
            "Click **Install** — the model will be downloaded and extracted to:\n"
            "```\n") + installPath + QStringLiteral("\n```\n"
            "JARVIS will detect it automatically on next launch.\n\n"
            "## Manual install\n"
            "1. Download from: https://alphacephei.com/vosk/models\n"
            "2. Extract the zip to: `") + installPath + QStringLiteral("`\n"
            "3. The folder should contain: `conf/model.conf`\n\n"
            "## Which model to choose?\n"
            "- `vosk-model-small-*` — fast, ~40MB, good accuracy\n"
            "- `vosk-model-*` (no 'small') — slower, ~1.4GB, best accuracy\n"
        ));
    } else if (id == QStringLiteral("mermaid")) {
        txt->setMarkdown(QStringLiteral(
            "# Mermaid CLI — Installation Guide\n\n"
            "**Type:** Runtime — works immediately after install\n\n"
            "## Requirements\n"
            "Node.js must be installed first:\n"
            "https://nodejs.org/en/download/\n\n"
            "## Install via Component Manager\n"
            "Click **Install** — runs: `npm install -g @mermaid-js/mermaid-cli`\n\n"
            "## Verify\n"
            "Open Terminal and run:\n"
            "```\nmmdc --version\n```\n"
            "Should print something like: `10.x.x`\n\n"
            "## Without Mermaid CLI\n"
            "Diagrams still work — JARVIS uses a fallback QPainter renderer.\n"
            "Mermaid CLI gives higher quality SVG output.\n"
        ));
    } else {
        txt->setMarkdown(QStringLiteral(
            "# Installation Guide\n\n"
            "Click **Install** to download and install automatically.\n\n"
            "**Install path:**\n```\n") + installPath + QStringLiteral("\n```\n"
        ));
    }

    layout->addWidget(txt, 1);

    // Copy path button
    if (!installPath.isEmpty() && installPath != QStringLiteral("C:/")) {
        auto* copyBtn = new QPushButton(
            QStringLiteral("📋 Copy install path"), dlg);
        copyBtn->setStyleSheet(kBtnSec);
        connect(copyBtn, &QPushButton::clicked, this, [installPath]() {
            QApplication::clipboard()->setText(installPath);
        });
        layout->addWidget(copyBtn, 0, Qt::AlignLeft);
    }

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), dlg);
    closeBtn->setStyleSheet(kBtnSec);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    dlg->show();
}

// ============================================================
//  Download helper (streaming)
// ============================================================

void DependencyManagerDialog::downloadFile(const QString& url,
                                            const QString& dest,
                                            ComponentCard* card,
                                            std::function<void(bool)> onComplete)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    card->setDownloading(true, QStringLiteral("Downloading..."));
    card->setProgress(0);

    const QUrl qurl(url);
    QNetworkRequest req(qurl);
    req.setMaximumRedirectsAllowed(10);
    auto* reply = m_network->get(req);

    auto* outFile = new QFile(dest, this);
    if (!outFile->open(QIODevice::WriteOnly)) {
        card->setDownloading(false);
        delete outFile;
        card->setStatus(QStringLiteral("✗ Cannot write to disk"), QStringLiteral("#ff5050"));
        if (onComplete) onComplete(false);
        return;
    }

    connect(reply, &QNetworkReply::readyRead, this, [reply, outFile]() {
        outFile->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::downloadProgress, this,
            [card](qint64 received, qint64 total) {
        if (total > 0) card->setProgress(static_cast<int>(received * 100 / total));
    });

    connect(reply, &QNetworkReply::finished, this,
            [reply, outFile, card, onComplete]() {
        outFile->close();
        outFile->deleteLater();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            card->setDownloading(false);
            card->setProgress(0);
            card->setStatus(
                QStringLiteral("✗ Download failed: ") + reply->errorString(),
                QStringLiteral("#ff5050"));
            if (onComplete) onComplete(false);
            return;
        }

        card->setProgress(100);
        if (onComplete) onComplete(true);
    });
}

// ============================================================
//  OpenCV: download .exe then extract silently
// ============================================================

void DependencyManagerDialog::installOpenCV(ComponentCard* card)
{
    const QString appDir   = QCoreApplication::applicationDirPath();
    const QString tmpDir   = tempDownloadDir();
    const QString exePath  = tmpDir + QStringLiteral("/opencv-4.10.0-windows.exe");

    auto doExtractAndCopy = [this, card, exePath, tmpDir, appDir]() {
        card->setDownloading(true, QStringLiteral("Extracting SDK..."));
        card->setProgress(50);

        QProcess* proc = new QProcess(this);
        connect(proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, card, proc, tmpDir, appDir, exePath](int code, QProcess::ExitStatus) {
            proc->deleteLater();

            if (code != 0) {
                card->setDownloading(false);
                card->setStatus(QStringLiteral("✗ Extraction failed"), QStringLiteral("#ff5050"));
                card->setProgress(0);
                return;
            }

            card->setDownloading(true, QStringLiteral("Copying DLLs..."));
            card->setProgress(75);

            // Copy only the runtime DLLs we need (release versions)
            const QString binDir = tmpDir + QStringLiteral("/opencv/build/x64/vc16/bin");
            const QStringList dlls = {
                QStringLiteral("opencv_world4100.dll"),
                QStringLiteral("opencv_videoio_msmf4100_64.dll"),
            };

            bool anyOk = false;
            for (const auto& dllName : dlls) {
                const QString src = binDir + QStringLiteral("/") + dllName;
                const QString dst = appDir + QStringLiteral("/") + dllName;
                if (QFileInfo::exists(src)) {
                    QFile::remove(dst);
                    if (QFile::copy(src, dst)) {
                        qDebug() << "[ComponentMgr] Copied" << dllName << "→" << appDir;
                        anyOk = true;
                    } else {
                        qWarning() << "[ComponentMgr] Failed to copy" << dllName;
                    }
                }
            }

            // Also copy Haar cascade next to exe if not already there
            const QString cascadeSrc = tmpDir +
                QStringLiteral("/opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml");
            const QString cascadeDst = appDir +
                QStringLiteral("/haarcascade_frontalface_default.xml");
            if (QFileInfo::exists(cascadeSrc) && !QFileInfo::exists(cascadeDst))
                QFile::copy(cascadeSrc, cascadeDst);

            // Cleanup: remove the extracted SDK and installer (they're huge)
            QDir(tmpDir + QStringLiteral("/opencv")).removeRecursively();
            QFile::remove(exePath);

            card->setDownloading(false);
            card->refresh();

            if (anyOk && QFileInfo::exists(
                    appDir + QStringLiteral("/opencv_world4100.dll"))) {
                card->setProgress(100);
                card->setInstalled(true);
            } else {
                card->setStatus(
                    QStringLiteral("✗ Copy failed — try running JARVIS as administrator"),
                    QStringLiteral("#ff5050"));
                card->setProgress(0);
            }
        });

        proc->start(QStringLiteral("powershell.exe"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
            QStringLiteral("Start-Process -FilePath '%1' -ArgumentList '-o\"%2\" -y' "
                           "-WindowStyle Hidden -Wait").arg(exePath, tmpDir)
        });
    };

    if (QFileInfo::exists(exePath)) {
        doExtractAndCopy();
        return;
    }

    downloadFile(card->info().downloadUrl, exePath, card,
                 [doExtractAndCopy](bool ok) { if (ok) doExtractAndCopy(); });
}

// ============================================================
//  Vosk: download zip and extract with PowerShell
// ============================================================

void DependencyManagerDialog::installVosk(ComponentCard* card)
{
    const QString zipPath = card->info().installPath + QStringLiteral("/model.zip");

    downloadFile(card->info().downloadUrl, zipPath, card,
                 [this, card, zipPath](bool ok) {
        if (!ok) return;

        card->setDownloading(true, QStringLiteral("Extracting..."));
        QProcess* proc = new QProcess(this);
        connect(proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this, [card, proc, zipPath](int, QProcess::ExitStatus) {
            proc->deleteLater();
            QFile::remove(zipPath);
            card->setDownloading(false);
            card->refresh();
        });
        proc->start(QStringLiteral("powershell.exe"), {
            QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
            QStringLiteral("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(zipPath, card->info().installPath)
        });
    });
}

// ============================================================
//  Mermaid CLI via npm
// ============================================================

void DependencyManagerDialog::installMermaidCli(ComponentCard* card)
{
    card->setDownloading(true, QStringLiteral("npm install..."));

    QProcess* proc = new QProcess(this);
    connect(proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [card, proc](int code, QProcess::ExitStatus) {
        proc->deleteLater();
        card->setDownloading(false);

        // Check if mmdc is now on PATH
        QProcess check;
        check.start(QStringLiteral("where"), {QStringLiteral("mmdc")});
        check.waitForFinished(3000);
        const bool found = check.exitCode() == 0;

        if (code == 0 && found) {
            card->setInstalled(true);
            card->setProgress(100);
        } else {
            card->setStatus(
                QStringLiteral("✗ npm failed. Make sure Node.js is installed. "
                               "Click '? How to' for manual steps."),
                QStringLiteral("#ff5050"));
        }
    });

    proc->start(QStringLiteral("npm"), {
        QStringLiteral("install"), QStringLiteral("-g"),
        QStringLiteral("@mermaid-js/mermaid-cli")
    });
}
