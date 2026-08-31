#pragma once
// ============================================================
// chat_controller.h — мост между QML-лентой и MainWindow.
//
// QML не должен знать про MainWindow: иначе экран нельзя ни
// открыть отдельно, ни отрисовать в харнессе без запуска всего
// ассистента (а он на старте поднимает голос, Telegram и камеру).
//
// Контроллер отдаёт наружу только состояние, нужное для показа,
// и сигналы намерений. Что делать с намерением — решает
// MainWindow, у которого для этого уже есть слоты.
// ============================================================

#include <QObject>
#include <QString>

class ChatController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool english READ english NOTIFY englishChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

    // Оттенок состояния: "online" | "speaking" | "typing" | "thinking".
    // Строка, а не цвет: цвет — дело темы. Раньше состояние выражалось
    // прямо таблицей стилей точки ("color: #aa66ff; font-size: 18px;"),
    // а мигание вёл QTimer на 400 мс, подменявший эту таблицу дважды
    // в секунду мимо всякой системы анимаций.
    Q_PROPERTY(QString statusTone READ statusTone NOTIFY statusToneChanged)
    Q_PROPERTY(QString agentName READ agentName NOTIFY agentNameChanged)

    // Текст в поле ввода. Свойство, а не «спросить у виджета»:
    // MainWindow подставляет черновик из десятка мест (переспрос,
    // подсказка, распознанная речь, «вспомни …»), и все они должны
    // работать одинаково независимо от того, чем поле нарисовано.
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    Q_PROPERTY(bool inputEnabled READ inputEnabled NOTIFY inputEnabledChanged)
    Q_PROPERTY(QString placeholder READ placeholder NOTIFY placeholderChanged)

    // Состояние микрофона. Раньше оно жило прямо на QPushButton:
    // setText("🔴"), setProperty("active"), unpolish/polish — то есть
    // логика «слушаю» была размазана по вызовам перерисовки виджета.
    Q_PROPERTY(QString micGlyph READ micGlyph NOTIFY micGlyphChanged)
    Q_PROPERTY(bool micEnabled READ micEnabled NOTIFY micEnabledChanged)
    Q_PROPERTY(QString micTooltip READ micTooltip NOTIFY micTooltipChanged)

    // Пользователь говорит прямо сейчас (порог по уровню сигнала).
    // Отдельно от listening: «микрофон включён» и «в него сейчас
    // говорят» — разные факты, и раньше второй выражался тем, что
    // кнопке на каждый замер громкости подменяли таблицу стилей.
    Q_PROPERTY(bool micSpeaking READ micSpeaking NOTIFY micSpeakingChanged)

    // ── Фаза ядра ─────────────────────────────────────────
    // Это не четвёртая копия «занят ли JARVIS», а окно в JarvisState:
    // контроллер ничего здесь не хранит, только пробрасывает. QML
    // читает состояние через ту же связь, что и всё остальное, —
    // иначе экрану пришлось бы знать про ещё один context property,
    // а забытая регистрация такого property гасит весь экран молча.
    //
    // "idle" | "listening" | "thinking" | "planning" | "executing" |
    // "verifying" | "waiting" | "recovering" | "error"
    Q_PROPERTY(QString corePhase READ corePhase NOTIFY coreChanged)

    // Что именно делается прямо сейчас: «Запустить Rider».
    Q_PROPERTY(QString coreActivity READ coreActivity NOTIFY coreChanged)

    // "18s" / "2m 04s" / пусто в покое. Считается в JarvisState, а не
    // таймером в QML: иначе подпись и тултип показывали бы разное
    // время про одно и то же ожидание.
    Q_PROPERTY(QString coreElapsed READ coreElapsed NOTIFY coreElapsedChanged)

    // Сколько инструментов отработало в текущем прогоне — «3 tools active»
    // под анимацией ядра.
    Q_PROPERTY(int coreToolsRun READ coreToolsRun NOTIFY coreChanged)

public:
    explicit ChatController(QObject* parent = nullptr);

    bool    english()    const { return m_english; }
    bool    busy()       const { return m_busy; }
    bool    listening()  const { return m_listening; }
    QString statusText() const { return m_status; }
    QString statusTone() const { return m_statusTone; }
    QString agentName()  const { return m_agent; }
    QString draft()        const { return m_draft; }
    bool    inputEnabled() const { return m_inputEnabled; }
    QString placeholder()  const { return m_placeholder; }
    QString micGlyph()     const { return m_micGlyph; }
    bool    micEnabled()   const { return m_micEnabled; }
    QString micTooltip()   const { return m_micTooltip; }
    bool    micSpeaking()  const { return m_micSpeaking; }

    QString corePhase()    const;
    QString coreActivity() const;
    QString coreElapsed()  const;
    int     coreToolsRun() const;

    void setEnglish(bool v);
    void setBusy(bool v);
    void setListening(bool v);
    void setStatusText(const QString& v);

    // Текст и оттенок меняются всегда вместе — раздельные вызовы
    // означали бы кадр, в котором подпись уже новая, а цвет ещё старый.
    void setStatus(const QString& text, const QString& tone);
    void setAgentName(const QString& v);
    void setDraft(const QString& v);
    void setInputEnabled(bool v);
    void setPlaceholder(const QString& v);
    void setMicGlyph(const QString& v);
    void setMicEnabled(bool v);
    void setMicTooltip(const QString& v);
    void setMicSpeaking(bool v);

    // Фокус — не свойство: это разовое действие, а не состояние.
    // Хранить «сфокусировано» в C++ значит рассинхронизироваться с
    // настоящим фокусом при первом же клике мимо.
    void requestFocus() { emit focusRequested(); }

    // Экранная клавиатура правит текст посимвольно.
    Q_INVOKABLE void insertText(const QString& s);
    Q_INVOKABLE void backspace();

    // ── Намерения из QML ──────────────────────────────────
    Q_INVOKABLE void send(const QString& text);
    Q_INVOKABLE void toggleMic();
    Q_INVOKABLE void attach();

    // Гамбургер открывает НАСТОЯЩЕЕ меню окна (QMenuBar), а не свою
    // копию в QML: пункты меню — нативные, с горячими клавишами и
    // галочками, и дублировать их разметкой значит развести две
    // версии одного меню.
    Q_INVOKABLE void openMenu();

signals:
    void englishChanged();
    void busyChanged();
    void listeningChanged();
    void statusTextChanged();
    void statusToneChanged();
    void agentNameChanged();
    void draftChanged();
    void inputEnabledChanged();
    void placeholderChanged();
    void micGlyphChanged();
    void micEnabledChanged();
    void micTooltipChanged();
    void micSpeakingChanged();
    void focusRequested();

    // Фаза, подпись и счётчик инструментов меняются вместе и часто в
    // одном кадре — три отдельных сигнала заставили бы QML
    // перевязываться трижды на один переход.
    void coreChanged();
    void coreElapsedChanged();

    // MainWindow подключает их к своим существующим слотам.
    void sendRequested(const QString& text);
    void micToggleRequested();
    void attachRequested();
    void menuRequested();

private:
    bool    m_english   = false;
    bool    m_busy      = false;
    bool    m_listening = false;
    QString m_status;
    QString m_statusTone = QStringLiteral("online");
    QString m_agent;
    QString m_draft;
    bool    m_inputEnabled = true;
    QString m_placeholder;
    QString m_micGlyph   = QStringLiteral("🎤");
    bool    m_micEnabled = false;
    QString m_micTooltip;
    bool    m_micSpeaking = false;
};
