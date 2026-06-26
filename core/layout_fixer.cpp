// ============================================================
// layout_fixer.cpp — QWERTY ↔ Cyrillic layout auto-correction
// ============================================================

#include "layout_fixer.h"
#include <QDebug>

// ============================================================
//  Mapping tables — standard QWERTY ↔ ЙЦУКЕН physical keys
// ============================================================

static QMap<QChar, QChar> buildEnToRu()
{
    // Lower-case English key → Russian character on the same physical key
    return {
        {u'q', u'й'}, {u'w', u'ц'}, {u'e', u'у'}, {u'r', u'к'}, {u't', u'е'},
        {u'y', u'н'}, {u'u', u'г'}, {u'i', u'ш'}, {u'o', u'щ'}, {u'p', u'з'},
        {u'[', u'х'}, {u']', u'ъ'},
        {u'a', u'ф'}, {u's', u'ы'}, {u'd', u'в'}, {u'f', u'а'}, {u'g', u'п'},
        {u'h', u'р'}, {u'j', u'о'}, {u'k', u'л'}, {u'l', u'д'},
        {u';', u'ж'}, {u'\'', u'э'},
        {u'z', u'я'}, {u'x', u'ч'}, {u'c', u'с'}, {u'v', u'м'}, {u'b', u'и'},
        {u'n', u'т'}, {u'm', u'ь'},
        {u',', u'б'}, {u'.', u'ю'}, {u'/', u'.'},
        {u'`', u'ё'},
        // Upper-case
        {u'Q', u'Й'}, {u'W', u'Ц'}, {u'E', u'У'}, {u'R', u'К'}, {u'T', u'Е'},
        {u'Y', u'Н'}, {u'U', u'Г'}, {u'I', u'Ш'}, {u'O', u'Щ'}, {u'P', u'З'},
        {u'{', u'Х'}, {u'}', u'Ъ'},
        {u'A', u'Ф'}, {u'S', u'Ы'}, {u'D', u'В'}, {u'F', u'А'}, {u'G', u'П'},
        {u'H', u'Р'}, {u'J', u'О'}, {u'K', u'Л'}, {u'L', u'Д'},
        {u':', u'Ж'}, {u'"', u'Э'},
        {u'Z', u'Я'}, {u'X', u'Ч'}, {u'C', u'С'}, {u'V', u'М'}, {u'B', u'И'},
        {u'N', u'Т'}, {u'M', u'Ь'},
        {u'<', u'Б'}, {u'>', u'Ю'}, {u'?', u','},
        {u'~', u'Ё'},
    };
}

static QMap<QChar, QChar> buildRuToEn()
{
    const auto forward = buildEnToRu();
    QMap<QChar, QChar> reverse;
    for (auto it = forward.begin(); it != forward.end(); ++it)
        reverse[it.value()] = it.key();
    return reverse;
}

const QMap<QChar, QChar>& LayoutFixer::enToRuMap()
{
    static const auto m = buildEnToRu();
    return m;
}

const QMap<QChar, QChar>& LayoutFixer::ruToEnMap()
{
    static const auto m = buildRuToEn();
    return m;
}

// ============================================================
//  Conversion
// ============================================================

QString LayoutFixer::enToRu(const QString& input)
{
    const auto& map = enToRuMap();
    QString out;
    out.reserve(input.size());
    for (QChar ch : input) {
        auto it = map.find(ch);
        out += (it != map.end()) ? it.value() : ch;
    }
    return out;
}

QString LayoutFixer::ruToEn(const QString& input)
{
    const auto& map = ruToEnMap();
    QString out;
    out.reserve(input.size());
    for (QChar ch : input) {
        auto it = map.find(ch);
        out += (it != map.end()) ? it.value() : ch;
    }
    return out;
}

// ============================================================
//  Heuristics
// ============================================================

double LayoutFixer::cyrillicRatio(const QString& s)
{
    if (s.isEmpty()) return 0.0;
    int cyrillic = 0;
    int alpha = 0;
    for (QChar ch : s) {
        if (ch.isLetter()) {
            ++alpha;
            if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF)
                ++cyrillic;
        }
    }
    return alpha > 0 ? static_cast<double>(cyrillic) / alpha : 0.0;
}

// ============================================================
//  Command-aware fixer
// ============================================================

LayoutFixer::Result LayoutFixer::tryFixCommand(const QString& input,
                                                const QSet<QString>& knownCommands)
{
    Result r;
    r.text = input;

    if (input.length() < 2 || !input.startsWith(QLatin1Char('/')))
        return r;

    // Extract the command token (first word, lowered)
    const QString cmdToken = input.section(QLatin1Char(' '), 0, 0).toLower();
    const QString args     = input.mid(cmdToken.length());

    // If it already matches a known command, nothing to fix
    if (knownCommands.contains(cmdToken))
        return r;

    // Try both conversion directions and check against known commands.
    // Case 1: user typed "/ьуте" (Russian layout) meaning "/menu"
    //   → ruToEn("/ьуте") = "/menu" → known → fix
    // Case 2: user typed "/vtye" meaning "/меню" — but "/меню" is
    //   not a known command, so enToRu won't help here. However
    //   ruToEn won't change ASCII text, so it's harmless.

    // Try Cyrillic → English first (more common: phone has Russian
    // keyboard active, user types a Latin command phonetically)
    QString ruFixed = ruToEn(cmdToken);
    if (ruFixed != cmdToken && knownCommands.contains(ruFixed)) {
        r.text = ruFixed + args;
        r.changed = true;
        qDebug() << "[LayoutFixer] RU→EN:" << cmdToken << "→" << ruFixed;
        return r;
    }

    // Try English → Cyrillic → then back to English
    // This handles the rarer case where the physical key sequence
    // was correct but the OS layout was wrong in a different way.
    // e.g., "ghbdtn" on QWERTY = "привет" on ЙЦУКЕН, but that's
    // not a command. Skip if it doesn't yield a known command.
    QString enFixed = enToRu(cmdToken);
    if (enFixed != cmdToken) {
        // The enToRu result itself won't be a known command (they're
        // all Latin), but check if the user meant a transliterated
        // command — not applicable for our ACL. Skip.
    }

    return r;
}
