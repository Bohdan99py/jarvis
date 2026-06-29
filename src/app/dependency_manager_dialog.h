#pragma once
// ============================================================
// dependency_manager_dialog.h — Component Manager UI
//
// Shows all optional JARVIS dependencies with install/status:
//   • Vosk (speech recognition models)
//   • OpenCV (security camera, face detection)
//   • Tesseract (OCR)
//   • Mermaid CLI (diagram rendering)
//
// Each component shows: status, size, install/remove button,
// progress bar during download.
// ============================================================

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QNetworkAccessManager>

struct ComponentInfo {
    QString id;
    QString name;
    QString description;
    QString downloadUrl;
    QString installPath;     // where to check / install
    QString checkFile;       // file that proves it's installed
    QString sizeHuman;       // "174 MB", "45 MB"
    bool    installed = false;
};

class ComponentCard : public QFrame
{
    Q_OBJECT
public:
    explicit ComponentCard(const ComponentInfo& info, QWidget* parent = nullptr);

    void refresh();
    void setProgress(int percent);
    void setDownloading(bool active);
    void setInstalled(bool ok);
    const ComponentInfo& info() const { return m_info; }

signals:
    void installRequested(const QString& componentId);
    void openFolderRequested(const QString& path);

private:
    ComponentInfo m_info;
    QLabel*       m_statusLabel = nullptr;
    QLabel*       m_sizeLabel   = nullptr;
    QPushButton*  m_actionBtn   = nullptr;
    QPushButton*  m_folderBtn   = nullptr;
    QProgressBar* m_progress    = nullptr;
};

class DependencyManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DependencyManagerDialog(QWidget* parent = nullptr);

private slots:
    void onInstallComponent(const QString& id);

private:
    QList<ComponentInfo> buildComponentList() const;
    void installOpenCV(ComponentCard* card);
    void installVosk(ComponentCard* card);
    void installMermaidCli(ComponentCard* card);
    void downloadFile(const QString& url, const QString& dest,
                      ComponentCard* card,
                      std::function<void(bool)> onComplete);

    QVBoxLayout*          m_layout  = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QMap<QString, ComponentCard*> m_cards;
};
