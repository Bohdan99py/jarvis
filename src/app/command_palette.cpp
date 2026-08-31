// -------------------------------------------------------
// command_palette.cpp — см. command_palette.h
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "command_palette.h"

#include "agent_loop.h"
#include "event_feed.h"
#include "jarvis.h"

#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>
#include <QDebug>

#include <windows.h>

// ============================================================
//  GlobalHotkey
// ============================================================

namespace {

int g_nextHotkeyId = 0xB0B0;   // произвольная база, лишь бы не 0

// Приёмник WM_HOTKEY. Виджет создаётся, но никогда не показывается:
// winId() поднимает нативное окно и без show(), а сообщения хоткеев
// приходят в очередь окна независимо от того, видно его или нет.
//
// Удалять его некому и не нужно: он один на процесс и должен пережить
// любое окно, включая главное.
HWND hotkeyHostWindow()
{
    static QWidget* host = nullptr;
    if (!host) {
        host = new QWidget(nullptr, Qt::Tool);
        host->resize(1, 1);
    }
    return reinterpret_cast<HWND>(host->winId());
}

// Строка результата: заголовок и подпись в одном элементе. Разные
// стили в одной строке QListWidget без делегата не умеет, а свой
// делегат ради двух цветов — избыточно; разделяем отступом.
QString formatHit(const SearchHit& hit)
{
    QString line = hit.icon.isEmpty() ? QString() : hit.icon + QStringLiteral("  ");
    line += hit.title;
    if (!hit.subtitle.isEmpty())
        line += QStringLiteral("        ") + hit.subtitle;
    return line;
}

} // namespace

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterHotkey();
}

bool GlobalHotkey::registerHotkey(quint32 modifiers, quint32 virtualKey)
{
    unregisterHotkey();

    m_owner = reinterpret_cast<quintptr>(hotkeyHostWindow());
    m_id    = g_nextHotkeyId++;

    // MOD_NOREPEAT — иначе зажатая комбинация выстрелит десятки раз.
    if (!RegisterHotKey(reinterpret_cast<HWND>(m_owner), m_id,
                        modifiers | MOD_NOREPEAT, virtualKey)) {
        qWarning() << "[Hotkey] RegisterHotKey failed, error" << GetLastError()
                   << "- the combination is probably taken by another app";
        m_id = 0;
        return false;
    }

    qApp->installNativeEventFilter(this);
    m_registered = true;
    return true;
}

void GlobalHotkey::unregisterHotkey()
{
    if (!m_registered)
        return;
    UnregisterHotKey(reinterpret_cast<HWND>(m_owner), m_id);
    qApp->removeNativeEventFilter(this);
    m_registered = false;
    m_id = 0;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("windows_generic_MSG")
        && eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;

    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == m_id) {
        emit activated();
        return true;
    }
    return false;
}

// ============================================================
//  CommandPalette
// ============================================================

CommandPalette::CommandPalette(Jarvis* jarvis, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_jarvis(jarvis)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // Палитра — окно верхнего уровня без родителя: она должна всплывать
    // поверх чужих приложений даже когда главное окно свёрнуто. Поэтому
    // же она не должна считаться "последним окном" при закрытии.
    setAttribute(Qt::WA_QuitOnClose, false);
    setFixedWidth(760);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("paletteCard"));
    outer->addWidget(card);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);

    m_input = new QLineEdit(card);
    m_input->setObjectName(QStringLiteral("paletteInput"));
    m_input->setClearButtonEnabled(true);
    layout->addWidget(m_input);

    m_list = new QListWidget(card);
    m_list->setObjectName(QStringLiteral("paletteSteps"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setWordWrap(true);
    m_list->setMaximumHeight(360);
    m_list->hide();
    layout->addWidget(m_list);

    m_hint = new QLabel(card);
    m_hint->setObjectName(QStringLiteral("paletteHint"));
    layout->addWidget(m_hint);

    card->setStyleSheet(QStringLiteral(R"(
        #paletteCard {
            background-color: rgba(10, 14, 24, 248);
            border: 1px solid rgba(0, 212, 255, 90);
            border-radius: 14px;
        }
        #paletteInput {
            background-color: rgba(255, 255, 255, 8);
            border: 1px solid rgba(0, 212, 255, 70);
            border-radius: 9px;
            padding: 12px 14px;
            color: #e8f0fe;
            font-size: 19px;
            font-family: "Segoe UI", sans-serif;
        }
        #paletteInput:focus {
            border: 1px solid #00d4ff;
        }
        #paletteSteps {
            background: transparent;
            color: #c0d8ee;
            font-family: "Consolas", "Segoe UI", monospace;
            font-size: 13px;
        }
        #paletteSteps::item { padding: 4px 2px; }
        #paletteSteps::item:selected {
            background-color: rgba(0, 212, 255, 45);
            border-radius: 5px;
        }
        #paletteHint {
            color: #3a4a5e;
            font-size: 11px;
        }
    )"));

    connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::submit);
    connect(m_input, &QLineEdit::textChanged,   this, &CommandPalette::onTextChanged);
    connect(m_list,  &QListWidget::itemActivated, this,
            [this](QListWidgetItem*) { activateCurrentHit(); });

    // Сигналы агента нужны только в режиме Act — в режиме поиска в
    // списке лежат результаты, и дописывать туда шаги нельзя.
    if (m_jarvis && m_jarvis->agent()) {
        AgentLoop* agent = m_jarvis->agent();

        connect(agent, &AgentLoop::toolStarted, this,
                [this](const QString&, const QString& summary) {
            if (isVisible() && m_mode == Mode::Act)
                addLine(QStringLiteral("⚙ ") + summary, QStringLiteral("#00d4ff"));
        });

        connect(agent, &AgentLoop::toolFinished, this,
                [this](const QString&, bool ok, const QString& summary) {
            if (isVisible() && m_mode == Mode::Act)
                addLine((ok ? QStringLiteral("✓ ") : QStringLiteral("✕ ")) + summary,
                        ok ? QStringLiteral("#00e676") : QStringLiteral("#ff5252"));
        });

        connect(agent, &AgentLoop::toolDenied, this,
                [this](const QString&, const QString& reason) {
            if (isVisible() && m_mode == Mode::Act)
                addLine(QStringLiteral("⛔ ") + reason, QStringLiteral("#ff5252"));
        });

        connect(agent, &AgentLoop::finished, this, [this](const QString& text) {
            m_busy = false;
            if (!isVisible() || m_mode != Mode::Act)
                return;
            addLine(text, QStringLiteral("#e8f0fe"));
            m_input->setEnabled(true);
            m_input->setFocus();
            m_hint->setText(
                QStringLiteral("Enter — новая команда    ·    Tab — поиск    ·    Esc — закрыть"));
        });

        connect(agent, &AgentLoop::failed, this, [this](const QString& error) {
            m_busy = false;
            if (!isVisible() || m_mode != Mode::Act)
                return;
            addLine(QStringLiteral("✕ ") + error, QStringLiteral("#ff5252"));
            m_input->setEnabled(true);
            m_input->setFocus();
        });
    }

    setMode(Mode::Act);
}

// ============================================================
//  Режимы
// ============================================================

void CommandPalette::setMode(Mode mode)
{
    m_mode = mode;

    if (mode == Mode::Act) {
        m_input->setPlaceholderText(
            QStringLiteral("Что сделать?  ·  открой Steam, собери проект, покажи процессы…"));
        m_hint->setText(
            QStringLiteral("Enter — выполнить    ·    Tab — поиск    ·    Esc — закрыть"));
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
    } else {
        m_input->setPlaceholderText(
            QStringLiteral("Найти:  приложение, файл, сценарий, профиль, действие…"));
        m_hint->setText(QStringLiteral(
            "↑↓ — выбор    ·    Enter — открыть    ·    Tab — выполнить    ·    Esc — закрыть"));
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    // Список принадлежит режиму: смешивать шаги агента и результаты
    // поиска нельзя — по Enter станет непонятно, что должно произойти.
    clearList();
    if (mode == Mode::Find && !m_input->text().trimmed().isEmpty())
        runSearch(m_input->text());
}

void CommandPalette::onTextChanged(const QString& text)
{
    if (m_mode == Mode::Find)
        runSearch(text);
}

// ============================================================
//  Поиск
// ============================================================

void CommandPalette::runSearch(const QString& query)
{
    if (!m_jarvis || !m_jarvis->search())
        return;

    clearList();

    const QString q = query.trimmed();
    if (q.isEmpty())
        return;

    m_hits = m_jarvis->search()->search(q, 5);
    if (m_hits.isEmpty())
        return;

    m_list->show();

    QString lastCategory;
    for (int i = 0; i < m_hits.size(); ++i) {
        const SearchHit& hit = m_hits[i];

        if (hit.category != lastCategory) {
            lastCategory = hit.category;
            auto* header = new QListWidgetItem(hit.category.toUpper(), m_list);
            header->setForeground(QColor(QStringLiteral("#3a4a5e")));
            header->setFlags(Qt::NoItemFlags);   // заголовок не выбирается
        }

        auto* item = new QListWidgetItem(formatHit(hit), m_list);
        item->setForeground(QColor(hit.isActionable() ? QStringLiteral("#e8f0fe")
                                                      : QStringLiteral("#8fa7bf")));
        item->setData(Qt::UserRole, i);
    }

    // Первый выбираемый элемент выделяем сразу: Enter должен работать
    // без обязательного нажатия стрелки вниз.
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(row);
            break;
        }
    }

    adjustSize();
}

void CommandPalette::moveSelection(int delta)
{
    if (m_list->count() == 0)
        return;

    int row = m_list->currentRow();
    for (int step = 0; step < m_list->count(); ++step) {
        row += delta;
        if (row < 0)                 row = m_list->count() - 1;
        if (row >= m_list->count())  row = 0;
        if (m_list->item(row)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void CommandPalette::activateCurrentHit()
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;

    const QVariant idx = item->data(Qt::UserRole);
    if (!idx.isValid())
        return;

    const int i = idx.toInt();
    if (i < 0 || i >= m_hits.size())
        return;

    const SearchHit hit = m_hits[i];   // копия: список сейчас очистится

    if (hit.action == SearchHit::Action::RunCommand) {
        // Команда интерфейса — выполняет окно, Jarvis про меню не знает.
        hide();
        if (m_commandRunner)
            m_commandRunner(hit.payload);
        return;
    }

    if (hit.action == SearchHit::Action::AskAgent) {
        // Запрос уходит агенту — показывать надо шаги, а не результаты.
        m_input->setText(hit.payload);
        setMode(Mode::Act);
        submit();
        return;
    }

    const QString result = m_jarvis->activateSearchHit(hit);

    clearList();
    addLine(formatHit(hit), QStringLiteral("#7c4dff"));
    if (!result.isEmpty())
        addLine(result.left(2000), QStringLiteral("#c0d8ee"));
}

// ============================================================
//  Режим действия
// ============================================================

void CommandPalette::submit()
{
    if (m_mode == Mode::Find) {
        activateCurrentHit();
        return;
    }

    const QString request = m_input->text().trimmed();
    if (request.isEmpty() || m_busy || !m_jarvis)
        return;

    clearList();
    addLine(QStringLiteral("> ") + request, QStringLiteral("#7c4dff"));

    m_busy = true;
    m_input->clear();
    m_input->setEnabled(false);
    m_hint->setText(QStringLiteral("Выполняю…    ·    Esc — свернуть панель"));

    // Палитра всегда идёт через агента: сюда пишут задания, а не вопросы.
    m_jarvis->runAgentTask(request);
}

// ============================================================
//  Список
// ============================================================

void CommandPalette::addLine(const QString& text, const QString& color)
{
    if (!m_list->isVisible())
        m_list->show();

    auto* item = new QListWidgetItem(text, m_list);
    item->setForeground(QColor(color));
    item->setFlags(Qt::NoItemFlags);
    m_list->scrollToBottom();

    adjustSize();
}

void CommandPalette::clearList()
{
    m_list->clear();
    m_list->hide();
    m_hits.clear();
    adjustSize();
}

// ============================================================
//  Показ и клавиши
// ============================================================

void CommandPalette::setCommandRunner(std::function<void(const QString&)> runner)
{
    m_commandRunner = std::move(runner);
}

void CommandPalette::showPalette(Mode mode)
{
    // По центру сверху — там, где взгляд ищет строку поиска.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect g = screen->availableGeometry();
        move(g.center().x() - width() / 2, g.top() + g.height() / 6);
    }

    setMode(mode);

    show();
    raise();
    activateWindow();

    // Windows не отдаёт фокус окну чужого процесса по одному show():
    // без SetForegroundWindow строка появится, но печатать будет некуда.
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));

    m_input->setEnabled(!m_busy);
    m_input->setFocus();
    m_input->selectAll();

    m_shownAt.start();
}

void CommandPalette::togglePalette(Mode mode)
{
    // Повторное нажатие ТОЙ ЖЕ комбинации закрывает панель, а другой —
    // переключает режим, не пряча её: иначе Ctrl+K из режима действия
    // выглядел бы как случайное закрытие.
    if (isVisible() && isActiveWindow() && m_mode == mode)
        hide();
    else
        showPalette(mode);
}

void CommandPalette::changeEvent(QEvent* event)
{
    // Ушёл фокус — панель прячется. Так ведёт себя любая строка запуска,
    // и без этого она превращается в окно, которое нечем убрать: рамки
    // нет, крестика нет, а Escape уходит уже другому приложению.
    //
    // Первые доли секунды после показа игнорируем: SetForegroundWindow
    // сам по себе даёт короткую смену активности, и панель пропадала бы
    // сразу после появления.
    if (event->type() == QEvent::ActivationChange
        && !isActiveWindow()
        && isVisible()
        && m_shownAt.isValid()
        && m_shownAt.elapsed() > 400) {
        hide();
    }
    QWidget::changeEvent(event);
}

void CommandPalette::mousePressEvent(QMouseEvent* event)
{
    // До сюда доходят только нажатия по фону карточки: поле ввода и
    // список разбирают свои сами. То есть тащить можно за рамку вокруг
    // них — ровно там, где у обычного окна был бы заголовок.
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragFrom = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CommandPalette::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragFrom);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CommandPalette::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        hide();
        return;

    case Qt::Key_Tab:
        setMode(m_mode == Mode::Act ? Mode::Find : Mode::Act);
        return;

    case Qt::Key_Down:
        if (m_mode == Mode::Find) { moveSelection(1); return; }
        break;

    case Qt::Key_Up:
        if (m_mode == Mode::Find) { moveSelection(-1); return; }
        break;

    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

// ============================================================
//  installPaletteHotkeys
// ============================================================

void installPaletteHotkeys(CommandPalette* palette, QObject* parent)
{
    if (!palette)
        return;

    auto* act  = new GlobalHotkey(parent);
    auto* find = new GlobalHotkey(parent);

    // Лямбды, а не прямой connect на togglePalette: у неё аргумент со
    // значением по умолчанию, и сигнал без аргументов к ней не привяжется.
    QObject::connect(act, &GlobalHotkey::activated, palette, [palette]() {
        palette->togglePalette(CommandPalette::Mode::Act);
    });
    QObject::connect(find, &GlobalHotkey::activated, palette, [palette]() {
        palette->togglePalette(CommandPalette::Mode::Find);
    });

    QStringList ready;
    QStringList taken;
    (act->registerHotkey(MOD_CONTROL, VK_SPACE) ? ready : taken)
        << QStringLiteral("Ctrl+Space");
    (find->registerHotkey(MOD_CONTROL, 'K') ? ready : taken)
        << QStringLiteral("Ctrl+K");

    if (!ready.isEmpty()) {
        EventFeed::instance().post(
            QStringLiteral("system"), EventLevel::Info,
            QStringLiteral("Горячие клавиши: %1").arg(ready.join(QStringLiteral(", "))),
            QStringLiteral("Ctrl+Space — выполнить, Ctrl+K — найти"),
            QStringLiteral("hotkeys/ready"));
    }
    if (!taken.isEmpty()) {
        EventFeed::instance().post(
            QStringLiteral("system"), EventLevel::Warning,
            QStringLiteral("Комбинация занята другой программой: %1")
                .arg(taken.join(QStringLiteral(", "))),
            QStringLiteral("JARVIS остался без этой горячей клавиши"),
            QStringLiteral("hotkeys/taken"));
    }
}
