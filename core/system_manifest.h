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
