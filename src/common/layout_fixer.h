#pragma once
// ============================================================
// layout_fixer.h — QWERTY ↔ Cyrillic layout auto-correction
//
// Detects text typed in the wrong keyboard layout (English keys
// producing Russian intent, or vice versa) and converts it.
//
// Stateless, header-heavy, zero allocations on the hot path
// (lookup tables are constexpr-initialized).
//
// Usage:
//   LayoutFixer::Result r = LayoutFixer::tryFix(input);
//   if (r.changed) { /* use r.text */ }
// ============================================================

#include <QString>
#include <QMap>
#include <QSet>

class LayoutFixer
{
public:
    struct Result {
        QString text;
        bool    changed = false;
    };

    // Fix a slash command if it was typed in the wrong layout.
    // Only applies the conversion if the result matches a known command.
    // Example: "/vtye" → try enToRu → "/меню" → not in knownCmds →
    //          try ruToEn on the Cyrillic → doesn't apply →
    //          but "/ьуте" → ruToEn → "/menu" → IS in knownCmds → fix.
    static Result tryFixCommand(const QString& input,
                                const QSet<QString>& knownCommands);

    // Raw conversion functions
    static QString enToRu(const QString& input);
    static QString ruToEn(const QString& input);

    // Heuristic: what fraction of the string is Cyrillic?
    static double cyrillicRatio(const QString& s);

private:
    static const QMap<QChar, QChar>& enToRuMap();
    static const QMap<QChar, QChar>& ruToEnMap();
};
