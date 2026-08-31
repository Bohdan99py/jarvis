#pragma once
// ============================================================
// notice_controller.h — состояние полос-уведомлений над строкой
// ввода: подсказка, уточнение, «ты отвечаешь на …», обновление.
//
// Раньше каждая полоса была QWidget, а её состояние выражалось
// прямыми вызовами по виджету: setText(), setVisible(), плюс свой
// QPropertyAnimation по maximumHeight на каждый показ и на каждое
// скрытие. Из-за этого «показана ли полоса» нельзя было спросить —
// только посмотреть на виджет, и четыре почти одинаковых блока
// успели разойтись в цветах, отступах и длительностях.
//
// Здесь полоса — это данные. Анимацию раскрытия ведёт QML, потому
// что только он знает настоящую высоту содержимого.
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>

class NoticeController : public QObject
{
    Q_OBJECT

    // ── Подсказка: «→ открыть Telegram?» ──────────────────
    Q_PROPERTY(bool suggestionOpen READ suggestionOpen NOTIFY suggestionChanged)
    Q_PROPERTY(QString suggestionText READ suggestionText NOTIFY suggestionChanged)

    // ── Уточнение Brain: вопрос + варианты ────────────────
    Q_PROPERTY(bool clarifyOpen READ clarifyOpen NOTIFY clarifyChanged)
    Q_PROPERTY(QString clarifyText READ clarifyText NOTIFY clarifyChanged)
    Q_PROPERTY(QStringList clarifyOptions READ clarifyOptions NOTIFY clarifyChanged)

    // ── «Отвечаешь на: …» ─────────────────────────────────
    Q_PROPERTY(bool answerOpen READ answerOpen NOTIFY answerChanged)
    Q_PROPERTY(QString answerText READ answerText NOTIFY answerChanged)

    // ── Обновление приложения ─────────────────────────────
    Q_PROPERTY(bool updateOpen READ updateOpen NOTIFY updateChanged)
    Q_PROPERTY(QString updateText READ updateText NOTIFY updateChanged)
    Q_PROPERTY(bool updateBusy READ updateBusy NOTIFY updateChanged)
    Q_PROPERTY(int updateProgress READ updateProgress NOTIFY updateChanged)

    // Подпись кнопки меняется по ходу: «Обновить» → «Открыть папку»
    // после загрузки. Раньше это делалось setText() плюс disconnect()
    // всех обработчиков кнопки и подключением нового — то есть
    // поведение кнопки переписывалось на лету.
    Q_PROPERTY(QString updateActionLabel READ updateActionLabel NOTIFY updateChanged)

public:
    explicit NoticeController(QObject* parent = nullptr) : QObject(parent) {}

    bool        suggestionOpen() const { return m_suggestionOpen; }
    QString     suggestionText() const { return m_suggestionText; }
    bool        clarifyOpen()    const { return m_clarifyOpen; }
    QString     clarifyText()    const { return m_clarifyText; }
    QStringList clarifyOptions() const { return m_clarifyOptions; }
    bool        answerOpen()     const { return m_answerOpen; }
    QString     answerText()     const { return m_answerText; }
    bool        updateOpen()     const { return m_updateOpen; }
    QString     updateText()     const { return m_updateText; }
    bool        updateBusy()     const { return m_updateBusy; }
    int         updateProgress() const { return m_updateProgress; }
    QString     updateActionLabel() const { return m_updateActionLabel; }

    void showSuggestion(const QString& text);
    void hideSuggestion();
    void showClarify(const QString& text, const QStringList& options);
    void hideClarify();
    void showAnswer(const QString& text);
    void hideAnswer();
    void showUpdate(const QString& text);
    void hideUpdate();
    void setUpdateProgress(int percent);
    void setUpdateBusy(bool busy);
    void setUpdateActionLabel(const QString& label);

    // ── Намерения из QML ──────────────────────────────────
    Q_INVOKABLE void acceptSuggestion();
    Q_INVOKABLE void dismissSuggestion();
    Q_INVOKABLE void chooseClarify(int index);
    Q_INVOKABLE void dismissClarify();
    Q_INVOKABLE void dismissAnswer();
    Q_INVOKABLE void acceptUpdate();
    Q_INVOKABLE void dismissUpdate();

signals:
    void suggestionChanged();
    void clarifyChanged();
    void answerChanged();
    void updateChanged();

    void suggestionAccepted();
    void suggestionDismissed();
    // Вариант нумеруется с единицы: onClarificationChoice() ждёт
    // именно такую нумерацию, и переводить её туда-сюда на границе
    // QML — лишний повод ошибиться на единицу.
    void clarifyChosen(int choice);
    void clarifyDismissed();
    void answerDismissed();
    void updateAccepted();
    void updateDismissed();

private:
    bool        m_suggestionOpen = false;
    QString     m_suggestionText;
    bool        m_clarifyOpen = false;
    QString     m_clarifyText;
    QStringList m_clarifyOptions;
    bool        m_answerOpen = false;
    QString     m_answerText;
    bool        m_updateOpen = false;
    QString     m_updateText;
    bool        m_updateBusy = false;
    int         m_updateProgress = 0;
    QString     m_updateActionLabel;
};
