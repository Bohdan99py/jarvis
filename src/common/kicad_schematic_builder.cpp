// ============================================================
// kicad_schematic_builder.cpp
// ============================================================
#include "kicad_schematic_builder.h"

#include <QUuid>
#include <QSet>
#include <QtMath>
#include <cmath>
#include <QHash>
#include <algorithm>

KiCadSchematicBuilder::KiCadSchematicBuilder(const QString& installRoot)
    : m_symbolCache(installRoot)
{
}

QString KiCadSchematicBuilder::propertyBlock(const QString& name, const QString& value,
                                              double x, double y, bool hidden)
{
    QString s = QStringLiteral(
        "\t\t(property \"%1\" \"%2\"\n"
        "\t\t\t(at %3 %4 0)\n"
        "\t\t\t(effects\n"
        "\t\t\t\t(font\n"
        "\t\t\t\t\t(size 1.27 1.27)\n"
        "\t\t\t\t)\n")
        .arg(name, value)
        .arg(x, 0, 'f', 2)
        .arg(y, 0, 'f', 2);
    if (hidden)
        s += QStringLiteral("\t\t\t\t(hide yes)\n");
    s += QStringLiteral(
        "\t\t\t)\n"
        "\t\t)\n");
    return s;
}

QString KiCadSchematicBuilder::buildWire(QPointF from, QPointF to)
{
    return QStringLiteral(
        "\t(wire\n"
        "\t\t(pts\n"
        "\t\t\t(xy %1 %2) (xy %3 %4)\n"
        "\t\t)\n"
        "\t\t(stroke\n"
        "\t\t\t(width 0)\n"
        "\t\t\t(type default)\n"
        "\t\t)\n"
        "\t\t(uuid \"%5\")\n"
        "\t)\n")
        .arg(from.x(), 0, 'f', 2)
        .arg(from.y(), 0, 'f', 2)
        .arg(to.x(), 0, 'f', 2)
        .arg(to.y(), 0, 'f', 2)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QPointF KiCadSchematicBuilder::resolvePinPosition(const QMap<QString, KiCadPlacement>& byRef,
                                                   const KiCadWireEndpoint& endpoint, bool* ok)
{
    const auto it = byRef.constFind(endpoint.componentRef);
    if (it == byRef.constEnd()) {
        if (ok) *ok = false;
        return QPointF(0, 0);
    }
    const KiCadPlacement& p = it.value();
    const QPointF localLibFrame = KiCadSymbolCache::pinLocalOffset(p.componentType, endpoint.pin);

    // Library frame is Y-up; the schematic sheet is Y-down — flip first.
    const QPointF flipped(localLibFrame.x(), -localLibFrame.y());

    // Then apply the instance's rotation (standard CCW rotation matrix).
    // Exact at rotationDeg==0 regardless of convention details (cos0=1,
    // sin0=0 reduces the matrix to identity) — that's the case this was
    // verified against via a real kicad-cli ERC run.
    const double rad = qDegreesToRadians(static_cast<double>(p.rotationDeg));
    const double rx = flipped.x() * std::cos(rad) - flipped.y() * std::sin(rad);
    const double ry = flipped.x() * std::sin(rad) + flipped.y() * std::cos(rad);

    if (ok) *ok = true;
    return QPointF(p.x + rx, p.y + ry);
}

QString KiCadSchematicBuilder::buildSymbolInstance(const KiCadPlacement& p, const QString& libId,
                                                     const QString& schematicUuid) const
{
    const QString instUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool isPower = KiCadSymbolCache::isPowerSymbol(p.componentType);
    // Power symbols (GND/VCC) carry a fixed, non-editable value — the
    // symbol name itself — rather than a user-supplied one.
    const QString value = isPower ? libId.mid(libId.indexOf(QLatin1Char(':')) + 1) : p.value;

    QString s;
    s += QStringLiteral("\t(symbol\n");
    s += QStringLiteral("\t\t(lib_id \"%1\")\n").arg(libId);
    s += QStringLiteral("\t\t(at %1 %2 %3)\n")
             .arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2).arg(p.rotationDeg);
    s += QStringLiteral(
        "\t\t(unit 1)\n"
        "\t\t(exclude_from_sim no)\n"
        "\t\t(in_bom yes)\n"
        "\t\t(on_board yes)\n"
        "\t\t(dnp no)\n");
    s += QStringLiteral("\t\t(uuid \"%1\")\n").arg(instUuid);
    s += propertyBlock(QStringLiteral("Reference"), p.reference, p.x, p.y - 4.0, false);
    s += propertyBlock(QStringLiteral("Value"), value, p.x, p.y + 4.0, false);
    s += propertyBlock(QStringLiteral("Footprint"), QString(), p.x, p.y, true);
    s += propertyBlock(QStringLiteral("Datasheet"), QStringLiteral("~"), p.x, p.y, true);

    const QStringList pins = KiCadSymbolCache::pinNumbersFor(p.componentType);
    for (const QString& pin : pins) {
        s += QStringLiteral("\t\t(pin \"%1\"\n\t\t\t(uuid \"%2\")\n\t\t)\n")
                 .arg(pin, QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    s += QStringLiteral(
        "\t\t(instances\n"
        "\t\t\t(project \"\"\n");
    s += QStringLiteral("\t\t\t\t(path \"/%1\"\n").arg(schematicUuid);
    s += QStringLiteral("\t\t\t\t\t(reference \"%1\")\n").arg(p.reference);
    s += QStringLiteral(
        "\t\t\t\t\t(unit 1)\n"
        "\t\t\t\t)\n"
        "\t\t\t)\n"
        "\t\t)\n"
        "\t)\n");
    return s;
}

QString KiCadSchematicBuilder::buildLibSymbols(const QList<KiCadPlacement>& placements, QString* errorOut)
{
    QSet<QString> seen;
    QString body;
    for (const auto& p : placements) {
        const QString libId = KiCadSymbolCache::libIdFor(p.componentType);
        if (libId.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Unknown component type: %1").arg(p.componentType);
            return {};
        }
        if (seen.contains(libId)) continue;
        seen.insert(libId);

        QString err;
        const QString sexpr = m_symbolCache.symbolSExpr(libId, &err);
        if (sexpr.isEmpty()) {
            if (errorOut) *errorOut = err;
            return {};
        }
        body += QStringLiteral("\t\t") + sexpr + QLatin1Char('\n');
    }
    if (body.isEmpty()) return {};
    return QStringLiteral("\t(lib_symbols\n") + body + QStringLiteral("\t)\n");
}

KiCadBuildResult KiCadSchematicBuilder::build(const QList<KiCadPlacement>& placements,
                                               const QList<KiCadWire>& wires)
{
    KiCadBuildResult result;

    if (placements.isEmpty()) {
        result.errorMessage = QStringLiteral("No components to place");
        return result;
    }

    QString errorOut;
    const QString libSymbols = buildLibSymbols(placements, &errorOut);
    if (!errorOut.isEmpty()) {
        result.errorMessage = errorOut;
        return result;
    }

    const QString schematicUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Resolve final references (auto-numbering power symbols that didn't
    // supply one) BEFORE anything looks placements up by reference — wires
    // need this to target an auto-numbered power symbol reliably.
    // Snap placements to KiCad's standard 1.27mm (0.05") connection grid.
    // Pin offsets are all exact multiples of 1.27 (3.81 = 3x1.27), so a
    // grid-aligned instance position keeps every pin on-grid too —
    // eliminates "endpoint off grid" ERC warnings regardless of what
    // coordinates the caller (ultimately Claude) picks.
    static constexpr double kGrid = 1.27;
    const auto snap = [](double v) { return std::round(v / kGrid) * kGrid; };

    QList<KiCadPlacement> resolved;
    QMap<QString, KiCadPlacement> byRef;
    int powerCounter = 0;
    for (const auto& p : placements) {
        KiCadPlacement pp = p;
        pp.x = snap(pp.x);
        pp.y = snap(pp.y);
        if (KiCadSymbolCache::isPowerSymbol(p.componentType) && pp.reference.isEmpty()) {
            ++powerCounter;
            pp.reference = QStringLiteral("#PWR%1").arg(powerCounter, 2, 10, QLatin1Char('0'));
        }
        if (pp.reference.isEmpty()) {
            result.errorMessage = QStringLiteral("Component missing a reference designator (type: %1)")
                                       .arg(pp.componentType);
            return result;
        }
        resolved.append(pp);
        byRef.insert(pp.reference, pp);
    }

    QString content;
    content += QStringLiteral(
        "(kicad_sch\n"
        "\t(version 20250114)\n"
        "\t(generator \"jarvis\")\n"
        "\t(generator_version \"1.0\")\n");
    content += QStringLiteral("\t(uuid \"%1\")\n").arg(schematicUuid);
    content += QStringLiteral("\t(paper \"A4\")\n");
    content += libSymbols;

    for (const auto& w : wires) {
        bool fromOk = false, toOk = false;
        const QPointF from = resolvePinPosition(byRef, w.from, &fromOk);
        const QPointF to   = resolvePinPosition(byRef, w.to, &toOk);
        if (!fromOk || !toOk) {
            result.errorMessage = QStringLiteral("Wire references an unknown component: %1")
                                       .arg(!fromOk ? w.from.componentRef : w.to.componentRef);
            return result;
        }
        content += buildWire(from, to);
    }

    for (const auto& pp : resolved)
        content += buildSymbolInstance(pp, KiCadSymbolCache::libIdFor(pp.componentType), schematicUuid);

    content += QStringLiteral(
        "\t(sheet_instances\n"
        "\t\t(path \"/\"\n"
        "\t\t\t(page \"1\")\n"
        "\t\t)\n"
        "\t)\n"
        "\t(embedded_fonts no)\n"
        ")\n");

    result.success = true;
    result.content = content;
    return result;
}

// ============================================================
//  Автораскладка — схема как граф
// ============================================================

void KiCadSchematicBuilder::autoPlace(QList<KiCadPlacement>& placements,
                                      const QList<KiCadWire>& wires)
{
    if (placements.isEmpty()) return;

    // Лист A4 в KiCad — 297x210 мм; держимся внутри рамки с полями,
    // чтобы символы и их подписи не вылезали за край.
    constexpr double kLeft = 40.0, kRight = 250.0;
    constexpr double kTop  = 35.0, kBottom = 175.0;
    constexpr double kGrid = 1.27;   // шаг сетки KiCad
    constexpr int    kIterations = 400;

    auto snap = [](double v) { return std::round(v / kGrid) * kGrid; };

    QHash<QString, int> indexByRef;
    for (int i = 0; i < placements.size(); ++i)
        indexByRef.insert(placements[i].reference, i);

    // Питание раскладывается не силами, а по конвенции — см. ниже.
    QVector<bool> isPower(placements.size(), false);
    for (int i = 0; i < placements.size(); ++i)
        isPower[i] = KiCadSymbolCache::isPowerSymbol(placements[i].componentType);

    // Индексы деталей, которые действительно расставляем силовой моделью.
    QVector<int> movable;
    for (int i = 0; i < placements.size(); ++i)
        if (!isPower[i] && !placements[i].positionGiven) movable.append(i);

    if (!movable.isEmpty()) {
        const int n = movable.size();
        QVector<QPointF> pos(n);
        // Стартуем с окружности, а не со случайных точек: одинаковый
        // нетлист обязан давать одинаковый лист.
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * M_PI * i / n;
            pos[i] = QPointF(0.5 + 0.30 * std::cos(a), 0.5 + 0.30 * std::sin(a));
        }

        QHash<int, int> slotOf;   // индекс в placements -> индекс в pos
        for (int k = 0; k < n; ++k) slotOf.insert(movable[k], k);

        // Рёбра между подвижными деталями. Провода к питанию здесь не
        // участвуют — иначе каждый GND стягивал бы к себе пол-схемы.
        QVector<QPair<int, int>> edges;
        for (const KiCadWire& w : wires) {
            const auto a = indexByRef.constFind(w.from.componentRef);
            const auto b = indexByRef.constFind(w.to.componentRef);
            if (a == indexByRef.constEnd() || b == indexByRef.constEnd()) continue;
            if (!slotOf.contains(*a) || !slotOf.contains(*b)) continue;
            if (*a == *b) continue;
            edges.append({ slotOf.value(*a), slotOf.value(*b) });
        }

        const double k = std::sqrt(1.0 / n);
        QVector<QPointF> disp(n);

        for (int iter = 0; iter < kIterations; ++iter) {
            const double temp = 0.08 * (1.0 - double(iter) / kIterations) + 0.0005;
            disp.fill(QPointF(0.0, 0.0));

            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    double dx = pos[i].x() - pos[j].x();
                    double dy = pos[i].y() - pos[j].y();
                    double len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-6) { dx = 1e-3 * (i + 1); dy = 1e-3 * (j + 1); len = std::sqrt(dx*dx + dy*dy); }
                    const double f = (k * k) / len;
                    disp[i] += QPointF(dx / len * f, dy / len * f);
                    disp[j] -= QPointF(dx / len * f, dy / len * f);
                }
            }

            for (const auto& e : edges) {
                double dx = pos[e.first].x() - pos[e.second].x();
                double dy = pos[e.first].y() - pos[e.second].y();
                const double len = std::max(std::sqrt(dx * dx + dy * dy), 1e-6);
                const double f = (len * len) / k;
                disp[e.first]  -= QPointF(dx / len * f, dy / len * f);
                disp[e.second] += QPointF(dx / len * f, dy / len * f);
            }

            for (int i = 0; i < n; ++i) {
                const double len = std::sqrt(disp[i].x() * disp[i].x() + disp[i].y() * disp[i].y());
                if (len > 1e-9) {
                    const double step = std::min(len, temp);
                    pos[i] += QPointF(disp[i].x() / len * step, disp[i].y() / len * step);
                }
                pos[i].setX(std::clamp(pos[i].x(), 0.0, 1.0));
                pos[i].setY(std::clamp(pos[i].y(), 0.0, 1.0));
            }
        }

        for (int slot = 0; slot < n; ++slot) {
            KiCadPlacement& p = placements[movable[slot]];
            p.x = snap(kLeft + pos[slot].x() * (kRight - kLeft));
            p.y = snap(kTop  + pos[slot].y() * (kBottom - kTop));
            p.positionGiven = true;
        }
    }

    // Питание — по конвенции чтения схемы: GND под своей деталью, VCC над.
    // Ставится после основной раскладки, потому что зависит от её итога.
    constexpr double kPowerOffset = 7.62;   // 6 шагов сетки — символ не наезжает на деталь
    for (int i = 0; i < placements.size(); ++i) {
        if (!isPower[i] || placements[i].positionGiven) continue;

        int anchor = -1;
        for (const KiCadWire& w : wires) {
            if (w.from.componentRef == placements[i].reference)
                anchor = indexByRef.value(w.to.componentRef, -1);
            else if (w.to.componentRef == placements[i].reference)
                anchor = indexByRef.value(w.from.componentRef, -1);
            if (anchor >= 0 && anchor != i) break;
            anchor = -1;
        }

        const bool isGnd = placements[i].componentType.trimmed().toLower()
                               == QStringLiteral("gnd");
        if (anchor >= 0) {
            placements[i].x = placements[anchor].x;
            placements[i].y = snap(placements[anchor].y
                                   + (isGnd ? kPowerOffset : -kPowerOffset));
        } else {
            // Ни к чему не подключено — ставим у края, чтобы было видно,
            // что символ висит в воздухе, а не прятать его в середине.
            placements[i].x = snap(kLeft);
            placements[i].y = snap(isGnd ? kBottom : kTop);
        }
        placements[i].positionGiven = true;
    }
}
