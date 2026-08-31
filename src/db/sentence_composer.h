#pragma once
// ============================================================
// sentence_composer.h — собственный словарь фраз
//
// LlmCacheManager отвечает уровнем «вопрос → ответ»: нашёл достаточно
// похожий прошлый вопрос — отдал тот ответ целиком. Это работает, пока
// новый вопрос близок к старому, и не работает, когда нужный факт лежит
// внутри ответа на ДРУГОЙ вопрос.
//
// Здесь уровень мельче — предложение. Каждый ответ Клода разбирается на
// фразы, каждая помечается понятиями (тем же токенайзером, что и граф
// синапсов), и на новый вопрос собирается реплика из фраз, сказанных в
// разных разговорах.
//
// ЧЕСТНО О ГРАНИЦАХ: это ИЗВЛЕЧЕНИЕ, а не генерация. Джарвис не
// сочиняет новых предложений — он переиспользует те, что уже слышал,
// поэтому:
//   • собранный ответ всегда помечается как собранный, иначе его не
//     отличить от свежего ответа модели;
//   • фразы с местоимениями и связками ("поэтому", "он", "это") в
//     отрыве от контекста читаются как бессмыслица, и такие кандидаты
//     отсеиваются;
//   • при слабом совпадении лучше промолчать, чем склеить наугад —
//     склейка не относящихся к делу фраз выглядит как уверенный бред.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>

class SentenceComposer : public QObject
{
    Q_OBJECT

public:
    static SentenceComposer& instance();

    SentenceComposer(const SentenceComposer&)            = delete;
    SentenceComposer& operator=(const SentenceComposer&) = delete;

    struct Composition {
        QString     text;                // собранный ответ ("" = не собрался)
        QStringList usedSentences;       // из чего собран — для объяснения
        float       coverage = 0.0f;     // доля понятий вопроса, покрытых фразами
    };

    // Разбирает ответ на предложения и запоминает их. Вызывается там же,
    // где кэшируется сам ответ — учиться нужно на каждом ответе модели,
    // а не по особому случаю.
    void learn(qint64 ownerId, const QString& response,
               const QString& sourceHash = QString());

    // Пытается собрать ответ на query из запомненных фраз.
    // Пустой Composition::text — собрать не удалось; вызывающий должен
    // идти дальше (в кэш, в сеть), а не показывать пустоту.
    Composition compose(qint64 ownerId, const QString& query,
                        int maxSentences = 3) const;

    // Сколько фраз накоплено — для UI обучения.
    int sentenceCount(qint64 ownerId) const;

private:
    explicit SentenceComposer(QObject* parent = nullptr);

    static QStringList splitSentences(const QString& text);
    static bool        isSelfContained(const QString& sentence);

    // Ниже этого покрытия сборка считается неудачной: половина понятий
    // вопроса без ответа — это не ответ, а набор похожих слов.
    static constexpr float kMinCoverage      = 0.5f;
    static constexpr int   kMinSentenceChars = 25;
    static constexpr int   kMaxSentenceChars = 400;
};
