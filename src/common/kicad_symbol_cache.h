#pragma once
// ============================================================
// kicad_symbol_cache.h — Real KiCad symbol definitions, not
// hand-generated ones.
//
// A schematic's lib_symbols block doesn't just reference a component by
// name — it embeds that component's full graphical definition (pins,
// rectangles, properties) inline. Letting an LLM hand-write this
// S-expression from scratch is a real correctness risk (KiCad is strict
// about file validity). Instead, this class extracts real symbol
// definitions verbatim from KiCad's own bundled .kicad_sym library files
// — the exact same files KiCad itself reads — and renames them from their
// bare in-library form ("R") to the library-qualified form a schematic
// expects ("Device:R"). Zero project dependencies (Qt only), consistent
// with the rest of src/common.
// ============================================================

#include <QString>
#include <QStringList>
#include <QMap>
#include <QPointF>

class KiCadSymbolCache
{
public:
    // installRoot: KiCad's install directory, e.g. "C:/Program Files/KiCad/10.0"
    // (see AppLauncher::kicadInstallRoot()). Symbol libraries are read from
    // installRoot/share/kicad/symbols/.
    explicit KiCadSymbolCache(const QString& installRoot);

    // Component nicknames the schematic builder understands.
    static QStringList knownComponentTypes();

    // "resistor" -> "Device:R". Empty if the nickname isn't recognized.
    static QString libIdFor(const QString& componentType);

    // Pin numbers the symbol declares, e.g. resistor -> {"1","2"},
    // gnd -> {"1"}. Empty if the nickname isn't recognized.
    static QStringList pinNumbersFor(const QString& componentType);

    // True for power symbols (gnd/vcc) — these use a "#PWR" reference
    // prefix and a fixed, non-editable Value (the symbol name itself),
    // unlike regular components.
    static bool isPowerSymbol(const QString& componentType);

    // A pin's connection point, in the symbol's own local coordinate frame
    // at rotation 0 (Y-up, as declared in the .kicad_sym library — e.g.
    // resistor pin "1" is (0, 3.81)). KiCadSchematicBuilder converts this
    // to sheet-space coordinates (Y-down, plus the instance's own rotation)
    // when routing wires, so callers never have to guess pin positions by
    // hand. (0,0) if the nickname/pin isn't recognized.
    static QPointF pinLocalOffset(const QString& componentType, const QString& pinNumber);

    // Returns the raw (symbol "Device:R" ...) S-expression block for a
    // library-qualified id (as returned by libIdFor), read from the
    // matching .kicad_sym file and renamed to the qualified form. Empty
    // (with *errorOut set, if provided) if the library file is missing or
    // doesn't contain that symbol.
    QString symbolSExpr(const QString& libId, QString* errorOut = nullptr);

private:
    struct ComponentInfo {
        QString library;      // e.g. "Device" -> Device.kicad_sym
        QString symbolName;   // e.g. "R"
        QStringList pinNumbers;
        QMap<QString, QPointF> pinOffsets; // pin number -> local (x,y), Y-up, rotation 0
        bool isPower = false;
    };
    static const QMap<QString, ComponentInfo>& componentTable();

    // Balanced-parenthesis extraction (string-literal-aware, since library
    // descriptions routinely contain literal parens) from `startNeedle`
    // (must point at an opening paren) to its matching close.
    static QString extractBalanced(const QString& content, const QString& startNeedle);

    QString libraryContent(const QString& libraryName, QString* errorOut);

    QString m_installRoot;
    QMap<QString, QString> m_libraryContentCache; // library name -> full file text
};
