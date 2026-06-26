#pragma once
// ============================================================
// j2j_mesh_connector.h — JARVIS-to-JARVIS P2P Mesh Network
//
// UDP beacon discovery + TCP command channel for peer instances.
// Enables knowledge sync, task delegation, and capability
// exchange across JARVIS nodes on the local network.
// ============================================================

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>

class MobilePairingManager;

struct J2JPeer {
    QString   nodeId;
    QString   nodeName;
    QString   version;
    QString   role;         // peer's current_role (Developer, QA_Tester, etc.)
    QHostAddress address;
    quint16   tcpPort    = 0;
    QDateTime lastSeen;
    bool      authorized = false;
};

class J2JMeshConnector : public QObject
{
    Q_OBJECT
public:
    explicit J2JMeshConnector(QObject* parent = nullptr);
    ~J2JMeshConnector() override;

    void start(quint16 tcpPort = 9090, quint16 udpPort = 9091);
    void stop();
    bool isRunning() const { return m_running; }

    void setNodeName(const QString& name) { m_nodeName = name; }
    QString nodeName() const { return m_nodeName; }
    void setNodeRole(const QString& role) { m_nodeRole = role; }
    QString nodeRole() const { return m_nodeRole; }

    QList<J2JPeer> activePeers() const;
    int peerCount() const;

    void broadcastKnowledge(const QJsonArray& facts);
    void delegateTask(const QString& peerId, const QJsonObject& task);

    // Mobile pairing coordinator
    MobilePairingManager* mobilePairing() const { return m_mobilePairing; }
    void initMobilePairing();

signals:
    void peerDiscovered(const QString& nodeName, const QString& address);
    void peerLost(const QString& nodeName);
    void peerAuthorized(const QString& nodeName, const QString& address);
    void knowledgeReceived(const QString& fromNode, int factCount);
    void taskReceived(const QString& fromNode, const QString& taskTitle);
    void meshError(const QString& message);

private slots:
    void onBeaconTimer();
    void onReadBeacon();
    void onNewTcpConnection();
    void onTcpDataReady();
    void onTcpDisconnected();
    void onPeerCleanupTimer();

private:
    void sendBeacon();
    void processPacket(QTcpSocket* socket, const QJsonObject& packet);
    void handleHandshake(QTcpSocket* socket, const QJsonObject& data);
    void handleSyncKnowledge(QTcpSocket* socket, const QJsonObject& data);
    void handleDelegateTask(QTcpSocket* socket, const QJsonObject& data);
    void sendReply(QTcpSocket* socket, const QString& type,
                   const QJsonObject& payload, bool success = true);

    bool verifyToken(const QJsonObject& packet) const;
    QString authToken() const;
    QString nodeId() const { return m_nodeId; }

    QTcpServer*  m_tcpServer  = nullptr;
    QUdpSocket*  m_udpSocket  = nullptr;
    QTimer*      m_beaconTimer = nullptr;
    QTimer*      m_cleanupTimer = nullptr;

    QMap<QString, J2JPeer>         m_peers;
    QMap<QTcpSocket*, QByteArray>  m_socketBuffers;
    MobilePairingManager*          m_mobilePairing = nullptr;

    QString  m_nodeId;
    QString  m_nodeName;
    QString  m_nodeRole = QStringLiteral("Developer");
    quint16  m_tcpPort = 9090;
    quint16  m_udpPort = 9091;
    bool     m_running = false;

    static constexpr int BEACON_INTERVAL_MS  = 10000;
    static constexpr int PEER_TIMEOUT_SEC    = 35;
    static constexpr int CLEANUP_INTERVAL_MS = 15000;
    static constexpr int MAX_PACKET_SIZE     = 1024 * 1024;
};
