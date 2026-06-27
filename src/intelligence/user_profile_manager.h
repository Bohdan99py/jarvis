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

enum FocusState { Developer, Creative, Admin, Hardware, QA_Tester, Student_Academic, Casual };

inline QString focusLabel(FocusState f)
{
    switch (f) {
    case Developer: return QStringLiteral("Developer");
    case Creative:  return QStringLiteral("Creative");
    case Admin:     return QStringLiteral("Admin");
    case Hardware:         return QStringLiteral("Hardware");
    case QA_Tester:        return QStringLiteral("QA_Tester");
    case Student_Academic: return QStringLiteral("Student_Academic");
    case Casual:           return QStringLiteral("Casual");
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
    case QA_Tester:
        return QStringLiteral(
            "User is in a QA/testing workflow. "
            "Focus on test case design, bug reproduction steps, edge cases, "
            "regression analysis, and test coverage strategy. "
            "Structure responses as: Expected vs Actual, Steps to Reproduce. "
            "Highlight Kanban QA artifacts (bug reports, test plans). "
            "Be methodical, detail-oriented, and skeptical of 'it works on my machine'.");
    case Student_Academic:
        return QStringLiteral(
            "User is in a university/academic workflow. "
            "Focus on lecture comprehension, assignment structure, study materials, "
            "exam preparation, and academic writing. "
            "Track context around courses, deadlines, and study topics. "
            "Summarize concepts clearly, provide mnemonic aids, and reference "
            "academic conventions. Be patient and pedagogical.");
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

    int devScore = 0, creativeScore = 0, adminScore = 0, hwScore = 0,
        qaScore = 0, studentScore = 0;

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

    static const QStringList qaKeywords = {
        QStringLiteral("test"),      QStringLiteral("testing"),   QStringLiteral("qa"),
        QStringLiteral("bug"),       QStringLiteral("regression"),QStringLiteral("coverage"),
        QStringLiteral("assert"),    QStringLiteral("fixture"),   QStringLiteral("mock"),
        QStringLiteral("selenium"),  QStringLiteral("cypress"),   QStringLiteral("pytest"),
        QStringLiteral("gtest"),     QStringLiteral("junit"),     QStringLiteral("test case"),
        QStringLiteral("edge case"), QStringLiteral("reproduce"), QStringLiteral("expected"),
        QStringLiteral("actual"),    QStringLiteral("defect"),    QStringLiteral("ticket"),
        QStringLiteral("тест"),      QStringLiteral("баг"),       QStringLiteral("дефект"),
    };

    static const QStringList studentKeywords = {
        QStringLiteral("lecture"),     QStringLiteral("assignment"),QStringLiteral("exam"),
        QStringLiteral("homework"),    QStringLiteral("course"),    QStringLiteral("university"),
        QStringLiteral("professor"),   QStringLiteral("semester"),  QStringLiteral("study"),
        QStringLiteral("thesis"),      QStringLiteral("diploma"),   QStringLiteral("grade"),
        QStringLiteral("deadline"),    QStringLiteral("campus"),    QStringLiteral("textbook"),
        QStringLiteral("лекция"),      QStringLiteral("задани"),    QStringLiteral("экзамен"),
        QStringLiteral("курс"),        QStringLiteral("универ"),    QStringLiteral("диплом"),
        QStringLiteral("семестр"),      QStringLiteral("зачёт"),     QStringLiteral("сессия"),
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
        for (const auto& kw : qaKeywords)
            if (lo.contains(kw)) { qaScore += 2; break; }
        for (const auto& kw : studentKeywords)
            if (lo.contains(kw)) { studentScore += 2; break; }
    }

    // Role override: if user has an explicit role set, boost that category
    auto userOpt = DatabaseManager::instance().getUser(userId);
    if (userOpt) {
        const QString& role = userOpt->currentRole;
        if (role == QStringLiteral("QA_Tester"))        qaScore += 6;
        else if (role == QStringLiteral("Student_Academic")) studentScore += 6;
        else if (role == QStringLiteral("Developer"))    devScore += 4;
    }

    struct { int score; FocusState state; } ranking[] = {
        { hwScore,      Hardware },
        { qaScore,      QA_Tester },
        { studentScore, Student_Academic },
        { devScore,     Developer },
        { creativeScore,Creative },
        { adminScore,   Admin },
    };
    int bestScore = 4;
    FocusState best = Casual;
    for (const auto& r : ranking) {
        if (r.score >= bestScore) { bestScore = r.score; best = r.state; }
    }
    return best;
}

inline QString buildFocusContext(qint64 userId)
{
    FocusState focus = detectFocus(userId);
    return QStringLiteral("=== ADAPTIVE FOCUS: %1 ===\n%2\n\n")
        .arg(focusLabel(focus), focusInstruction(focus));
}

} // namespace UserProfileManager
