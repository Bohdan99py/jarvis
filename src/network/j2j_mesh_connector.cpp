// ============================================================
// j2j_mesh_connector.cpp — JARVIS-to-JARVIS P2P Mesh Network
// ============================================================

#include "j2j_mesh_connector.h"
#include "face_registry.h"
#include "mobile_pairing_manager.h"
#include "j2j_telegram_gateway.h"
#include "media_routing_manager.h"
#include "database_manager.h"
#include "system_manifest.h"
#include "activity_tracker.h"
#include "memory_consolidation.h"
#include "user_profile_extended.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QUuid>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>

#ifndef JARVIS_VERSION
#define JARVIS_VERSION "2.5.0"
#endif

// ============================================================

J2JMeshConnector::J2JMeshConnector(QObject* parent)
    : QObject(parent)
{
    m_nodeId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);

    // Persistent node ID across restarts
    auto& db = DatabaseManager::instance();
    QString stored = db.getConfig(QStringLiteral("j2j_node_id"), QString()).toString();
    if (!stored.isEmpty()) {
        m_nodeId = stored;
    } else {
        db.setConfig(QStringLiteral("j2j_node_id"), m_nodeId);
    }

    // Default node name from hostname
    m_nodeName = QStringLiteral("JARVIS-") + QSysInfo::machineHostName().left(12);
}

J2JMeshConnector::~J2JMeshConnector()
{
    stop();
}

void J2JMeshConnector::start(quint16 tcpPort, quint16 udpPort)
{
    if (m_running) return;

    m_tcpPort = tcpPort;
    m_udpPort = udpPort;

    // TCP server for command channel
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection,
            this, &J2JMeshConnector::onNewTcpConnection);

    if (!m_tcpServer->listen(QHostAddress::Any, m_tcpPort)) {
        // Port may be busy — try next 4 ports
        for (quint16 p = m_tcpPort + 1; p <= m_tcpPort + 4; ++p) {
            if (m_tcpServer->listen(QHostAddress::Any, p)) {
                m_tcpPort = p;
                break;
            }
        }
        if (!m_tcpServer->isListening()) {
            emit meshError(QStringLiteral("J2J: Cannot bind TCP port %1-%2")
                               .arg(tcpPort).arg(tcpPort + 4));
            delete m_tcpServer;
            m_tcpServer = nullptr;
            return;
        }
    }

    // UDP socket for beacon discovery
    m_udpSocket = new QUdpSocket(this);
    m_udpSocket->bind(m_udpPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udpSocket, &QUdpSocket::readyRead,
            this, &J2JMeshConnector::onReadBeacon);

    // Beacon timer — broadcast presence every 10s
    m_beaconTimer = new QTimer(this);
    connect(m_beaconTimer, &QTimer::timeout, this, &J2JMeshConnector::onBeaconTimer);
    m_beaconTimer->start(BEACON_INTERVAL_MS);

    // Peer cleanup timer — remove stale peers
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &J2JMeshConnector::onPeerCleanupTimer);
    m_cleanupTimer->start(CLEANUP_INTERVAL_MS);

    m_running = true;
    sendBeacon();

    qDebug() << "[J2J] Mesh started — TCP:" << m_tcpPort
             << "UDP:" << m_udpPort << "Node:" << m_nodeName;
}

void J2JMeshConnector::stop()
{
    if (!m_running) return;
    m_running = false;

    if (m_beaconTimer)  { m_beaconTimer->stop(); delete m_beaconTimer; m_beaconTimer = nullptr; }
    if (m_cleanupTimer) { m_cleanupTimer->stop(); delete m_cleanupTimer; m_cleanupTimer = nullptr; }

    for (auto* sock : m_socketBuffers.keys()) {
        sock->disconnectFromHost();
        sock->deleteLater();
    }
    m_socketBuffers.clear();

    if (m_udpSocket)  { m_udpSocket->close(); delete m_udpSocket; m_udpSocket = nullptr; }
    if (m_tcpServer)  { m_tcpServer->close(); delete m_tcpServer; m_tcpServer = nullptr; }

    m_peers.clear();
    qDebug() << "[J2J] Mesh stopped";
}

// ============================================================
//  UDP Beacon
// ============================================================

void J2JMeshConnector::sendBeacon()
{
    if (!m_udpSocket) return;

    QJsonObject beacon;
    beacon[QStringLiteral("proto")]    = QStringLiteral("JARVIS_NODE_ALIVE");
    beacon[QStringLiteral("nodeId")]   = m_nodeId;
    beacon[QStringLiteral("name")]     = m_nodeName;
    beacon[QStringLiteral("version")]  = QStringLiteral(JARVIS_VERSION);
    beacon[QStringLiteral("tcpPort")]  = m_tcpPort;
    beacon[QStringLiteral("role")]     = m_nodeRole;

    QByteArray data = QJsonDocument(beacon).toJson(QJsonDocument::Compact);

    // Broadcast to all network interfaces
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            QHostAddress broadcast = entry.broadcast();
            if (!broadcast.isNull()) {
                m_udpSocket->writeDatagram(data, broadcast, m_udpPort);
            }
        }
    }
}

void J2JMeshConnector::onBeaconTimer()
{
    sendBeacon();
}

void J2JMeshConnector::onReadBeacon()
{
    while (m_udpSocket && m_udpSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        m_udpSocket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        if (obj[QStringLiteral("proto")].toString() != QStringLiteral("JARVIS_NODE_ALIVE"))
            continue;

        const QString peerId = obj[QStringLiteral("nodeId")].toString();
        if (peerId.isEmpty() || peerId == m_nodeId) continue;

        const bool isNew = !m_peers.contains(peerId);

        J2JPeer& peer = m_peers[peerId];
        peer.nodeId   = peerId;
        peer.nodeName = obj[QStringLiteral("name")].toString();
        peer.version  = obj[QStringLiteral("version")].toString();
        peer.role     = obj[QStringLiteral("role")].toString(QStringLiteral("Developer"));
        peer.address  = sender;
        peer.tcpPort  = static_cast<quint16>(obj[QStringLiteral("tcpPort")].toInt());
        peer.lastSeen = QDateTime::currentDateTime();

        if (isNew) {
            qDebug() << "[J2J] Peer discovered:" << peer.nodeName
                     << "@" << sender.toString() << ":" << peer.tcpPort;
            emit peerDiscovered(peer.nodeName, sender.toString());
        }
    }
}

void J2JMeshConnector::onPeerCleanupTimer()
{
    const QDateTime now = QDateTime::currentDateTime();
    QStringList expired;
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (it->lastSeen.secsTo(now) > PEER_TIMEOUT_SEC)
            expired.append(it.key());
    }
    for (const auto& id : expired) {
        const QString name = m_peers[id].nodeName;
        m_peers.remove(id);
        qDebug() << "[J2J] Peer lost:" << name;
        emit peerLost(name);
    }
}

// ============================================================
//  TCP Command Channel
// ============================================================

void J2JMeshConnector::onNewTcpConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket* sock = m_tcpServer->nextPendingConnection();
        m_socketBuffers[sock] = QByteArray();
        connect(sock, &QTcpSocket::readyRead, this, &J2JMeshConnector::onTcpDataReady);
        connect(sock, &QTcpSocket::disconnected, this, &J2JMeshConnector::onTcpDisconnected);
        qDebug() << "[J2J] Incoming connection from" << sock->peerAddress().toString();
    }
}

void J2JMeshConnector::onTcpDataReady()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    m_socketBuffers[sock].append(sock->readAll());
    QByteArray& buf = m_socketBuffers[sock];

    if (buf.size() > MAX_PACKET_SIZE) {
        qDebug() << "[J2J] Packet too large, dropping connection";
        sock->disconnectFromHost();
        return;
    }

    // Newline-delimited JSON packets
    while (true) {
        int nlPos = buf.indexOf('\n');
        if (nlPos < 0) break;

        QByteArray line = buf.left(nlPos).trimmed();
        buf.remove(0, nlPos + 1);

        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;

        processPacket(sock, doc.object());
    }
}

void J2JMeshConnector::onTcpDisconnected()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    m_socketBuffers.remove(sock);
    sock->deleteLater();
}

// ============================================================
//  Packet Protocol
// ============================================================

void J2JMeshConnector::processPacket(QTcpSocket* socket, const QJsonObject& packet)
{
    if (!verifyToken(packet)) {
        sendReply(socket, QStringLiteral("Error"),
                  QJsonObject{{QStringLiteral("message"), QStringLiteral("Unauthorized")}}, false);
        qDebug() << "[J2J] Unauthorized packet from" << socket->peerAddress().toString();
        return;
    }

    const QString type = packet[QStringLiteral("type")].toString();

    if (type == QStringLiteral("Handshake"))
        handleHandshake(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("SyncKnowledge"))
        handleSyncKnowledge(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("DelegateTask"))
        handleDelegateTask(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("DelegateAsset"))
        handleDelegateAsset(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("RequestCache"))
        handleRequestCache(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("ProfileSync"))
        handleProfileSync(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("TgRelay"))
        handleTgRelay(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("TgBinding"))
        handleTgBinding(socket, packet[QStringLiteral("data")].toObject());
    else if (type == QStringLiteral("FaceSync"))
        handleFaceSync(socket, packet[QStringLiteral("data")].toObject());
    else
        sendReply(socket, QStringLiteral("Error"),
                  QJsonObject{{QStringLiteral("message"), QStringLiteral("Unknown type")}}, false);
}

void J2JMeshConnector::handleHandshake(QTcpSocket* socket, const QJsonObject& data)
{
    const QString peerId   = data[QStringLiteral("nodeId")].toString();
    const QString peerName = data[QStringLiteral("name")].toString();

    if (m_peers.contains(peerId))
        m_peers[peerId].authorized = true;

    // Build capabilities response
    const auto caps = SystemManifest::activeCapabilities();
    QJsonArray capsArr;
    for (const auto& c : caps) {
        capsArr.append(QJsonObject{
            {QStringLiteral("id"),    c.id},
            {QStringLiteral("label"), c.label}
        });
    }

    QJsonObject reply;
    reply[QStringLiteral("nodeId")]       = m_nodeId;
    reply[QStringLiteral("name")]         = m_nodeName;
    reply[QStringLiteral("version")]      = QStringLiteral(JARVIS_VERSION);
    reply[QStringLiteral("role")]         = m_nodeRole;
    reply[QStringLiteral("capabilities")] = capsArr;

    sendReply(socket, QStringLiteral("Handshake"), reply);

    qDebug() << "[J2J] Handshake completed with" << peerName << "[" << data[QStringLiteral("role")].toString() << "]";
    emit peerAuthorized(peerName, socket->peerAddress().toString());

    // Новый узел в мешe — делимся профилями известных лиц, чтобы его
    // камера сразу узнавала людей, обученных на этом ПК.
    broadcastFaceProfiles();
}

void J2JMeshConnector::handleSyncKnowledge(QTcpSocket* socket, const QJsonObject& data)
{
    const QString fromNode  = data[QStringLiteral("fromNode")].toString();
    const QString fromRole  = data[QStringLiteral("originRole")].toString(QStringLiteral("Developer"));
    const QJsonArray facts  = data[QStringLiteral("facts")].toArray();

    int imported = 0, linked = 0;

    for (const auto& f : facts) {
        QJsonObject fact = f.toObject();
        const QString category  = fact[QStringLiteral("category")].toString();
        const QString key       = fact[QStringLiteral("key")].toString();
        const QString value     = fact[QStringLiteral("value")].toString();
        const QString originTag = fact[QStringLiteral("origin")].toString(fromRole);

        if (key.isEmpty() || value.isEmpty()) continue;
        if (key.length() > 200 || value.length() > 500) continue;

        const QString fullKey = QStringLiteral("mesh_") + key;

        // Insert with origin profile role and dedup
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(R"(INSERT INTO knowledge_base
                     (user_id, category, key, value, confidence, origin_profile_role)
                     VALUES (1, :cat, :key, :val, 0.5, :origin)
                     ON CONFLICT(user_id, key) DO UPDATE SET
                         confidence = MIN(1.0, confidence + 0.05),
                         reinforcements = reinforcements + 1,
                         last_seen = datetime('now'))");
        q.bindValue(":cat",    category.left(30));
        q.bindValue(":key",    fullKey);
        q.bindValue(":val",    value);
        q.bindValue(":origin", originTag);
        if (q.exec()) ++imported;

        // Synaptic cross-linking: QA facts → Developer tasks
        if ((originTag == QStringLiteral("QA_Tester") || originTag == QStringLiteral("QA_Expertise"))
            && (category == QStringLiteral("tool") || category == QStringLiteral("workflow")
                || value.toLower().contains(QStringLiteral("bug"))
                || value.toLower().contains(QStringLiteral("test"))))
        {
            // Search for a matching task in the local Kanban board
            const QString valLower = value.toLower();
            auto tasks = DatabaseManager::instance().getTasks(1);
            for (const auto& task : tasks) {
                if (task.status == QStringLiteral("Done")) continue;
                const QString titleLo = task.title.toLower();
                // Match if the fact references a keyword from the task title
                bool match = false;
                const auto words = valLower.split(' ', Qt::SkipEmptyParts);
                for (const auto& w : words) {
                    if (w.length() >= 4 && titleLo.contains(w)) { match = true; break; }
                }
                if (!match) continue;

                // Establish structural link
                QSqlQuery linkQ(QSqlDatabase::database());
                linkQ.prepare(R"(UPDATE knowledge_base SET linked_artifact_id = :tid
                                 WHERE user_id = 1 AND key = :key AND linked_artifact_id IS NULL)");
                linkQ.bindValue(":tid", task.id);
                linkQ.bindValue(":key", fullKey);
                if (linkQ.exec() && linkQ.numRowsAffected() > 0) {
                    ++linked;
                    qDebug() << "[J2J Synapse]" << originTag << "fact linked to task:" << task.title;
                }
                break;
            }
        }
    }

    QJsonObject reply;
    reply[QStringLiteral("imported")] = imported;
    reply[QStringLiteral("linked")]   = linked;
    sendReply(socket, QStringLiteral("SyncKnowledge"), reply);

    qDebug() << "[J2J] Synced" << imported << "facts (" << linked << "cross-linked) from"
             << fromNode << "[" << fromRole << "]";
    if (imported > 0)
        emit knowledgeReceived(fromNode, imported);
}

void J2JMeshConnector::handleDelegateTask(QTcpSocket* socket, const QJsonObject& data)
{
    const QString fromNode = data[QStringLiteral("fromNode")].toString();
    const QJsonObject taskObj = data[QStringLiteral("task")].toObject();

    const QString title    = taskObj[QStringLiteral("title")].toString().trimmed();
    const QString category = taskObj[QStringLiteral("category")].toString();
    const QString priority = taskObj[QStringLiteral("priority")].toString();
    const QString deadline = taskObj[QStringLiteral("deadline")].toString();

    if (title.isEmpty() || title.length() > 200) {
        sendReply(socket, QStringLiteral("DelegateTask"),
                  QJsonObject{{QStringLiteral("message"), QStringLiteral("Invalid task")}}, false);
        return;
    }

    DbTask t;
    t.userId   = 1;
    t.title    = QStringLiteral("[%1] %2").arg(fromNode.left(15), title);
    t.category = category.isEmpty() ? QStringLiteral("General") : category;
    t.status   = QStringLiteral("Todo");
    t.priority = priority.isEmpty() ? QStringLiteral("Medium") : priority;
    if (!deadline.isEmpty())
        t.deadline = QDateTime::fromString(deadline, Qt::ISODate);

    qint64 id = DatabaseManager::instance().addTask(t);

    QJsonObject reply;
    reply[QStringLiteral("taskId")] = id;
    reply[QStringLiteral("accepted")] = (id > 0);
    sendReply(socket, QStringLiteral("DelegateTask"), reply);

    if (id > 0) {
        qDebug() << "[J2J] Task delegated from" << fromNode << ":" << title;
        emit taskReceived(fromNode, title);
    }
}

// ============================================================
//  Outgoing operations
// ============================================================

void J2JMeshConnector::broadcastKnowledge(const QJsonArray& facts)
{
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const J2JPeer& peer = it.value();
        auto* sock = new QTcpSocket(this);
        const QString token = authToken();
        const QString fromNode = m_nodeName;
        const QString fromRole = m_nodeRole;

        connect(sock, &QTcpSocket::connected, this,
                [sock, token, fromNode, fromRole, facts]() {
            QJsonObject packet;
            packet[QStringLiteral("token")] = token;
            packet[QStringLiteral("type")]  = QStringLiteral("SyncKnowledge");
            QJsonObject data;
            data[QStringLiteral("fromNode")]   = fromNode;
            data[QStringLiteral("originRole")] = fromRole;
            data[QStringLiteral("facts")]      = facts;
            packet[QStringLiteral("data")]     = data;
            sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + "\n");
            sock->flush();
            QTimer::singleShot(5000, sock, &QTcpSocket::deleteLater);
        });
        connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);

        sock->connectToHost(peer.address, peer.tcpPort);
    }
}

void J2JMeshConnector::delegateTask(const QString& peerId, const QJsonObject& task)
{
    if (!m_peers.contains(peerId)) return;
    const J2JPeer& peer = m_peers[peerId];

    auto* sock = new QTcpSocket(this);
    const QString token = authToken();
    const QString fromNode = m_nodeName;

    connect(sock, &QTcpSocket::connected, this,
            [sock, token, fromNode, task]() {
        QJsonObject packet;
        packet[QStringLiteral("token")] = token;
        packet[QStringLiteral("type")]  = QStringLiteral("DelegateTask");
        QJsonObject data;
        data[QStringLiteral("fromNode")] = fromNode;
        data[QStringLiteral("task")]     = task;
        packet[QStringLiteral("data")]   = data;
        sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + "\n");
        sock->flush();
        QTimer::singleShot(5000, sock, &QTcpSocket::deleteLater);
    });
    connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);

    sock->connectToHost(peer.address, peer.tcpPort);
}

// ============================================================
//  Face profiles (P2P sync)
// ============================================================

void J2JMeshConnector::broadcastFaceProfiles()
{
    const QJsonArray faces = FaceRegistry::instance().localFacesJson();
    if (faces.isEmpty()) return;

    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const J2JPeer& peer = it.value();
        auto* sock = new QTcpSocket(this);
        const QString token    = authToken();
        const QString fromNode = m_nodeId;

        connect(sock, &QTcpSocket::connected, this,
                [sock, token, fromNode, faces]() {
            QJsonObject packet;
            packet[QStringLiteral("token")] = token;
            packet[QStringLiteral("type")]  = QStringLiteral("FaceSync");
            QJsonObject data;
            data[QStringLiteral("fromNode")] = fromNode;
            data[QStringLiteral("faces")]    = faces;
            packet[QStringLiteral("data")]   = data;
            sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + "\n");
            sock->flush();
            QTimer::singleShot(5000, sock, &QTcpSocket::deleteLater);
        });
        connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);

        sock->connectToHost(peer.address, peer.tcpPort);
    }

    qDebug() << "[J2J] Broadcasting" << faces.size()
             << "face profile(s) to" << m_peers.size() << "peer(s)";
}

void J2JMeshConnector::handleFaceSync(QTcpSocket* socket, const QJsonObject& data)
{
    Q_UNUSED(socket)
    const QString fromNode  = data[QStringLiteral("fromNode")].toString();
    const QJsonArray faces  = data[QStringLiteral("faces")].toArray();
    if (faces.isEmpty()) return;

    const int imported = FaceRegistry::instance().importFromJson(faces, fromNode);
    if (imported > 0)
        emit faceProfilesReceived(fromNode, imported);
}

// ============================================================
//  Telegram multi-PC routing
// ============================================================

bool J2JMeshConnector::sendTgRelay(const QString& peerId, const QJsonObject& payload)
{
    if (!m_peers.contains(peerId)) return false;
    const J2JPeer& peer = m_peers[peerId];

    auto* sock = new QTcpSocket(this);
    const QString token    = authToken();
    const QString fromNode = m_nodeId;

    connect(sock, &QTcpSocket::connected, this,
            [sock, token, fromNode, payload]() {
        QJsonObject packet;
        packet[QStringLiteral("token")] = token;
        packet[QStringLiteral("type")]  = QStringLiteral("TgRelay");
        QJsonObject data = payload;
        data[QStringLiteral("fromNode")] = fromNode;
        packet[QStringLiteral("data")]   = data;
        sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + "\n");
        sock->flush();
        QTimer::singleShot(5000, sock, &QTcpSocket::deleteLater);
    });
    connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);

    sock->connectToHost(peer.address, peer.tcpPort);
    return true;
}

void J2JMeshConnector::broadcastTgBinding(qint64 chatId, const QString& deviceId,
                                          const QString& pcName)
{
    QJsonObject data;
    data[QStringLiteral("chatId")]   = static_cast<double>(chatId);
    data[QStringLiteral("deviceId")] = deviceId;
    data[QStringLiteral("pcName")]   = pcName;

    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const J2JPeer& peer = it.value();
        auto* sock = new QTcpSocket(this);
        const QString token = authToken();

        connect(sock, &QTcpSocket::connected, this,
                [sock, token, data]() {
            QJsonObject packet;
            packet[QStringLiteral("token")] = token;
            packet[QStringLiteral("type")]  = QStringLiteral("TgBinding");
            packet[QStringLiteral("data")]  = data;
            sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact) + "\n");
            sock->flush();
            QTimer::singleShot(5000, sock, &QTcpSocket::deleteLater);
        });
        connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);

        sock->connectToHost(peer.address, peer.tcpPort);
    }
}

void J2JMeshConnector::handleTgRelay(QTcpSocket* socket, const QJsonObject& data)
{
    Q_UNUSED(socket)
    if (m_telegramGw)
        m_telegramGw->onMeshRelay(data);
}

void J2JMeshConnector::handleTgBinding(QTcpSocket* socket, const QJsonObject& data)
{
    Q_UNUSED(socket)
    if (m_telegramGw)
        m_telegramGw->onMeshBinding(data);
}

// ============================================================
//  Auth & helpers
// ============================================================

QString J2JMeshConnector::authToken() const
{
    return DatabaseManager::instance()
        .getConfig(QStringLiteral("j2j_token"), QStringLiteral("jarvis_mesh_default"))
        .toString();
}

bool J2JMeshConnector::verifyToken(const QJsonObject& packet) const
{
    return packet[QStringLiteral("token")].toString() == authToken();
}

void J2JMeshConnector::sendReply(QTcpSocket* socket, const QString& type,
                                  const QJsonObject& payload, bool success)
{
    QJsonObject reply;
    reply[QStringLiteral("type")]    = type;
    reply[QStringLiteral("success")] = success;
    reply[QStringLiteral("data")]    = payload;
    reply[QStringLiteral("nodeId")]  = m_nodeId;
    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
    socket->flush();
}

QList<J2JPeer> J2JMeshConnector::activePeers() const
{
    return m_peers.values();
}

int J2JMeshConnector::peerCount() const
{
    return m_peers.size();
}

void J2JMeshConnector::initMobilePairing()
{
    if (m_mobilePairing) return;
    m_mobilePairing = new MobilePairingManager(m_nodeId, this);
    qDebug() << "[J2J] Mobile pairing coordinator initialized";
}

void J2JMeshConnector::initTelegramGateway()
{
    if (m_telegramGw) return;
    m_telegramGw = new J2JTelegramGateway(this);
    m_telegramGw->setMeshConnector(this);
    MediaRoutingManager::instance().setTelegramGateway(m_telegramGw);
    qDebug() << "[J2J] Telegram gateway initialized";
}

// ============================================================
//  P2P Knowledge Delegation — asset offload + cache transfer
// ============================================================

bool J2JMeshConnector::hasPrimaryStorage() const
{
    return MemoryConsolidation::instance().isExternalAvailable();
}

void J2JMeshConnector::delegateRawAsset(const QString& peerId,
                                          const QString& assetType,
                                          const QString& fileName,
                                          const QByteArray& data)
{
    if (!m_peers.contains(peerId)) return;
    const J2JPeer& peer = m_peers[peerId];

    auto* sock = new QTcpSocket(this);
    const QString token = authToken();
    const QString from  = m_nodeName;

    connect(sock, &QTcpSocket::connected, this,
            [sock, token, from, assetType, fileName, data]() {
        QJsonObject packet;
        packet[QStringLiteral("token")] = token;
        packet[QStringLiteral("type")]  = QStringLiteral("DelegateAsset");
        QJsonObject d;
        d[QStringLiteral("fromNode")]  = from;
        d[QStringLiteral("assetType")] = assetType;
        d[QStringLiteral("fileName")]  = fileName;
        d[QStringLiteral("dataB64")]   = QString::fromLatin1(data.toBase64());
        packet[QStringLiteral("data")] = d;
        sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        sock->flush();
        sock->disconnectFromHost();
        sock->deleteLater();
    });

    connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);
    sock->connectToHost(peer.address, peer.tcpPort);
    emit assetDelegated(peer.nodeName, assetType);
}

void J2JMeshConnector::handleDelegateAsset(QTcpSocket* socket,
                                             const QJsonObject& data)
{
    const QString fromNode  = data[QStringLiteral("fromNode")].toString();
    const QString assetType = data[QStringLiteral("assetType")].toString();
    const QString fileName  = data[QStringLiteral("fileName")].toString();
    const QByteArray rawData = QByteArray::fromBase64(
        data[QStringLiteral("dataB64")].toString().toLatin1());

    if (fileName.isEmpty() || rawData.isEmpty()) {
        sendReply(socket, QStringLiteral("DelegateAsset"),
                  QJsonObject{{QStringLiteral("message"),
                               QStringLiteral("Empty asset")}}, false);
        return;
    }

    // Store on our external drive (Tier 1)
    auto& mc = MemoryConsolidation::instance();
    const bool stored = mc.storeRawAsset(assetType, fileName, rawData);

    sendReply(socket, QStringLiteral("DelegateAsset"),
              QJsonObject{
                  {QStringLiteral("stored"), stored},
                  {QStringLiteral("fileName"), fileName},
              }, stored);

    if (stored)
        qDebug() << "[J2J] Stored delegated asset from" << fromNode
                 << ":" << fileName << "(" << rawData.size() / 1024 << "KB)";
}

void J2JMeshConnector::requestDistilledCache(const QString& peerId)
{
    if (!m_peers.contains(peerId)) return;
    const J2JPeer& peer = m_peers[peerId];

    auto* sock = new QTcpSocket(this);
    const QString token = authToken();
    const QString from  = m_nodeName;

    connect(sock, &QTcpSocket::connected, this,
            [sock, token, from]() {
        QJsonObject packet;
        packet[QStringLiteral("token")] = token;
        packet[QStringLiteral("type")]  = QStringLiteral("RequestCache");
        QJsonObject d;
        d[QStringLiteral("fromNode")] = from;
        packet[QStringLiteral("data")] = d;
        sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        sock->flush();
    });

    // Handle response with distilled entries
    connect(sock, &QTcpSocket::readyRead, this,
            [this, sock]() {
        const QByteArray raw = sock->readAll();
        const QJsonObject resp = QJsonDocument::fromJson(raw).object();
        const QJsonArray entries = resp[QStringLiteral("entries")].toArray();

        auto& mc = MemoryConsolidation::instance();
        int imported = 0;
        for (const auto& val : entries) {
            QJsonObject e = val.toObject();
            ConsolidationEntry ce;
            ce.sourceFile      = e[QStringLiteral("source")].toString();
            ce.assetType       = e[QStringLiteral("type")].toString();
            ce.summary         = e[QStringLiteral("summary")].toString();
            ce.tokens          = e[QStringLiteral("tokens")].toString()
                                     .split(QChar(','), Qt::SkipEmptyParts);
            ce.importance      = e[QStringLiteral("importance")].toDouble();
            if (mc.storeConsolidatedEntry(ce) >= 0) ++imported;
        }

        emit distilledCacheReceived(resp[QStringLiteral("fromNode")].toString(),
                                     imported);
        sock->disconnectFromHost();
        sock->deleteLater();
    });

    connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);
    sock->connectToHost(peer.address, peer.tcpPort);
}

void J2JMeshConnector::handleRequestCache(QTcpSocket* socket,
                                            const QJsonObject& data)
{
    Q_UNUSED(data)

    // Send our recent consolidated entries as lightweight Tier 2 cache
    auto& mc = MemoryConsolidation::instance();
    auto entries = mc.recentConsolidations(50);

    QJsonArray arr;
    for (const auto& e : entries) {
        QJsonObject obj;
        obj[QStringLiteral("source")]     = e.sourceFile;
        obj[QStringLiteral("type")]       = e.assetType;
        obj[QStringLiteral("summary")]    = e.summary;
        obj[QStringLiteral("tokens")]     = e.tokens.join(QChar(','));
        obj[QStringLiteral("importance")] = e.importance;
        arr.append(obj);
    }

    QJsonObject reply;
    reply[QStringLiteral("fromNode")] = m_nodeName;
    reply[QStringLiteral("entries")]  = arr;
    reply[QStringLiteral("success")]  = true;

    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
    socket->flush();

    qDebug() << "[J2J] Sent" << arr.size() << "distilled cache entries";
}

void J2JMeshConnector::syncUserProfile(const QString& peerId)
{
    if (!m_peers.contains(peerId)) return;
    const J2JPeer& peer = m_peers[peerId];

    auto& profile = UserProfileExtended::instance();
    const QJsonObject profileData = profile.toJson(profile.currentUserId());

    auto* sock = new QTcpSocket(this);
    const QString token = authToken();
    const QString from  = m_nodeName;
    const QString uid   = profile.currentUserId();

    connect(sock, &QTcpSocket::connected, this,
            [sock, token, from, uid, profileData]() {
        QJsonObject packet;
        packet[QStringLiteral("token")] = token;
        packet[QStringLiteral("type")]  = QStringLiteral("ProfileSync");
        QJsonObject d;
        d[QStringLiteral("fromNode")] = from;
        d[QStringLiteral("userId")]   = uid;
        d[QStringLiteral("profile")]  = profileData;
        packet[QStringLiteral("data")] = d;
        sock->write(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        sock->flush();
        sock->disconnectFromHost();
        sock->deleteLater();
    });

    connect(sock, &QTcpSocket::errorOccurred, sock, &QTcpSocket::deleteLater);
    sock->connectToHost(peer.address, peer.tcpPort);
    emit profileSynced(peer.nodeName);
}

void J2JMeshConnector::handleProfileSync(QTcpSocket* socket,
                                           const QJsonObject& data)
{
    const QString fromNode = data[QStringLiteral("fromNode")].toString();
    const QString userId   = data[QStringLiteral("userId")].toString();
    const QJsonObject prof = data[QStringLiteral("profile")].toObject();

    if (userId.isEmpty() || prof.isEmpty()) {
        sendReply(socket, QStringLiteral("ProfileSync"),
                  QJsonObject{{QStringLiteral("message"),
                               QStringLiteral("Empty profile")}}, false);
        return;
    }

    UserProfileExtended::instance().fromJson(userId, prof);

    sendReply(socket, QStringLiteral("ProfileSync"),
              QJsonObject{{QStringLiteral("imported"), true}}, true);

    qDebug() << "[J2J] Synced profile from" << fromNode
             << "userId:" << userId.left(8);
}
