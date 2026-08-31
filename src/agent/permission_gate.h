#pragma once
// -------------------------------------------------------
// permission_gate.h — Разрешения на действия
//
// Как только модель получает право что-то делать на машине,
// нужен барьер между "открыть браузер" и "удалить папку".
// Гейт решает три вещи:
//
//   1. можно ли выполнить инструмент молча (Safe),
//   2. нужно ли спросить человека (Moderate/Dangerous),
//   3. что показать в вопросе.
//
// Гейт НЕ рисует UI. Он поднимает confirmationRequired() и ждёт
// resolve(). Если интерактивного UI нет (Telegram, фон, автозадача)
// — запрос отклоняется, а не зависает.
// -------------------------------------------------------

#include "tool_registry.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <functional>

class QTimer;

// Насколько JARVIS самостоятелен
enum class PermissionMode {
    Paranoid = 0,   // спрашивать вообще всё
    Balanced = 1,   // Safe — молча, остальное спросить  (по умолчанию)
    Trusted  = 2    // Safe и Moderate — молча, Dangerous всегда спросить
};

QString        permissionModeName(PermissionMode mode);
PermissionMode permissionModeFromString(const QString& name,
                                        PermissionMode fallback = PermissionMode::Balanced);

// Человеческое описание уровня — для меню и для ответа модели
QString permissionModeDescription(PermissionMode mode, bool english);

class PermissionGate : public QObject
{
    Q_OBJECT

public:
    explicit PermissionGate(QObject* parent = nullptr);

    void            setMode(PermissionMode mode);
    PermissionMode  mode() const { return m_mode; }

    // UI обязан объявить о себе — иначе подтверждать некому и
    // всё, что требует подтверждения, будет отклонено.
    void setInteractive(bool on) { m_interactive = on; }
    bool isInteractive() const   { return m_interactive; }

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    bool needsConfirmation(ToolRisk risk, const QString& toolName) const;

    // Главный вход. done вызывается ровно один раз — сразу
    // (если подтверждение не нужно) или после ответа человека.
    using Decision = std::function<void(bool allowed, const QString& reason)>;
    void evaluate(const QString& toolName,
                  ToolRisk risk,
                  const QString& summary,
                  Decision done);

    // То же самое для вызывающих, которым нечего делать до ответа:
    // сценарий и триггер выполняют шаги строго по одному. Внутри —
    // вложенный цикл событий, поэтому вызывать можно только из
    // главного потока и только вне обработчика самого диалога.
    bool evaluateBlocking(const QString& toolName,
                          ToolRisk risk,
                          const QString& summary,
                          QString* reasonOut = nullptr);

    // Ответ пользователя. rememberForSession — "больше не спрашивать
    // про этот инструмент до перезапуска".
    void resolve(quint64 requestId, bool allowed, bool rememberForSession = false);

    void allowForSession(const QString& toolName);
    void clearSessionGrants();
    QStringList sessionGrants() const;

    bool hasPending() const { return !m_pending.isEmpty(); }

    void load();
    void save() const;

signals:
    void modeChanged(int mode);

    // risk — int, чтобы сигнал был доступен из QML без регистрации enum
    void confirmationRequired(quint64 requestId,
                              const QString& toolName,
                              const QString& summary,
                              int risk);
    void decisionMade(quint64 requestId, bool allowed);

private:
    struct Pending {
        QString  toolName;
        QString  summary;
        Decision done;
        QTimer*  timer = nullptr;
    };

    void finish(quint64 id, bool allowed, const QString& reason, bool remember);

    PermissionMode         m_mode        = PermissionMode::Balanced;
    bool                   m_interactive = false;
    int                    m_timeoutMs   = 90000;
    quint64                m_nextId      = 1;
    QHash<quint64, Pending> m_pending;
    QStringList            m_sessionGrants;
};
