// ============================================================
// kicad_symbol_cache.cpp
// ============================================================
#include "kicad_symbol_cache.h"

#include <QFile>
#include <QTextStream>
#include <QDir>

KiCadSymbolCache::KiCadSymbolCache(const QString& installRoot)
    : m_installRoot(installRoot)
{
}

const QMap<QString, KiCadSymbolCache::ComponentInfo>& KiCadSymbolCache::componentTable()
{
    // Pin offsets read directly from each part's own .kicad_sym pin
    // declarations (the "(pin ... (at x y angle) ... (number "N"))" blocks)
    // — not guessed. Device.kicad_sym/power.kicad_sym cover the basic
    // passives; Motor.kicad_sym, Connector_Generic.kicad_sym and
    // Driver_Motor.kicad_sym cover motor_dc/connector3/l298n respectively.
    // Vertical two-pin parts (R/C) use (0,+-3.81); horizontal ones (LED/D)
    // use (+-3.81,0); power symbols connect exactly at their placement
    // origin (0,0).
    static const QMap<QString, ComponentInfo> table = {
        {QStringLiteral("resistor"),
            {QStringLiteral("Device"), QStringLiteral("R"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(0, 3.81)}, {QStringLiteral("2"), QPointF(0, -3.81)}}, false}},
        {QStringLiteral("capacitor"),
            {QStringLiteral("Device"), QStringLiteral("C"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(0, 3.81)}, {QStringLiteral("2"), QPointF(0, -3.81)}}, false}},
        {QStringLiteral("led"),
            {QStringLiteral("Device"), QStringLiteral("LED"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(-3.81, 0)}, {QStringLiteral("2"), QPointF(3.81, 0)}}, false}},
        {QStringLiteral("diode"),
            {QStringLiteral("Device"), QStringLiteral("D"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(-3.81, 0)}, {QStringLiteral("2"), QPointF(3.81, 0)}}, false}},
        {QStringLiteral("gnd"),
            {QStringLiteral("power"), QStringLiteral("GND"), {QStringLiteral("1")},
             {{QStringLiteral("1"), QPointF(0, 0)}}, true}},
        {QStringLiteral("vcc"),
            {QStringLiteral("power"), QStringLiteral("VCC"), {QStringLiteral("1")},
             {{QStringLiteral("1"), QPointF(0, 0)}}, true}},
        {QStringLiteral("battery"),
            {QStringLiteral("Device"), QStringLiteral("Battery_Cell"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(0, 5.08)}, {QStringLiteral("2"), QPointF(0, -2.54)}}, false}},
        {QStringLiteral("motor_dc"),
            {QStringLiteral("Motor"), QStringLiteral("Motor_DC"), {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(0, 5.08)}, {QStringLiteral("2"), QPointF(0, -7.62)}}, false}},
        // 3-pin header (signal/+/GND) — the standard way an RC receiver
        // channel or hobby servo connects; there's no dedicated "RC
        // receiver" symbol in KiCad's stock libraries, but this is what a
        // real schematic for one looks like (a Conn_01x03 with the pins
        // labeled by the wiring, not the part itself).
        {QStringLiteral("connector3"),
            {QStringLiteral("Connector_Generic"), QStringLiteral("Conn_01x03"),
             {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")},
             {{QStringLiteral("1"), QPointF(-5.08, 2.54)}, {QStringLiteral("2"), QPointF(-5.08, 0)},
              {QStringLiteral("3"), QPointF(-5.08, -2.54)}}, false}},
        // "L298N" itself (the bare TO-220 part) is a KiCad `extends "L298HN"`
        // stub — its own S-expression block carries no pins/graphics at all
        // (those live in L298HN's), so extracting it verbatim the way this
        // cache extracts everything else would produce an unusable, pinless
        // symbol. L298HN is the real base part with the full 15-pin
        // Multiwatt package definition and an identical pinout/footprint
        // family, so it's used here under the "l298n" nickname instead.
        {QStringLiteral("l298n"),
            {QStringLiteral("Driver_Motor"), QStringLiteral("L298HN"),
             {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
              QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
              QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12"),
              QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("15")},
             {{QStringLiteral("1"),  QPointF(-7.62, -17.78)},  // SENSE_A
              {QStringLiteral("2"),  QPointF(15.24, 5.08)},    // OUT1
              {QStringLiteral("3"),  QPointF(15.24, 2.54)},    // OUT2
              {QStringLiteral("4"),  QPointF(2.54, 17.78)},    // Vs
              {QStringLiteral("5"),  QPointF(-15.24, 12.7)},   // IN1
              {QStringLiteral("6"),  QPointF(-15.24, 7.62)},   // EnA
              {QStringLiteral("7"),  QPointF(-15.24, 10.16)},  // IN2
              {QStringLiteral("8"),  QPointF(0, -17.78)},      // GND
              {QStringLiteral("9"),  QPointF(0, 17.78)},       // Vss
              {QStringLiteral("10"), QPointF(-15.24, 2.54)},   // IN3
              {QStringLiteral("11"), QPointF(-15.24, -2.54)},  // EnB
              {QStringLiteral("12"), QPointF(-15.24, 0)},      // IN4
              {QStringLiteral("13"), QPointF(15.24, -2.54)},   // OUT3
              {QStringLiteral("14"), QPointF(15.24, -5.08)},   // OUT4
              {QStringLiteral("15"), QPointF(-5.08, -17.78)}}, // SENSE_B
             false}},

        // ── Добавлено: транзистор, резонатор, кнопка, ESP32 ──────────
        // Координаты пинов не выведены "по смыслу", а вынуты из реальных
        // .kicad_sym установленной KiCad 10.0 — как и всё выше. Пин,
        // промахнувшийся мимо сетки хотя бы на 0.01 мм, KiCad считает
        // неподключённым, и схема тихо разваливается на ERC.

        // Q_NPN_BEC: имя описывает порядок выводов — 1=База, 2=Эмиттер,
        // 3=Коллектор. Существует парный Q_NPN_CBE с той же графикой и
        // другой нумерацией, так что тип назван явно.
        {QStringLiteral("transistor_npn"),
            {QStringLiteral("Transistor_BJT"), QStringLiteral("Q_NPN_BEC"),
             {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")},
             {{QStringLiteral("1"), QPointF(-5.08, 0)},      // B
              {QStringLiteral("2"), QPointF(2.54, -5.08)},   // E
              {QStringLiteral("3"), QPointF(2.54, 5.08)}},   // C
             false}},

        // Двухвыводный кварц. Трёхвыводный резонатор с землёй — отдельный
        // символ (Resonator), здесь не заводится, чтобы "crystal" не
        // означал две разные детали.
        {QStringLiteral("crystal"),
            {QStringLiteral("Device"), QStringLiteral("Crystal"),
             {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(-3.81, 0)},
              {QStringLiteral("2"), QPointF(3.81, 0)}},
             false}},

        {QStringLiteral("button"),
            {QStringLiteral("Switch"), QStringLiteral("SW_Push"),
             {QStringLiteral("1"), QStringLiteral("2")},
             {{QStringLiteral("1"), QPointF(-5.08, 0)},
              {QStringLiteral("2"), QPointF(5.08, 0)}},
             false}},

        // ESP32-WROOM-32 — 39 выводов. Пины 1/15/38/39 сидят в одной точке
        // (0,-35.56): это земля модуля, разведённая несколькими выводами
        // корпуса, и в символе они намеренно совмещены. Дублирование здесь
        // не ошибка копирования — так в самой библиотеке.
        {QStringLiteral("esp32"),
            {QStringLiteral("RF_Module"), QStringLiteral("ESP32-WROOM-32"),
             {QStringLiteral("1"),  QStringLiteral("2"),  QStringLiteral("3"),  QStringLiteral("4"),
              QStringLiteral("5"),  QStringLiteral("6"),  QStringLiteral("7"),  QStringLiteral("8"),
              QStringLiteral("9"),  QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12"),
              QStringLiteral("13"), QStringLiteral("14"), QStringLiteral("15"), QStringLiteral("16"),
              QStringLiteral("17"), QStringLiteral("18"), QStringLiteral("19"), QStringLiteral("20"),
              QStringLiteral("21"), QStringLiteral("22"), QStringLiteral("23"), QStringLiteral("24"),
              QStringLiteral("25"), QStringLiteral("26"), QStringLiteral("27"), QStringLiteral("28"),
              QStringLiteral("29"), QStringLiteral("30"), QStringLiteral("31"), QStringLiteral("32"),
              QStringLiteral("33"), QStringLiteral("34"), QStringLiteral("35"), QStringLiteral("36"),
              QStringLiteral("37"), QStringLiteral("38"), QStringLiteral("39")},
             {{QStringLiteral("1"),  QPointF(0, -35.56)},      // GND
              {QStringLiteral("2"),  QPointF(0, 35.56)},       // 3V3
              {QStringLiteral("3"),  QPointF(-15.24, 30.48)},  // EN
              {QStringLiteral("4"),  QPointF(-15.24, 25.4)},
              {QStringLiteral("5"),  QPointF(-15.24, 22.86)},
              {QStringLiteral("6"),  QPointF(15.24, -25.4)},
              {QStringLiteral("7"),  QPointF(15.24, -27.94)},
              {QStringLiteral("8"),  QPointF(15.24, -20.32)},
              {QStringLiteral("9"),  QPointF(15.24, -22.86)},
              {QStringLiteral("10"), QPointF(15.24, -12.7)},
              {QStringLiteral("11"), QPointF(15.24, -15.24)},
              {QStringLiteral("12"), QPointF(15.24, -17.78)},
              {QStringLiteral("13"), QPointF(15.24, 10.16)},
              {QStringLiteral("14"), QPointF(15.24, 15.24)},
              {QStringLiteral("15"), QPointF(0, -35.56)},      // GND
              {QStringLiteral("16"), QPointF(15.24, 12.7)},
              {QStringLiteral("17"), QPointF(-15.24, -5.08)},
              {QStringLiteral("18"), QPointF(-15.24, -7.62)},
              {QStringLiteral("19"), QPointF(-15.24, -12.7)},
              {QStringLiteral("20"), QPointF(-15.24, -10.16)},
              {QStringLiteral("21"), QPointF(-15.24, 0)},
              {QStringLiteral("22"), QPointF(-15.24, -2.54)},
              {QStringLiteral("23"), QPointF(15.24, 7.62)},
              {QStringLiteral("24"), QPointF(15.24, 25.4)},
              {QStringLiteral("25"), QPointF(15.24, 30.48)},
              {QStringLiteral("26"), QPointF(15.24, 20.32)},
              {QStringLiteral("27"), QPointF(15.24, 5.08)},
              {QStringLiteral("28"), QPointF(15.24, 2.54)},
              {QStringLiteral("29"), QPointF(15.24, 17.78)},
              {QStringLiteral("30"), QPointF(15.24, 0)},
              {QStringLiteral("31"), QPointF(15.24, -2.54)},
              {QStringLiteral("32"), QPointF(-12.7, -27.94)},
              {QStringLiteral("33"), QPointF(15.24, -5.08)},
              {QStringLiteral("34"), QPointF(15.24, 22.86)},
              {QStringLiteral("35"), QPointF(15.24, 27.94)},
              {QStringLiteral("36"), QPointF(15.24, -7.62)},
              {QStringLiteral("37"), QPointF(15.24, -10.16)},
              {QStringLiteral("38"), QPointF(0, -35.56)},      // GND
              {QStringLiteral("39"), QPointF(0, -35.56)}},     // GND
             false}},
    };
    return table;
}

QStringList KiCadSymbolCache::knownComponentTypes()
{
    return componentTable().keys();
}

QString KiCadSymbolCache::libIdFor(const QString& componentType)
{
    const auto it = componentTable().constFind(componentType.trimmed().toLower());
    if (it == componentTable().constEnd()) return {};
    return it->library + QLatin1Char(':') + it->symbolName;
}

QStringList KiCadSymbolCache::pinNumbersFor(const QString& componentType)
{
    const auto it = componentTable().constFind(componentType.trimmed().toLower());
    if (it == componentTable().constEnd()) return {};
    return it->pinNumbers;
}

bool KiCadSymbolCache::isPowerSymbol(const QString& componentType)
{
    const auto it = componentTable().constFind(componentType.trimmed().toLower());
    if (it == componentTable().constEnd()) return false;
    return it->isPower;
}

QPointF KiCadSymbolCache::pinLocalOffset(const QString& componentType, const QString& pinNumber)
{
    const auto it = componentTable().constFind(componentType.trimmed().toLower());
    if (it == componentTable().constEnd()) return QPointF(0, 0);
    return it->pinOffsets.value(pinNumber, QPointF(0, 0));
}

QString KiCadSymbolCache::libraryContent(const QString& libraryName, QString* errorOut)
{
    const auto cached = m_libraryContentCache.constFind(libraryName);
    if (cached != m_libraryContentCache.constEnd()) return cached.value();

    if (m_installRoot.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("KiCad install root not found");
        return {};
    }

    const QString path = QDir(m_installRoot).filePath(
        QStringLiteral("share/kicad/symbols/%1.kicad_sym").arg(libraryName));

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = QStringLiteral("Cannot open symbol library: %1").arg(path);
        return {};
    }

    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    const QString content = ts.readAll();
    m_libraryContentCache.insert(libraryName, content);
    return content;
}

QString KiCadSymbolCache::extractBalanced(const QString& content, const QString& startNeedle)
{
    const int start = content.indexOf(startNeedle);
    if (start < 0) return {};

    int depth = 0;
    bool inString = false;
    int i = start;
    for (; i < content.length(); ++i) {
        const QChar c = content.at(i);
        if (inString) {
            if (c == QLatin1Char('\\')) { ++i; continue; } // skip escaped char
            if (c == QLatin1Char('"')) inString = false;
            continue;
        }
        if (c == QLatin1Char('"')) { inString = true; continue; }
        if (c == QLatin1Char('(')) {
            ++depth;
        } else if (c == QLatin1Char(')')) {
            --depth;
            if (depth == 0) { ++i; break; }
        }
    }
    if (depth != 0) return {}; // unbalanced — malformed library file, bail out
    return content.mid(start, i - start);
}

QString KiCadSymbolCache::symbolSExpr(const QString& libId, QString* errorOut)
{
    const int sep = libId.indexOf(QLatin1Char(':'));
    if (sep < 0) {
        if (errorOut) *errorOut = QStringLiteral("Invalid lib_id (expected Library:Symbol): %1").arg(libId);
        return {};
    }
    const QString library = libId.left(sep);
    const QString symbolName = libId.mid(sep + 1);

    const QString content = libraryContent(library, errorOut);
    if (content.isEmpty()) return {};

    // Top-level symbol definitions are tab-indented once, e.g. "\t(symbol \"R\"".
    // Matching the exact quoted name (closing quote right after it) avoids
    // false positives against longer names sharing a prefix (e.g. "R_0_1"
    // sub-symbols nested inside — those are indented deeper and have a
    // different name entirely, but being precise here costs nothing).
    const QString needle = QStringLiteral("(symbol \"%1\"").arg(symbolName);
    QString block = extractBalanced(content, needle);
    if (block.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Symbol \"%1\" not found in %2.kicad_sym")
                                       .arg(symbolName, library);
        return {};
    }

    // Rename from the bare in-library name to the library-qualified form a
    // schematic's lib_symbols block expects — only the first occurrence
    // (the needle itself, at the very start of block) needs replacing.
    const QString qualifiedNeedle = QStringLiteral("(symbol \"%1\"").arg(libId);
    block.replace(0, needle.length(), qualifiedNeedle);
    return block;
}
