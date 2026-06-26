#pragma once
// ============================================================
// curiosity_engine.h — Core Curiosity Engine
//
// Low-priority background routine that generates conversational
// philosophical/personal questions during idle states.
// Responses are saved into user_personality_matrix SQLite table.
// ============================================================

#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>

class J2JTelegramGateway;

class CuriosityEngine : public QObject
{
    Q_OBJECT

public:
    static CuriosityEngine& instance();

    CuriosityEngine(const CuriosityEngine&)            = delete;
    CuriosityEngine& operator=(const CuriosityEngine&) = delete;

    void setTelegramGateway(J2JTelegramGateway* gw) { m_gateway = gw; }
    void setTargetChatId(qint64 chatId) { m_targetChatId = chatId; }

    void start(int intervalMinutes = 120);
    void stop();
    bool isRunning() const;

    void saveResponse(qint64 chatId,
                      const QString& question,
                      const QString& answer);

    void ensureTable();

signals:
    void questionPosted(const QString& question);

private:
    explicit CuriosityEngine(QObject* parent = nullptr);
    ~CuriosityEngine() override;

    void postRandomQuestion();
    QString pickQuestion();

    J2JTelegramGateway* m_gateway      = nullptr;
    QTimer*             m_idleTimer    = nullptr;
    qint64              m_targetChatId = 0;
    int                 m_questionIndex = 0;

    static const QStringList& questionPool();
};
