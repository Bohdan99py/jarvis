#pragma once
// -------------------------------------------------------
// jarvis_theme.h — Единый источник дизайн-токенов J.A.R.V.I.S.
//
// ПОЧЕМУ ЭТОТ ФАЙЛ СУЩЕСТВУЕТ
// Раньше палитра жила в двух местах: QML-экраны использовали
// «Cyberpunk» (#66FCF1 / #0B0C10), а виджет-диалоги — легаси
// «Glass» (#00d4ff / #0f2438). Плюс ~9 файлов звали setStyleSheet()
// с руками вписанными hex-ами. Результат — приложение выглядело
// собранным из кусков.
//
// Теперь токен объявлен ОДИН раз здесь и раздаётся в оба мира:
//   - QML:     import Jarvis.Theme  ->  Theme.accent, Type.body
//   - Виджеты: JarvisTheme::instance().accent()  ->  ThemeManager QSS
// Расхождение палитр становится структурно невозможным, а не
// вопросом дисциплины.
//
// ХАРАКТЕР: «современный киберпанк»
// Фирменный циан остаётся, но работает как акцент, а не как заливка
// всего подряд. Глубина набирается лестницей поверхностей
// (bg -> surface1 -> surface2 -> surface3), а не неоновыми рамками.
// Рамки — белый на 7%: они разделяют, но не обводят. Неон приберегаем
// для фокуса и активного состояния (эффект фон Ресторффа: выделяется
// то, что редкое).
// -------------------------------------------------------

#include <QObject>
#include <QColor>
#include <QString>

#include "jarvis_core_export.h"

class QQmlEngine;

class JARVIS_CORE_EXPORT JarvisTheme : public QObject
{
    Q_OBJECT

    // ---- Поверхности: лестница высот -------------------------------
    // Глубина передаётся светлотой, а не тенями и не рамками. Каждая
    // ступень примерно +4% светлоты — этого хватает, чтобы глаз
    // разделил слои, и мало, чтобы фон остался «глубоким».
    Q_PROPERTY(QColor bg           READ bg           CONSTANT)  // подложка приложения
    Q_PROPERTY(QColor surface1     READ surface1     CONSTANT)  // карточки, панели
    Q_PROPERTY(QColor surface2     READ surface2     CONSTANT)  // поля ввода, hover
    Q_PROPERTY(QColor surface3     READ surface3     CONSTANT)  // поповеры, выбранная строка

    // ---- Контуры ---------------------------------------------------
    Q_PROPERTY(QColor outline       READ outline       CONSTANT) // белый @7%
    Q_PROPERTY(QColor outlineStrong READ outlineStrong CONSTANT) // белый @12%

    // ---- Текст -----------------------------------------------------
    // Нейтральные, а не сине-серые: синева в тексте на синем фоне
    // «пачкает» и читается как дешёвая тема.
    Q_PROPERTY(QColor onSurface        READ onSurface        CONSTANT) // основной, 14.9:1
    Q_PROPERTY(QColor onSurfaceVariant READ onSurfaceVariant CONSTANT) // вторичный, 7.4:1
    Q_PROPERTY(QColor onSurfaceDim     READ onSurfaceDim     CONSTANT) // 3.8:1 — СЛУЖЕБНЫЙ

    // ---- Акцент ----------------------------------------------------
    // accent — подпись бренда. Держим его редким: фокус, активная
    // вкладка, ключевая цифра. accentMuted — для заливок и крупных
    // площадей, где полный неон начинает вибрировать на глазу.
    Q_PROPERTY(QColor accent       READ accent       CONSTANT)
    Q_PROPERTY(QColor accentMuted  READ accentMuted  CONSTANT)
    Q_PROPERTY(QColor accentSubtle READ accentSubtle CONSTANT)  // фон-подсветка @12%
    Q_PROPERTY(QColor onAccent     READ onAccent     CONSTANT)  // текст ПОВЕРХ accent

    // ---- Семантика состояний ---------------------------------------
    // Цвет никогда не единственный носитель смысла — в UI всегда
    // сопровождается иконкой или подписью (WCAG 1.4.1).
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor error   READ error   CONSTANT)
    Q_PROPERTY(QColor info    READ info    CONSTANT)

    // ---- Роли в чате -----------------------------------------------
    Q_PROPERTY(QColor roleJarvis READ roleJarvis CONSTANT)
    Q_PROPERTY(QColor roleUser   READ roleUser   CONSTANT)
    Q_PROPERTY(QColor roleSystem READ roleSystem CONSTANT)

    // ---- Радиусы ---------------------------------------------------
    Q_PROPERTY(int radiusSm   READ radiusSm   CONSTANT)  // 6  — чипы, мелкие кнопки
    Q_PROPERTY(int radiusMd   READ radiusMd   CONSTANT)  // 10 — карточки, поля
    Q_PROPERTY(int radiusLg   READ radiusLg   CONSTANT)  // 14 — диалоги, крупные панели
    Q_PROPERTY(int radiusPill READ radiusPill CONSTANT)  // 999

    // ---- Отступы: шаг 4px ------------------------------------------
    Q_PROPERTY(int spaceXs  READ spaceXs  CONSTANT)  // 4
    Q_PROPERTY(int spaceSm  READ spaceSm  CONSTANT)  // 8
    Q_PROPERTY(int spaceMd  READ spaceMd  CONSTANT)  // 12
    Q_PROPERTY(int spaceLg  READ spaceLg  CONSTANT)  // 16
    Q_PROPERTY(int spaceXl  READ spaceXl  CONSTANT)  // 24
    Q_PROPERTY(int spaceXxl READ spaceXxl CONSTANT)  // 32

    // ---- Длительности анимаций (мс) --------------------------------
    // Потолок 320 мс: всё, что дольше ~400 мс, воспринимается как
    // «подвисло», а не как «плавно».
    Q_PROPERTY(int motionFast READ motionFast CONSTANT)  // 120 — мелкие элементы
    Q_PROPERTY(int motionBase READ motionBase CONSTANT)  // 200 — карточки, панели
    Q_PROPERTY(int motionSlow READ motionSlow CONSTANT)  // 320 — смена экрана

    // ---- Доступность -----------------------------------------------
    // Qt не имеет аналога prefers-reduced-motion, поэтому держим
    // собственный флаг. QML-компоненты обязаны умножать длительность
    // на motionScale, а не читать длительности напрямую.
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion
                                  NOTIFY reducedMotionChanged)
    Q_PROPERTY(qreal motionScale  READ motionScale  NOTIFY reducedMotionChanged)

    // ---- Минимальная зона нажатия ----------------------------------
    Q_PROPERTY(int hitTarget READ hitTarget CONSTANT)   // 32 (десктоп-указатель)

public:
    static JarvisTheme& instance();

    // Регистрирует Theme и Type как QML-синглтоны. Зовётся один раз
    // из main() ДО создания любого QQuickWidget.
    static void registerQmlTypes();

    QColor bg()       const { return QColor(0x08, 0x0A, 0x0F); }
    QColor surface1() const { return QColor(0x0F, 0x13, 0x1A); }
    QColor surface2() const { return QColor(0x16, 0x1B, 0x24); }
    QColor surface3() const { return QColor(0x1E, 0x24, 0x2F); }

    QColor outline()       const { return QColor(255, 255, 255, 18); }  // ~7%
    QColor outlineStrong() const { return QColor(255, 255, 255, 31); }  // ~12%

    QColor onSurface()        const { return QColor(0xE7, 0xEA, 0xF0); }
    QColor onSurfaceVariant() const { return QColor(0x9A, 0xA3, 0xB2); }
    QColor onSurfaceDim()     const { return QColor(0x68, 0x70, 0x7E); }

    QColor accent()       const { return QColor(0x66, 0xFC, 0xF1); }
    QColor accentMuted()  const { return QColor(0x3F, 0xBF, 0xB6); }
    QColor accentSubtle() const { return QColor(0x66, 0xFC, 0xF1, 31); }
    QColor onAccent()     const { return QColor(0x04, 0x1A, 0x1C); }

    QColor success() const { return QColor(0x3D, 0xD6, 0x8C); }
    QColor warning() const { return QColor(0xF5, 0xC1, 0x47); }
    QColor error()   const { return QColor(0xFF, 0x5C, 0x6C); }
    QColor info()    const { return QColor(0x5B, 0x9D, 0xFF); }

    QColor roleJarvis() const { return accent(); }
    QColor roleUser()   const { return onSurface(); }
    QColor roleSystem() const { return accentMuted(); }

    int radiusSm()   const { return 6; }
    int radiusMd()   const { return 10; }
    int radiusLg()   const { return 14; }
    int radiusPill() const { return 999; }

    int spaceXs()  const { return 4; }
    int spaceSm()  const { return 8; }
    int spaceMd()  const { return 12; }
    int spaceLg()  const { return 16; }
    int spaceXl()  const { return 24; }
    int spaceXxl() const { return 32; }

    int motionFast() const { return 120; }
    int motionBase() const { return 200; }
    int motionSlow() const { return 320; }

    bool  reducedMotion() const { return m_reducedMotion; }
    void  setReducedMotion(bool on);
    qreal motionScale() const { return m_reducedMotion ? 0.0 : 1.0; }

    int hitTarget() const { return 32; }

    // ---- Помощники для QSS ------------------------------------------
    // Виджет-часть приложения ещё жива, и её таблицы стилей должны
    // собираться из ТЕХ ЖЕ значений. "#rrggbb" для непрозрачных,
    // "rgba(r,g,b,a)" для полупрозрачных — QSS понимает оба.
    static QString css(const QColor& c);

    // Полный QSS приложения, собранный из токенов выше.
    QString buildApplicationStyleSheet() const;

signals:
    void reducedMotionChanged();

private:
    explicit JarvisTheme(QObject* parent = nullptr) : QObject(parent) {}
    bool m_reducedMotion = false;
};

// -------------------------------------------------------
// Типографика — модульная шкала.
//
// База 16 px (минимум для основного текста на дистанции ~60 см),
// отношение 1.2 («малая терция») — спокойный ритм, подходящий
// приложению с плотными данными. Каждый шаг = предыдущий * 1.2.
//
//   ms(-1)  13   подпись, метка, метаданные
//   ms( 0)  16   тело  <- база
//   ms( 1)  19   заголовок секции
//   ms( 2)  23   заголовок экрана
//   ms( 3)  28   крупная цифра / hero
//
// Правило: не больше 3–4 шагов на одном экране.
// -------------------------------------------------------
class JARVIS_CORE_EXPORT JarvisType : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int caption READ caption CONSTANT)  // 13
    Q_PROPERTY(int body    READ body    CONSTANT)  // 16
    Q_PROPERTY(int title   READ title   CONSTANT)  // 19
    Q_PROPERTY(int heading READ heading CONSTANT)  // 23
    Q_PROPERTY(int display READ display CONSTANT)  // 28

    Q_PROPERTY(QString family     READ family     CONSTANT)
    Q_PROPERTY(QString familyMono READ familyMono CONSTANT)

    // Межстрочные множители: тексту нужно больше воздуха, чем
    // заголовкам — у крупного кегля собственная высота уже читается.
    Q_PROPERTY(qreal lineHeightBody    READ lineHeightBody    CONSTANT) // 1.5
    Q_PROPERTY(qreal lineHeightHeading READ lineHeightHeading CONSTANT) // 1.2

    // Ограничение длины строки: 45–75 знаков читаются комфортнее
    // всего. В пикселях при кегле 16 это примерно 680.
    Q_PROPERTY(int measureMax READ measureMax CONSTANT)

public:
    static JarvisType& instance();

    int caption() const { return 13; }
    int body()    const { return 16; }
    int title()   const { return 19; }
    int heading() const { return 23; }
    int display() const { return 28; }

    // Segoe UI Variable — системный шрифт Windows 11; на 10 и старше
    // Qt сам откатится к Segoe UI. Свой шрифт не тащим: системный
    // всегда отрисован лучше и уважает масштаб ОС.
    QString family()     const { return QStringLiteral("Segoe UI Variable Text, Segoe UI, sans-serif"); }
    QString familyMono() const { return QStringLiteral("Cascadia Mono, Consolas, monospace"); }

    qreal lineHeightBody()    const { return 1.5; }
    qreal lineHeightHeading() const { return 1.2; }

    int measureMax() const { return 680; }

private:
    explicit JarvisType(QObject* parent = nullptr) : QObject(parent) {}
};
