#pragma once
// ============================================================
// system_manifest.h — Autonomous Feature Awareness System
//
// Lightweight header-only module that:
//   1. Maintains a capability matrix of active JARVIS subsystems
//   2. Detects version upgrades via the settings KV store
//   3. Injects active capabilities into the LLM system prompt
//   4. Generates a first-run welcome notification on version change
// ============================================================

#include <QString>
#include <QStringList>
#include <QVector>
#include "database_manager.h"

namespace SystemManifest {

struct Capability {
    QString id;
    QString label;
    QString description;
};

inline QVector<Capability> activeCapabilities()
{
    return {
        { QStringLiteral("core"),
          QStringLiteral("Core Intelligence"),
          QStringLiteral("Multi-LLM routing (Claude/Ollama/Gemini), "
                         "intent analysis, offline behavior patterns") },
        { QStringLiteral("voice_pipeline"),
          QStringLiteral("Voice Pipeline"),
          QStringLiteral("Vosk-based wake word detection, multi-language ASR, "
                         "passive voice journal, voice PC control") },
        { QStringLiteral("training_pipeline"),
          QStringLiteral("Background Training Pipeline"),
          QStringLiteral("Asynchronous processing of voice journal entries "
                         "into training pairs, auto-export to .jsonl, "
                         "audio cleanup after processing") },
        { QStringLiteral("adaptive_focus"),
          QStringLiteral("Adaptive Focus Engine"),
          QStringLiteral("Time-decay memory analysis detecting Developer, Creative, "
                         "Hardware, Admin, or Casual workflow states with 100+ keywords "
                         "including electronics/EDA (KiCad, PCB, Gerber) and "
                         "3D modeling (Blender, FreeCAD, mesh, topology)") },
        { QStringLiteral("activity_awareness"),
          QStringLiteral("Activity Awareness"),
          QStringLiteral("Real-time window/app tracking, knowledge extraction, "
                         "user role detection, scenario-based profile learning") },
        { QStringLiteral("project_indexer"),
          QStringLiteral("Project Indexer & RAG"),
          QStringLiteral("Symbol-level C++/Python indexing, grep search, "
                         "auto-context injection for coding requests") },
        { QStringLiteral("screen_vision"),
          QStringLiteral("Screen Vision"),
          QStringLiteral("Screenshot capture, OCR (Tesseract), clipboard monitoring, "
                         "visual context for AI queries") },
        { QStringLiteral("pc_control"),
          QStringLiteral("PC Voice Control"),
          QStringLiteral("Mouse/keyboard emulation, app launching, window management, "
                         "system commands, macro recording — all by voice") },
        { QStringLiteral("task_manager"),
          QStringLiteral("Task Manager"),
          QStringLiteral("Cyberpunk Kanban board with categories (UE5, KiCad, Blender, General), "
                         "priority levels, deadlines, and automatic overdue notifications. "
                         "Tasks manageable via voice/text commands") },
        { QStringLiteral("translation"),
          QStringLiteral("Translation Engine"),
          QStringLiteral("Bidirectional FR/EN/RU translation via LLM, "
                         "audio file transcription (Vosk), translation, and structured "
                         "Markdown summarization with knowledge_base storage") },
        { QStringLiteral("j2j_mesh"),
          QStringLiteral("J2J Mesh Network"),
          QStringLiteral("Peer-to-peer mesh protocol for multi-instance JARVIS communication. "
                         "UDP beacon discovery, TCP command channel, knowledge sync, "
                         "and task delegation across LAN peers") },
        { QStringLiteral("mobile_sync"),
          QStringLiteral("Mobile Sync & Wake-on-LAN"),
          QStringLiteral("Zero-config mobile pairing via dynamic 6-char PIN codes. "
                         "No manual bot creation or token setup — pair like WhatsApp Web. "
                         "Gateway polling auto-binds smartphones to role profiles. "
                         "One-click Wake-on-LAN shortcut export for iOS/Android") },
        { QStringLiteral("telegram_gateway"),
          QStringLiteral("Telegram QA Gateway"),
          QStringLiteral("Multi-tenant Telegram bot bridge with per-chat localization. "
                         "QA_Tester profiles get full English UI — interactive bug wizard, "
                         "severity selection, and professional Markdown export for Jira/GitHub. "
                         "Main user profiles default to Russian. Inline keyboards for "
                         "system telemetry, Kanban board, and Wake-on-LAN") },

        // === New cognitive architecture ===
        { QStringLiteral("curiosity_engine"),
          QStringLiteral("Proactive Curiosity Engine"),
          QStringLiteral("You initiate context-aware questions during user idle time. "
                         "Categories: Philosophy, WellBeing, ProjectCheckIn, TechCuriosity, "
                         "TimeAwareness, VisualContextual (from screenshot metadata), "
                         "DoubtVerification (asking the user to verify your uncertain learnings), "
                         "PersonalProfiling (calibrating empty profile fields). "
                         "Attention cost model: NEVER interrupt gaming or deep coding (5+ min). "
                         "You track idle time, message count, cooldown, and session question limit.") },

        { QStringLiteral("self_journal"),
          QStringLiteral("Self-Reflection Journal"),
          QStringLiteral("You maintain an internal doubt/learning journal. When you learn "
                         "something from a PDF or code with confidence < 0.75, you log it as "
                         "'Unverified_Doubt' with a specific doubt reason. You can reference "
                         "your doubts naturally: 'I read X but I'm not sure because Y — can "
                         "you clarify?' When the user confirms or corrects, the doubt is resolved. "
                         "The journal writes Markdown entries to D:/Jarvis/self_journal.md.") },

        { QStringLiteral("pdf_distiller"),
          QStringLiteral("PDF Knowledge Distiller"),
          QStringLiteral("You have a background scanner that reads PDFs from "
                         "D:/Jarvis/knowledge_base/, extracts text, chunks it into semantic "
                         "units, scores confidence, and stores distilled knowledge locally. "
                         "Low-confidence chunks become self-doubts you can ask the user about. "
                         "You can reference learned PDF knowledge when answering questions.") },

        { QStringLiteral("memory_consolidation"),
          QStringLiteral("Two-Tier Memory System"),
          QStringLiteral("Tier 1: External 4TB drive (D:/Jarvis/) for raw assets — screenshots, "
                         "build logs, source snapshots. May be disconnected at any time. "
                         "Tier 2: Local SSD SQLite cache with distilled semantic summaries — "
                         "always available. Background consolidation every 15 min extracts "
                         "insights from Tier 1 into Tier 2. If the drive is disconnected, "
                         "you operate fully from local cache with zero data loss.") },

        { QStringLiteral("ethics_learning"),
          QStringLiteral("Binary Ethics & Experience Learning"),
          QStringLiteral("You have an ethics evaluation layer. Actions are scored from -1.0 "
                         "(data theft, policy breach) to +1.0 (task assistance, learning). "
                         "Neutral actions (system commands, web search, entertainment) learn "
                         "from user feedback via 'Did this help?' prompts every 12 interactions. "
                         "Ethical weights persist in jarvis_ethics_weights.json.") },

        { QStringLiteral("user_profile_ext"),
          QStringLiteral("Multi-User Identity System"),
          QStringLiteral("Each user has a crypto-hash userId with fields: nickname, "
                         "active_hours_start/end, dev_style, ui_accent_color, mesh_role. "
                         "Fields carry confidence scores. Low-confidence or empty fields "
                         "trigger calibration questions via the Curiosity Engine.") },

        { QStringLiteral("self_update_reflector"),
          QStringLiteral("Auto-Documentation Engine"),
          QStringLiteral("Your changelog and user manual are auto-generated. The changelog "
                         "is built from git commit history on each new version. The manual "
                         "is compiled from active module introspection at runtime — zero "
                         "hardcoded documentation strings.") },

        { QStringLiteral("kicad_schematic"),
          QStringLiteral("KiCad Schematic Generation"),
          QStringLiteral("You can generate real KiCad schematics directly — not by clicking "
                         "around the KiCad UI, by emitting a structured block that gets turned "
                         "into a genuinely valid .kicad_sch file (real symbol definitions "
                         "extracted from KiCad's own bundled libraries, pin-accurate wiring — "
                         "verified end-to-end against a real KiCad install's ERC checker). "
                         "Emit: [KICAD_SCH:name.kicad_sch]\\n{json}\\n[/KICAD_SCH] where json is "
                         "{\"components\":[{\"type\":\"resistor|capacitor|led|diode|gnd|vcc|"
                         "battery|motor_dc|l298n|connector3\","
                         "\"ref\":\"R1\",\"value\":\"10k\",\"x\":100,\"y\":50,\"rotation\":0}],"
                         "\"wires\":[{\"from\":{\"ref\":\"R1\",\"pin\":\"1\"},"
                         "\"to\":{\"ref\":\"R2\",\"pin\":\"1\"}}]}. Never hand-write the "
                         ".kicad_sch S-expression content yourself — always use this block; "
                         "wires connect by component reference + pin number, never guess raw "
                         "coordinates for a pin. Coordinates are in mm; component types are "
                         "currently limited to that fixed set (more can be added later). Notes: "
                         "'l298n' is the real L298HN 15-pin symbol (pins: 1=SENSE_A, 2=OUT1, "
                         "3=OUT2, 4=Vs, 5=IN1, 6=EnA, 7=IN2, 8=GND, 9=Vss, 10=IN3, 11=EnB, "
                         "12=IN4, 13=OUT3, 14=OUT4, 15=SENSE_B) — KiCad has no standalone "
                         "'L298N' graphic (it's a bare variant of L298HN with no pins of its "
                         "own), so this is used under the 'l298n' type name and is pin-identical. "
                         "'connector3' is a generic 3-pin header (pins 1/2/3, no fixed meaning) — "
                         "use it for anything that's physically a 3-pin connector: an RC "
                         "receiver channel (signal/+/GND), a hobby servo lead, a JST-3 battery "
                         "connector, etc. — say what each pin is via wiring/labels, not the part "
                         "itself. 'motor_dc' and 'battery' are plain 2-pin symbols.") },
    };
}

inline QString buildCapabilitiesContext()
{
    const auto caps = activeCapabilities();
    QString ctx = QStringLiteral("=== ACTIVE CAPABILITIES ===\n"
        "You have the following modules running. Reference them naturally "
        "when relevant — suggest voice commands, training data review, "
        "focus-state awareness, or project indexing when they fit the "
        "user's workflow. Never list them unprompted.\n");
    for (const auto& c : caps)
        ctx += QStringLiteral("- %1: %2\n").arg(c.label, c.description);
    ctx += QStringLiteral("\n");
    return ctx;
}

struct VersionCheckResult {
    bool   isUpgrade    = false;
    QString previousVer;
    QString currentVer;
};

inline VersionCheckResult checkAndUpdateVersion(const QString& currentVersion)
{
    static const QString kKey = QStringLiteral("jarvis_last_version");
    auto& db = DatabaseManager::instance();

    VersionCheckResult r;
    r.currentVer  = currentVersion;
    r.previousVer = db.getConfig(kKey, QStringLiteral("")).toString();
    r.isUpgrade   = (!r.previousVer.isEmpty() && r.previousVer != currentVersion);

    if (r.previousVer != currentVersion)
        db.setConfig(kKey, currentVersion);

    return r;
}

inline QString buildUpgradeNotification(const VersionCheckResult& vr, bool english)
{
    if (!vr.isUpgrade) return QString();

    if (english) {
        return QStringLiteral(
            "I've been updated from **%1** to **%2**. Here's what's new:\n\n"
            "**Background Training Pipeline** — I now automatically process "
            "your voice journal entries in the background, match them with my "
            "responses, and compile training pairs into a .jsonl dataset. "
            "Raw audio is cleaned up after processing.\n\n"
            "**Expanded Adaptive Focus** — My focus detection now recognizes "
            "**Hardware/Electronics** workflows (KiCad, PCB design, schematics, "
            "Gerber export) and **3D Modeling** workflows (Blender, FreeCAD, "
            "mesh topology, UV mapping). When I detect these, I switch to a "
            "specialized assistant mode automatically.\n\n"
            "Everything is running. No configuration needed."
        ).arg(vr.previousVer, vr.currentVer);
    }

    return QStringLiteral(
        "Обновился с **%1** до **%2**. Что нового:\n\n"
        "**Фоновый конвейер обучения** — теперь автоматически обрабатываю "
        "голосовой журнал в фоне: сопоставляю записи с моими ответами, "
        "формирую пары для обучения в .jsonl датасет. "
        "Сырые аудиофайлы удаляются после обработки.\n\n"
        "**Расширенный Adaptive Focus** — распознаю "
        "**Hardware/Electronics** (KiCad, PCB, схемы, Gerber) и "
        "**3D-моделирование** (Blender, FreeCAD, меш, топология, UV). "
        "Когда вижу такие задачи — автоматически переключаюсь в "
        "режим специализированного ассистента.\n\n"
        "Всё работает. Настройка не нужна."
    ).arg(vr.previousVer, vr.currentVer);
}

} // namespace SystemManifest
