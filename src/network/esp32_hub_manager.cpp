// ============================================================
// esp32_hub_manager.cpp — ESP32 Physical Node Manager
// ============================================================

#include "esp32_hub_manager.h"
#include "jarvis_paths.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDebug>
#include <QUrl>

#ifdef JARVIS_HAS_SERIALPORT
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

// ============================================================
// Constructor / Destructor
// ============================================================

Esp32HubManager::Esp32HubManager(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &Esp32HubManager::onPollTimer);

    loadConfig();
}

Esp32HubManager::~Esp32HubManager()
{
    stop();
}

// ============================================================
// Lifecycle
// ============================================================

void Esp32HubManager::start()
{
    if (m_running) return;
    m_running = true;
    m_failCount = 0;

    qDebug() << "[ESP32] Starting hub manager";

    // Try serial first if configured
#ifdef JARVIS_HAS_SERIALPORT
    if (!m_serialPortName.isEmpty()) {
        openSerial();
        if (m_serial && m_serial->isOpen()) {
            m_useSerial = true;
            m_connected = true;
            emit connected();
            qDebug() << "[ESP32] Connected via Serial:" << m_serialPortName;
        }
    }
#endif

    // Fall back to WiFi if serial not available
    if (!m_connected && !m_nodeIp.isEmpty()) {
        m_useSerial = false;
        requestStatus(); // first probe
    }

    m_pollTimer->start();
}

void Esp32HubManager::stop()
{
    if (!m_running) return;
    m_running = false;
    m_pollTimer->stop();

#ifdef JARVIS_HAS_SERIALPORT
    closeSerial();
#endif

    if (m_connected) {
        m_connected = false;
        emit disconnected();
    }

    qDebug() << "[ESP32] Hub manager stopped";
}

// ============================================================
// WiFi Transport Config
// ============================================================

void Esp32HubManager::setNodeAddress(const QString& ip, int port)
{
    m_nodeIp   = ip;
    m_nodePort = port;
    saveConfig();
}

// ============================================================
// Serial Transport Config
// ============================================================

void Esp32HubManager::setSerialPort(const QString& portName, int baud)
{
    m_serialPortName = portName;
    m_serialBaud     = baud;
    saveConfig();
}

bool Esp32HubManager::isSerialAvailable() const
{
#ifdef JARVIS_HAS_SERIALPORT
    return true;
#else
    return false;
#endif
}

void Esp32HubManager::scanSerialPorts()
{
#ifdef JARVIS_HAS_SERIALPORT
    QStringList ports;
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        // ESP32 typically shows as CP210x or CH340
        const QString desc = info.description().toLower();
        const QString mfr  = info.manufacturer().toLower();
        const bool likelyEsp = desc.contains(QStringLiteral("cp210"))
                            || desc.contains(QStringLiteral("ch340"))
                            || desc.contains(QStringLiteral("ch9102"))
                            || mfr.contains(QStringLiteral("silicon"))
                            || mfr.contains(QStringLiteral("wch"));
        if (likelyEsp)
            ports.prepend(info.portName());
        else
            ports.append(info.portName());
    }
    emit serialPortsFound(ports);
#else
    emit serialPortsFound({});
#endif
}

// ============================================================
// Commands → ESP32
// ============================================================

void Esp32HubManager::setLed(const QString& mode, int speedMs)
{
    QJsonObject body;
    body[QStringLiteral("mode")]  = mode;
    body[QStringLiteral("speed")] = speedMs;

    if (m_useSerial) {
        body[QStringLiteral("cmd")] = QStringLiteral("led");
        sendSerialCommand(body);
    } else {
        httpPost(QStringLiteral("/led"), body);
    }
}

void Esp32HubManager::sendNotification(const QString& type, int durationMs)
{
    QJsonObject body;
    body[QStringLiteral("type")]     = type;
    body[QStringLiteral("duration")] = durationMs;

    if (m_useSerial) {
        body[QStringLiteral("cmd")] = QStringLiteral("notify");
        sendSerialCommand(body);
    } else {
        httpPost(QStringLiteral("/notify"), body);
    }
}

void Esp32HubManager::requestStatus()
{
    if (m_useSerial) {
        QJsonObject cmd;
        cmd[QStringLiteral("cmd")] = QStringLiteral("status");
        sendSerialCommand(cmd);
    } else {
        httpGet(QStringLiteral("/status"), [this](const QJsonObject& obj) {
            parseSensorData(obj);
        });
    }
}

// ============================================================
// Poll Timer
// ============================================================

void Esp32HubManager::onPollTimer()
{
    if (!m_running) return;

#ifdef JARVIS_HAS_SERIALPORT
    // Read available serial data
    if (m_useSerial && m_serial && m_serial->isOpen()) {
        while (m_serial->canReadLine()) {
            QString line = QString::fromUtf8(m_serial->readLine()).trimmed();
            if (!line.isEmpty())
                processSerialLine(line);
        }
    }
#endif

    requestStatus();
}

// ============================================================
// WiFi HTTP Helpers
// ============================================================

void Esp32HubManager::httpGet(const QString& path,
                               std::function<void(const QJsonObject&)> onSuccess)
{
    if (m_nodeIp.isEmpty()) return;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_nodeIp);
    url.setPort(m_nodePort);
    url.setPath(path);

    QNetworkRequest req(url);
    req.setTransferTimeout(CONNECT_TIMEOUT_MS);

    QNetworkReply* reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_failCount++;
            if (m_connected && m_failCount >= MAX_FAIL_BEFORE_DISCONNECT) {
                m_connected = false;
                emit disconnected();
                emit nodeError(QStringLiteral("ESP32 node unreachable: ") + reply->errorString());
            }
            return;
        }

        if (!m_connected) {
            m_connected = true;
            m_failCount = 0;
            emit connected();
            qDebug() << "[ESP32] Connected via WiFi:" << m_nodeIp;
        }
        m_failCount = 0;

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            if (onSuccess) onSuccess(doc.object());
        }
    });
}

void Esp32HubManager::httpPost(const QString& path, const QJsonObject& body)
{
    if (m_nodeIp.isEmpty()) return;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_nodeIp);
    url.setPort(m_nodePort);
    url.setPath(path);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(CONNECT_TIMEOUT_MS);

    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
    });
}

// ============================================================
// Serial Helpers
// ============================================================

void Esp32HubManager::openSerial()
{
#ifdef JARVIS_HAS_SERIALPORT
    if (m_serial) closeSerial();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_serialPortName);
    m_serial->setBaudRate(m_serialBaud);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        qWarning() << "[ESP32] Failed to open serial port:" << m_serialPortName
                    << m_serial->errorString();
        emit nodeError(QStringLiteral("Cannot open ") + m_serialPortName
                       + QStringLiteral(": ") + m_serial->errorString());
        delete m_serial;
        m_serial = nullptr;
        return;
    }

    connect(m_serial, &QSerialPort::readyRead, this, [this]() {
        while (m_serial->canReadLine()) {
            QString line = QString::fromUtf8(m_serial->readLine()).trimmed();
            if (!line.isEmpty())
                processSerialLine(line);
        }

        // Buffer partial lines
        if (m_serial->bytesAvailable() > 0) {
            m_serialBuffer += QString::fromUtf8(m_serial->readAll());
            int nl = m_serialBuffer.indexOf(QLatin1Char('\n'));
            while (nl >= 0) {
                QString line = m_serialBuffer.left(nl).trimmed();
                m_serialBuffer = m_serialBuffer.mid(nl + 1);
                if (!line.isEmpty())
                    processSerialLine(line);
                nl = m_serialBuffer.indexOf(QLatin1Char('\n'));
            }
            if (m_serialBuffer.size() > 2048)
                m_serialBuffer.clear();
        }
    });

    qDebug() << "[ESP32] Serial port opened:" << m_serialPortName;
#endif
}

void Esp32HubManager::closeSerial()
{
#ifdef JARVIS_HAS_SERIALPORT
    if (m_serial) {
        m_serial->close();
        delete m_serial;
        m_serial = nullptr;
        m_serialBuffer.clear();
    }
#endif
}

void Esp32HubManager::sendSerialCommand(const QJsonObject& cmd)
{
#ifdef JARVIS_HAS_SERIALPORT
    if (!m_serial || !m_serial->isOpen()) return;
    QByteArray data = QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n";
    m_serial->write(data);
#else
    Q_UNUSED(cmd);
#endif
}

void Esp32HubManager::processSerialLine(const QString& line)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("event"))) {
        parseEvent(obj);
    } else if (obj.contains(QStringLiteral("temp_c"))) {
        parseSensorData(obj);
    }
}

// ============================================================
// Data Parsers
// ============================================================

void Esp32HubManager::parseSensorData(const QJsonObject& obj)
{
    m_lastData.tempC     = static_cast<float>(obj.value(QStringLiteral("temp_c")).toDouble());
    m_lastData.hall      = obj.value(QStringLiteral("hall")).toInt();
    m_lastData.touch     = obj.value(QStringLiteral("touch")).toBool();
    m_lastData.touchRaw  = obj.value(QStringLiteral("touch_raw")).toInt();
    m_lastData.wifiRssi  = obj.value(QStringLiteral("wifi_rssi")).toInt();
    m_lastData.freeHeap  = obj.value(QStringLiteral("heap")).toInt();
    m_lastData.uptimeSec = obj.value(QStringLiteral("uptime")).toInt();
    m_lastData.ip        = obj.value(QStringLiteral("ip")).toString();
    m_lastData.ledMode   = obj.value(QStringLiteral("led_mode")).toString();

    emit sensorDataUpdated(m_lastData);
}

void Esp32HubManager::parseEvent(const QJsonObject& obj)
{
    const QString event = obj.value(QStringLiteral("event")).toString();

    if (event == QStringLiteral("touch")) {
        int taps = obj.value(QStringLiteral("taps")).toInt(1);
        emit touchEvent(taps);
    }
    else if (event == QStringLiteral("hall")) {
        int value = obj.value(QStringLiteral("value")).toInt();
        int delta = obj.value(QStringLiteral("delta")).toInt();
        emit hallEvent(value, delta);
    }
    else if (event == QStringLiteral("heartbeat")) {
        int uptime = obj.value(QStringLiteral("uptime")).toInt();
        emit heartbeatReceived(uptime);

        if (!m_connected) {
            m_connected = true;
            emit connected();
        }
    }
    else if (event == QStringLiteral("boot")) {
        qDebug() << "[ESP32] Node booted, firmware"
                 << obj.value(QStringLiteral("version")).toString();
        if (!m_connected) {
            m_connected = true;
            emit connected();
        }
    }
    else if (event == QStringLiteral("wifi")) {
        qDebug() << "[ESP32] Node WiFi connected at"
                 << obj.value(QStringLiteral("ip")).toString();
    }
}

// ============================================================
// Config Persistence
// ============================================================

void Esp32HubManager::loadConfig()
{
    const QString path = JarvisPaths::subPath(QStringLiteral("esp32_config.json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_nodeIp         = root.value(QStringLiteral("ip")).toString();
    m_nodePort       = root.value(QStringLiteral("port")).toInt(80);
    m_serialPortName = root.value(QStringLiteral("serial_port")).toString();
    m_serialBaud     = root.value(QStringLiteral("serial_baud")).toInt(115200);
}

void Esp32HubManager::saveConfig() const
{
    QJsonObject root;
    root[QStringLiteral("ip")]          = m_nodeIp;
    root[QStringLiteral("port")]        = m_nodePort;
    root[QStringLiteral("serial_port")] = m_serialPortName;
    root[QStringLiteral("serial_baud")] = m_serialBaud;

    const QString path = JarvisPaths::subPath(QStringLiteral("esp32_config.json"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
