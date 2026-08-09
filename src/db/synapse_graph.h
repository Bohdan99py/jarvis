#pragma once
// ============================================================
// synapse_graph.h — Ассоциативная память: граф понятий + Хеббовское
// обучение + spreading activation ("сборка пазла")
//
// LlmCacheManager::route() находит совпадения по ТЕКСТУ (keyword
// overlap / FTS5) — "не могу собрать проект" и "билд падает" для него
// два разных случая, хотя по смыслу это одно и то же. SynapseGraph —
// слой поверх: понятия запроса становятся узлами графа (synapse_nodes),
// рёбра между ними (synapse_edges) — это связи "встречались вместе"
// с весом, который растёт при успехе и падает при ошибке (Хеббовское
// правило: "нейроны, активирующиеся вместе, связываются сильнее").
//
// Новый запрос активирует свои узлы → активация растекается по рёбрам
// на несколько шагов, ослабевая с каждым хопом → подсвечивается кластер
// связанного опыта, включая случаи, текстуально с запросом не связанные
// вовсе. Кейсы (llm_cache), привязанные к засветившимся узлам, и есть
// "кусочки пазла" — assemble() их находит и ранжирует, а также
// сообщает какие понятия запроса не нашли отклика (missingConcepts) —
// это "дырка в пазле", по которой имеет смысл спрашивать Claude
// адресно, а не про весь запрос целиком.
//
// Область памяти скопирована по owner_id (0 = desktop, иначе Telegram
// chat_id), как и весь остальной llm_cache/heuristics/opinions —
// обучение остаётся индивидуальным для каждого человека.
//
// Только SQLite, только Qt6::Sql — без embedding-моделей, без
// PyTorch/ONNX. Понятия извлекаются переиспользованием
// MemoryManager::tokenize() (тот же RU/EN токенайзер + стоп-слова,
// что и у TF-IDF памяти).
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class SynapseGraph : public QObject
{
    Q_OBJECT

public:
    static SynapseGraph& instance();

    SynapseGraph(const SynapseGraph&)            = delete;
    SynapseGraph& operator=(const SynapseGraph&) = delete;

    // ── Активированный узел — результат spreading activation ──────
    struct ActivatedNode {
        qint64  nodeId     = 0;
        QString label;          // заполнено только для узлов-сидов (hops==0)
        float   activation  = 0.0f;   // 1.0 = напрямую в запросе, дальше — decay
        int     hops        = 0;
    };

    // ── Один "кусочек пазла" — кейс, найденный через активированные узлы ──
    struct Fragment {
        QString caseHash;       // = llm_cache.query_hash
        QString query;
        QString response;
        float   activation = 0.0f;   // сила отклика (для ранжирования)
        float   coverage   = 0.0f;   // 0..1 — какую долю понятий запроса покрывает
    };

    // ── Результат сборки ответа из фрагментов ──────────────────────
    struct Assembly {
        QList<Fragment> fragments;         // топ кандидатов, по убыванию activation
        QStringList     coveredConcepts;   // понятия запроса, для которых нашёлся фрагмент
        QStringList     missingConcepts;   // понятия запроса, для которых пазл не собрался
        // covered / (covered + missing); 0 если понятий не было вообще
        float           completeness = 0.0f;
    };

    struct GraphStats {
        int nodeCount = 0;
        int edgeCount = 0;
    };

    // ── Хеббовское обучение ─────────────────────────────────────────
    // Извлекает понятия из text, создаёт/подкрепляет их узлы и усиливает
    // рёбра между КАЖДОЙ парой понятий, встретившихся в этом запросе —
    // "сработали вместе → связь крепче". Если caseHash не пуст (кейс
    // реально сохранён в llm_cache), понятия дополнительно привязываются
    // к этому кейсу (synapse_case_links) — это то, что делает кейс
    // находимым через spreading activation при следующем похожем, но
    // текстуально другом запросе. Вызывать при: сохранении нового кейса
    // (saveResponse) и при каждом успешном попадании роутера (Exact/
    // Similar/Associative) — так граф растёт и от новых, и от повторно
    // используемых знаний.
    void reinforce(qint64 ownerId, const QString& text, const QString& caseHash = QString());

    // Обратная связь "это было неверно" (например пользователь нажал
    // "Неверно" на уточнение по doubtId) — ослабляет связи между
    // понятиями этого запроса. Симметрично reinforce(): ошибка → минус
    // вместо плюс. Веса не уходят в минус — пол в нуле.
    void weaken(qint64 ownerId, const QString& text);

    // ── Spreading activation / сборка пазла ─────────────────────────
    // Понятия query активируют свои узлы (если такие уже есть в графе
    // этого owner_id), активация растекается по synapse_edges на
    // kMaxHops шагов с decay на каждом хопе, затем по всем засветившимся
    // узлам собираются привязанные кейсы (synapse_case_links), ранжируются
    // по накопленной активации — топ maxFragments и есть "assembly".
    Assembly assemble(qint64 ownerId, const QString& query, int maxFragments = 3) const;

    // Токенизация + дедупликация + отсечение стоп-слов (переиспользует
    // MemoryManager::tokenize), плюс потолок kMaxConceptsPerQuery —
    // без него длинный запрос давал бы комбинаторный взрыв рёбер.
    static QStringList extractConcepts(const QString& text);

    // Introspection — сколько узлов/рёбер накопилось для этого owner_id
    // (для UI обучения / отладки, по аналогии с cacheEntryCount()).
    GraphStats stats(qint64 ownerId) const;

private:
    explicit SynapseGraph(QObject* parent = nullptr);

    qint64 findOrCreateNode(qint64 ownerId, const QString& label);
    void   linkEdge(qint64 ownerId, qint64 nodeA, qint64 nodeB, float delta);
    void   linkCase(qint64 nodeId, const QString& caseHash, float weight);
    QList<ActivatedNode> spreadActivation(qint64 ownerId, const QStringList& seedConcepts) const;

    static constexpr int   kMaxConceptsPerQuery = 10;    // потолок узлов на один запрос
    static constexpr int   kMaxHops             = 2;     // сколько шагов растекается активация
    static constexpr float kHopDecay            = 0.5f;  // множитель активации за хоп
    static constexpr float kActivationFloor     = 0.05f; // ниже — активация гаснет, не растекается дальше
    static constexpr float kHebbianIncrement    = 0.15f; // подкрепление ребра за одно совместное появление
    static constexpr float kHebbianDecrement    = 0.25f; // ослабление ребра при негативном фидбеке (быстрее забываем плохое)
    static constexpr float kEdgeWeightMax       = 3.0f;  // потолок веса ребра
    static constexpr float kCaseLinkMax         = 3.0f;  // потолок веса привязки узел→кейс
    static constexpr float kCaseLinkIncrement   = 0.5f;  // подкрепление привязки узел→кейс при повторном появлении
};
