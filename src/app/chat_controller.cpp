#include "chat_controller.h"

#include "jarvis_state.h"

ChatController::ChatController(QObject* parent)
    : QObject(parent)
{
    // Подписка, а не опрос: фазу меняет агент из своего колбэка, и
    // узнать об этом в момент отрисовки кадра неоткуда.
    JarvisState& state = JarvisState::instance();
    connect(&state, &JarvisState::changed, this, &ChatController::coreChanged);
    connect(&state, &JarvisState::elapsedChanged, this,
            [this](int) { emit coreElapsedChanged(); });
}

QString ChatController::corePhase() const
{
    return JarvisState::instance().phaseName();
}

QString ChatController::coreActivity() const
{
    return JarvisState::instance().activity();
}

QString ChatController::coreElapsed() const
{
    return JarvisState::instance().elapsedText();
}

int ChatController::coreToolsRun() const
{
    return JarvisState::instance().toolsRun();
}

void ChatController::setEnglish(bool v)
{
    if (m_english == v) return;
    m_english = v;
    emit englishChanged();
}

void ChatController::setBusy(bool v)
{
    if (m_busy == v) return;
    m_busy = v;
    emit busyChanged();
}

void ChatController::setListening(bool v)
{
    if (m_listening == v) return;
    m_listening = v;
    // Микрофон — единственная фаза, о которой ядро узнаёт отсюда:
    // здесь она уже собрана из голосового ввода, пассивного
    // слушателя и ручного тумблера в одно «слушаю».
    JarvisState::instance().setListening(v);
    emit listeningChanged();
}

void ChatController::setStatusText(const QString& v)
{
    if (m_status == v) return;
    m_status = v;
    emit statusTextChanged();
}

void ChatController::setAgentName(const QString& v)
{
    if (m_agent == v) return;
    m_agent = v;
    emit agentNameChanged();
}

void ChatController::send(const QString& text)
{
    // Пустое сообщение отсекаем здесь, а не в QML: то же правило
    // понадобится любому другому вызывающему (голос, Telegram).
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    emit sendRequested(trimmed);
}

void ChatController::toggleMic() { emit micToggleRequested(); }
void ChatController::attach()    { emit attachRequested(); }
void ChatController::openMenu() { emit menuRequested(); }

void ChatController::setDraft(const QString& v)
{
    if (m_draft == v) return;
    m_draft = v;
    emit draftChanged();
}

void ChatController::setInputEnabled(bool v)
{
    if (m_inputEnabled == v) return;
    m_inputEnabled = v;
    emit inputEnabledChanged();
}

void ChatController::setPlaceholder(const QString& v)
{
    if (m_placeholder == v) return;
    m_placeholder = v;
    emit placeholderChanged();
}

void ChatController::insertText(const QString& s)
{
    // Вставка в конец, а не по курсору: экранная клавиатура своего
    // курсора не имеет, а курсор поля к моменту нажатия уже ушёл —
    // клик по клавише забирает фокус.
    setDraft(m_draft + s);
    emit focusRequested();
}

void ChatController::backspace()
{
    if (m_draft.isEmpty()) return;
    setDraft(m_draft.chopped(1));
    emit focusRequested();
}

void ChatController::setMicGlyph(const QString& v)
{
    if (m_micGlyph == v) return;
    m_micGlyph = v;
    emit micGlyphChanged();
}

void ChatController::setMicEnabled(bool v)
{
    if (m_micEnabled == v) return;
    m_micEnabled = v;
    emit micEnabledChanged();
}

void ChatController::setMicTooltip(const QString& v)
{
    if (m_micTooltip == v) return;
    m_micTooltip = v;
    emit micTooltipChanged();
}

void ChatController::setMicSpeaking(bool v)
{
    if (m_micSpeaking == v) return;
    m_micSpeaking = v;
    emit micSpeakingChanged();
}

void ChatController::setStatus(const QString& text, const QString& tone)
{
    setStatusText(text);
    if (m_statusTone != tone) {
        m_statusTone = tone;
        emit statusToneChanged();
    }
}
