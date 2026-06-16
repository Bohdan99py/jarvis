// =============================================================================
// screen_agent.cpp
// =============================================================================

#include "screen_agent.h"

#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTimer>
#include <QPointer>
#include <QWidget>

// shellapi.h уже включён через screen_agent.h (Q_OS_WIN блок)

ScreenAgent::ScreenAgent(QObject* parent)
    : QObject(parent)
{}

// =============================================================================
// Скриншоты
// =============================================================================

QImage ScreenAgent::captureScreen(int monitor) const
{
    const QList<QScreen*> screens = QApplication::screens();
    if (monitor < screens.size())
        return screens[monitor]->grabWindow(0).toImage();
    return QApplication::primaryScreen()->grabWindow(0).toImage();
}

QImage ScreenAgent::captureRegion(const QRect& rect) const
{
    const QImage full = captureScreen();
    if (rect.isEmpty()) return full;
    return full.copy(rect);
}

QImage ScreenAgent::captureActiveWindow() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return captureScreen();
    RECT wr;
    GetWindowRect(hwnd, &wr);
    return captureRegion(QRect(wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top));
#else
    return captureScreen();
#endif
}

// =============================================================================
// Tesseract OCR
// =============================================================================

QString ScreenAgent::tesseractPath() const
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/Tesseract-OCR/tesseract.exe"),
        exeDir + QStringLiteral("/tesseract.exe"),
        QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe"),
        QStringLiteral("tesseract"),
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return c;
    return QStringLiteral("tesseract");
}

QString ScreenAgent::runTesseract(const QImage& img) const
{
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/jarvis_ocr_XXXXXX.png"));
    if (!tmp.open()) return {};
    const QString imgPath = tmp.fileName();
    img.save(imgPath, "PNG");
    tmp.close();

    const QString outBase = imgPath + QStringLiteral("_out");
    QProcess proc;
    proc.start(tesseractPath(), {
        imgPath, outBase,
        QStringLiteral("-l"), QStringLiteral("rus+eng"),
        QStringLiteral("--psm"), QStringLiteral("3")
    });
    proc.waitForFinished(15000);

    QFile outFile(outBase + QStringLiteral(".txt"));
    if (!outFile.open(QIODevice::ReadOnly)) return {};
    const QString result = QString::fromUtf8(outFile.readAll()).trimmed();
    outFile.remove();
    QFile::remove(imgPath);
    return result;
}

QString ScreenAgent::extractScreenText(int monitor) const
{
    return runTesseract(captureScreen(monitor));
}

// =============================================================================
// Поиск текста
// =============================================================================

ScreenFindResult ScreenAgent::findText(const QString& text, int monitor) const
{
    ScreenFindResult result;
    const QImage screen = captureScreen(monitor);
    const int tileW = screen.width()  / 3;
    const int tileH = screen.height() / 3;

    for (int row = 0; row < 3 && !result.found; ++row) {
        for (int col = 0; col < 3 && !result.found; ++col) {
            const QRect tile(col * tileW, row * tileH, tileW, tileH);
            if (runTesseract(screen.copy(tile)).contains(text, Qt::CaseInsensitive)) {
                result.found = true;
                result.x     = tile.center().x();
                result.y     = tile.center().y();
                result.text  = text;
            }
        }
    }
    return result;
}

// =============================================================================
// Мышь
// =============================================================================

void ScreenAgent::moveTo(int x, int y) const
{
#ifdef Q_OS_WIN
    SetCursorPos(x, y);
#else
    Q_UNUSED(x) Q_UNUSED(y)
#endif
}

void ScreenAgent::click(int x, int y) const
{
#ifdef Q_OS_WIN
    SetCursorPos(x, y);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_LEFTUP,   0, 0, 0, 0);
#else
    Q_UNUSED(x) Q_UNUSED(y)
#endif
}

void ScreenAgent::rightClick(int x, int y) const
{
#ifdef Q_OS_WIN
    SetCursorPos(x, y);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_RIGHTUP,   0, 0, 0, 0);
#else
    Q_UNUSED(x) Q_UNUSED(y)
#endif
}

void ScreenAgent::doubleClick(int x, int y) const
{
#ifdef Q_OS_WIN
    SetCursorPos(x, y);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_LEFTUP,   0, 0, 0, 0);
    QThread::msleep(100);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_LEFTUP,   0, 0, 0, 0);
#else
    Q_UNUSED(x) Q_UNUSED(y)
#endif
}

void ScreenAgent::scroll(int x, int y, int delta) const
{
#ifdef Q_OS_WIN
    SetCursorPos(x, y);
    QThread::msleep(50);
    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(delta * WHEEL_DELTA), 0);
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(delta)
#endif
}

// clickText — не const т.к. emit
bool ScreenAgent::clickText(const QString& text) const
{
    const ScreenFindResult r = findText(text);
    if (r.found) {
        click(r.x, r.y);
        // emit нельзя из const — используем const_cast безопасно (QObject сигналы thread-safe)
        const_cast<ScreenAgent*>(this)->textFound(text, r.x, r.y);
        return true;
    }
    return false;
}

// =============================================================================
// Клавиатура
// =============================================================================

#ifdef Q_OS_WIN
// static — не привязан к const this
WORD ScreenAgent::parseVirtualKey(const QString& name)
{
    const QString n = name.toLower().trimmed();
    if (n == QStringLiteral("ctrl"))      return VK_CONTROL;
    if (n == QStringLiteral("alt"))       return VK_MENU;
    if (n == QStringLiteral("shift"))     return VK_SHIFT;
    if (n == QStringLiteral("win"))       return VK_LWIN;
    if (n == QStringLiteral("enter"))     return VK_RETURN;
    if (n == QStringLiteral("esc"))       return VK_ESCAPE;
    if (n == QStringLiteral("tab"))       return VK_TAB;
    if (n == QStringLiteral("space"))     return VK_SPACE;
    if (n == QStringLiteral("backspace")) return VK_BACK;
    if (n == QStringLiteral("delete"))    return VK_DELETE;
    if (n == QStringLiteral("home"))      return VK_HOME;
    if (n == QStringLiteral("end"))       return VK_END;
    if (n == QStringLiteral("up"))        return VK_UP;
    if (n == QStringLiteral("down"))      return VK_DOWN;
    if (n == QStringLiteral("left"))      return VK_LEFT;
    if (n == QStringLiteral("right"))     return VK_RIGHT;
    if (n.startsWith(QLatin1Char('f')) && n.length() <= 3) {
        bool ok; int num = n.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) return static_cast<WORD>(VK_F1 + num - 1);
    }
    if (n.length() == 1) return static_cast<WORD>(VkKeyScanW(n[0].unicode()) & 0xFF);
    return 0;
}
#endif

void ScreenAgent::pressKey(const QString& keyCombo) const
{
#ifdef Q_OS_WIN
    const QStringList parts = keyCombo.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QVector<INPUT> inputs;

    auto addKey = [&inputs](WORD vk, bool down) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        if (!down) inp.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.append(inp);
    };

    QVector<WORD> keys;
    for (const QString& p : parts) {
        WORD vk = parseVirtualKey(p);
        if (vk) keys.append(vk);
    }
    for (WORD vk : keys) addKey(vk, true);
    for (int i = keys.size() - 1; i >= 0; --i) addKey(keys[i], false);
    if (!inputs.isEmpty())
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
#else
    Q_UNUSED(keyCombo)
#endif
}

void ScreenAgent::typeText(const QString& text, int delayMs) const
{
#ifdef Q_OS_WIN
    for (QChar c : text) {
        INPUT inp[2] = {};
        inp[0].type = inp[1].type = INPUT_KEYBOARD;
        inp[0].ki.wScan = inp[1].ki.wScan = static_cast<WORD>(c.unicode());
        inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inp, sizeof(INPUT));
        if (delayMs > 0) QThread::msleep(static_cast<unsigned long>(delayMs));
    }
#else
    Q_UNUSED(text) Q_UNUSED(delayMs)
#endif
}

// =============================================================================
// Управление окнами
// =============================================================================

#ifdef Q_OS_WIN
HWND ScreenAgent::findWindowByTitle(const QString& titleContains) const
{
    struct EnumData { QString search; HWND found; };
    EnumData data{ titleContains.toLower(), nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* d = reinterpret_cast<EnumData*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t buf[512] = {};
        GetWindowTextW(hwnd, buf, 511);
        if (QString::fromWCharArray(buf).toLower().contains(d->search)) {
            d->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));
    return data.found;
}
#endif

bool ScreenAgent::focusWindow(const QString& titleContains) const
{
#ifdef Q_OS_WIN
    HWND hwnd = findWindowByTitle(titleContains);
    if (!hwnd) return false;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    QThread::msleep(300);
    return true;
#else
    Q_UNUSED(titleContains) return false;
#endif
}

bool ScreenAgent::closeWindow(const QString& titleContains) const
{
#ifdef Q_OS_WIN
    HWND hwnd = findWindowByTitle(titleContains);
    if (!hwnd) return false;
    PostMessage(hwnd, WM_CLOSE, 0, 0);
    return true;
#else
    Q_UNUSED(titleContains) return false;
#endif
}

QStringList ScreenAgent::listWindows() const
{
    QStringList windows;
#ifdef Q_OS_WIN
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t buf[512] = {};
        GetWindowTextW(hwnd, buf, 511);
        const QString title = QString::fromWCharArray(buf).trimmed();
        if (!title.isEmpty())
            reinterpret_cast<QStringList*>(lp)->append(title);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
#endif
    return windows;
}

QString ScreenAgent::activeWindowTitle() const
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return {};
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 511);
    return QString::fromWCharArray(buf);
#else
    return {};
#endif
}

bool ScreenAgent::openUrl(const QString& url) const
{
#ifdef Q_OS_WIN
    ShellExecuteW(nullptr, L"open",
                  reinterpret_cast<LPCWSTR>(url.utf16()),
                  nullptr, nullptr, SW_SHOWNORMAL);
    QThread::msleep(1500);
    return true;
#else
    Q_UNUSED(url) return false;
#endif
}

// =============================================================================
// Vision API
// =============================================================================

QString ScreenAgent::captureToBase64(const QRect& rect) const
{
    const QImage img = rect.isEmpty() ? captureScreen() : captureRegion(rect);
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(ba.toBase64());
}

void ScreenAgent::describeScreen(
    const QString& claudeApiKey,
    std::function<void(const QString&)> callback,
    const QRect& region) const
{
    // ИСПРАВЛЕНИЕ: скрываем главное окно JARVIS перед захватом —
    // иначе Claude описывает собственный интерфейс вместо реального экрана.
    QWidget* mainWin = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->inherits("QMainWindow")) {
            mainWin = w;
            break;
        }
    }
    if (mainWin) mainWin->hide();

    // Ждём 400ms чтобы ОС успела перерисовать экран без нашего окна
    QPointer<const ScreenAgent> self = this;
    QTimer::singleShot(400, [self, claudeApiKey, callback, region, mainWin]() {
        // Захватываем экран пока JARVIS скрыт
        QString b64;
        if (self) b64 = self->captureToBase64(region);

        // Восстанавливаем окно сразу после захвата
        if (mainWin) { mainWin->show(); mainWin->raise(); mainWin->activateWindow(); }

        if (!self || b64.isEmpty()) {
            callback(QStringLiteral("[Vision: capture failed]"));
            return;
        }

    auto* nam = new QNetworkAccessManager();
    QNetworkRequest req(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key",         claudeApiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    const QJsonObject body {
        { "model",      QStringLiteral("claude-sonnet-4-6") },
        { "max_tokens", 1024 },
        { "messages",   QJsonArray{
            QJsonObject{
                { "role", "user" },
                { "content", QJsonArray{
                    QJsonObject{
                        { "type",   "image" },
                        { "source", QJsonObject{
                            { "type",       "base64" },
                            { "media_type", "image/png" },
                            { "data",       b64 }
                        }}
                    },
                    QJsonObject{
                        { "type", "text" },
                        { "text", QStringLiteral(
                            "Describe what you see on this screenshot: active app, "
                            "main UI elements, visible text, buttons, input fields. "
                            "Be concise and structured.") }
                    }
                }}
            }
        }}
    };

    auto* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, [reply, callback, nam]() {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString text;
        for (const QJsonValue& v : doc.object()[QStringLiteral("content")].toArray()) {
            if (v.toObject()[QStringLiteral("type")].toString() == QStringLiteral("text"))
                text += v.toObject()[QStringLiteral("text")].toString();
        }
        callback(text.isEmpty() ? QStringLiteral("[Vision: no response]") : text);
        reply->deleteLater();
        nam->deleteLater();
    });
    }); // конец QTimer::singleShot
}

void ScreenAgent::executeVisualCommand(
    const QString& userCommand,
    const QString& claudeApiKey,
    std::function<void(const QString&)> callback)
{
    // ИСПРАВЛЕНИЕ: скрываем JARVIS перед захватом экрана
    QWidget* mainWin = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->inherits("QMainWindow")) {
            mainWin = w;
            break;
        }
    }
    if (mainWin) mainWin->hide();

    QPointer<ScreenAgent> self = this;
    QTimer::singleShot(400, [self, userCommand, claudeApiKey, callback, mainWin]() {
        QString b64;
        if (self) b64 = self->captureToBase64();

        if (mainWin) { mainWin->show(); mainWin->raise(); mainWin->activateWindow(); }

        if (!self || b64.isEmpty()) {
            callback(QStringLiteral("[Vision: capture failed]"));
            return;
        }

    auto* nam = new QNetworkAccessManager();
    QNetworkRequest req(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key",         claudeApiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    const QString prompt = QString(QStringLiteral(
        "User wants: \"%1\"\n\n"
        "Given the screenshot, describe EXACT actions needed:\n"
        "- click(x, y)\n- type(text)\n- key(ctrl+c)\n"
        "- open_url(url)\n- focus(window title)\n\n"
        "Reply ONLY with the action list, no explanations."
    )).arg(userCommand);

    const QJsonObject body {
        { "model",      QStringLiteral("claude-sonnet-4-6") },
        { "max_tokens", 512 },
        { "messages",   QJsonArray{
            QJsonObject{
                { "role", "user" },
                { "content", QJsonArray{
                    QJsonObject{
                        { "type",   "image" },
                        { "source", QJsonObject{
                            { "type",       "base64" },
                            { "media_type", "image/png" },
                            { "data",       b64 }
                        }}
                    },
                    QJsonObject{ { "type", "text" }, { "text", prompt } }
                }}
            }
        }}
    };

    auto* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished,
                     [self, reply, callback, nam]() {

        QString instructions;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        for (const QJsonValue& v : doc.object()[QStringLiteral("content")].toArray()) {
            if (v.toObject()[QStringLiteral("type")].toString() == QStringLiteral("text"))
                instructions += v.toObject()[QStringLiteral("text")].toString();
        }
        reply->deleteLater();
        nam->deleteLater();

        QStringList done;
        static const QRegularExpression reClick(QStringLiteral(R"(click\((\d+),\s*(\d+)\))"));
        static const QRegularExpression reType(QStringLiteral(R"(type\(([^)]+)\))"));
        static const QRegularExpression reKey(QStringLiteral(R"(key\(([^)]+)\))"));
        static const QRegularExpression reUrl(QStringLiteral(R"(open_url\(([^)]+)\))"));
        static const QRegularExpression reFocus(QStringLiteral(R"(focus\(([^)]+)\))"));

        for (const QString& line : instructions.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            auto m = reClick.match(line);
            if (m.hasMatch()) {
                if (self) self->click(m.captured(1).toInt(), m.captured(2).toInt());
                done.append(QStringLiteral("🖱 click(") + m.captured(1) + QStringLiteral(",") + m.captured(2) + QStringLiteral(")"));
                QThread::msleep(200); continue;
            }
            m = reType.match(line);
            if (m.hasMatch()) {
                if (self) self->typeText(m.captured(1));
                done.append(QStringLiteral("⌨ type: ") + m.captured(1));
                QThread::msleep(100); continue;
            }
            m = reKey.match(line);
            if (m.hasMatch()) {
                if (self) self->pressKey(m.captured(1));
                done.append(QStringLiteral("⌨ key: ") + m.captured(1));
                QThread::msleep(100); continue;
            }
            m = reUrl.match(line);
            if (m.hasMatch()) {
                if (self) self->openUrl(m.captured(1));
                done.append(QStringLiteral("🌐 ") + m.captured(1)); continue;
            }
            m = reFocus.match(line);
            if (m.hasMatch()) {
                if (self) self->focusWindow(m.captured(1));
                done.append(QStringLiteral("🪟 focus: ") + m.captured(1));
                QThread::msleep(300); continue;
            }
        }

        const QString result = done.isEmpty()
            ? QStringLiteral("Could not parse actions from Vision response")
            : done.join(QStringLiteral("\n"));

        emit const_cast<ScreenAgent*>(self.data())->actionCompleted(result);
        callback(result);
    });
    }); // конец QTimer::singleShot
}