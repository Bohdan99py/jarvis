// -------------------------------------------------------
// semantic_mapper.cpp — Реализация семантического маппера
// -------------------------------------------------------

#include "semantic_mapper.h"
#include "session_memory.h"

#include <QFileInfo>
#include <QDir>
#include <QJsonObject>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

SemanticMapper::SemanticMapper(SessionMemory* memory)
    : m_memory(memory)
{}

// ============================================================
// needsSemanticExpansion
// ============================================================

bool SemanticMapper::needsSemanticExpansion(const QString& query) const
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return false;

    // Одно слово И выглядит как часть имени файла (нет пробелов,
    // содержит цифры или заглавные в оригинале) → не расширяем
    if (!q.contains(QChar(' '))) {
        // Если содержит расширение — конкретный запрос
        if (q.contains(QChar('.'))) return false;
        // Короткое техническое слово → скорее всего конкретное
        if (q.length() <= 4) return false;
    }

    // Проверяем есть ли в словаре или в памяти
    const SemanticMatch fromDict = fromDictionary(q);
    if (!fromDict.fileNamePatterns.isEmpty()) return true;

    const SemanticMatch fromMem  = fromMemory(q);
    if (!fromMem.fileNamePatterns.isEmpty()) return true;

    return false;
}

// ============================================================
// map — главный метод
// ============================================================

SemanticMatch SemanticMapper::map(const QString& query) const
{
    const QString q = query.trimmed().toLower();

    // 1. Сначала смотрим в памяти — выученные связи приоритетнее
    SemanticMatch result = fromMemory(q);

    // 2. Дополняем словарём (объединяем паттерны)
    const SemanticMatch dict = fromDictionary(q);
    for (const auto& p : dict.fileNamePatterns) {
        if (!result.fileNamePatterns.contains(p))
            result.fileNamePatterns.append(p);
    }
    for (const auto& k : dict.contentKeywords) {
        if (!result.contentKeywords.contains(k))
            result.contentKeywords.append(k);
    }
    if (result.extensions.isEmpty())
        result.extensions = dict.extensions;

    // 3. Добавляем имя/фамилию пользователя для личных документов
    enrichWithUserName(result);

    return result;
}

// ============================================================
// fromDictionary — встроенный словарь понятий
// ============================================================

SemanticMatch SemanticMapper::fromDictionary(const QString& q) const
{
    SemanticMatch m;

    // --------------------------------------------------------
    // Личные документы
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("резюме")) || q.contains(QStringLiteral("cv"))
        || q.contains(QStringLiteral("resume")) || q.contains(QStringLiteral("curriculum"))) {
        m.fileNamePatterns = {
            QStringLiteral("cv"), QStringLiteral("resume"),
            QStringLiteral("резюме"), QStringLiteral("curriculum")
        };
        m.contentKeywords  = {
            // RU
            QStringLiteral("опыт работы"),  QStringLiteral("образование"),
            QStringLiteral("навыки"),        QStringLiteral("резюме"),
            // EN
            QStringLiteral("experience"),    QStringLiteral("education"),
            QStringLiteral("skills"),        QStringLiteral("curriculum vitae"),
            // FR
            QStringLiteral("expérience"),    QStringLiteral("formation"),
            QStringLiteral("compétences"),   QStringLiteral("curriculum vitae"),
            QStringLiteral("emploi"),
        };
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx"),
                        QStringLiteral("doc")};
        return m;
    }

    // Отдельно — если пользователь ищет конкретную секцию резюме
    if (q.contains(QStringLiteral("опыт работы")) || q.contains(QStringLiteral("experience"))
        || q.contains(QStringLiteral("expérience")) || q.contains(QStringLiteral("стаж"))) {
        m.fileNamePatterns = {QStringLiteral("cv"), QStringLiteral("resume"),
                               QStringLiteral("резюме")};
        m.contentKeywords  = {
            QStringLiteral("experience"),   QStringLiteral("expérience"),
            QStringLiteral("опыт работы"),  QStringLiteral("emploi"),
            QStringLiteral("poste"),        QStringLiteral("работал"),
            QStringLiteral("worked at"),    QStringLiteral("company"),
            QStringLiteral("entreprise"),
        };
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx")};
        return m;
    }

    if (q.contains(QStringLiteral("образование")) || q.contains(QStringLiteral("education"))
        || q.contains(QStringLiteral("formation")) || q.contains(QStringLiteral("диплом"))) {
        m.fileNamePatterns = {QStringLiteral("cv"), QStringLiteral("resume"),
                               QStringLiteral("diploma"), QStringLiteral("диплом")};
        m.contentKeywords  = {
            QStringLiteral("education"),    QStringLiteral("formation"),
            QStringLiteral("образование"),  QStringLiteral("université"),
            QStringLiteral("university"),   QStringLiteral("diplôme"),
            QStringLiteral("degree"),       QStringLiteral("master"),
            QStringLiteral("bachelor"),     QStringLiteral("licence"),
        };
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx")};
        return m;
    }

    if (q.contains(QStringLiteral("паспорт")) || q.contains(QStringLiteral("passport"))) {
        m.fileNamePatterns = {QStringLiteral("passport"), QStringLiteral("паспорт"),
                               QStringLiteral("passeport")};
        m.contentKeywords  = {QStringLiteral("passport"), QStringLiteral("surname"),
                               QStringLiteral("nationality")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("jpg"),
                        QStringLiteral("png")};
        return m;
    }

    if (q.contains(QStringLiteral("диплом")) || q.contains(QStringLiteral("diploma"))
        || q.contains(QStringLiteral("сертификат")) || q.contains(QStringLiteral("certificate"))) {
        m.fileNamePatterns = {QStringLiteral("diploma"), QStringLiteral("диплом"),
                               QStringLiteral("certificate"), QStringLiteral("сертификат"),
                               QStringLiteral("diplome")};
        m.contentKeywords  = {QStringLiteral("diplôme"), QStringLiteral("certificate"),
                               QStringLiteral("awarded"), QStringLiteral("university")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("jpg"),
                        QStringLiteral("png")};
        return m;
    }

    // --------------------------------------------------------
    // Финансы
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("счёт")) || q.contains(QStringLiteral("счет"))
        || q.contains(QStringLiteral("инвойс")) || q.contains(QStringLiteral("invoice"))
        || q.contains(QStringLiteral("facture"))) {
        m.fileNamePatterns = {QStringLiteral("invoice"), QStringLiteral("счет"),
                               QStringLiteral("facture"), QStringLiteral("bill")};
        m.contentKeywords  = {QStringLiteral("total"), QStringLiteral("invoice"),
                               QStringLiteral("payment"), QStringLiteral("facture")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("xlsx"),
                        QStringLiteral("docx")};
        return m;
    }

    if (q.contains(QStringLiteral("налог")) || q.contains(QStringLiteral("tax"))
        || q.contains(QStringLiteral("декларац")) || q.contains(QStringLiteral("impôt"))
        || q.contains(QStringLiteral("fiscal"))) {
        m.fileNamePatterns = {QStringLiteral("tax"), QStringLiteral("налог"),
                               QStringLiteral("declaration"), QStringLiteral("fiscal"),
                               QStringLiteral("impot"), QStringLiteral("avis"),
                               QStringLiteral("attestation")};
        m.contentKeywords  = {
            QStringLiteral("налог"),       QStringLiteral("декларация"),
            QStringLiteral("доход"),
            QStringLiteral("tax"),         QStringLiteral("income"),
            QStringLiteral("declaration"), QStringLiteral("fiscal year"),
            QStringLiteral("impôt"),       QStringLiteral("taxe"),
            QStringLiteral("déclaration"), QStringLiteral("revenu"),
            QStringLiteral("avis d'imposition"),
        };
        m.extensions = {QStringLiteral("pdf")};
        return m;
    }

    if (q.contains(QStringLiteral("бюджет")) || q.contains(QStringLiteral("budget"))
        || q.contains(QStringLiteral("расход")) || q.contains(QStringLiteral("доход"))) {
        m.fileNamePatterns = {QStringLiteral("budget"), QStringLiteral("бюджет"),
                               QStringLiteral("expenses"), QStringLiteral("finance")};
        m.contentKeywords  = {QStringLiteral("budget"), QStringLiteral("expense"),
                               QStringLiteral("income"), QStringLiteral("расход")};
        m.extensions = {QStringLiteral("xlsx"), QStringLiteral("csv"),
                        QStringLiteral("ods")};
        return m;
    }

    // --------------------------------------------------------
    // Юридические документы
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("договор")) || q.contains(QStringLiteral("контракт"))
        || q.contains(QStringLiteral("contract")) || q.contains(QStringLiteral("contrat"))) {
        m.fileNamePatterns = {QStringLiteral("contract"), QStringLiteral("договор"),
                               QStringLiteral("contrat"),  QStringLiteral("agreement"),
                               QStringLiteral("контракт")};
        m.contentKeywords  = {
            QStringLiteral("договор"),     QStringLiteral("стороны"),
            QStringLiteral("обязательства"),
            QStringLiteral("contract"),    QStringLiteral("agreement"),
            QStringLiteral("parties"),     QStringLiteral("obligations"),
            QStringLiteral("contrat"),     QStringLiteral("accord"),
            QStringLiteral("signataire"),  QStringLiteral("clause"),
        };
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx")};
        return m;
    }

    if (q.contains(QStringLiteral("аренда")) || q.contains(QStringLiteral("rent"))
        || q.contains(QStringLiteral("loyer")) || q.contains(QStringLiteral("bail"))) {
        m.fileNamePatterns = {QStringLiteral("rent"), QStringLiteral("аренда"),
                               QStringLiteral("lease"), QStringLiteral("loyer"),
                               QStringLiteral("bail"),  QStringLiteral("location")};
        m.contentKeywords  = {
            QStringLiteral("аренда"),      QStringLiteral("арендатор"),
            QStringLiteral("арендодатель"),
            QStringLiteral("rent"),        QStringLiteral("tenant"),
            QStringLiteral("landlord"),    QStringLiteral("lease"),
            QStringLiteral("loyer"),       QStringLiteral("bail"),
            QStringLiteral("locataire"),   QStringLiteral("propriétaire"),
            QStringLiteral("location"),
        };
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx")};
        return m;
    }

    // --------------------------------------------------------
    // Рабочие документы
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("презентац")) || q.contains(QStringLiteral("presentation"))
        || q.contains(QStringLiteral("слайд")) || q.contains(QStringLiteral("slides"))) {
        m.fileNamePatterns = {QStringLiteral("presentation"), QStringLiteral("презентация"),
                               QStringLiteral("slides"), QStringLiteral("deck")};
        m.contentKeywords  = {QStringLiteral("slide"), QStringLiteral("презентация")};
        m.extensions = {QStringLiteral("pptx"), QStringLiteral("ppt"),
                        QStringLiteral("pdf")};
        return m;
    }

    if (q.contains(QStringLiteral("отчёт")) || q.contains(QStringLiteral("отчет"))
        || q.contains(QStringLiteral("report"))) {
        m.fileNamePatterns = {QStringLiteral("report"), QStringLiteral("отчет"),
                               QStringLiteral("rapport"), QStringLiteral("bilan")};
        m.contentKeywords  = {QStringLiteral("report"), QStringLiteral("summary"),
                               QStringLiteral("отчёт")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx"),
                        QStringLiteral("xlsx")};
        return m;
    }

    // --------------------------------------------------------
    // Медиа
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("аватар")) || q.contains(QStringLiteral("фото"))
        || q.contains(QStringLiteral("портрет")) || q.contains(QStringLiteral("photo"))
        || q.contains(QStringLiteral("avatar")) || q.contains(QStringLiteral("profile"))) {
        m.fileNamePatterns = {QStringLiteral("avatar"), QStringLiteral("photo"),
                               QStringLiteral("profile"), QStringLiteral("portrait"),
                               QStringLiteral("фото")};
        m.extensions = {QStringLiteral("jpg"), QStringLiteral("png"),
                        QStringLiteral("jpeg"), QStringLiteral("webp")};
        return m;
    }

    if (q.contains(QStringLiteral("скриншот")) || q.contains(QStringLiteral("screenshot"))
        || q.contains(QStringLiteral("снимок экрана"))) {
        m.fileNamePatterns = {QStringLiteral("screenshot"), QStringLiteral("screen"),
                               QStringLiteral("снимок"), QStringLiteral("capture"),
                               QStringLiteral("grab")};
        m.extensions = {QStringLiteral("png"), QStringLiteral("jpg")};
        return m;
    }

    // --------------------------------------------------------
    // Учёба
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("курсов")) || q.contains(QStringLiteral("курс"))
        || q.contains(QStringLiteral("лекц")) || q.contains(QStringLiteral("lecture"))
        || q.contains(QStringLiteral("cours"))) {
        m.fileNamePatterns = {QStringLiteral("cours"), QStringLiteral("lecture"),
                               QStringLiteral("курс"), QStringLiteral("lesson"),
                               QStringLiteral("module")};
        m.contentKeywords  = {QStringLiteral("lecture"), QStringLiteral("cours"),
                               QStringLiteral("chapter")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx"),
                        QStringLiteral("pptx")};
        return m;
    }

    // --------------------------------------------------------
    // Разработка / геймдев
    // --------------------------------------------------------
    if (q.contains(QStringLiteral("настройки проекта")) || q.contains(QStringLiteral("конфиг"))
        || q.contains(QStringLiteral("config"))) {
        m.fileNamePatterns = {QStringLiteral("config"), QStringLiteral("settings"),
                               QStringLiteral("конфиг"), QStringLiteral(".ini"),
                               QStringLiteral(".json"), QStringLiteral(".yaml")};
        m.extensions = {QStringLiteral("json"), QStringLiteral("yaml"),
                        QStringLiteral("ini"),  QStringLiteral("toml")};
        return m;
    }

    if (q.contains(QStringLiteral("докуме")) && q.contains(QStringLiteral("игр"))) {
        m.fileNamePatterns = {QStringLiteral("gdd"), QStringLiteral("design"),
                               QStringLiteral("document"), QStringLiteral("spec"),
                               QStringLiteral("дизайн")};
        m.contentKeywords  = {QStringLiteral("game design"), QStringLiteral("gameplay"),
                               QStringLiteral("mechanics")};
        m.extensions = {QStringLiteral("pdf"), QStringLiteral("docx"),
                        QStringLiteral("md")};
        return m;
    }

    return m;  // пустой — словарь не знает этот концепт
}

// ============================================================
// fromMemory — выученные связи
// ============================================================

SemanticMatch SemanticMapper::fromMemory(const QString& q) const
{
    SemanticMatch m;
    if (!m_memory) return m;

    // Ищем в постоянных фактах все ключи вида "semantic_link:X"
    const QJsonObject facts = m_memory->allFacts();
    for (auto it = facts.begin(); it != facts.end(); ++it) {
        const QString key = it.key();
        if (!key.startsWith(QString::fromUtf8(kMemoryPrefix))) continue;

        // Извлекаем запрос из ключа
        const QString storedQuery = key.mid(
            static_cast<int>(strlen(kMemoryPrefix))).toLower();

        // Проверяем совпадение (точное или частичное)
        if (storedQuery == q || q.contains(storedQuery) || storedQuery.contains(q)) {
            const QString filePath = it.value().toString();
            const QFileInfo fi(filePath);

            // Добавляем имя файла как паттерн с высоким приоритетом
            m.fileNamePatterns.prepend(fi.baseName().toLower());
            m.fileNamePatterns.prepend(fi.fileName().toLower());
            m.extensions.append(fi.suffix().toLower());
            m.learned = true;
        }
    }

    return m;
}

// ============================================================
// enrichWithUserName — добавляем фамилию/имя пользователя
// ============================================================

void SemanticMapper::enrichWithUserName(SemanticMatch& match) const
{
    if (!m_memory || match.fileNamePatterns.isEmpty()) return;

    // Достаём имя пользователя из фактов если есть
    const QString userName = m_memory->recallFact(QStringLiteral("username"));
    const QString userLastName = m_memory->recallFact(QStringLiteral("lastname"));
    const QString userFirstName = m_memory->recallFact(QStringLiteral("firstname"));

    // Добавляем части имени как дополнительные паттерны
    // (резюме скорее всего содержит имя пользователя)
    if (!userName.isEmpty()) {
        const QString lower = userName.toLower();
        if (!match.fileNamePatterns.contains(lower))
            match.fileNamePatterns.append(lower);
    }
    if (!userLastName.isEmpty()) {
        const QString lower = userLastName.toLower();
        if (!match.fileNamePatterns.contains(lower))
            match.fileNamePatterns.append(lower);
    }
    if (!userFirstName.isEmpty()) {
        const QString lower = userFirstName.toLower();
        if (!match.fileNamePatterns.contains(lower))
            match.fileNamePatterns.append(lower);
    }

    // Получаем имя Windows-пользователя как запасной вариант
#ifdef Q_OS_WIN
    wchar_t buf[256] = {};
    DWORD sz = 256;
    if (GetUserNameW(buf, &sz)) {
        const QString winUser = QString::fromWCharArray(buf).toLower();
        if (!winUser.isEmpty() && !match.fileNamePatterns.contains(winUser)) {
            match.fileNamePatterns.append(winUser);
        }
    }
#endif
}

// ============================================================
// learnFromResult — запоминаем связь запрос→файл
// ============================================================

void SemanticMapper::learnFromResult(const QString& query,
                                      const QString& foundFilePath)
{
    if (!m_memory || query.isEmpty() || foundFilePath.isEmpty()) return;

    const QString key = QString::fromUtf8(kMemoryPrefix) + query.trimmed().toLower();
    m_memory->rememberFact(key, foundFilePath);
}
