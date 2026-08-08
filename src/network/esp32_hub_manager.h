#pragma once
// ============================================================
// esp32_hub_manager.h — ESP32 Physical Node Manager
//
// Manages connection to an ESP32 running the Jarvis Node
// firmware. Supports two transport layers:
//   1) WiFi HTTP — REST calls to the ESP32's HTTP server
//   2) Serial USB — JSON-line protocol over COM port
//
// The manager polls sensor data periodically, forwards LED/
// notification commands, and emits signals for touch/hall
// events received from the node.
//
// Gated by the "esp32_hub" skill feature — the manager is
// only started when the skill is enabled.
// ============================================================

#include <QObject>
#include <QString>
#include <QTimer>
#include <QJsonObject>

#include "jarvis_core_export.h"

class QNetworkAccessManager;
class QNetworkReply;

#ifdef JARVIS_HAS_SERIALPORT
class QSerialPort;
#endif

struct Esp32SensorData
{
    float   tempC       = 0.0f;
    int     hall        = 0;
    bool    touch       = false;
    int     touchRaw    = 0;
    int     wifiRssi    = 0;
    int     freeHeap    = 0;
    int     uptimeSec   = 0;
    QString ip;
    QString ledMode;
};

class JARVIS_CORE_EXPORT Esp32HubManager : public QObject
{
    Q_OBJECT

public:
    explicit Esp32HubManager(QObject* parent = nullptr);
    ~Esp32HubManager() override;

    // ── Lifecycle ────────────────────────────────────────────
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    bool isConnected() const { return m_connected; }

    // ── WiFi Transport ───────────────────────────────────────
    void setNodeAddress(const QString& ip, int port = 80);
    QString nodeIp() const { return m_nodeIp; }
    int nodePort() const { return m_nodePort; }

    // ── Serial Transport ─────────────────────────────────────
    void setSerialPort(const QString& portName, int baud = 115200);
    QString serialPortName() const { return m_serialPortName; }
    bool isSerialAvailable() const;

    // ── Commands → ESP32 ─────────────────────────────────────
    void setLed(const QString& mode, int speedMs = 1000);
    void sendNotification(const QString& type, int durationMs = 3000);
    void requestStatus();

    // ── Auto-discovery ───────────────────────────────────────
    void scanSerialPorts();

    // ── Latest sensor snapshot ───────────────────────────────
    Esp32SensorData lastSensorData() const { return m_lastData; }

    // ── Configuration persistence ────────────────────────────
    void loadConfig();
    void saveConfig() const;

    static constexpr int POLL_INTERVAL_MS = 5000;
    static constexpr int CONNECT_TIMEOUT_MS = 3000;

signals:
    void connected();
    void disconnected();
    void sensorDataUpdated(const Esp32SensorData& data);
    void touchEvent(int taps);
    void hallEvent(int value, int delta);
    void heartbeatReceived(int uptimeSec);
    void nodeError(const QString& message);
    void serialPortsFound(const QStringList& ports);

private slots:
    void onPollTimer();

private:
    // WiFi HTTP helpers
    void httpGet(const QString& path,
                 std::function<void(const QJsonObject&)> onSuccess);
    void httpPost(const QString& path, const QJsonObject& body);

    // Serial helpers
    void openSerial();
    void closeSerial();
    void processSerialLine(const QString& line);
    void sendSerialCommand(const QJsonObject& cmd);

    void parseSensorData(const QJsonObject& obj);
    void parseEvent(const QJsonObject& obj);

    // Transport
    QNetworkAccessManager* m_network = nullptr;
    QTimer* m_pollTimer = nullptr;

    // WiFi config
    QString m_nodeIp;
    int     m_nodePort = 80;

    // Serial config
    QString m_serialPortName;
    int     m_serialBaud = 115200;
#ifdef JARVIS_HAS_SERIALPORT
    QSerialPort* m_serial = nullptr;
    QString      m_serialBuffer;
#endif

    // State
    bool m_running   = false;
    bool m_connected = false;
    bool m_useSerial = false;
    int  m_failCount = 0;

    Esp32SensorData m_lastData;

    static constexpr int MAX_FAIL_BEFORE_DISCONNECT = 3;
};
