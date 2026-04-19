// -------------------------------------------------------
// virtual_keyboard.cpp — Эмуляция клавиатуры + UI-панель
// -------------------------------------------------------

#include "virtual_keyboard.h"

#include <QThread>
#include <QMetaObject>
#include <QTimer>

// ============================================================
// KeyEmulator
// ============================================================

KeyEmulator::KeyEmulator(QObject* parent)
    : QObject(parent)
{
}

void KeyEmulator::sendUnicodeChar(wchar_t ch)
{
    INPUT inputs[2] = {};

    // Key down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

    // Key up
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}

void KeyEmulator::sendVKey(WORD vk, bool down)
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void KeyEmulator::pressKey(WORD vkCode)
{
    sendVKey(vkCode, true);
    sendVKey(vkCode, false);
}

void KeyEmulator::pressCombo(std::initializer_list<WORD> keys)
{
    // Нажать все клавиши
    for (WORD vk : keys) {
        sendVKey(vk, true);
    }
    // Отпустить в обратном порядке
    auto it = keys.end();
    while (it != keys.begin()) {
        --it;
        sendVKey(*it, false);
    }
}

void KeyEmulator::stopTyping()
{
    m_stopRequested.store(true);
}

void KeyEmulator::typeText(const QString& text, int delayMs)
{
    if (text.isEmpty()) return;
    if (m_typing.load()) return;

    m_typing.store(true);
    m_stopRequested.store(false);

    QString copy = text;
    int delay = delayMs;

    QThread* thread = QThread::create([this, copy, delay]() {
        const int total = copy.length();

        QMetaObject::invokeMethod(this, [this]() {
            emit typingStarted();
        }, Qt::QueuedConnection);

        for (int i = 0; i < total; ++i) {
            if (m_stopRequested.load()) break;

            wchar_t ch = copy.at(i).unicode();
            sendUnicodeChar(ch);

            const int current = i + 1;
            QMetaObject::invokeMethod(this, [this, current, total]() {
                emit typingProgress(current, total);
            }, Qt::QueuedConnection);

            if (delay > 0 && i < total - 1) {
                QThread::msleep(static_cast<unsigned long>(delay));
            }
        }

        m_typing.store(false);

        QMetaObject::invokeMethod(this, [this]() {
            emit typingFinished();
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

// ============================================================
// VirtualKeyboardWidget
// ============================================================

VirtualKeyboardWidget::VirtualKeyboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("keyboardPanel"));

    // Определяем клавиши:  labelRu, labelEn, row, col, colSpan, isSpecial
    m_keyDefs = {
        // Ряд 0: цифры
        {QStringLiteral("1"), QStringLiteral("1"), 0, 0, 1, false},
        {QStringLiteral("2"), QStringLiteral("2"), 0, 1, 1, false},
        {QStringLiteral("3"), QStringLiteral("3"), 0, 2, 1, false},
        {QStringLiteral("4"), QStringLiteral("4"), 0, 3, 1, false},
        {QStringLiteral("5"), QStringLiteral("5"), 0, 4, 1, false},
        {QStringLiteral("6"), QStringLiteral("6"), 0, 5, 1, false},
        {QStringLiteral("7"), QStringLiteral("7"), 0, 6, 1, false},
        {QStringLiteral("8"), QStringLiteral("8"), 0, 7, 1, false},
        {QStringLiteral("9"), QStringLiteral("9"), 0, 8, 1, false},
        {QStringLiteral("0"), QStringLiteral("0"), 0, 9, 1, false},

        // Ряд 1: QWERTY / ЙЦУКЕН
        {QStringLiteral("й"), QStringLiteral("q"), 1, 0, 1, false},
        {QStringLiteral("ц"), QStringLiteral("w"), 1, 1, 1, false},
        {QStringLiteral("у"), QStringLiteral("e"), 1, 2, 1, false},
        {QStringLiteral("к"), QStringLiteral("r"), 1, 3, 1, false},
        {QStringLiteral("е"), QStringLiteral("t"), 1, 4, 1, false},
        {QStringLiteral("н"), QStringLiteral("y"), 1, 5, 1, false},
        {QStringLiteral("г"), QStringLiteral("u"), 1, 6, 1, false},
        {QStringLiteral("ш"), QStringLiteral("i"), 1, 7, 1, false},
        {QStringLiteral("щ"), QStringLiteral("o"), 1, 8, 1, false},
        {QStringLiteral("з"), QStringLiteral("p"), 1, 9, 1, false},

        // Ряд 2: ASDF / ФЫВА
        {QStringLiteral("ф"), QStringLiteral("a"), 2, 0, 1, false},
        {QStringLiteral("ы"), QStringLiteral("s"), 2, 1, 1, false},
        {QStringLiteral("в"), QStringLiteral("d"), 2, 2, 1, false},
        {QStringLiteral("а"), QStringLiteral("f"), 2, 3, 1, false},
        {QStringLiteral("п"), QStringLiteral("g"), 2, 4, 1, false},
        {QStringLiteral("р"), QStringLiteral("h"), 2, 5, 1, false},
        {QStringLiteral("о"), QStringLiteral("j"), 2, 6, 1, false},
        {QStringLiteral("л"), QStringLiteral("k"), 2, 7, 1, false},
        {QStringLiteral("д"), QStringLiteral("l"), 2, 8, 1, false},
        {QStringLiteral("ж"), QStringLiteral(";"), 2, 9, 1, false},

        // Ряд 3: ZXCV / ЯЧСМ
        {QStringLiteral("я"), QStringLiteral("z"), 3, 0, 1, false},
        {QStringLiteral("ч"), QStringLiteral("x"), 3, 1, 1, false},
        {QStringLiteral("с"), QStringLiteral("c"), 3, 2, 1, false},
        {QStringLiteral("м"), QStringLiteral("v"), 3, 3, 1, false},
        {QStringLiteral("и"), QStringLiteral("b"), 3, 4, 1, false},
        {QStringLiteral("т"), QStringLiteral("n"), 3, 5, 1, false},
        {QStringLiteral("ь"), QStringLiteral("m"), 3, 6, 1, false},
        {QStringLiteral("б"), QStringLiteral(","), 3, 7, 1, false},
        {QStringLiteral("ю"), QStringLiteral("."), 3, 8, 1, false},
    };

    buildLayout();
}

void VirtualKeyboardWidget::buildLayout()
{
    m_grid = new QGridLayout(this);
    m_grid->setSpacing(3);
    m_grid->setContentsMargins(8, 6, 8, 6);

    // Обычные клавиши
    for (int i = 0; i < m_keyDefs.size(); ++i) {
        const auto& kd = m_keyDefs[i];
        auto* btn = new QPushButton(m_russian ? kd.labelRu : kd.labelEn, this);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setFixedHeight(36);

        const int idx = i;
        connect(btn, &QPushButton::clicked, this, [this, idx]() {
            const auto& kd = m_keyDefs[idx];
            emit charPressed(m_russian ? kd.labelRu : kd.labelEn);
        });

        m_grid->addWidget(btn, kd.row, kd.col, 1, kd.colSpan);
        m_keyButtons.append(btn);
    }

    // Специальные клавиши (ряд 3, справа)
    auto* backspace = new QPushButton(QStringLiteral("⌫"), this);
    backspace->setObjectName(QStringLiteral("kbSpecialKey"));
    backspace->setFixedHeight(36);
    connect(backspace, &QPushButton::clicked, this, &VirtualKeyboardWidget::backspacePressed);
    m_grid->addWidget(backspace, 3, 9, 1, 1);

    // Ряд 4: пробел, RU/EN, Enter
    auto* space = new QPushButton(QStringLiteral("Пробел"), this);
    space->setObjectName(QStringLiteral("kbSpaceBar"));
    space->setFixedHeight(36);
    connect(space, &QPushButton::clicked, this, [this]() {
        emit charPressed(QStringLiteral(" "));
    });
    m_grid->addWidget(space, 4, 0, 1, 6);

    auto* langBtn = new QPushButton(QStringLiteral("RU/EN"), this);
    langBtn->setObjectName(QStringLiteral("kbSpecialKey"));
    langBtn->setFixedHeight(36);
    connect(langBtn, &QPushButton::clicked, this, &VirtualKeyboardWidget::toggleLayout);
    m_grid->addWidget(langBtn, 4, 6, 1, 2);

    auto* enterBtn = new QPushButton(QStringLiteral("Enter"), this);
    enterBtn->setObjectName(QStringLiteral("kbSpecialKey"));
    enterBtn->setFixedHeight(36);
    connect(enterBtn, &QPushButton::clicked, this, &VirtualKeyboardWidget::enterPressed);
    m_grid->addWidget(enterBtn, 4, 8, 1, 2);
}

void VirtualKeyboardWidget::toggleLayout()
{
    m_russian = !m_russian;
    rebuildKeys();
}

void VirtualKeyboardWidget::rebuildKeys()
{
    for (int i = 0; i < m_keyButtons.size() && i < m_keyDefs.size(); ++i) {
        const auto& kd = m_keyDefs[i];
        m_keyButtons[i]->setText(m_russian ? kd.labelRu : kd.labelEn);
    }
}