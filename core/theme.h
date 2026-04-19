#pragma once
// -------------------------------------------------------
// theme.h — Стили интерфейса J.A.R.V.I.S. (sci-fi HUD)
// -------------------------------------------------------

#include <QString>

namespace Theme {

inline const QString& globalStyleSheet()
{
    static const QString css = QStringLiteral(R"(
        QMainWindow {
            background-color: #080c14;
        }
        QWidget {
            background-color: #080c14;
            color: #96c8e6;
            font-family: "Segoe UI", "Consolas", monospace;
            font-size: 13px;
        }

        /* === Заголовок === */
        #titleLabel {
            color: #00d4ff;
            font-size: 22px;
            font-weight: bold;
            font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
            padding: 4px 0;
        }
        #statusText {
            color: #00d4ff;
            font-size: 12px;
            font-family: "Segoe UI", sans-serif;
        }

        /* === Разделитель === */
        #separator {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 transparent,
                stop:0.2 #0a3a5a,
                stop:0.5 #00d4ff,
                stop:0.8 #0a3a5a,
                stop:1 transparent);
        }

        /* === Лог === */
        #logArea {
            background-color: #060a12;
            color: #96c8e6;
            border: 1px solid #0f2a3d;
            border-radius: 6px;
            padding: 10px;
            font-family: "Consolas", monospace;
            font-size: 13px;
            selection-background-color: #00d4ff;
            selection-color: #080c14;
        }
        #logArea QScrollBar:vertical {
            background: #080c14;
            width: 6px;
            border-radius: 3px;
        }
        #logArea QScrollBar::handle:vertical {
            background: #1a3a50;
            border-radius: 3px;
            min-height: 30px;
        }
        #logArea QScrollBar::handle:vertical:hover {
            background: #00d4ff;
        }
        #logArea QScrollBar::add-line:vertical,
        #logArea QScrollBar::sub-line:vertical {
            height: 0;
        }
        #logArea QScrollBar::add-page:vertical,
        #logArea QScrollBar::sub-page:vertical {
            background: transparent;
        }

        /* === Поле ввода === */
        #inputField {
            background-color: #0b1018;
            color: #e0f0ff;
            border: 1px solid #0f2a3d;
            border-radius: 5px;
            padding: 9px 14px;
            font-size: 14px;
            font-family: "Consolas", monospace;
        }
        #inputField:focus {
            border-color: #00d4ff;
        }
        #inputField::placeholder {
            color: #2a4a60;
        }

        /* === Кнопка отправки === */
        #sendBtn {
            background-color: #00243d;
            color: #00d4ff;
            border: 1px solid #00587a;
            border-radius: 5px;
            padding: 9px 18px;
            font-weight: bold;
            font-size: 14px;
            font-family: "Segoe UI Semibold", "Segoe UI", sans-serif;
        }
        #sendBtn:hover {
            background-color: #003d5e;
            border-color: #00d4ff;
        }
        #sendBtn:pressed {
            background-color: #00d4ff;
            color: #080c14;
        }

        /* === Кнопки нижней панели === */
        #clearBtn, #kbToggleBtn {
            background-color: transparent;
            color: #2a4a60;
            border: 1px solid #132030;
            border-radius: 4px;
            padding: 5px 14px;
            font-size: 11px;
        }
        #clearBtn:hover, #kbToggleBtn:hover {
            color: #96c8e6;
            border-color: #1a3a50;
        }
        #clearBtn:pressed, #kbToggleBtn:pressed {
            color: #00d4ff;
            border-color: #00d4ff;
        }

        /* === Виртуальная клавиатура === */
        #keyboardPanel {
            background-color: #0a0f1a;
            border-top: 1px solid #0f2a3d;
        }
        #keyboardPanel QPushButton {
            background-color: #0e1a28;
            color: #96c8e6;
            border: 1px solid #1a3050;
            border-radius: 4px;
            font-family: "Consolas", monospace;
            font-size: 13px;
            min-height: 34px;
            min-width: 32px;
            padding: 2px 4px;
        }
        #keyboardPanel QPushButton:hover {
            background-color: #142a40;
            border-color: #00587a;
            color: #e0f0ff;
        }
        #keyboardPanel QPushButton:pressed {
            background-color: #00d4ff;
            color: #080c14;
            border-color: #00d4ff;
        }
        #keyboardPanel #kbSpecialKey {
            background-color: #0c1828;
            color: #00d4ff;
            font-weight: bold;
        }
        #keyboardPanel #kbSpecialKey:hover {
            background-color: #0f2a40;
        }
        #keyboardPanel #kbSpaceBar {
            min-width: 200px;
        }
    )");
    return css;
}

// Цвета для лога
namespace LogColors {
    inline constexpr const char* jarvis   = "#00d4ff";
    inline constexpr const char* user     = "#e0f0ff";
    inline constexpr const char* system   = "#00ff88";
    inline constexpr const char* error    = "#ff4466";
    inline constexpr const char* timestamp = "#1e3a4d";
}

} // namespace Theme