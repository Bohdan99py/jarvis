#pragma once
// -------------------------------------------------------
// virtual_keyboard.h — Виртуальная клавиатура + эмуляция
// -------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _WIN32_WINNT 0x0601

#include <QObject>
#include <QString>
#include <QWidget>
#include <QGridLayout>
#include <QPushButton>
#include <QMap>
#include <windows.h>
#include <atomic>
#include <initializer_list>

// ============================================================
// Эмуляция клавиатуры через SendInput
// ============================================================

class KeyEmulator : public QObject
{
    Q_OBJECT

public:
    explicit KeyEmulator(QObject* parent = nullptr);

    // Напечатать строку посимвольно (в отдельном потоке)
    void typeText(const QString& text, int delayMs = 30);

    // Нажать одну виртуальную клавишу
    void pressKey(WORD vkCode);

    // Нажать комбинацию клавиш
    void pressCombo(std::initializer_list<WORD> keys);

    // Остановить набор
    void stopTyping();

    bool isTyping() const { return m_typing.load(); }

signals:
    void typingStarted();
    void typingProgress(int current, int total);
    void typingFinished();

private:
    void sendUnicodeChar(wchar_t ch);
    void sendVKey(WORD vk, bool down);

    std::atomic<bool> m_typing{false};
    std::atomic<bool> m_stopRequested{false};
};

// ============================================================
// Виджет виртуальной клавиатуры (QWERTY-панель)
// ============================================================

class VirtualKeyboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VirtualKeyboardWidget(QWidget* parent = nullptr);

signals:
    void charPressed(const QString& ch);   // символ для поля ввода
    void backspacePressed();
    void enterPressed();

private slots:
    void toggleLayout();

private:
    void buildLayout();
    void rebuildKeys();

    QGridLayout* m_grid = nullptr;
    bool m_russian = true; // стартуем с русской раскладки

    struct KeyDef {
        QString labelRu;
        QString labelEn;
        int row;
        int col;
        int colSpan;
        bool isSpecial;
    };

    QVector<KeyDef> m_keyDefs;
    QVector<QPushButton*> m_keyButtons;
};