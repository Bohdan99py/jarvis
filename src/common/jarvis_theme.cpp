// -------------------------------------------------------
// jarvis_theme.cpp — Дизайн-токены: QML-регистрация и генерация QSS
// -------------------------------------------------------

#include "jarvis_theme.h"

#include <QQmlEngine>
#include <QSettings>

// ============================================================
// Синглтоны
// ============================================================

JarvisTheme& JarvisTheme::instance()
{
    static JarvisTheme s;
    return s;
}

JarvisType& JarvisType::instance()
{
    static JarvisType s;
    return s;
}

void JarvisTheme::setReducedMotion(bool on)
{
    if (m_reducedMotion == on) return;
    m_reducedMotion = on;
    QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
        .setValue(QStringLiteral("ui/reducedMotion"), on);
    emit reducedMotionChanged();
}

void JarvisTheme::registerQmlTypes()
{
    // Синглтон-инстанс, а не тип: значения общие для всего приложения,
    // и C++-сторона (QSS, HTML сообщений) читает ровно тот же объект.
    // Владение остаётся за статическим instance() — QML не удалит.
    QQmlEngine::setObjectOwnership(&JarvisTheme::instance(),
                                   QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&JarvisType::instance(),
                                   QQmlEngine::CppOwnership);

    qmlRegisterSingletonInstance("Jarvis.Theme", 1, 0, "Theme",
                                 &JarvisTheme::instance());
    qmlRegisterSingletonInstance("Jarvis.Theme", 1, 0, "Type",
                                 &JarvisType::instance());

    // Восстанавливаем пользовательский выбор «уменьшить анимацию».
    JarvisTheme::instance().m_reducedMotion =
        QSettings(QStringLiteral("Bohdan99py"), QStringLiteral("JARVIS"))
            .value(QStringLiteral("ui/reducedMotion"), false).toBool();
}

// ============================================================
// QSS
// ============================================================

QString JarvisTheme::css(const QColor& c)
{
    if (c.alpha() == 255)
        return QStringLiteral("#%1").arg(c.rgb() & 0xFFFFFF, 6, 16, QLatin1Char('0'));
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(QString::number(c.alphaF(), 'f', 3));
}

QString JarvisTheme::buildApplicationStyleSheet() const
{
    // Хелперы, чтобы ниже читалась структура, а не арифметика цвета.
    const QString cBg       = css(bg());
    const QString cS1       = css(surface1());
    const QString cS2       = css(surface2());
    const QString cS3       = css(surface3());
    const QString cLine     = css(outline());
    const QString cLineHi   = css(outlineStrong());
    const QString cText     = css(onSurface());
    const QString cTextVar  = css(onSurfaceVariant());
    const QString cTextDim  = css(onSurfaceDim());
    const QString cAccent   = css(accent());
    const QString cAccentMu = css(accentMuted());
    const QString cAccentSb = css(accentSubtle());
    const QString cOnAccent = css(onAccent());
    const QString cError    = css(error());

    const QString font    = JarvisType::instance().family();
    const int     fBody   = JarvisType::instance().body();
    const int     fCap    = JarvisType::instance().caption();
    const int     rSm     = radiusSm();
    const int     rMd     = radiusMd();
    const int     rLg     = radiusLg();
    const int     hit     = hitTarget();

    QString qss;

    // ---- База ---------------------------------------------------
    // font-size задаём в px: Qt Widgets не умеет rem, а масштаб ОС
    // уже применён к логическим пикселям через DPI-awareness.
    qss += QStringLiteral(
        "* { font-family: %1; }"
        "QWidget { background: transparent; color: %2; font-size: %3px; }"
        "QMainWindow, QDialog { background-color: %4; color: %2; }"
        "QLabel { background: transparent; color: %2; }"
    ).arg(font, cText).arg(fBody).arg(cBg);

    // ---- Кнопки -------------------------------------------------
    // Обычная кнопка — вторичная по умолчанию: поверхность+контур.
    // Акцентная заливка достаётся только objectName="primary", чтобы
    // на экране был ровно один главный призыв к действию.
    qss += QStringLiteral(
        "QPushButton {"
        "  background-color: %1; color: %2;"
        "  border: 1px solid %3; border-radius: %4px;"
        "  padding: 7px 16px; min-height: %5px;"
        "}"
        "QPushButton:hover   { background-color: %6; border-color: %7; }"
        "QPushButton:pressed { background-color: %8; }"
        "QPushButton:focus   { border: 1px solid %9; outline: none; }"
        "QPushButton:disabled { color: %10; border-color: %3; background-color: %1; }"
        "QPushButton#primary {"
        "  background-color: %9; color: %11; border: 1px solid %9; font-weight: 600;"
        "}"
        "QPushButton#primary:hover   { background-color: %12; border-color: %12; }"
        "QPushButton#primary:pressed { background-color: %12; }"
        "QPushButton#danger  { color: %13; border-color: %13; }"
        "QPushButton#danger:hover { background-color: rgba(255,92,108,0.12); }"
        "QPushButton#ghost   { background: transparent; border-color: transparent; color: %14; }"
        "QPushButton#ghost:hover { background-color: %6; color: %2; }"
    ).arg(cS2, cText, cLine).arg(rSm).arg(hit)
     .arg(cS3, cLineHi, cS1, cAccent, cTextDim, cOnAccent, cAccentMu, cError, cTextVar);

    // ---- Поля ввода ---------------------------------------------
    // Фокус подсвечиваем рамкой акцента — видимый индикатор фокуса
    // обязателен для клавиатурной навигации (WCAG 2.4.7).
    qss += QStringLiteral(
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox {"
        "  background-color: %1; color: %2;"
        "  border: 1px solid %3; border-radius: %4px;"
        "  padding: 6px 10px; selection-background-color: %5; selection-color: %6;"
        "}"
        "QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover { border-color: %7; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,"
        "QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %8; }"
        "QLineEdit:disabled, QTextEdit:disabled { color: %9; }"
    ).arg(cS2, cText, cLine).arg(rSm).arg(cAccentSb, cText, cLineHi, cAccent, cTextDim);

    // ---- Меню ---------------------------------------------------
    qss += QStringLiteral(
        "QMenuBar { background-color: %1; color: %2; padding: 2px 6px; font-size: %3px; }"
        "QMenuBar::item { background: transparent; padding: 6px 12px; border-radius: %4px; }"
        "QMenuBar::item:selected { background-color: %5; color: %6; }"
        "QMenu {"
        "  background-color: %7; color: %8;"
        "  border: 1px solid %9; border-radius: %10px; padding: 6px;"
        "}"
        "QMenu::item { padding: 8px 28px 8px 14px; border-radius: %4px; margin: 1px 2px; }"
        "QMenu::item:selected { background-color: %5; color: %6; }"
        "QMenu::item:disabled { color: %11; }"
        "QMenu::separator { height: 1px; background: %9; margin: 6px 10px; }"
        "QMenu::indicator { width: 14px; height: 14px; margin-left: 8px; }"
        "QMenu::indicator:checked { background: %6; border-radius: 3px; }"
        "QMenu::indicator:unchecked { background: %12; border-radius: 3px; }"
    ).arg(cBg, cTextVar).arg(fCap).arg(rSm)
     .arg(cAccentSb, cAccent, cS3, cText, cLine).arg(rMd).arg(cTextDim, cS2);

    // ---- Списки и деревья ---------------------------------------
    qss += QStringLiteral(
        "QListWidget, QTreeWidget, QListView, QTreeView, QTableWidget, QTableView {"
        "  background-color: %1; color: %2;"
        "  border: 1px solid %3; border-radius: %4px;"
        "  outline: none; padding: 4px;"
        "}"
        "QListWidget::item, QTreeWidget::item { padding: 7px 8px; border-radius: %5px; }"
        "QListWidget::item:hover, QTreeWidget::item:hover { background-color: %6; }"
        "QListWidget::item:selected, QTreeWidget::item:selected {"
        "  background-color: %7; color: %8;"
        "}"
        "QHeaderView::section {"
        "  background-color: %6; color: %9; border: none;"
        "  border-bottom: 1px solid %3; padding: 7px 8px; font-size: %10px;"
        "}"
    ).arg(cS1, cText, cLine).arg(rMd).arg(rSm)
     .arg(cS2, cAccentSb, cAccent, cTextVar).arg(fCap);

    // ---- Выпадающие списки --------------------------------------
    qss += QStringLiteral(
        "QComboBox {"
        "  background-color: %1; color: %2; border: 1px solid %3;"
        "  border-radius: %4px; padding: 6px 10px; min-height: %5px;"
        "}"
        "QComboBox:hover { border-color: %6; }"
        "QComboBox:focus { border: 1px solid %7; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: %8; color: %2; border: 1px solid %3;"
        "  border-radius: %9px; padding: 4px;"
        "  selection-background-color: %10; selection-color: %7;"
        "}"
    ).arg(cS2, cText, cLine).arg(rSm).arg(hit)
     .arg(cLineHi, cAccent, cS3).arg(rMd).arg(cAccentSb);

    // ---- Флажки и переключатели ---------------------------------
    // Галочка/точка красится акцентом, но состояние дублируется
    // формой индикатора — цвет не единственный носитель смысла.
    qss += QStringLiteral(
        "QCheckBox, QRadioButton { color: %1; spacing: 8px; background: transparent; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }"
        "QCheckBox::indicator { border: 1px solid %2; border-radius: 4px; background: %3; }"
        "QCheckBox::indicator:checked { background: %4; border-color: %4; }"
        "QCheckBox::indicator:hover { border-color: %4; }"
        "QRadioButton::indicator { border: 1px solid %2; border-radius: 8px; background: %3; }"
        "QRadioButton::indicator:checked { background: %4; border-color: %4; }"
    ).arg(cText, cLineHi, cS2, cAccent);

    // ---- Вкладки ------------------------------------------------
    qss += QStringLiteral(
        "QTabWidget::pane { border: 1px solid %1; border-radius: %2px; top: -1px; }"
        "QTabBar::tab {"
        "  background: transparent; color: %3;"
        "  padding: 8px 16px; margin-right: 2px;"
        "  border-top-left-radius: %4px; border-top-right-radius: %4px;"
        "}"
        "QTabBar::tab:hover { color: %5; background-color: %6; }"
        "QTabBar::tab:selected { color: %7; background-color: %8; }"
    ).arg(cLine).arg(rMd).arg(cTextVar).arg(rSm).arg(cText, cS2, cAccent, cAccentSb);

    // ---- Прогресс -----------------------------------------------
    qss += QStringLiteral(
        "QProgressBar {"
        "  background-color: %1; border: none; border-radius: 4px;"
        "  height: 6px; text-align: center; color: %2; font-size: %3px;"
        "}"
        "QProgressBar::chunk { background-color: %4; border-radius: 4px; }"
    ).arg(cS2, cTextVar).arg(fCap).arg(cAccent);

    // ---- Полосы прокрутки ---------------------------------------
    // Тонкие, без стрелок, проявляются при наведении — не отнимают
    // внимание у содержимого.
    qss += QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical {"
        "  background: %1; border-radius: 4px; min-height: 32px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: %2; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal {"
        "  background: %1; border-radius: 4px; min-width: 32px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: %2; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
    ).arg(cLineHi, cAccentMu);

    // ---- Группы, разделители, подсказки -------------------------
    qss += QStringLiteral(
        "QGroupBox {"
        "  background-color: %1; border: 1px solid %2;"
        "  border-radius: %3px; margin-top: 14px; padding: 14px 12px 12px 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %4; font-size: %5px;"
        "}"
        "QFrame[frameShape=\"4\"], QFrame[frameShape=\"5\"] { background: %2; border: none; }"
        "QToolTip {"
        "  background-color: %6; color: %7;"
        "  border: 1px solid %2; border-radius: %8px; padding: 7px 10px;"
        "}"
        "QSplitter::handle { background: %2; }"
    ).arg(cS1, cLine).arg(rMd).arg(cTextVar).arg(fCap).arg(cS3, cText).arg(rSm);

    return qss;
}
