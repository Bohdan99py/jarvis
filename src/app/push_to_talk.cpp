// -------------------------------------------------------
// push_to_talk.cpp — см. push_to_talk.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "push_to_talk.h"

#include "command_palette.h"   // GlobalHotkey
#include "event_feed.h"
#include "jarvis.h"
#include "jarvis_state.h"
#include "lang.h"
#include "voice_input.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <windows.h>

// ============================================================
//  PttOverlay — «слушаю» поверх чужого окна
// ============================================================
//
// Маленькое окно без рамки, которое принципиально не активируется:
// человек говорит, не отрываясь от того, чем занят, и увести у него
// фокус на полсекунды — значит проглотить нажатие клавиши в чужом
// приложении.

class PttOverlay : public QWidget
{
public:
    explicit PttOverlay(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool
                        | Qt::FramelessWindowHint
                        | Qt::WindowStaysOnTopHint
                        | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_QuitOnClose, false);
        setFixedWidth(320);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        auto* card = new QWidget(this);
        card->setObjectName(QStringLiteral("pttCard"));
        outer->addWidget(card);

        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 14, 18, 14);
        layout->setSpacing(8);

        auto* title = new QLabel(QStringLiteral("◉  J.A.R.V.I.S."), card);
        title->setObjectName(QStringLiteral("pttTitle"));
        title->setAlignment(Qt::AlignCenter);
        layout->addWidget(title);

        m_status = new QLabel(card);
        m_status->setObjectName(QStringLiteral("pttStatus"));
        m_status->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_status);

        // Уровень сигнала: без него непонятно, слышно тебя или ты
        // говоришь в выключенный микрофон.
        auto* track = new QWidget(card);
        track->setObjectName(QStringLiteral("pttTrack"));
        track->setFixedHeight(4);
        auto* trackLayout = new QVBoxLayout(track);
        trackLayout->setContentsMargins(0, 0, 0, 0);

        m_level = new QWidget(track);
        m_level->setObjectName(QStringLiteral("pttLevel"));
        trackLayout->addWidget(m_level, 0, Qt::AlignLeft);
        m_level->setFixedHeight(4);
        m_level->setFixedWidth(0);

        layout->addWidget(track);

        card->setStyleSheet(QStringLiteral(R"(
            #pttCard {
                background-color: rgba(10, 14, 24, 240);
                border: 1px solid rgba(0, 212, 255, 90);
                border-radius: 12px;
            }
            #pttTitle  { color: #00d4ff; font-size: 13px; font-family: "Segoe UI", sans-serif; }
            #pttStatus { color: #c0d8ee; font-size: 15px; font-family: "Segoe UI", sans-serif; }
            #pttTrack  { background-color: rgba(255, 255, 255, 12); border-radius: 2px; }
            #pttLevel  { background-color: #00d4ff; border-radius: 2px; }
        )"));

        setStatus(IS_EN ? QStringLiteral("Listening…") : QStringLiteral("Слушаю…"));
    }

    void setStatus(const QString& text) { m_status->setText(text); }

    // db: -60..0. Ниже -50 считаем тишиной — иначе полоска всё время
    // дышит на фоновом шуме и перестаёт что-либо значить.
    void setLevel(float db)
    {
        const int usable = width() - 36;
        const float norm = qBound(0.0f, (db + 50.0f) / 45.0f, 1.0f);
        m_level->setFixedWidth(static_cast<int>(usable * norm));
    }

    void showCentered()
    {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect g = screen->availableGeometry();
            adjustSize();
            move(g.center().x() - width() / 2, g.bottom() - height() - 120);
        }
        m_level->setFixedWidth(0);
        show();
        raise();
    }

private:
    QLabel*  m_status = nullptr;
    QWidget* m_level  = nullptr;
};

// ============================================================
//  PushToTalk
// ============================================================

PushToTalk::PushToTalk(Jarvis* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
    m_hotkey  = new GlobalHotkey(this);
    m_overlay = new PttOverlay();

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, &PushToTalk::poll);

    connect(m_hotkey, &GlobalHotkey::activated, this, &PushToTalk::begin);

    if (VoiceInput* voice = m_core ? m_core->voiceInput() : nullptr) {
        connect(voice, &VoiceInput::volumeLevel, this, [this](float db) {
            if (m_active)
                m_overlay->setLevel(db);
        });
    }

    // Win+J. Комбинация не системная, но её вполне может занять чужая
    // программа — тогда молчать об этом нельзя, иначе push-to-talk
    // просто «не работает» без единого следа.
    if (!m_hotkey->registerHotkey(MOD_WIN, 'J')) {
        EventFeed::instance().post(
            QStringLiteral("system"), EventLevel::Warning,
            QStringLiteral("Win+J занят другой программой"),
            QStringLiteral("Push-to-talk остался без горячей клавиши"),
            QStringLiteral("hotkeys/ptt"));
    } else {
        EventFeed::instance().post(
            QStringLiteral("system"), EventLevel::Info,
            QStringLiteral("Push-to-talk: Win+J"),
            QStringLiteral("Держи — говори, отпусти — выполнит"),
            QStringLiteral("hotkeys/ptt"));
    }
}

PushToTalk::~PushToTalk()
{
    // Оверлей — окно верхнего уровня, родителя-виджета у него нет.
    delete m_overlay;
}

bool PushToTalk::isAvailable() const
{
    return m_hotkey && m_hotkey->isRegistered();
}

void PushToTalk::begin()
{
    VoiceInput* voice = m_core ? m_core->voiceInput() : nullptr;
    if (!voice || m_active)
        return;

    m_active = true;

    // Зажатая клавиша — это и есть обращение. Без этого фраза уходила
    // в общий фильтр, который принимает только речь со словом «джарвис»,
    // и push-to-talk слушал, но не реагировал.
    voice->captureWithoutWakeWord();

    // Микрофон мог быть уже включён кнопкой в окне. Тогда по отпусканию
    // клавиши его нельзя выключать: человек его не включал этим нажатием.
    m_wasListening = voice->isListening();
    if (!m_wasListening)
        voice->startListening();

    JarvisState::instance().setListening(true);

    m_overlay->setStatus(IS_EN ? QStringLiteral("Listening…") : QStringLiteral("Слушаю…"));
    m_overlay->showCentered();
    m_poll->start();
}

void PushToTalk::poll()
{
    // Старший бит — клавиша нажата прямо сейчас. Хоткей сработал по 'J'
    // с Win, но отпускают обычно в любом порядке, поэтому конец фразы
    // отмечает именно 'J'.
    if (GetAsyncKeyState('J') & 0x8000)
        return;

    finish();
}

void PushToTalk::finish()
{
    if (!m_active)
        return;

    m_active = false;
    m_poll->stop();
    m_overlay->hide();

    JarvisState::instance().setListening(false);

    VoiceInput* voice = m_core ? m_core->voiceInput() : nullptr;
    if (!voice)
        return;

    if (m_wasListening) {
        // Микрофон продолжает работать как работал — фразу отдаём на
        // распознавание, запись не трогаем.
        return;
    }

    // flush: конец фразы здесь отмечает отпущенная клавиша, а не пауза.
    voice->stopListening(/*flushPending=*/true);
}
