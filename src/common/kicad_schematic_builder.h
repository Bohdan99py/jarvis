#pragma once
// ============================================================
// kicad_schematic_builder.h — Assembles a valid .kicad_sch file
//
// Takes a structured list of component placements and wire connections
// (the only things an LLM needs to decide) and emits a complete, valid
// KiCad schematic text file. Symbol graphical definitions come from
// KiCadSymbolCache (real KiCad library data, not hand-generated), so the
// only new S-expression structure generated here is the schematic
// boilerplate, symbol instances, and wires — all modeled directly on a
// real file read from an actual KiCad 10 install (see the plan this was
// built from). Zero project dependencies (Qt only), consistent with the
// rest of src/common.
// ============================================================

#include <QString>
#include <QList>
#include <QMap>
#include <QPointF>
#include "kicad_symbol_cache.h"

struct KiCadPlacement {
    QString componentType; // nickname from KiCadSymbolCache::knownComponentTypes()
    QString reference;     // e.g. "R1" ("#PWR01" convention applies to power symbols regardless)
    QString value;         // e.g. "10k" — ignored for power symbols (value = symbol name)
    double  x = 0.0;
    double  y = 0.0;
    int     rotationDeg = 0;
};

// A wire endpoint identified by WHICH component pin it connects to, not a
// raw coordinate — the builder computes the exact connection point itself
// (instance position + the pin's real offset from KiCadSymbolCache,
// rotated to match the instance). Asking a caller (Claude) to guess exact
// pin coordinates by hand is exactly the kind of "almost right, technically
// invalid" mistake this whole approach exists to avoid.
struct KiCadWireEndpoint {
    QString componentRef; // matches a KiCadPlacement::reference
    QString pin;          // matches one of KiCadSymbolCache::pinNumbersFor(...)
};
struct KiCadWire { KiCadWireEndpoint from; KiCadWireEndpoint to; };

struct KiCadBuildResult {
    bool    success = false;
    QString content;       // full .kicad_sch text, valid iff success
    QString errorMessage;
};

class KiCadSchematicBuilder
{
public:
    // installRoot: see KiCadSymbolCache — needed to resolve real symbol
    // definitions for the lib_symbols block.
    explicit KiCadSchematicBuilder(const QString& installRoot);

    KiCadBuildResult build(const QList<KiCadPlacement>& placements,
                           const QList<KiCadWire>& wires);

private:
    QString buildLibSymbols(const QList<KiCadPlacement>& placements, QString* errorOut);
    QString buildSymbolInstance(const KiCadPlacement& p, const QString& libId,
                                const QString& schematicUuid) const;
    static QString buildWire(QPointF from, QPointF to);
    static QString propertyBlock(const QString& name, const QString& value,
                                  double x, double y, bool hidden);

    // Instance origin + the pin's local offset (from KiCadSymbolCache),
    // converted from the symbol library's Y-up frame to the schematic
    // sheet's Y-down frame and rotated to match the instance's placement
    // angle. Verified exact for rotation 0 (the common case) against a
    // real kicad-cli ERC run; other rotations use the standard formula but
    // are less thoroughly checked. *ok is false if componentRef doesn't
    // match any placement.
    static QPointF resolvePinPosition(const QMap<QString, KiCadPlacement>& byRef,
                                      const KiCadWireEndpoint& endpoint, bool* ok);

    KiCadSymbolCache m_symbolCache;
};
