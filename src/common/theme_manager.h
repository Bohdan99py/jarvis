#pragma once
// -------------------------------------------------------
// theme_manager.h — Dynamic theme engine for J.A.R.V.I.S.
// Swap-on-the-fly QSS themes with per-theme log colors.
//
// Общий стиль виджетов (кнопки, поля, меню, списки...) больше
// НЕ живёт здесь: он собирается из дизайн-токенов в
// JarvisTheme::buildApplicationStyleSheet(). В этом файле осталась
// только «обвязка приложения» — селекторы по objectName, которых
// нет в обычном Qt (#logArea, #sendBtn, #keyboardPanel и т.д.),
// и она тоже строится из тех же токенов.
// -------------------------------------------------------

#include <QString>
#include <QApplication>

#include "jarvis_theme.h"

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
    // Cyberpunk синхронизирован с JarvisTheme — правится только там.
    // Здесь строки, потому что уходят прямо в HTML сообщений чата.
    static const ThemeColors kCyberpunk = {
        "#66FCF1",                  // jarvis    = Theme.accent
        "#E7EAF0",                  // user      = Theme.onSurface
        "#3FBFB6",                  // system    = Theme.accentMuted
        "#FF5C6C",                  // error     = Theme.error
        "#68707E",                  // timestamp = Theme.onSurfaceDim
        "#161B24",                  // cardBg    = Theme.surface2
        "rgba(255,255,255,0.07)"    // cardBorder= Theme.outline
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
    // Тело сообщения набрано системным шрифтом, а не моноширинным:
    // Consolas в обычном тексте — главный признак «терминала из
    // 2000-х». Моноширинный остаётся только внутри <code>/<pre>,
    // которые формирует markdown-рендерер.
    // Имя роли отделено от текста весом и цветом, а не двоеточием
    // с переводом строки — меньше визуального шума.
    return QStringLiteral(
        "<div style='"
        "background:%1; border:1px solid %2; border-radius:12px; "
        "padding:12px 16px; margin:6px 0;'>"
        "<div style='margin-bottom:4px;'>"
        "<b style='color:%3; font-family:Segoe UI Variable Display,Segoe UI,sans-serif; "
        "font-size:13px; letter-spacing:0.3px;'>%4</b>"
        "<span style='color:%5; font-size:12px;'>&nbsp;&nbsp;%6</span>"
        "</div>"
        "<span style='color:%7; font-family:Segoe UI Variable Text,Segoe UI,sans-serif; "
        "font-size:15px; line-height:1.55;'>%8</span>"
        "</div>"
    ).arg(c.cardBg, c.cardBorder, roleColor, who,
          c.timestamp, time, QStringLiteral("#E7EAF0"), text);
}

// ── Cyberpunk: обвязка приложения ──────────────────────
// Только objectName-селекторы, которых нет в стандартном Qt.
// Всё остальное (кнопки, поля, меню, списки, скроллбары)
// приходит из JarvisTheme::buildApplicationStyleSheet().
inline QString appChromeStyleSheet()
{
    const auto& T = JarvisTheme::instance();
    const auto& F = JarvisType::instance();

    const QString cBg      = JarvisTheme::css(T.bg());
    const QString cS1      = JarvisTheme::css(T.surface1());
    const QString cS2      = JarvisTheme::css(T.surface2());
    const QString cS3      = JarvisTheme::css(T.surface3());
    const QString cLine    = JarvisTheme::css(T.outline());
    const QString cLineHi  = JarvisTheme::css(T.outlineStrong());
    const QString cText    = JarvisTheme::css(T.onSurface());
    const QString cTextVar = JarvisTheme::css(T.onSurfaceVariant());
    const QString cTextDim = JarvisTheme::css(T.onSurfaceDim());
    const QString cAccent  = JarvisTheme::css(T.accent());
    const QString cAccMu   = JarvisTheme::css(T.accentMuted());
    const QString cAccSb   = JarvisTheme::css(T.accentSubtle());
    const QString cOnAcc   = JarvisTheme::css(T.onAccent());
    const QString cError   = JarvisTheme::css(T.error());

    QString s;

    // ---- Шапка ---------------------------------------------------
    // Разрядка у заголовка уменьшена с 4px до 1.5px: широкий трекинг
    // на латинице — приём из заставок нулевых, современная типографика
    // разряжает только мелкие капительные подписи.
    s += QStringLiteral(
        "#titleLabel {"
        "  color: %1; font-size: %2px; font-weight: 600;"
        "  letter-spacing: 1.5px; padding: 4px 0 0 0;"
        "}"
        "#statusText { color: %3; font-size: %4px; padding-bottom: 2px; }"
        // Разделитель — тонкая линия-затухание вместо неонового градиента
        // в пол-экрана: он задаёт границу, а не претендует на внимание.
        "#separator {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 transparent, stop:0.5 %5, stop:1 transparent);"
        "  min-height: 1px; max-height: 1px; border: none;"
        "}"
        "#hamburgerBtn {"
        "  background: transparent; color: %3;"
        "  border: 1px solid transparent; border-radius: %6px;"
        "}"
        "#hamburgerBtn:hover { background: %7; color: %1; }"
    ).arg(cAccent).arg(F.heading()).arg(cTextVar).arg(F.caption())
     .arg(cLineHi).arg(T.radiusSm()).arg(cS2);

    // ---- Лента чата ----------------------------------------------
    s += QStringLiteral(
        "#logArea {"
        "  background-color: %1; color: %2;"
        "  border: 1px solid %3; border-radius: %4px;"
        "  padding: %5px;"
        "  selection-background-color: %6; selection-color: %2;"
        "}"
    ).arg(cS1, cText, cLine).arg(T.radiusLg()).arg(T.spaceMd()).arg(cAccSb);

    // ---- Строка ввода --------------------------------------------
    // Фокус меняет ТОЛЬКО цвет рамки, не её толщину: скачок с 1px на
    // 2px сдвигает содержимое на пиксель и выглядит как дребезг.
    s += QStringLiteral(
        "#inputField {"
        "  background-color: %1; color: %2;"
        "  border: 1px solid %3; border-radius: %4px;"
        "  padding: 10px 14px; font-size: %5px;"
        "}"
        "#inputField:hover { border-color: %6; }"
        "#inputField:focus { border-color: %7; background-color: %8; }"
    ).arg(cS2, cText, cLine).arg(T.radiusMd()).arg(F.body())
     .arg(cLineHi, cAccent, cS3);

    // ---- Кнопка отправки: единственный акцентный призыв ----------
    s += QStringLiteral(
        "#sendBtn {"
        "  background-color: %1; color: %2; border: none;"
        "  border-radius: %3px; padding: 10px 20px;"
        "  font-size: %4px; font-weight: 600;"
        "}"
        "#sendBtn:hover   { background-color: %5; }"
        "#sendBtn:pressed { background-color: %6; }"
        "#sendBtn:disabled { background-color: %7; color: %8; }"
    ).arg(cAccent, cOnAcc).arg(T.radiusMd()).arg(F.body())
     .arg(cAccMu, cAccMu, cS2, cTextDim);

    // ---- Вспомогательные кнопки строки ввода ---------------------
    // Все «тихие»: прозрачный фон, проявляются при наведении. На
    // экране должен быть один заметный призыв — отправка.
    s += QStringLiteral(
        "#clearBtn, #kbToggleBtn, #audioModeBtn, #micBtn, #likeBtn {"
        "  background-color: transparent; color: %1;"
        "  border: 1px solid %2; border-radius: %3px;"
        "  padding: 5px 12px; min-height: %4px;"
        "}"
        "#clearBtn:hover, #kbToggleBtn:hover, #audioModeBtn:hover,"
        "#micBtn:hover, #likeBtn:hover {"
        "  color: %5; border-color: %6; background-color: %7;"
        "}"
        "#likeBtn:disabled { color: %8; border-color: %2; }"
        // Активные состояния несут И цвет, И заливку — не только оттенок
        "#likeBtn[liked=\"true\"] {"
        "  color: %9; border-color: %9; background-color: %10;"
        "}"
        "#micBtn[active=\"true\"] {"
        "  color: %11; border-color: %11; background-color: rgba(255,92,108,0.14);"
        "}"
    ).arg(cTextVar, cLine).arg(T.radiusSm()).arg(T.hitTarget())
     .arg(cText, cLineHi, cS2, cTextDim, cAccent, cAccSb, cError);

    // ---- Плавающие панели ----------------------------------------
    s += QStringLiteral(
        "QFrame#suggestionPanel, QFrame#clarifyPanel,"
        "QFrame#updatePanel, QFrame#attachPanel {"
        "  background-color: %1; border: 1px solid %2;"
        "  border-radius: %3px; padding: %4px;"
        "}"
    ).arg(cS2, cLine).arg(T.radiusMd()).arg(T.spaceMd());

    // ---- Экранная клавиатура -------------------------------------
    s += QStringLiteral(
        "#keyboardPanel { background-color: %1; border-top: 1px solid %2; }"
        "#keyboardPanel QPushButton {"
        "  background-color: %3; color: %4;"
        "  border: 1px solid %2; border-radius: %5px;"
        "  font-size: %6px; min-height: 34px; min-width: 32px;"
        "}"
        "#keyboardPanel QPushButton:hover {"
        "  background-color: %7; border-color: %8; color: %9;"
        "}"
        "#keyboardPanel QPushButton:pressed { background-color: %9; color: %10; }"
        "#keyboardPanel #kbSpecialKey { color: %9; font-weight: 600; }"
        "#keyboardPanel #kbSpaceBar { min-width: 200px; }"
    ).arg(cBg, cLine, cS2, cText).arg(T.radiusSm()).arg(F.caption())
     .arg(cS3, cLineHi, cAccent, cOnAcc);

    return s;
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
    // Порядок важен: сначала общие правила из токенов, потом обвязка
    // приложения. QSS решает конфликты по специфичности, а при равной
    // специфичности выигрывает последнее правило — селекторы по
    // objectName (#sendBtn) и так специфичнее типовых (QPushButton),
    // но идущий следом блок делает это ещё и очевидным при чтении.
    if (index == SoftLight) {
        qApp->setStyleSheet(softLightStyleSheet());
        return;
    }
    qApp->setStyleSheet(JarvisTheme::instance().buildApplicationStyleSheet()
                        + appChromeStyleSheet());
}

} // namespace ThemeManager
