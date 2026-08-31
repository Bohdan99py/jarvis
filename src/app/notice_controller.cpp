#include "notice_controller.h"

void NoticeController::showSuggestion(const QString& text)
{
    m_suggestionText = text;
    m_suggestionOpen = true;
    emit suggestionChanged();
}

void NoticeController::hideSuggestion()
{
    if (!m_suggestionOpen) return;
    m_suggestionOpen = false;
    emit suggestionChanged();
}

void NoticeController::showClarify(const QString& text, const QStringList& options)
{
    m_clarifyText = text;
    m_clarifyOptions = options;
    m_clarifyOpen = true;
    emit clarifyChanged();
}

void NoticeController::hideClarify()
{
    if (!m_clarifyOpen) return;
    m_clarifyOpen = false;
    emit clarifyChanged();
}

void NoticeController::showAnswer(const QString& text)
{
    m_answerText = text;
    m_answerOpen = true;
    emit answerChanged();
}

void NoticeController::hideAnswer()
{
    if (!m_answerOpen) return;
    m_answerOpen = false;
    emit answerChanged();
}

void NoticeController::showUpdate(const QString& text)
{
    m_updateText = text;
    m_updateOpen = true;
    m_updateBusy = false;
    m_updateProgress = 0;
    emit updateChanged();
}

void NoticeController::hideUpdate()
{
    if (!m_updateOpen) return;
    m_updateOpen = false;
    emit updateChanged();
}

void NoticeController::setUpdateProgress(int percent)
{
    if (m_updateProgress == percent) return;
    m_updateProgress = percent;
    emit updateChanged();
}

void NoticeController::setUpdateBusy(bool busy)
{
    if (m_updateBusy == busy) return;
    m_updateBusy = busy;
    emit updateChanged();
}

void NoticeController::setUpdateActionLabel(const QString& label)
{
    if (m_updateActionLabel == label) return;
    m_updateActionLabel = label;
    emit updateChanged();
}

void NoticeController::acceptSuggestion()
{
    // Полосу закрываем здесь, а не в обработчике: намерение уже
    // принято, и если обработчик откроет диалог, полоса не должна
    // висеть под ним.
    hideSuggestion();
    emit suggestionAccepted();
}

void NoticeController::dismissSuggestion()
{
    hideSuggestion();
    emit suggestionDismissed();
}

void NoticeController::chooseClarify(int index)
{
    hideClarify();
    emit clarifyChosen(index + 1);
}

void NoticeController::dismissClarify()
{
    hideClarify();
    emit clarifyDismissed();
}

void NoticeController::dismissAnswer()
{
    hideAnswer();
    emit answerDismissed();
}

void NoticeController::acceptUpdate()
{
    setUpdateBusy(true);
    emit updateAccepted();
}

void NoticeController::dismissUpdate()
{
    hideUpdate();
    emit updateDismissed();
}
