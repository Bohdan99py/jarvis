#pragma once
// -------------------------------------------------------
// theme.h — Glassmorphic UI theme for J.A.R.V.I.S.
// Modern semi-transparent holographic AI interface
// -------------------------------------------------------

#include <QString>

namespace Theme {

inline const QString& globalStyleSheet()
{
    static const QString css = QStringLiteral(R"(

        /* ================================================
           BASE — dark translucent foundation
           ================================================ */
        QMainWindow {
            background-color: rgba(8, 10, 18, 245);
        }
        QWidget {
            background-color: transparent;
            color: #c0d8ee;
            font-family: "Segoe UI", "Consolas", monospace;
            font-size: 13px;
        }

        /* ================================================
           TITLE — big, letter-spaced "J.A.R.V.I.S."
           ================================================ */
        #titleLabel {
            color: #00d4ff;
            font-size: 26px;
            font-weight: bold;
            font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
            letter-spacing: 4px;
            padding: 6px 0 2px 0;
        }

        /* ================================================
           STATUS TEXT — smaller with a soft glow feel
           ================================================ */
        #statusText {
            color: rgba(0, 212, 255, 180);
            font-size: 11px;
            font-family: "Segoe UI", sans-serif;
            padding-bottom: 2px;
        }

        /* ================================================
           SEPARATOR — gradient accent line
           ================================================ */
        #separator {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0   transparent,
                stop:0.15 rgba(124, 77, 255, 80),
                stop:0.35 #00d4ff,
                stop:0.5  #7c4dff,
                stop:0.65 #00d4ff,
                stop:0.85 rgba(124, 77, 255, 80),
                stop:1    transparent);
            min-height: 2px;
            max-height: 2px;
        }

        /* ================================================
           LOG AREA — glass panel with soft inner shadow
           ================================================ */
        #logArea {
            background-color: rgba(10, 14, 24, 200);
            color: #c0d8ee;
            border: 1px solid rgba(0, 212, 255, 40);
            border-radius: 10px;
            padding: 12px;
            font-family: "Consolas", monospace;
            font-size: 13px;
            selection-background-color: rgba(0, 212, 255, 160);
            selection-color: #ffffff;
        }

        /* --- Scrollbars: thin, rounded, semi-transparent --- */
        #logArea QScrollBar:vertical {
            background: transparent;
            width: 5px;
            border-radius: 2px;
            margin: 4px 1px;
        }
        #logArea QScrollBar::handle:vertical {
            background: rgba(0, 212, 255, 50);
            border-radius: 2px;
            min-height: 30px;
        }
        #logArea QScrollBar::handle:vertical:hover {
            background: rgba(0, 212, 255, 140);
        }
        #logArea QScrollBar::add-line:vertical,
        #logArea QScrollBar::sub-line:vertical {
            height: 0;
        }
        #logArea QScrollBar::add-page:vertical,
        #logArea QScrollBar::sub-page:vertical {
            background: transparent;
        }

        /* ================================================
           INPUT FIELD — glassmorphic with glowing focus
           ================================================ */
        #inputField {
            background-color: rgba(14, 18, 30, 180);
            color: #e8f0fe;
            border: 1px solid rgba(0, 212, 255, 35);
            border-radius: 8px;
            padding: 10px 14px;
            font-size: 14px;
            font-family: "Consolas", monospace;
        }
        #inputField:focus {
            border: 2px solid qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #00d4ff, stop:1 #7c4dff);
            background-color: rgba(14, 18, 30, 220);
            padding: 9px 13px;
        }
        #inputField::placeholder {
            color: rgba(150, 200, 230, 60);
        }

        /* ================================================
           SEND BUTTON — gradient cyan-to-purple, glow hover
           ================================================ */
        #sendBtn {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #00d4ff, stop:1 #7c4dff);
            color: #ffffff;
            border: none;
            border-radius: 8px;
            padding: 9px 18px;
            font-weight: bold;
            font-size: 14px;
            font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
        }
        #sendBtn:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #33dfff, stop:1 #9b7aff);
            border: 1px solid rgba(0, 212, 255, 120);
        }
        #sendBtn:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #7c4dff, stop:1 #00d4ff);
            color: #ffffff;
        }

        /* ================================================
           BOTTOM BUTTONS — clear, keyboard toggle
           ================================================ */
        #clearBtn, #kbToggleBtn {
            background-color: transparent;
            color: rgba(150, 200, 230, 80);
            border: 1px solid rgba(0, 212, 255, 20);
            border-radius: 6px;
            padding: 5px 14px;
            font-size: 11px;
        }
        #clearBtn:hover, #kbToggleBtn:hover {
            color: #c0d8ee;
            border-color: rgba(0, 212, 255, 60);
            background-color: rgba(0, 212, 255, 10);
        }
        #clearBtn:pressed, #kbToggleBtn:pressed {
            color: #00d4ff;
            border-color: rgba(0, 212, 255, 120);
            background-color: rgba(0, 212, 255, 20);
        }

        /* ================================================
           MIC BUTTON — glass base, red glow when active
           ================================================ */
        #micBtn {
            background-color: rgba(14, 22, 38, 180);
            color: rgba(100, 160, 200, 160);
            border: 1px solid rgba(0, 212, 255, 30);
            border-radius: 8px;
            font-size: 16px;
        }
        #micBtn:hover {
            background-color: rgba(20, 30, 50, 200);
            color: #00d4ff;
            border-color: rgba(0, 212, 255, 80);
        }
        #micBtn[active="true"] {
            background-color: rgba(60, 10, 10, 200);
            color: #ff5252;
            border: 2px solid rgba(255, 82, 82, 180);
        }

        /* ================================================
           LIKE BUTTON — green glow when liked
           ================================================ */
        #likeBtn {
            background: transparent;
            color: rgba(0, 230, 118, 60);
            border: 1px solid rgba(0, 230, 118, 30);
            border-radius: 6px;
            font-size: 13px;
            padding: 0 8px;
        }
        #likeBtn:hover {
            background: rgba(0, 230, 118, 15);
            color: #00e676;
            border-color: rgba(0, 230, 118, 100);
        }
        #likeBtn:disabled {
            color: rgba(0, 230, 118, 25);
            border-color: rgba(0, 230, 118, 10);
        }
        #likeBtn[liked="true"] {
            color: #00e676;
            border-color: #00e676;
            background: rgba(0, 230, 118, 20);
        }

        /* ================================================
           MENU BAR — frosted glass with subtle borders
           ================================================ */
        QMenuBar {
            background-color: rgba(10, 14, 24, 220);
            color: #c0d8ee;
            font-size: 12px;
            border-bottom: 1px solid rgba(0, 212, 255, 25);
        }
        QMenuBar::item {
            background: transparent;
            padding: 5px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: rgba(0, 212, 255, 25);
            color: #00d4ff;
        }
        QMenu {
            background-color: rgba(12, 18, 30, 235);
            color: #c0d8ee;
            border: 1px solid rgba(0, 212, 255, 30);
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 24px 6px 12px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: rgba(0, 212, 255, 30);
            color: #00d4ff;
        }
        QMenu::separator {
            height: 1px;
            background: rgba(0, 212, 255, 20);
            margin: 4px 8px;
        }

        /* ================================================
           VIRTUAL KEYBOARD — glass panel
           ================================================ */
        #keyboardPanel {
            background-color: rgba(10, 14, 24, 210);
            border-top: 1px solid rgba(0, 212, 255, 25);
        }
        #keyboardPanel QPushButton {
            background-color: rgba(16, 24, 40, 180);
            color: #c0d8ee;
            border: 1px solid rgba(0, 212, 255, 25);
            border-radius: 6px;
            font-family: "Consolas", monospace;
            font-size: 13px;
            min-height: 34px;
            min-width: 32px;
            padding: 2px 4px;
        }
        #keyboardPanel QPushButton:hover {
            background-color: rgba(0, 212, 255, 20);
            border-color: rgba(0, 212, 255, 60);
            color: #e8f0fe;
        }
        #keyboardPanel QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #00d4ff, stop:1 #7c4dff);
            color: #ffffff;
            border-color: #00d4ff;
        }
        #keyboardPanel #kbSpecialKey {
            background-color: rgba(12, 20, 34, 200);
            color: #00d4ff;
            font-weight: bold;
            border-color: rgba(0, 212, 255, 40);
        }
        #keyboardPanel #kbSpecialKey:hover {
            background-color: rgba(0, 212, 255, 25);
        }
        #keyboardPanel #kbSpaceBar {
            min-width: 200px;
        }

        /* ================================================
           GLASSMORPHIC CARD PANELS
           Suggestion, clarify, update, attach, etc.
           ================================================ */
        QGroupBox, QFrame#suggestionPanel, QFrame#clarifyPanel,
        QFrame#updatePanel, QFrame#attachPanel {
            background-color: rgba(12, 18, 30, 190);
            border: 1px solid rgba(0, 212, 255, 30);
            border-radius: 10px;
            padding: 10px;
        }

        /* ================================================
           GLOBAL SCROLLBAR STYLE (non-log areas)
           ================================================ */
        QScrollBar:vertical {
            background: transparent;
            width: 5px;
            border-radius: 2px;
        }
        QScrollBar::handle:vertical {
            background: rgba(0, 212, 255, 40);
            border-radius: 2px;
            min-height: 24px;
        }
        QScrollBar::handle:vertical:hover {
            background: rgba(0, 212, 255, 120);
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 5px;
            border-radius: 2px;
        }
        QScrollBar::handle:horizontal {
            background: rgba(0, 212, 255, 40);
            border-radius: 2px;
            min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover {
            background: rgba(0, 212, 255, 120);
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0;
        }
        QScrollBar::add-page:horizontal,
        QScrollBar::sub-page:horizontal {
            background: transparent;
        }

        /* ================================================
           TOOLTIPS — frosted glass
           ================================================ */
        QToolTip {
            background-color: rgba(12, 18, 30, 230);
            color: #e8f0fe;
            border: 1px solid rgba(0, 212, 255, 50);
            border-radius: 6px;
            padding: 6px 10px;
            font-size: 12px;
        }

    )");
    return css;
}

// Log message colors — holographic palette
namespace LogColors {
    inline constexpr const char* jarvis    = "#00d4ff";
    inline constexpr const char* user      = "#e8f0fe";
    inline constexpr const char* system    = "#00e676";
    inline constexpr const char* error     = "#ff5252";
    inline constexpr const char* timestamp = "#3a4a5e";
}

} // namespace Theme
