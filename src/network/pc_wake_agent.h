#pragma once
// ============================================================
// pc_wake_agent.h — Two-Layer Communication: WoL + Cloud Relay
//
// Provides a dual communication path for Jarvis:
//
// Layer 1 — Local Agent (this class):
//   Runs on the user's PC. On startup, registers itself as ONLINE
//   in the cloud relay DB. Sends periodic heartbeats. When the
//   app shuts down, marks itself OFFLINE.
//
// Layer 2 — Cloud Relay (lightweight server-side):
//   Accepts Telegram messages while the local PC sleeps.
//   When a message arrives and the target PC is OFFLINE, the
//   relay sends a Wake-on-LAN Magic Packet to wake it.
//   Once the PC boots and registers ONLINE, queued messages
//   are delivered.
//
// Wake-on-LAN:
//   Sends a UDP Magic Packet (6x 0xFF + 16x MAC) to the
//   broadcast address on port 9 to wake the target machine.
//
// Thread safety: signals/slots + QMutex for state.
// Network I/O: async via QNetworkAccessManager + QUdpSocket.
// ============================================================

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QMutex>
#include <QJsonObject>
#include <QList>

class QNetworkAccessManager;
class QUdpSocket;

// ── PC status in the cloud relay ─────────────────────────────
enum class PcStatus {
    Online,
    Offline,
    Sleeping,
    Unknown,
};

inline QString pcStatusToString(PcStatus s)
{
    switch (s) {
    case PcStatus::Online:   return QStringLiteral("ONLINE");
    case PcStatus::Offline:  return QStringLiteral("OFFLINE");
    case PcStatus::Sleeping: return QStringLiteral("SLEEPING");
    case PcStatus::Unknown:  return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

inline PcStatus pcStatusFromString(const QString& s)
{
    if (s == QStringLiteral("ONLINE"))   return PcStatus::Online;
    if (s == QStringLiteral("OFFLINE"))  return PcStatus::Offline;
    if (s == QStringLiteral("SLEEPING")) return PcStatus::Sleeping;
    return PcStatus::Unknown;
}

// ── Queued message from cloud relay ──────────────────────────
struct RelayQueuedMessage {
    qint64    chatId     = 0;
    QString   text;
    QDateTime receivedAt;
    bool      delivered  = false;
};

// ── Local PC registration record ─────────────────────────────
struct PcRegistration {
    QString   deviceId;
    QString   macAddress;
    QString   broadcastAddress;
    QString   pcName;
    PcStatus  status    = PcStatus::Unknown;
    QDateTime lastHeartbeat;
    int       wolPort   = 9;
};

// ── PcWakeAgent class ────────────────────────────────────────

class PcWakeAgent : public QObject
{
    Q_OBJECT

public:
    explicit PcWakeAgent(const QString& deviceId,
                         QObject* parent = nullptr);
    ~PcWakeAgent() override;

    // ── Lifecycle ────────────────────────────────────────────
    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // ── Cloud Relay Configuration ────────────────────────────
    void setRelayUrl(const QString& url) { m_relayUrl = url; }
    QString relayUrl() const { return m_relayUrl; }

    void setRelayApiKey(const QString& key) { m_relayApiKey = key; }

    // ── Status Management ────────────────────────────────────
    PcStatus currentStatus() const;
    void reportStatus(PcStatus status);

    // ── Wake-on-LAN ──────────────────────────────────────────
    // Send a WoL Magic Packet to wake a specific PC
    bool sendWolPacket(const QString& macAddress,
                       const QString& broadcastAddress = QStringLiteral("255.255.255.255"),
                       int port = 9);

    // Discover THIS machine's MAC + broadcast for registration
    PcRegistration discoverLocalNetwork() const;

    // ── Message Queue (from cloud relay) ─────────────────────
    // Fetch and deliver any messages queued while PC was asleep
    void fetchQueuedMessages();

    // ── Registered PCs (admin view) ──────────────────────────
    // Query the relay for all registered PCs and their status
    void queryAllPcStatus();

    // ── Configuration ────────────────────────────────────────
    static constexpr int HEARTBEAT_INTERVAL_MS  = 60000;  // 1 minute
    static constexpr int WOL_PORT_DEFAULT       = 9;
    static constexpr int MAGIC_PACKET_SIZE      = 102;    // 6 + 16*6

signals:
    void statusChanged(PcStatus newStatus);
    void heartbeatSent();
    void heartbeatFailed(const QString& error);
    void wolPacketSent(const QString& macAddress);
    void wolPacketFailed(const QString& error);
    void queuedMessageReceived(qint64 chatId, const QString& text);
    void pcStatusQueryResult(const QList<PcRegistration>& pcs);
    void relayError(const QString& message);

private slots:
    void onHeartbeatTimer();

private:
    void registerOnline();
    void registerOffline();
    void sendHeartbeat();
    QByteArray buildMagicPacket(const QString& macAddress) const;
    void persistLocalRegistration(const PcRegistration& reg);
    void loadLocalRegistration();

    QString m_deviceId;
    QString m_relayUrl;
    QString m_relayApiKey;

    QNetworkAccessManager* m_network       = nullptr;
    QUdpSocket*            m_wolSocket     = nullptr;
    QTimer*                m_heartbeatTimer = nullptr;

    PcRegistration m_localReg;
    PcStatus       m_currentStatus = PcStatus::Offline;
    bool           m_running       = false;

    mutable QMutex m_mutex;
};
