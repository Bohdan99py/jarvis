#pragma once
// =============================================================================
// screen_agent.h — Зрение и управление интерфейсом
// =============================================================================

#include <QString>
#include <QRect>
#include <QImage>
#include <QObject>
#include <functional>

// Windows types нужны в заголовке — включаем только тут
#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif

struct ScreenFindResult {
    bool    found = false;
    int     x     = 0;
    int     y     = 0;
    QString text;
};

class ScreenAgent : public QObject {
    Q_OBJECT
public:
    explicit ScreenAgent(QObject* parent = nullptr);

    // --- Скриншот ---
    QImage captureScreen(int monitor = 0) const;
    QImage captureRegion(const QRect& rect) const;
    QImage captureActiveWindow() const;

    // --- OCR ---
    ScreenFindResult findText(const QString& text, int monitor = 0) const;
    QString          extractScreenText(int monitor = 0) const;

    // --- Мышь ---
    void moveTo(int x, int y) const;
    void click(int x, int y) const;
    void rightClick(int x, int y) const;
    void doubleClick(int x, int y) const;
    void scroll(int x, int y, int delta) const;
    bool clickText(const QString& text) const;  // найти + кликнуть (не const — emit)

    // --- Клавиатура ---
    void pressKey(const QString& keyCombo) const;
    void typeText(const QString& text, int delayMs = 30) const;

    // --- Окна ---
    bool        focusWindow(const QString& titleContains) const;
    bool        closeWindow(const QString& titleContains) const;
    QStringList listWindows() const;
    QString     activeWindowTitle() const;

    // --- Высокоуровневые ---
    bool openUrl(const QString& url) const;

    // Скриншот → base64 PNG
    QString captureToBase64(const QRect& rect = {}) const;

    // Описать экран через Claude Vision
    void describeScreen(const QString& claudeApiKey,
                        std::function<void(const QString&)> callback,
                        const QRect& region = {}) const;

    // Скриншот + Vision + выполнение инструкций
    void executeVisualCommand(const QString& userCommand,
                              const QString& claudeApiKey,
                              std::function<void(const QString&)> callback);

signals:
    void screenshotTaken(const QImage& img);
    void textFound(const QString& text, int x, int y);
    void actionCompleted(const QString& description);

private:
    QString runTesseract(const QImage& img) const;
    QString tesseractPath() const;

#ifdef Q_OS_WIN
    // parseVirtualKey — не const метод не нужен, делаем статическим
    static WORD parseVirtualKey(const QString& name);
    HWND findWindowByTitle(const QString& titleContains) const;
#endif
};
