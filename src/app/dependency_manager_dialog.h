#pragma once
// ============================================================
// dependency_manager_dialog.h — Component Manager UI
// ============================================================

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <functional>

enum class ComponentType {
    Runtime,    // works without rebuild (Vosk models, Cascade, Mermaid)
    BuildTime,  // requires recompile after install (OpenCV)
};

struct ComponentInfo {
    QString       id;
    QString       name;
    QString       description;
    QString       downloadUrl;
    QString       installPath;   // absolute path where files land
    QString       checkFile;     // relative to installPath — proves installed
    QString       sizeHuman;
    ComponentType type = ComponentType::Runtime;
    bool          installed = false;
};

class ComponentCard : public QFrame
{
    Q_OBJECT
public:
    explicit ComponentCard(const ComponentInfo& info, QWidget* parent = nullptr);

    void refresh();
    void setProgress(int percent);
    void setStatus(const QString& text, const QString& color = QStringLiteral("#ff5050"));
    void setDownloading(bool active, const QString& label = QString());
    void setInstalled(bool ok);
    void setInstallDisabled(const QString& reasonLabel);
    const ComponentInfo& info() const { return m_info; }

signals:
    void installRequested(const QString& componentId);
    void openFolderRequested(const QString& path);
    void howToRequested(const QString& componentId);

private:
    ComponentInfo m_info;
    QLabel*       m_statusLabel = nullptr;
    QLabel*       m_pathLabel   = nullptr;
    QPushButton*  m_actionBtn   = nullptr;
    QPushButton*  m_folderBtn   = nullptr;
    QPushButton*  m_howToBtn    = nullptr;
    QProgressBar* m_progress    = nullptr;
};

class DependencyManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DependencyManagerDialog(QWidget* parent = nullptr);

private slots:
    void onInstallComponent(const QString& id);
    void onHowTo(const QString& id);

private:
    QList<ComponentInfo> buildComponentList() const;

    static QString tempDownloadDir();

    void installOpenCV(ComponentCard* card);
    void installVosk(ComponentCard* card);
    void installMermaidCli(ComponentCard* card);
    void downloadFile(const QString& url, const QString& dest,
                      ComponentCard* card,
                      std::function<void(bool)> onComplete);

    QVBoxLayout*           m_layout  = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QMap<QString, ComponentCard*> m_cards;
};
