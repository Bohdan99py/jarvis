#pragma once
// ============================================================
// media_analyzer_module.h — Media Summary Module
//
// Activated via /summarize — parses long articles or video
// links and returns structured bullet-point summaries.
// ============================================================

#include <QObject>
#include <QString>

class Jarvis;

class MediaAnalyzerModule : public QObject
{
    Q_OBJECT

public:
    explicit MediaAnalyzerModule(QObject* parent = nullptr);

    void setJarvisCore(Jarvis* jarvis) { m_jarvis = jarvis; }

    QString summarize(const QString& content, bool english);

private:
    static bool containsUrl(const QString& text);

    Jarvis* m_jarvis = nullptr;
};
