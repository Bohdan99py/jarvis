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
class J2JTelegramGateway;

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

    // Постоянный идентификатор этого узла (нужен Telegram-гейтвею для
    // привязки chat_id → ПК).
    QString localNodeId() const { return m_nodeId; }

    void broadcastKnowledge(const QJsonArray& facts);
    void delegateTask(const QString& peerId, const QJsonObject& task);

    // Ask every mesh peer "does anyone know about <topic>?" — e.g. an
    // unfamiliar phrase heard in another language. Peers search their
    // own knowledge_base and reply; results arrive via knowledgeQueryResult
    // (fired once per responding peer, so listen for a short window).
    void requestKnowledge(const QString& queryId, const QString& topic);

    // ── Face profiles (P2P) ─────────────────────────────────
    // Рассылает локально обученные лица (имя, возраст, статус,
    // LBP-признаки) всем узлам меша — их камеры будут узнавать
    // этого человека и подписывать его в кадре.
    void broadcastFaceProfiles();

    // ── Telegram multi-PC routing ───────────────────────────
    // Пересылка Telegram-события (сообщение/привязка) узлу-владельцу чата.
    // Возвращает false, если пир не найден в мешe (оффлайн).
    bool sendTgRelay(const QString& peerId, const QJsonObject& payload);
    // Рассылка всем пирам: "чат N теперь привязан к устройству X" —
    // чтобы каждый узел знал, чьи апдейты пересылать, а чьи выполнять.
    void broadcastTgBinding(qint64 chatId, const QString& deviceId,
                            const QString& pcName);

    // P2P knowledge delegation — secondary node offloads to primary
    void delegateRawAsset(const QString& peerId, const QString& assetType,
                          const QString& fileName, const QByteArray& data);
    void requestDistilledCache(const QString& peerId);
    void syncUserProfile(const QString& peerId);
    bool hasPrimaryStorage() const;

    // Mobile pairing coordinator
    MobilePairingManager* mobilePairing() const { return m_mobilePairing; }
    void initMobilePairing();

    // Telegram gateway
    J2JTelegramGateway* telegramGateway() const { return m_telegramGw; }
    void initTelegramGateway();

signals:
    void peerDiscovered(const QString& nodeName, const QString& address);
    void peerLost(const QString& nodeName);
    void peerAuthorized(const QString& nodeName, const QString& address);
    void knowledgeReceived(const QString& fromNode, int factCount);
    void knowledgeQueryResult(const QString& queryId, const QString& fromNode,
                              const QJsonArray& facts);
    void faceProfilesReceived(const QString& fromNode, int faceCount);
    void taskReceived(const QString& fromNode, const QString& taskTitle);
    void assetDelegated(const QString& toNode, const QString& assetType);
    void distilledCacheReceived(const QString& fromNode, int entryCount);
    void profileSynced(const QString& withNode);
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
    void handleKnowledgeQuery(QTcpSocket* socket, const QJsonObject& data);
    void handleDelegateTask(QTcpSocket* socket, const QJsonObject& data);
    void handleDelegateAsset(QTcpSocket* socket, const QJsonObject& data);
    void handleRequestCache(QTcpSocket* socket, const QJsonObject& data);
    void handleProfileSync(QTcpSocket* socket, const QJsonObject& data);
    void handleTgRelay(QTcpSocket* socket, const QJsonObject& data);
    void handleTgBinding(QTcpSocket* socket, const QJsonObject& data);
    void handleFaceSync(QTcpSocket* socket, const QJsonObject& data);
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
    J2JTelegramGateway*            m_telegramGw    = nullptr;

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
