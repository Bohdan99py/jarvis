#pragma once
// ============================================================
// VoskSetupDialog.h — J.A.R.V.I.S.
//
// Два сценария использования:
//   1. VoskSetupDialog — диалог первого запуска: выбор моделей + скачивание
//   2. VoskModelManagerWidget — виджет для Settings → "Голосовые модели"
//      (докачать, удалить, статус каждой модели)
// ============================================================

#include <QDialog>
#include <QWidget>
#include <QStackedWidget>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QTextEdit>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QMap>

#include "voice_input.h"

// ============================================================
//  ModelCard — карточка одной модели
// ============================================================

class ModelCard : public QFrame
{
    Q_OBJECT
public:
    explicit ModelCard(const VoskModelInfo& info,
                       const QString& installDir,
                       bool isEnglish = false,
                       QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool v);
    const VoskModelInfo& modelInfo() const { return m_info; }

    void refresh(const QString& installDir);
    void setDownloadProgress(int percent);
    void setDownloading(bool active);
    void setInstalled(bool installed);

signals:
    void checkChanged(const QString& modelId, bool checked);
    void downloadRequested(const QString& modelId);
    void deleteRequested(const QString& modelId);

private:
    void buildUi();
    void updateStyle();

    VoskModelInfo   m_info;
    bool            m_isEn          = false;
    QCheckBox*      m_check         = nullptr;
    QLabel*         m_nameLabel     = nullptr;
    QLabel*         m_descLabel     = nullptr;
    QLabel*         m_statusLabel   = nullptr;
    QProgressBar*   m_progressBar   = nullptr;
    QPushButton*    m_downloadBtn   = nullptr;
    QPushButton*    m_deleteBtn     = nullptr;
    bool            m_installed     = false;
    bool            m_downloading   = false;
};

// ============================================================
//  VoskSetupDialog — первый запуск
// ============================================================

class VoskSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VoskSetupDialog(VoiceInput* voiceInput, QWidget* parent = nullptr);

    // Возвращает выбранные пользователем ID моделей
    QStringList selectedModelIds() const;

signals:
    void setupStarted(const QStringList& modelIds);
    void setupCompleted();

private slots:
    void onStartClicked();
    void onProgress(const QString& component, int percent, qint64 total);
    void onComponentReady(const QString& component);
    void onLogMessage(const QString& msg);
    void onSetupFinished(bool success, const QString& error);

private:
    void buildUi();
    void switchToProgress();
    void switchToSuccess();
    void switchToError(const QString& err);

    VoiceInput*            m_voiceInput   = nullptr;
    QWidget*               m_selectionPage = nullptr;
    QWidget*               m_progressPage  = nullptr;
    QWidget*               m_resultPage    = nullptr;
    QStackedWidget*        m_stack         = nullptr;

    QVector<ModelCard*>    m_cards;
    QPushButton*           m_startBtn      = nullptr;
    QLabel*                m_currentTask   = nullptr;
    QProgressBar*          m_totalProgress = nullptr;
    QProgressBar*          m_itemProgress  = nullptr;
    QTextEdit*             m_logView       = nullptr;
    QLabel*                m_resultIcon    = nullptr;
    QLabel*                m_resultText    = nullptr;
    QPushButton*           m_finishBtn     = nullptr;

    QStringList            m_selectedIds;
    int                    m_totalComponents = 0;
    int                    m_doneComponents  = 0;
};

// ============================================================
//  VoskModelManagerWidget — Settings панель
// ============================================================

class VoskModelManagerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VoskModelManagerWidget(VoiceInput* voiceInput, QWidget* parent = nullptr);

    // Вызвать после возврата в Settings чтобы обновить статус
    void refresh();

signals:
    void modelsChanged(); // перезагрузка нужна

private slots:
    void onDownloadRequested(const QString& modelId);
    void onDeleteRequested(const QString& modelId);
    void onDownloadProgress(const QString& modelId, int percent, qint64 total);
    void onDownloadFinished(const QString& modelId, bool success);

private:
    void buildUi();

    VoiceInput*            m_voiceInput = nullptr;
    QVector<ModelCard*>    m_cards;
    QLabel*                m_diskUsageLabel = nullptr;
    QLabel*                m_privacyNote    = nullptr;

    QString updateDiskUsage() const;
};