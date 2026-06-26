#pragma once
// -------------------------------------------------------
// theme_manager.h — Dynamic theme engine for J.A.R.V.I.S.
// Swap-on-the-fly QSS themes with per-theme log colors.
// -------------------------------------------------------

#include <QString>
#include <QApplication>

struct ThemeColors {
    const char* jarvis;
    const char* user;
    const char* system;
    const char* error;
    const char* timestamp;
    const char* cardBg;        // message card background
    const char* cardBorder;    // message card border
};

namespace ThemeManager {

enum Theme { Cyberpunk = 0, SoftLight = 1, Glass = 2, ThemeCount };

inline const ThemeColors& colors(int index)
{
    static const ThemeColors kCyberpunk = {
        "#66FCF1", "#C5C6C7", "#45A29E", "#FF4C4C",
        "#4B5D6B", "rgba(11,12,16,0.85)", "rgba(102,252,241,0.12)"
    };
    static const ThemeColors kSoftLight = {
        "#4A5568", "#2D3748", "#38A169", "#E53E3E",
        "#A0AEC0", "rgba(255,255,255,0.72)", "rgba(203,213,224,0.45)"
    };
    static const ThemeColors kGlass = {
        "#00d4ff", "#e8f0fe", "#00e676", "#ff5252",
        "#3a4a5e", "rgba(10,14,24,0.65)", "rgba(0,212,255,0.10)"
    };
    switch (index) {
    case SoftLight: return kSoftLight;
    case Glass:     return kGlass;
    default:        return kCyberpunk;
    }
}

inline QString buildMessageHtml(const ThemeColors& c,
                                const QString& time,
                                const QString& who,
                                const QString& text,
                                const QString& roleColor)
{
    return QStringLiteral(
        "<div style='"
        "background:%1; border:1px solid %2; border-radius:10px; "
        "padding:10px 14px; margin:4px 0;'>"
        "<span style='color:%3; font-size:11px; font-family:Segoe UI,sans-serif;'>[%4]</span> "
        "<b style='color:%5; font-family:Segoe UI Semibold,Segoe UI,sans-serif;'>%6:</b><br>"
        "<span style='color:%7; font-family:Consolas,Roboto Mono,monospace; "
        "font-size:13px; line-height:1.5;'>%8</span>"
        "</div>"
    ).arg(c.cardBg, c.cardBorder, c.timestamp, time,
          roleColor, who, roleColor, text);
}

// ── Cyberpunk ──────────────────────────────────────────
inline const QString& cyberpunkStyleSheet()
{
    static const QString css = QStringLiteral(R"(
        QMainWindow { background-color: #0B0C10; }
        QWidget { background-color: transparent; color: #C5C6C7;
          font-family: 'Segoe UI', 'Consolas', monospace; font-size: 13px; }

        #titleLabel { color: #66FCF1; font-size: 26px; font-weight: bold;
          font-family: 'Segoe UI Semibold', sans-serif; letter-spacing: 4px; padding: 6px 0 2px 0; }
        #statusText { color: rgba(102,252,241,180); font-size: 11px; padding-bottom: 2px; }
        #separator { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
          stop:0 transparent, stop:0.15 rgba(155,89,182,60),
          stop:0.35 #66FCF1, stop:0.5 #9B59B6, stop:0.65 #66FCF1,
          stop:0.85 rgba(155,89,182,60), stop:1 transparent);
          min-height: 2px; max-height: 2px; }

        #logArea { background-color: rgba(11,12,16,220); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,30); border-radius: 12px;
          padding: 10px; font-family: 'Consolas', monospace; font-size: 13px;
          selection-background-color: rgba(102,252,241,160); selection-color: #0B0C10; }
        #logArea QScrollBar:vertical { background: transparent; width: 5px; border-radius: 2px; margin: 4px 1px; }
        #logArea QScrollBar::handle:vertical { background: rgba(102,252,241,40); border-radius: 2px; min-height: 30px; }
        #logArea QScrollBar::handle:vertical:hover { background: rgba(102,252,241,120); }
        #logArea QScrollBar::add-line:vertical, #logArea QScrollBar::sub-line:vertical { height: 0; }
        #logArea QScrollBar::add-page:vertical, #logArea QScrollBar::sub-page:vertical { background: transparent; }

        #inputField { background-color: rgba(15,16,22,200); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,25); border-radius: 8px;
          padding: 10px 14px; font-size: 14px; font-family: 'Consolas', monospace; }
        #inputField:focus { border: 2px solid qlineargradient(x1:0,y1:0,x2:1,y2:0,
          stop:0 #66FCF1, stop:1 #9B59B6); background-color: rgba(15,16,22,240); padding: 9px 13px; }
        #inputField::placeholder { color: rgba(197,198,199,40); }

        #sendBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #66FCF1, stop:1 #9B59B6); color: #0B0C10; border: none;
          border-radius: 8px; padding: 9px 18px; font-weight: bold; font-size: 14px;
          font-family: 'Segoe UI Semibold', sans-serif; }
        #sendBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #7DFDF6, stop:1 #B07AE0); border: 1px solid rgba(102,252,241,100); }
        #sendBtn:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #9B59B6, stop:1 #66FCF1); }

        #clearBtn, #kbToggleBtn { background-color: transparent;
          color: rgba(197,198,199,60); border: 1px solid rgba(102,252,241,15);
          border-radius: 6px; padding: 5px 14px; font-size: 11px; }
        #clearBtn:hover, #kbToggleBtn:hover { color: #C5C6C7;
          border-color: rgba(102,252,241,50); background-color: rgba(102,252,241,8); }

        #micBtn { background-color: rgba(15,16,22,180); color: rgba(102,252,241,100);
          border: 1px solid rgba(102,252,241,22); border-radius: 8px; font-size: 16px; }
        #micBtn:hover { background-color: rgba(20,22,30,200); color: #66FCF1;
          border-color: rgba(102,252,241,70); }
        #micBtn[active="true"] { background-color: rgba(60,10,10,200); color: #ff5252;
          border: 2px solid rgba(255,82,82,180); }

        #likeBtn { background: transparent; color: rgba(69,162,158,50);
          border: 1px solid rgba(69,162,158,25); border-radius: 6px; font-size: 13px; padding: 0 8px; }
        #likeBtn:hover { background: rgba(69,162,158,12); color: #45A29E;
          border-color: rgba(69,162,158,80); }
        #likeBtn:disabled { color: rgba(69,162,158,20); border-color: rgba(69,162,158,8); }
        #likeBtn[liked="true"] { color: #45A29E; border-color: #45A29E; background: rgba(69,162,158,15); }

        #audioModeBtn { background-color: rgba(15,16,22,180); color: rgba(102,252,241,100);
          border: 1px solid rgba(102,252,241,22); border-radius: 8px; font-size: 12px;
          padding: 4px 10px; }
        #audioModeBtn:hover { color: #66FCF1; border-color: rgba(102,252,241,60);
          background-color: rgba(102,252,241,8); }

        QMenuBar { background-color: #0B0C10; color: #C5C6C7; font-size: 12px;
          border-bottom: 1px solid rgba(102,252,241,18); }
        QMenuBar::item { background: transparent; padding: 5px 12px; border-radius: 4px; }
        QMenuBar::item:selected { background-color: rgba(102,252,241,18); color: #66FCF1; }
        QMenu, QMenu QMenu {
          background-color: rgba(11,12,16,248); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,20); border-radius: 8px; padding: 6px 4px;
          font-family: 'Segoe UI', sans-serif; font-size: 13px; }
        QMenu::item { padding: 7px 28px 7px 14px; border-radius: 5px; margin: 1px 4px; }
        QMenu::item:selected { background-color: rgba(102,252,241,0.12); color: #66FCF1; }
        QMenu::item:disabled { color: rgba(197,198,199,0.30); }
        QMenu::separator { height: 1px; background: rgba(102,252,241,0.10); margin: 4px 10px; }
        QMenu::indicator { width: 14px; height: 14px; margin-left: 6px; }
        QMenu::indicator:checked { background: #66FCF1; border-radius: 3px; }
        QMenu::indicator:unchecked { background: rgba(102,252,241,0.10); border-radius: 3px; }
        QMenu::right-arrow { width: 8px; height: 8px; }

        #keyboardPanel { background-color: rgba(11,12,16,230); border-top: 1px solid rgba(102,252,241,18); }
        #keyboardPanel QPushButton { background-color: rgba(15,16,22,180); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,18); border-radius: 6px;
          font-family: 'Consolas', monospace; font-size: 13px; min-height: 34px; min-width: 32px; }
        #keyboardPanel QPushButton:hover { background-color: rgba(102,252,241,15);
          border-color: rgba(102,252,241,50); color: #66FCF1; }
        #keyboardPanel QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #66FCF1, stop:1 #9B59B6); color: #0B0C10; }
        #keyboardPanel #kbSpecialKey { background-color: rgba(11,12,16,200);
          color: #66FCF1; font-weight: bold; border-color: rgba(102,252,241,30); }
        #keyboardPanel #kbSpaceBar { min-width: 200px; }

        QGroupBox, QFrame#suggestionPanel, QFrame#clarifyPanel,
        QFrame#updatePanel, QFrame#attachPanel {
          background-color: rgba(11,12,16,200); border: 1px solid rgba(102,252,241,22);
          border-radius: 10px; padding: 10px; }

        QScrollBar:vertical { background: transparent; width: 5px; border-radius: 2px; }
        QScrollBar::handle:vertical { background: rgba(102,252,241,35); border-radius: 2px; min-height: 24px; }
        QScrollBar::handle:vertical:hover { background: rgba(102,252,241,100); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: transparent; height: 5px; border-radius: 2px; }
        QScrollBar::handle:horizontal { background: rgba(102,252,241,35); border-radius: 2px; min-width: 24px; }
        QScrollBar::handle:horizontal:hover { background: rgba(102,252,241,100); }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

        QToolTip { background-color: rgba(11,12,16,240); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,40); border-radius: 6px; padding: 6px 10px; font-size: 12px; }

        QDialog { background-color: rgba(11,12,16,250); color: #C5C6C7; }
        QLabel { background: transparent; }
        QCheckBox { color: #C5C6C7; spacing: 6px; }
        QRadioButton { color: #C5C6C7; spacing: 6px; }
        QComboBox { background-color: rgba(15,16,22,200); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,22); border-radius: 6px; padding: 4px 8px; }
        QComboBox QAbstractItemView { background-color: rgba(11,12,16,245); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,22); selection-background-color: rgba(102,252,241,22); }
        QSpinBox, QDoubleSpinBox { background-color: rgba(15,16,22,200); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,22); border-radius: 4px; padding: 2px 6px; }
        QLineEdit { background-color: rgba(15,16,22,180); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,22); border-radius: 6px; padding: 4px 8px; }
        QTextEdit { background-color: rgba(11,12,16,200); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,25); border-radius: 8px; }
        QPushButton { background-color: rgba(15,16,22,180); color: #C5C6C7;
          border: 1px solid rgba(102,252,241,18); border-radius: 6px; padding: 5px 12px; }
        QPushButton:hover { background-color: rgba(102,252,241,10); color: #66FCF1;
          border-color: rgba(102,252,241,40); }
        QProgressBar { background: rgba(11,12,16,200); border: 1px solid rgba(102,252,241,18);
          border-radius: 4px; color: #C5C6C7; font-size: 10px; text-align: center; }
        QProgressBar::chunk { background: rgba(102,252,241,160); border-radius: 3px; }
        #hamburgerBtn { background: transparent; color: rgba(197,198,199,60);
          border: 1px solid transparent; border-radius: 8px; }
        #hamburgerBtn:hover { background: rgba(102,252,241,6); color: #66FCF1;
          border-color: rgba(102,252,241,15); }
    )");
    return css;
}

// ── Soft Light (iOS-inspired) ──────────────────────────
inline const QString& softLightStyleSheet()
{
    static const QString css = QStringLiteral(R"(
        QMainWindow { background-color: #F4F4F2; }
        QWidget { background-color: transparent; color: #3D3D3D;
          font-family: 'Segoe UI', 'Roboto', sans-serif; font-size: 13px; }

        #titleLabel { color: #4A5568; font-size: 26px; font-weight: bold;
          font-family: 'Segoe UI Semibold', sans-serif; letter-spacing: 4px; padding: 6px 0 2px 0; }
        #statusText { color: rgba(74,85,104,140); font-size: 11px; padding-bottom: 2px; }
        #separator { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
          stop:0 transparent, stop:0.2 #CBD5E0,
          stop:0.5 #A0AEC0, stop:0.8 #CBD5E0,
          stop:1 transparent);
          min-height: 1px; max-height: 1px; }

        #logArea { background-color: #FAFAF8; color: #2D3748;
          border: 1px solid #E2E8F0; border-radius: 14px;
          padding: 10px; font-family: 'Consolas', 'Roboto Mono', monospace; font-size: 13px;
          selection-background-color: rgba(66,153,225,0.35); selection-color: #1A202C; }
        #logArea QScrollBar:vertical { background: transparent; width: 4px; border-radius: 2px; margin: 4px 1px; }
        #logArea QScrollBar::handle:vertical { background: rgba(160,174,192,0.35); border-radius: 2px; min-height: 30px; }
        #logArea QScrollBar::handle:vertical:hover { background: rgba(160,174,192,0.65); }
        #logArea QScrollBar::add-line:vertical, #logArea QScrollBar::sub-line:vertical { height: 0; }
        #logArea QScrollBar::add-page:vertical, #logArea QScrollBar::sub-page:vertical { background: transparent; }

        #inputField { background-color: #FAFAF8; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 10px;
          padding: 10px 14px; font-size: 14px; font-family: 'Consolas', monospace; }
        #inputField:focus { border: 1px solid #90B4D6; background-color: #FAFAF8; padding: 10px 14px; }
        #inputField::placeholder { color: rgba(160,174,192,0.55); }

        #sendBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #4299E1, stop:1 #667EEA); color: #FFFFFF; border: none;
          border-radius: 10px; padding: 9px 18px; font-weight: bold; font-size: 14px;
          font-family: 'Segoe UI Semibold', sans-serif; }
        #sendBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #63B3ED, stop:1 #7F9CF5); }
        #sendBtn:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #667EEA, stop:1 #4299E1); }

        #clearBtn, #kbToggleBtn { background-color: transparent;
          color: #A0AEC0; border: 1px solid #E2E8F0; border-radius: 8px;
          padding: 5px 14px; font-size: 11px; }
        #clearBtn:hover, #kbToggleBtn:hover { color: #4A5568;
          border-color: #CBD5E0; background-color: rgba(237,242,247,0.6); }

        #micBtn { background-color: #EDF2F7; color: #A0AEC0;
          border: 1px solid #E2E8F0; border-radius: 10px; font-size: 16px; }
        #micBtn:hover { background-color: #E2E8F0; color: #4299E1; border-color: #4299E1; }
        #micBtn[active="true"] { background-color: #FED7D7; color: #E53E3E;
          border: 2px solid #FC8181; }

        #likeBtn { background: transparent; color: rgba(56,161,105,0.35);
          border: 1px solid rgba(56,161,105,0.18); border-radius: 8px; font-size: 13px; padding: 0 8px; }
        #likeBtn:hover { background: rgba(56,161,105,0.06); color: #38A169;
          border-color: rgba(56,161,105,0.5); }
        #likeBtn:disabled { color: rgba(56,161,105,0.15); border-color: rgba(56,161,105,0.08); }
        #likeBtn[liked="true"] { color: #38A169; border-color: #38A169; background: rgba(56,161,105,0.08); }

        #audioModeBtn { background-color: #EDF2F7; color: #718096;
          border: 1px solid #E2E8F0; border-radius: 8px; font-size: 12px;
          padding: 4px 10px; }
        #audioModeBtn:hover { color: #4A5568; border-color: #CBD5E0;
          background-color: #E2E8F0; }

        QMenuBar { background-color: #F4F4F2; color: #4A5568; font-size: 12px;
          border-bottom: 1px solid #E2E8F0; }
        QMenuBar::item { background: transparent; padding: 5px 12px; border-radius: 6px; }
        QMenuBar::item:selected { background-color: #EDF2F7; color: #2D3748; }
        QMenu { background-color: #FAFAF8; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 10px; padding: 4px; }
        QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
        QMenu::item:selected { background-color: #EDF2F7; color: #2D3748; }
        QMenu::separator { height: 1px; background: #EDF2F7; margin: 4px 8px; }

        #keyboardPanel { background-color: #EDF2F7; border-top: 1px solid #E2E8F0; }
        #keyboardPanel QPushButton { background-color: #F0F2F0; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 8px;
          font-family: 'Consolas', monospace; font-size: 13px; min-height: 34px; min-width: 32px; }
        #keyboardPanel QPushButton:hover { background-color: #EDF2F7; border-color: #4299E1; color: #4299E1; }
        #keyboardPanel QPushButton:pressed { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
          stop:0 #4299E1, stop:1 #667EEA); color: #FFFFFF; }
        #keyboardPanel #kbSpecialKey { background-color: #EDF2F7; color: #4299E1;
          font-weight: bold; border-color: #CBD5E0; }
        #keyboardPanel #kbSpaceBar { min-width: 200px; }

        QGroupBox, QFrame#suggestionPanel, QFrame#clarifyPanel,
        QFrame#updatePanel, QFrame#attachPanel {
          background-color: #FAFAF8; border: 1px solid #E8ECF0;
          border-radius: 12px; padding: 10px; }

        QScrollBar:vertical { background: transparent; width: 4px; border-radius: 2px; }
        QScrollBar::handle:vertical { background: rgba(160,174,192,0.25); border-radius: 2px; min-height: 24px; }
        QScrollBar::handle:vertical:hover { background: rgba(160,174,192,0.50); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: transparent; height: 4px; border-radius: 2px; }
        QScrollBar::handle:horizontal { background: rgba(160,174,192,0.25); border-radius: 2px; min-width: 24px; }
        QScrollBar::handle:horizontal:hover { background: rgba(160,174,192,0.50); }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

        QToolTip { background-color: #F4F4F2; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 8px; padding: 6px 10px; font-size: 12px; }

        QDialog { background-color: #F4F4F2; color: #2D3748; }
        QLabel { background: transparent; }
        QCheckBox { color: #2D3748; spacing: 6px; }
        QRadioButton { color: #2D3748; spacing: 6px; }
        QComboBox { background-color: #F0F2F0; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 6px; padding: 4px 8px; }
        QComboBox QAbstractItemView { background-color: #FAFAF8; color: #2D3748;
          border: 1px solid #E8ECF0; selection-background-color: #EDF2F7; }
        QSpinBox, QDoubleSpinBox { background-color: #F0F2F0; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 4px; padding: 2px 6px; }
        QLineEdit { background-color: #F0F2F0; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 6px; padding: 4px 8px; }
        QTextEdit { background-color: #FAFAF8; color: #2D3748;
          border: 1px solid #E8ECF0; border-radius: 8px; }
        QPushButton { background-color: #EEEEE8; color: #4A5568;
          border: 1px solid #E8ECF0; border-radius: 8px; padding: 5px 12px; }
        QPushButton:hover { background-color: #E2E8E0; color: #2D3748;
          border-color: #D0D8D0; }
        QProgressBar { background: #EDF2F7; border: 1px solid #E2E8F0;
          border-radius: 4px; color: #4A5568; font-size: 10px; text-align: center; }
        QProgressBar::chunk { background: #4299E1; border-radius: 3px; }
        #hamburgerBtn { background: transparent; color: #A0AEC0;
          border: 1px solid transparent; border-radius: 8px; }
        #hamburgerBtn:hover { background: rgba(66,153,225,0.06); color: #4A5568;
          border-color: rgba(66,153,225,0.15); }
    )");
    return css;
}

inline void applyStyleSheet(int index)
{
    switch (index) {
    case Cyberpunk: qApp->setStyleSheet(cyberpunkStyleSheet()); break;
    case SoftLight: qApp->setStyleSheet(softLightStyleSheet()); break;
    default:        qApp->setStyleSheet(cyberpunkStyleSheet()); break;
    }
}

} // namespace ThemeManager
