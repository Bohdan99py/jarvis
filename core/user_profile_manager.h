#pragma once
// -------------------------------------------------------
// user_profile_manager.h — Dynamic focus-state profiling
//
// Analyzes recent Memory Stream events to detect the user's
// current focus (developer, creative, admin, hardware,
// casual) and injects an adaptive system instruction into
// SessionMemory.
//
// Expanded keyword dictionaries cover:
//   - Software development (code, build tools, VCS)
//   - Hardware/electronics (KiCad, PCB, schematics)
//   - 3D modeling & creative design (Blender, FreeCAD)
//   - Admin/organizational tasks
// -------------------------------------------------------

#include <QString>
#include <QStringList>
#include "database_manager.h"

namespace UserProfileManager {

enum FocusState { Developer, Creative, Admin, Hardware, Casual };

inline QString focusLabel(FocusState f)
{
    switch (f) {
    case Developer: return QStringLiteral("Developer");
    case Creative:  return QStringLiteral("Creative");
    case Admin:     return QStringLiteral("Admin");
    case Hardware:  return QStringLiteral("Hardware");
    case Casual:    return QStringLiteral("Casual");
    }
    return QStringLiteral("Casual");
}

inline QString focusInstruction(FocusState f)
{
    switch (f) {
    case Developer:
        return QStringLiteral(
            "User is currently in a development/scripting workflow. "
            "Prioritize technical precision: code snippets, debugging strategies, "
            "architecture suggestions. Be terse and direct — skip pleasantries.");
    case Creative:
        return QStringLiteral(
            "User is in a creative/design workflow (3D modeling, visual design, art). "
            "Offer visual thinking, brainstorming, aesthetic feedback. "
            "For 3D tools (Blender, FreeCAD): provide precise topology/UV/render guidance. "
            "For design tools (Photoshop, Krita): focus on composition, color theory, workflow. "
            "Be collaborative and open-ended.");
    case Admin:
        return QStringLiteral(
            "User is doing administrative/organizational tasks. "
            "Focus on clarity, step-by-step instructions, file management, "
            "and system operations. Be concise and action-oriented.");
    case Hardware:
        return QStringLiteral(
            "User is in a hardware/electronics engineering workflow. "
            "Act as a precise electronics design assistant: component selection, "
            "schematic review, PCB layout strategies, DRC/ERC validation, "
            "Gerber export checklists, BOM management. "
            "Reference IPC standards where relevant. "
            "Be direct, action-oriented, and technically exact.");
    case Casual:
        return QStringLiteral(
            "User is in casual conversation mode. "
            "Be personable, witty (JARVIS-style), and relaxed. "
            "Keep answers short unless depth is requested.");
    }
    return QString();
}

inline FocusState detectFocus(qint64 userId)
{
    auto events = DatabaseManager::instance().getTopMemoryEvents(userId, 10);
    if (events.isEmpty()) return Casual;

    int devScore = 0, creativeScore = 0, adminScore = 0, hwScore = 0;

    static const QStringList devKeywords = {
        QStringLiteral("code"),    QStringLiteral("function"), QStringLiteral("class"),
        QStringLiteral("bug"),     QStringLiteral("fix"),      QStringLiteral("compile"),
        QStringLiteral("build"),   QStringLiteral("debug"),    QStringLiteral("implement"),
        QStringLiteral("refactor"),QStringLiteral("api"),      QStringLiteral("script"),
        QStringLiteral(".cpp"),    QStringLiteral(".h"),        QStringLiteral(".py"),
        QStringLiteral(".js"),     QStringLiteral(".ts"),       QStringLiteral("cmake"),
        QStringLiteral("git"),     QStringLiteral("commit"),    QStringLiteral("branch"),
        QStringLiteral("struct"),  QStringLiteral("method"),    QStringLiteral("variable"),
        QStringLiteral("error"),   QStringLiteral("файл"),      QStringLiteral("функци"),
        QStringLiteral("класс"),   QStringLiteral("исправ"),    QStringLiteral("добав"),
        QStringLiteral("напиши"),  QStringLiteral("создай"),    QStringLiteral("реализ"),
    };

    static const QStringList artKeywords = {
        QStringLiteral("design"),    QStringLiteral("art"),       QStringLiteral("color"),
        QStringLiteral("layout"),    QStringLiteral("texture"),   QStringLiteral("render"),
        QStringLiteral("blender"),   QStringLiteral("photoshop"), QStringLiteral("krita"),
        QStringLiteral("дизайн"),    QStringLiteral("рисо"),      QStringLiteral("модел"),
        // 3D modeling / design
        QStringLiteral("freecad"),   QStringLiteral("mesh"),      QStringLiteral("topology"),
        QStringLiteral("uv mapping"),QStringLiteral("3d model"),  QStringLiteral("sculpt"),
        QStringLiteral("vertex"),    QStringLiteral("polygon"),   QStringLiteral("shader"),
        QStringLiteral("material"),  QStringLiteral("animation"), QStringLiteral("rigging"),
        QStringLiteral("viewport"),  QStringLiteral("wireframe"), QStringLiteral("subdivision"),
        QStringLiteral("бленд"),     QStringLiteral("полигон"),   QStringLiteral("текстур"),
    };

    static const QStringList adminKeywords = {
        QStringLiteral("folder"),  QStringLiteral("file"),     QStringLiteral("install"),
        QStringLiteral("setting"), QStringLiteral("config"),   QStringLiteral("update"),
        QStringLiteral("backup"),  QStringLiteral("move"),     QStringLiteral("copy"),
        QStringLiteral("delete"),  QStringLiteral("rename"),   QStringLiteral("path"),
        QStringLiteral("папк"),    QStringLiteral("настрой"),  QStringLiteral("устано"),
    };

    static const QStringList hwKeywords = {
        QStringLiteral("kicad"),     QStringLiteral("pcb"),       QStringLiteral("schematic"),
        QStringLiteral("footprint"), QStringLiteral("gerber"),    QStringLiteral("circuit"),
        QStringLiteral("routing"),   QStringLiteral("capacitor"), QStringLiteral("resistor"),
        QStringLiteral("inductor"),  QStringLiteral("diode"),     QStringLiteral("transistor"),
        QStringLiteral("mosfet"),    QStringLiteral("ic "),       QStringLiteral("smd"),
        QStringLiteral("soldering"), QStringLiteral("oscilloscope"), QStringLiteral("multimeter"),
        QStringLiteral("erc"),       QStringLiteral("drc"),       QStringLiteral("bom"),
        QStringLiteral("netlist"),   QStringLiteral("via"),       QStringLiteral("trace"),
        QStringLiteral("copper"),    QStringLiteral("layer"),     QStringLiteral("ground plane"),
        QStringLiteral("voltage"),   QStringLiteral("current"),   QStringLiteral("impedance"),
        QStringLiteral("datasheet"), QStringLiteral("pinout"),
        QStringLiteral("плат"),      QStringLiteral("схем"),      QStringLiteral("паяль"),
        QStringLiteral("компонент"), QStringLiteral("разъём"),    QStringLiteral("микросхем"),
    };

    for (const DbMemoryEvent& ev : events) {
        const QString lo = ev.content.toLower();
        for (const auto& kw : devKeywords)
            if (lo.contains(kw)) { devScore += 2; break; }
        for (const auto& kw : artKeywords)
            if (lo.contains(kw)) { creativeScore += 2; break; }
        for (const auto& kw : adminKeywords)
            if (lo.contains(kw)) { adminScore += 2; break; }
        for (const auto& kw : hwKeywords)
            if (lo.contains(kw)) { hwScore += 2; break; }
    }

    // Hardware is the most specialized — prioritize if threshold met
    if (hwScore >= 4 && hwScore >= devScore && hwScore >= creativeScore && hwScore >= adminScore)
        return Hardware;
    if (devScore >= creativeScore && devScore >= adminScore && devScore >= hwScore && devScore >= 4)
        return Developer;
    if (creativeScore >= devScore && creativeScore >= adminScore && creativeScore >= hwScore && creativeScore >= 4)
        return Creative;
    if (adminScore >= devScore && adminScore >= creativeScore && adminScore >= hwScore && adminScore >= 4)
        return Admin;
    return Casual;
}

inline QString buildFocusContext(qint64 userId)
{
    FocusState focus = detectFocus(userId);
    return QStringLiteral("=== ADAPTIVE FOCUS: %1 ===\n%2\n\n")
        .arg(focusLabel(focus), focusInstruction(focus));
}

} // namespace UserProfileManager
