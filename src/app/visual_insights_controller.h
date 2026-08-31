#pragma once
// ============================================================
// visual_insights_controller.h — состояние боковой панели с
// диаграммами, картинками и ссылками на файлы.
//
// Панель была QWidget на 935 строк, внутри которого жил
// QWebEngineView. Проверено пробой, что WebEngineView прекрасно
// работает и внутри QQuickWidget, поэтому Chromium-рендеринг
// Mermaid никуда не девается — переезжает в QML вместе с панелью.
//
// В C++ остаётся то, что и было отлажено: история показов,
// сборка HTML-страниц и опрос страницы при экспорте. Наружу
// уходит вид.
//
// JS исполняется через мост: контроллер просит скрипт сигналом,
// QML запускает его на своём WebEngineView и возвращает результат.
// Так вся логика опроса (с токенами и таймаутами) остаётся здесь,
// а C++ при этом ничего не знает про конкретный движок показа.
// ============================================================

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class VisualInsightsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool open READ open NOTIFY openChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString html READ html NOTIFY currentChanged)
    Q_PROPERTY(QString svgSource READ svgSource NOTIFY currentChanged)
    Q_PROPERTY(bool canPrev READ canPrev NOTIFY currentChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY currentChanged)
    Q_PROPERTY(bool webEngineAvailable READ webEngineAvailable CONSTANT)

public:
    // Экспорт отдаёт SVG и растр отрисованной диаграммы — им
    // пользуется отправка в Telegram.
    using ExportCallback = std::function<void(const QByteArray& svg,
                                              const QImage& raster)>;

    explicit VisualInsightsController(QObject* parent = nullptr);

    bool    open()       const { return m_open; }
    QString title()      const;
    QString html()       const;
    QString svgSource()  const;
    bool    canPrev()    const { return m_index > 0; }
    bool    canNext()    const { return m_index >= 0 && m_index < m_history.size() - 1; }
    bool    hasFile()    const;
    bool    hasHistory() const { return !m_history.isEmpty(); }
    bool    webEngineAvailable() const;

    // ── Показ (зовёт MainWindow) ──────────────────────────
    void showMermaid(const QString& mermaidSource);
    void showSvg(const QByteArray& svgData, const QString& mermaidSource);
    void showDiagram(const QImage& image);
    void showImageFile(const QString& filePath);
    void showFileRef(const QString& filePath, const QString& title);
    void reopen();

    void exportRendered(ExportCallback cb);

    // Сырые данные текущего показа — их пишет на диск MainWindow:
    // диалогу сохранения нужен родитель-виджет, а панель его не имеет.
    QString    currentMermaid()  const;
    QImage     currentImage()    const;
    QByteArray currentSvgData()  const;
    QString    currentFilePath() const;

    // ── Действия из QML ───────────────────────────────────
    Q_INVOKABLE void prev();
    Q_INVOKABLE void next();
    Q_INVOKABLE void close();
    Q_INVOKABLE void save();
    Q_INVOKABLE void openFolder();

    // Ответ моста: результат скрипта, запрошенного runJsRequested.
    Q_INVOKABLE void jsResult(int callId, const QString& value);

signals:
    void openChanged();
    void currentChanged();

    // QML обязан выполнить script на своём WebEngineView и вернуть
    // результат через jsResult(callId, ...).
    void runJsRequested(int callId, const QString& script);

    // Просьба к MainWindow: показать диалог сохранения.
    void saveRequested(const QString& suggestedName);

private:
    enum class Kind { Mermaid, Svg, Image, FileRef };

    struct HistoryEntry {
        Kind       kind;
        QString    title;
        QString    mermaidSource;
        QByteArray svgData;
        QImage     image;
        QString    filePath;
    };

    void pushAndShow(HistoryEntry entry);
    void setOpen(bool on);

    QString buildMermaidHtml(const QString& mermaidCode) const;
    QString buildImageHtml(const QImage& image) const;
    QString buildFileCardHtml(const QString& filePath, const QString& title) const;
    QString buildSvgHtml(const QByteArray& svg) const;

    void runJs(const QString& script, std::function<void(const QString&)> cb);
    void pollExport(ExportCallback cb, quint64 token, int attemptsLeft);
    void fetchExportResults(ExportCallback cb, quint64 token);

    QVector<HistoryEntry> m_history;
    int    m_index = -1;
    bool   m_open  = false;

    // Смена показанной диаграммы обесценивает экспорт, который ещё
    // летит: токен отсекает поздний ответ от прошлой страницы.
    quint64 m_renderToken = 0;

    int m_nextCallId = 1;
    QHash<int, std::function<void(const QString&)>> m_pendingJs;
};
