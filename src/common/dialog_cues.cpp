// ============================================================
// dialog_cues.cpp — см. dialog_cues.h
// ============================================================

#include "dialog_cues.h"

#include <QRegularExpression>
#include <QStringList>

namespace DialogCues
{

namespace {

struct Marker { const char* word; Verdict verdict; };

// Порядок значим: «почти» проверяется раньше «нет», потому что
// «нет, почти» встречается, а «почти, нет» — нет. Длинные варианты
// стоят раньше своих префиксов («не совсем» до «нет», «не то» до «нет»).
const Marker kMarkers[] = {
    { "почти что",    Verdict::Almost  },
    { "почти",        Verdict::Almost  },
    { "не совсем",    Verdict::Almost  },
    { "частично",     Verdict::Almost  },
    { "отчасти",      Verdict::Almost  },
    { "almost",       Verdict::Almost  },
    { "not quite",    Verdict::Almost  },
    { "partly",       Verdict::Almost  },
    { "sort of",      Verdict::Almost  },

    { "так и есть",   Verdict::Correct },
    { "верно",        Verdict::Correct },
    { "точно",        Verdict::Correct },
    { "правильно",    Verdict::Correct },
    { "именно",       Verdict::Correct },
    { "да",           Verdict::Correct },
    { "exactly",      Verdict::Correct },
    { "correct",      Verdict::Correct },
    { "right",        Verdict::Correct },
    { "yes",          Verdict::Correct },

    { "неправильно",  Verdict::Wrong   },
    { "неверно",      Verdict::Wrong   },
    { "не верно",     Verdict::Wrong   },
    { "не то",        Verdict::Wrong   },
    { "не так",       Verdict::Wrong   },
    { "ошибся",       Verdict::Wrong   },
    { "мимо",         Verdict::Wrong   },
    { "нет",          Verdict::Wrong   },
    { "incorrect",    Verdict::Wrong   },
    { "wrong",        Verdict::Wrong   },
    { "no",           Verdict::Wrong   },
};

bool isSeparator(QChar c)
{
    return c == QLatin1Char(',') || c == QLatin1Char('.')
        || c == QLatin1Char('!') || c == QLatin1Char(';')
        || c == QLatin1Char(':') || c == QLatin1Char('-')
        || c == QChar(0x2014)    // —
        || c == QChar(0x2013);   // –
}

// Слово должно кончаться на границе, иначе «нетронутый» начинается с «нет».
bool endsAtWordBoundary(const QString& rest)
{
    if (rest.isEmpty()) return true;
    const QChar c = rest.at(0);
    return c.isSpace() || isSeparator(c) || c == QLatin1Char('?');
}

} // namespace

bool parseVerdict(const QString& text, Verdict& verdict, QString& explanation)
{
    const QString trimmed = text.trimmed();
    const QString lo = trimmed.toLower();
    if (lo.isEmpty()) return false;

    for (const Marker& m : kMarkers) {
        const QString word = QString::fromUtf8(m.word);
        if (!lo.startsWith(word)) continue;

        const QString rest = lo.mid(word.length());
        if (!endsAtWordBoundary(rest)) continue;

        // Пробелы перед знаком препинания снимаем: «почти — но я спрашивал
        // про комедии» отбито тире ровно так же, как «почти, но...», просто
        // с пробелом перед ним.
        const QString body = rest.trimmed();
        if (!body.isEmpty() && !isSeparator(body.at(0))) {
            // Хвост не отбит знаком препинания. Короткий («нет не так») —
            // всё ещё оценка; длинный — обычная фраза, начавшаяся с этого
            // слова. Прежний код резал по четырём словам всю реплику
            // целиком, из-за чего объяснить что-либо было нельзя вовсе.
            const int words = body.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts).size();
            if (words > 3) continue;
        }

        verdict = m.verdict;

        // Объяснение берём из ИСХОДНОГО текста, не из lower-версии.
        QString tail = trimmed.mid(word.length());
        static const QRegularExpression lead(
            QStringLiteral("^[\\s,.;:!\\-\\x{2013}\\x{2014}]+"));
        tail.remove(lead);
        explanation = tail.trimmed();
        return true;
    }
    return false;
}

QString trailingQuestion(const QString& reply)
{
    const QStringList lines = reply.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    const int from = qMax(0, lines.size() - 3);
    for (int i = lines.size() - 1; i >= from; --i) {
        QString line = lines[i].trimmed();

        // Строка-оговорка («🔎 _Похоже на похожий случай…_») стоит ПОСЛЕ
        // вопроса и не должна закрывать его собой.
        if (line.startsWith(QStringLiteral("\U0001F50E"))
            || line.startsWith(QStringLiteral("\U0001F52E")))
            continue;
        if (!line.endsWith(QLatin1Char('?'))) continue;

        // Из абзаца берём последнее предложение — сам вопрос.
        const int cut = qMax(line.lastIndexOf(QStringLiteral(". ")),
                             qMax(line.lastIndexOf(QStringLiteral("! ")),
                                  line.lastIndexOf(QStringLiteral("? "))));
        if (cut > 0) line = line.mid(cut + 2).trimmed();
        return line.left(300);
    }
    return {};
}

bool looksLikeNewRequest(const QString& text)
{
    static const QStringList kRequestVerbs = {
        QStringLiteral("пришли"),  QStringLiteral("покажи"),
        QStringLiteral("выдай"),   QStringLiteral("дай"),
        QStringLiteral("сделай"),  QStringLiteral("расскажи"),
        QStringLiteral("объясни"), QStringLiteral("напиши"),
        QStringLiteral("найди"),   QStringLiteral("открой"),
        QStringLiteral("запусти"), QStringLiteral("создай"),
        QStringLiteral("send"),    QStringLiteral("show"),
        QStringLiteral("give"),    QStringLiteral("make"),
        QStringLiteral("tell"),    QStringLiteral("explain"),
        QStringLiteral("write"),   QStringLiteral("find"),
        QStringLiteral("open"),    QStringLiteral("run"),
    };

    // Разговорная затравка перед просьбой. Отбрасываем её, иначе
    // «а покажи файл» не опознаётся как задача и уходит в ответ на
    // вопрос Джарвиса.
    static const QStringList kFillers = {
        QStringLiteral("а"),       QStringLiteral("ну"),
        QStringLiteral("ладно"),   QStringLiteral("ок"),
        QStringLiteral("окей"),    QStringLiteral("хорошо"),
        QStringLiteral("слушай"),  QStringLiteral("кстати"),
        QStringLiteral("тогда"),   QStringLiteral("лучше"),
        QStringLiteral("ok"),      QStringLiteral("okay"),
        QStringLiteral("well"),    QStringLiteral("then"),
        QStringLiteral("actually"),QStringLiteral("hey"),
    };

    QString lo = text.trimmed().toLower();

    // Затравок может быть несколько подряд («ну ладно, открой…»), но
    // не бесконечно: три — заведомо больше, чем встречается в живой
    // речи, и это же ограничивает цикл.
    for (int strip = 0; strip < 3; ++strip) {
        bool stripped = false;
        for (const QString& f : kFillers) {
            if (!lo.startsWith(f)) continue;
            const QString rest = lo.mid(f.length());
            if (rest.isEmpty()) continue;
            if (!rest.at(0).isSpace() && !isSeparator(rest.at(0))) continue;
            lo = rest.mid(1).trimmed();
            stripped = true;
            break;
        }
        if (!stripped) break;
    }

    for (const QString& v : kRequestVerbs) {
        if (!lo.startsWith(v)) continue;
        const QString rest = lo.mid(v.length());
        if (rest.isEmpty() || rest.at(0).isSpace() || isSeparator(rest.at(0)))
            return true;
    }
    return false;
}

bool looksLikeCounterQuestion(const QString& text)
{
    const QString lo = text.trimmed().toLower();
    if (!lo.endsWith(QLatin1Char('?'))) return false;

    // Вопросительное слово или модальный глагол в начале. Без этого
    // условия «TDD, наверное?» — неуверенный ответ — читалось бы
    // встречным вопросом и не засчиталось бы вовсе.
    static const QStringList kOpeners = {
        QStringLiteral("как"),     QStringLiteral("что"),
        QStringLiteral("почему"),  QStringLiteral("зачем"),
        QStringLiteral("какой"),   QStringLiteral("какая"),
        QStringLiteral("какие"),   QStringLiteral("где"),
        QStringLiteral("когда"),   QStringLiteral("кто"),
        QStringLiteral("сколько"), QStringLiteral("умеешь"),
        QStringLiteral("можешь"),  QStringLiteral("ты"),
        QStringLiteral("а ты"),    QStringLiteral("а можешь"),
        QStringLiteral("what"),    QStringLiteral("how"),
        QStringLiteral("why"),     QStringLiteral("which"),
        QStringLiteral("where"),   QStringLiteral("when"),
        QStringLiteral("who"),     QStringLiteral("can you"),
        QStringLiteral("do you"),  QStringLiteral("could you"),
    };

    for (const QString& o : kOpeners) {
        if (!lo.startsWith(o)) continue;
        const QString rest = lo.mid(o.length());
        if (rest.isEmpty() || rest.at(0).isSpace() || isSeparator(rest.at(0)))
            return true;
    }
    return false;
}

bool looksLikeAnswer(const QString& text)
{
    if (text.trimmed().isEmpty()) return false;
    return !looksLikeNewRequest(text) && !looksLikeCounterQuestion(text);
}


} // namespace DialogCues
