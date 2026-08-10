#pragma once
// ============================================================
// system_manifest.h — Self-knowledge: what is actually running
//
// This is what Jarvis knows about his own insides, and it is the ONE
// registry for it — both the LLM system prompt (buildCapabilitiesContext)
// and the user manual (SelfUpdateReflector::activeModules) read from here.
//
// It used to be a hand-written QVector of description strings, and
// SelfUpdateReflector kept a second, separate hand-written list of the same
// subsystems. Nothing tied either to reality: a module could be missing,
// disabled, or deleted and both lists would keep confidently describing it —
// one entry even advertised "compiled from active module introspection at
// runtime" while being a literal hardcoded string. A brain that only knows
// what someone typed into it doesn't know anything.
//
// So every entry now carries `active`, established one of two ways, never
// assumed:
//   • PROBED — checked here and now (compile-time feature flags, presence of
//     Vosk/Tesseract/Poppler on disk, row counts in SQLite).
//   • REPORTED — the code that actually constructs the subsystem calls
//     setRuntimeState() when it wires it up, so "the Telegram gateway is
//     live" comes from the gateway existing, not from this file's opinion.
//
// `state` carries the live detail (case counts, concept counts, model
// names) so self-description is numbers from the database rather than
// adjectives.
// ============================================================

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QPair>
#include <QFileInfo>
#include <QCoreApplication>

#include "database_manager.h"
#include "llm_cache_manager.h"
#include "synapse_graph.h"
#include "ocr_extractor.h"

namespace SystemManifest {

struct Capability {
    QString id;
    QString label;
    QString description;   // what it does — and, where relevant, the prompt contract
    QString commands;      // how a person reaches it (feeds the generated manual)
    QString icon;
    bool    active = true; // genuinely available right now — probed or reported
    QString state;         // live detail: counts, model names, "drive offline"
};

// ── Runtime state reported by subsystem owners ────────────────────────
// A subsystem this header cannot see from the outside (a gateway holding a
// socket, a serial hub with a board on the other end) is reported by the
// code that owns it. Absent = never reported = treated as not running,
// which is the honest default: silence means "no evidence it's up".
inline QHash<QString, QPair<bool, QString>>& runtimeStates()
{
    static QHash<QString, QPair<bool, QString>> states;
    return states;
}

inline void setRuntimeState(const QString& id, bool active, const QString& state = QString())
{
    runtimeStates().insert(id, { active, state });
}

namespace detail {

inline bool exists(const QString& path)
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

inline QString appDir() { return QCoreApplication::applicationDirPath(); }

// Vosk ships as a DLL beside the binary (see the CMake copy step); without
// it the whole speech path is dead weight no matter what this list claims.
inline bool haveVosk()
{
    return exists(appDir() + QStringLiteral("/libvosk.dll"));
}

// Reuses OcrExtractor's own detection rather than re-deriving redist paths,
// so there is exactly one answer to "is OCR usable" in the codebase.
inline bool haveOcr()
{
    OcrExtractor ocr;
    return ocr.isTesseractAvailable();
}

inline bool havePdfText()
{
    OcrExtractor ocr;
    return ocr.isPopplerAvailable();
}

// Reported-state lookup with an explicit default for "nobody reported yet".
inline bool reported(const QString& id, bool fallback = false)
{
    const auto it = runtimeStates().constFind(id);
    return it == runtimeStates().constEnd() ? fallback : it->first;
}

inline QString reportedState(const QString& id)
{
    const auto it = runtimeStates().constFind(id);
    return it == runtimeStates().constEnd() ? QString() : it->second;
}

inline QString plural(int n, const QString& one, const QString& many)
{
    return QString::number(n) + QStringLiteral(" ") + (n == 1 ? one : many);
}

} // namespace detail

inline QVector<Capability> activeCapabilities()
{
    // Live figures, read once per call — this is what makes self-description
    // checkable instead of decorative.
    auto& db = DatabaseManager::instance();
    const qint64 owner = LlmCacheManager::kDesktopOwnerId;
    const int cases     = LlmCacheManager::instance().cacheEntryCount();
    const auto synapses = SynapseGraph::instance().stats(owner);
    const int trainPairs = db.trainingLogCount(owner);
    const int journal    = db.voiceJournalCount(owner, false);
    const int indexed    = db.indexedFileCount();
    const int patterns   = db.patternCount(owner);

    const bool vosk    = detail::haveVosk();
    const bool ocr     = detail::haveOcr();
    const bool pdfText = detail::havePdfText();

    QVector<Capability> caps = {
        { QStringLiteral("core"),
          QStringLiteral("Core Intelligence"),
          QStringLiteral("Multi-LLM routing (Claude/Ollama), "
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
                         "renders the capability registry, whose active/inactive state is "
                         "probed at runtime (feature flags, tools on disk, SQLite counts) or "
                         "reported by each subsystem as it starts — so it shows what is "
                         "actually loaded, not a fixed list.") },

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
                         "\"ref\":\"R1\",\"value\":\"10k\",\"rotation\":0}],"
                         "\"wires\":[{\"from\":{\"ref\":\"R1\",\"pin\":\"1\"},"
                         "\"to\":{\"ref\":\"R2\",\"pin\":\"1\"}}]}. Never hand-write the "
                         ".kicad_sch S-expression content yourself — always use this block; "
                         "wires connect by component reference + pin number, never guess raw "
                         "coordinates for a pin. DO NOT emit \"x\"/\"y\" for components: the "
                         "layout is computed from the wiring by the same force-directed "
                         "algorithm that draws your synapse graph, then snapped to KiCad's "
                         "1.27mm grid, with GND placed under its part and VCC above it. Just "
                         "say what connects to what and the sheet arranges itself. (You may "
                         "still pass x/y in mm to pin a specific part; anything without them "
                         "is placed automatically.) Component types are "
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

        // These three used to exist only in SelfUpdateReflector's own
        // parallel list, which is why they never reached the LLM prompt at
        // all. One registry now, so the manual and the prompt cannot
        // describe different machines.
        { QStringLiteral("brain"),
          QStringLiteral("Intent Classifier (Brain)"),
          QStringLiteral("Analyzes every input to determine intent (Search, Open, Ask, "
                         "Modify) and domain (Project, Filesystem, Web, Chat). Includes a "
                         "conversational filter for small-talk, philosophy, and greetings.") },
        { QStringLiteral("action_predictor"),
          QStringLiteral("Action Predictor + Ethics"),
          QStringLiteral("Predicts next actions from usage patterns. Includes binary ethics "
                         "scoring (-1.0 to +1.0) with hardcoded safety rules and weights "
                         "learned from user feedback.") },
        { QStringLiteral("user_profile"),
          QStringLiteral("Behavioral Profile"),
          QStringLiteral("Learns work patterns: which commands you use in which scenario "
                         "(GameDev, Art, Gaming, Browsing) at which time of day. Decays old "
                         "patterns at 3%/day.") },
    };

    // ── Usage text ───────────────────────────────────────────────────
    // How a person actually reaches each subsystem. Lives here rather than in
    // the manual generator so the manual is a *rendering* of the registry,
    // not a second copy of it.
    auto usage = [&caps](const QString& id, const QString& icon, const QString& commands) {
        for (auto& c : caps) {
            if (c.id == id) { c.icon = icon; c.commands = commands; return; }
        }
    };

    usage(QStringLiteral("brain"), QStringLiteral("🧠"),
          QStringLiteral("Any natural language input — Jarvis routes automatically."));
    usage(QStringLiteral("action_predictor"), QStringLiteral("⚡"),
          QStringLiteral("Automatic — suggestions appear after commands. "
                         "Reply 'yes'/'no' to feedback prompts to train weights."));
    usage(QStringLiteral("user_profile"), QStringLiteral("📊"),
          QStringLiteral("Automatic. Review under Training → App Usage."));
    usage(QStringLiteral("user_profile_ext"), QStringLiteral("🪪"),
          QStringLiteral("User Center → My Profile."));
    usage(QStringLiteral("curiosity_engine"), QStringLiteral("💡"),
          QStringLiteral("Automatic when you go idle. Answer via the reply bar above the "
                         "input, the notification's Yes/No, or just type — the question "
                         "stays answerable until you reply or dismiss it."));
    usage(QStringLiteral("memory_consolidation"), QStringLiteral("📀"),
          QStringLiteral("Automatic every 15 min. Works from local cache if the external "
                         "drive is unplugged."));
    usage(QStringLiteral("pdf_distiller"), QStringLiteral("📚"),
          QStringLiteral("Drop PDFs into the knowledge_base folder — scanned in background."));
    usage(QStringLiteral("self_journal"), QStringLiteral("📓"),
          QStringLiteral("Automatic. Confirm or correct Jarvis when he flags a doubt."));
    usage(QStringLiteral("voice_pipeline"), QStringLiteral("🎙"),
          QStringLiteral("Say the wake word, or use the mic button. Models: Vosk Setup."));
    usage(QStringLiteral("screen_vision"), QStringLiteral("👁"),
          QStringLiteral("Training → 📸 Screenshot + AI Description, or ask about the screen."));
    usage(QStringLiteral("j2j_mesh"), QStringLiteral("🕸"),
          QStringLiteral("Automatic LAN discovery. Peers appear in the log as they link."));
    usage(QStringLiteral("telegram_gateway"), QStringLiteral("📱"),
          QStringLiteral("Phone & Server → Telegram QA Gateway. "
                         "API keys and model selection: Models & Intelligence."));
    usage(QStringLiteral("mobile_sync"), QStringLiteral("📲"),
          QStringLiteral("Phone & Server → Mobile Sync — pair with a 6-character PIN."));
    usage(QStringLiteral("core"), QStringLiteral("🧩"),
          QStringLiteral("Always on. Associative memory is visible in "
                         "Training → Synapse Graph."));
    usage(QStringLiteral("training_pipeline"), QStringLiteral("🎓"),
          QStringLiteral("Like (👍) good answers, then Training → Local Training."));
    usage(QStringLiteral("project_indexer"), QStringLiteral("🗂"),
          QStringLiteral("Index a project folder, then ask about symbols or files."));
    usage(QStringLiteral("activity_awareness"), QStringLiteral("📡"),
          QStringLiteral("Automatic window/app tracking. Review under Training → App Usage."));
    usage(QStringLiteral("kicad_schematic"), QStringLiteral("🔌"),
          QStringLiteral("Ask for a schematic in words — Jarvis emits a real .kicad_sch."));
    usage(QStringLiteral("task_manager"), QStringLiteral("🗒"),
          QStringLiteral("Task board, or say \"add task ...\" / \"what are my tasks\"."));
    usage(QStringLiteral("translation"), QStringLiteral("🌐"),
          QStringLiteral("Ask to translate text, or drop an audio file in."));
    usage(QStringLiteral("pc_control"), QStringLiteral("🖱"),
          QStringLiteral("Speak or type commands: open/close apps, volume, lock, macros."));
    usage(QStringLiteral("adaptive_focus"), QStringLiteral("🎯"),
          QStringLiteral("Automatic — Jarvis switches assistant mode to match your work."));
    usage(QStringLiteral("ethics_learning"), QStringLiteral("⚖"),
          QStringLiteral("Automatic. Answer the occasional \"did this help?\" prompt."));
    usage(QStringLiteral("self_update_reflector"), QStringLiteral("📝"),
          QStringLiteral("Help → System Manual, generated from this registry."));

    // ── Availability pass ────────────────────────────────────────────
    // Everything not touched below is in-process code constructed
    // unconditionally at startup, so it is genuinely always on. The entries
    // here depend on something outside the binary — a runtime DLL, an
    // external tool, a socket, a drive — and each gets its answer from that
    // thing rather than from this file's opinion.
    auto mark = [&caps](const QString& id, bool active, const QString& state) {
        for (auto& c : caps) {
            if (c.id == id) { c.active = active; c.state = state; return; }
        }
    };

    mark(QStringLiteral("core"), true,
         detail::plural(cases, QStringLiteral("cached case"), QStringLiteral("cached cases"))
         + QStringLiteral(", ")
         + detail::plural(synapses.nodeCount, QStringLiteral("concept"), QStringLiteral("concepts"))
         + QStringLiteral(" / ")
         + detail::plural(synapses.edgeCount, QStringLiteral("synapse"), QStringLiteral("synapses")));

    mark(QStringLiteral("voice_pipeline"), vosk,
         vosk ? QStringLiteral("Vosk runtime present")
              : QStringLiteral("libvosk.dll missing — speech input unavailable"));

    mark(QStringLiteral("training_pipeline"), true,
         detail::plural(trainPairs, QStringLiteral("training pair"), QStringLiteral("training pairs"))
         + QStringLiteral(", ")
         + detail::plural(journal, QStringLiteral("journal entry"), QStringLiteral("journal entries")));

    mark(QStringLiteral("screen_vision"), ocr,
         ocr ? QStringLiteral("Tesseract OCR available")
             : QStringLiteral("Tesseract missing — screenshots capture, but no text extraction"));

    mark(QStringLiteral("pdf_distiller"), pdfText,
         pdfText ? QStringLiteral("Poppler available")
                 : QStringLiteral("Poppler missing — PDFs cannot be read"));

    mark(QStringLiteral("project_indexer"), indexed > 0,
         indexed > 0
            ? detail::plural(indexed, QStringLiteral("file indexed"), QStringLiteral("files indexed"))
            : QStringLiteral("no project indexed yet"));

    mark(QStringLiteral("activity_awareness"), true,
         detail::plural(patterns, QStringLiteral("behaviour pattern"),
                                  QStringLiteral("behaviour patterns")));

    // Reported by their owners (see setRuntimeState call sites). Default
    // false: nothing reported it, so there is no evidence it is running —
    // and claiming otherwise is how Jarvis ends up offering to send a
    // Telegram message through a gateway that was never started.
    const QStringList reportedIds = {
        QStringLiteral("telegram_gateway"), QStringLiteral("mobile_sync"),
        QStringLiteral("j2j_mesh"),         QStringLiteral("memory_consolidation"),
    };
    for (const QString& id : reportedIds)
        mark(id, detail::reported(id), detail::reportedState(id));

    return caps;
}

// Only what is genuinely up — for the manual and any UI showing the user
// which parts of Jarvis are alive.
inline QVector<Capability> availableCapabilities()
{
    QVector<Capability> out;
    const auto caps = activeCapabilities();
    for (const auto& c : caps)
        if (c.active) out.append(c);
    return out;
}

inline QString buildCapabilitiesContext()
{
    const auto caps = activeCapabilities();

    QString ctx = QStringLiteral("=== ACTIVE CAPABILITIES ===\n"
        "These modules are running right now, with their live state. Reference "
        "them naturally when relevant — suggest voice commands, training data "
        "review, focus-state awareness, or project indexing when they fit the "
        "user's workflow. Never list them unprompted.\n");
    for (const auto& c : caps) {
        if (!c.active) continue;
        ctx += QStringLiteral("- %1: %2").arg(c.label, c.description);
        if (!c.state.isEmpty())
            ctx += QStringLiteral(" [now: %1]").arg(c.state);
        ctx += QStringLiteral("\n");
    }

    // The gaps matter as much as the abilities: a model told only what works
    // will confidently offer what doesn't.
    QStringList offline;
    for (const auto& c : caps) {
        if (c.active) continue;
        offline += c.state.isEmpty()
            ? c.label
            : QStringLiteral("%1 (%2)").arg(c.label, c.state);
    }
    if (!offline.isEmpty()) {
        ctx += QStringLiteral("\n=== NOT AVAILABLE — do not offer these ===\n- ")
             + offline.join(QStringLiteral("\n- ")) + QStringLiteral("\n");
    }

    ctx += QStringLiteral("\n");
    return ctx;
}

} // namespace SystemManifest
