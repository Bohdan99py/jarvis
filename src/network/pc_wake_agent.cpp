// ============================================================
// pc_wake_agent.cpp — Two-Layer Communication: WoL + Cloud Relay
// ============================================================

#include "pc_wake_agent.h"
#include "database_manager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUdpSocket>
#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>

// ============================================================
//  Construction
// ============================================================

PcWakeAgent::PcWakeAgent(const QString& deviceId, QObject* parent)
    : QObject(parent)
    , m_deviceId(deviceId)
    , m_relayUrl(QStringLiteral("https://jarvis-relay.local/api/v1"))
{
    m_network = new QNetworkAccessManager(this);
    m_wolSocket = new QUdpSocket(this);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(HEARTBEAT_INTERVAL_MS);
    connect(m_heartbeatTimer, &QTimer::timeout,
            this, &PcWakeAgent::onHeartbeatTimer);

    // Ensure the pc_registry table exists locally
    {
        QSqlQuery q(QSqlDatabase::database());
        q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS pc_registry ("
            "  device_id         TEXT PRIMARY KEY,"
            "  mac_address       TEXT NOT NULL DEFAULT '',"
            "  broadcast_address TEXT NOT NULL DEFAULT '255.255.255.255',"
            "  pc_name           TEXT NOT NULL DEFAULT '',"
            "  status            TEXT NOT NULL DEFAULT 'UNKNOWN',"
            "  last_heartbeat    TEXT NOT NULL DEFAULT (datetime('now')),"
            "  wol_port          INTEGER NOT NULL DEFAULT 9"
            ")"));
    }

    loadLocalRegistration();

    qDebug() << "[PcWakeAgent] Initialized for device:" << m_deviceId;
}

PcWakeAgent::~PcWakeAgent()
{
    if (m_running)
        stop();
}

// ============================================================
//  Lifecycle
// ============================================================

void PcWakeAgent::start()
{
    if (m_running) return;
    m_running = true;

    // Discover our network identity
    m_localReg = discoverLocalNetwork();
    persistLocalRegistration(m_localReg);

    // Register ONLINE with the cloud relay
    registerOnline();

    // Start heartbeat loop
    m_heartbeatTimer->start();

    // Fetch any messages queued while we were asleep
    fetchQueuedMessages();

    qDebug() << "[PcWakeAgent] Started. MAC:" << m_localReg.macAddress
             << "Broadcast:" << m_localReg.broadcastAddress;
}

void PcWakeAgent::stop()
{
    if (!m_running) return;

    m_heartbeatTimer->stop();
    registerOffline();
    m_running = false;

    qDebug() << "[PcWakeAgent] Stopped.";
}

// ============================================================
//  Status Management
// ============================================================

PcStatus PcWakeAgent::currentStatus() const
{
    QMutexLocker lock(&m_mutex);
    return m_currentStatus;
}

void PcWakeAgent::reportStatus(PcStatus status)
{
    {
        QMutexLocker lock(&m_mutex);
        m_currentStatus = status;
    }

    // Update local DB
    {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral(
            "UPDATE pc_registry SET status = :st, last_heartbeat = datetime('now') "
            "WHERE device_id = :did"));
        q.bindValue(QStringLiteral(":st"),  pcStatusToString(status));
        q.bindValue(QStringLiteral(":did"), m_deviceId);
        q.exec();
    }

    // Notify cloud relay
    QJsonObject payload;
    payload[QStringLiteral("device_id")] = m_deviceId;
    payload[QStringLiteral("status")]    = pcStatusToString(status);
    payload[QStringLiteral("mac")]       = m_localReg.macAddress;
    payload[QStringLiteral("broadcast")] = m_localReg.broadcastAddress;
    payload[QStringLiteral("pc_name")]   = m_localReg.pcName;

    QNetworkRequest request(QUrl(m_relayUrl + QStringLiteral("/pc/status")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_relayApiKey.isEmpty())
        request.setRawHeader("X-API-Key", m_relayApiKey.toUtf8());
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, status]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[PcWakeAgent] Status report failed:"
                     << reply->errorString();
            return;
        }
        qDebug() << "[PcWakeAgent] Status reported:"
                 << pcStatusToString(status);
    });

    emit statusChanged(status);
}

void PcWakeAgent::registerOnline()
{
    reportStatus(PcStatus::Online);
}

void PcWakeAgent::registerOffline()
{
    reportStatus(PcStatus::Offline);
}

// ============================================================
//  Heartbeat
// ============================================================

void PcWakeAgent::onHeartbeatTimer()
{
    sendHeartbeat();
}

void PcWakeAgent::sendHeartbeat()
{
    QJsonObject payload;
    payload[QStringLiteral("device_id")] = m_deviceId;
    payload[QStringLiteral("status")]    = QStringLiteral("ONLINE");
    payload[QStringLiteral("mac")]       = m_localReg.macAddress;
    payload[QStringLiteral("pc_name")]   = m_localReg.pcName;

    QNetworkRequest request(QUrl(m_relayUrl + QStringLiteral("/pc/heartbeat")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_relayApiKey.isEmpty())
        request.setRawHeader("X-API-Key", m_relayApiKey.toUtf8());
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[PcWakeAgent] Heartbeat failed:"
                     << reply->errorString();
            emit heartbeatFailed(reply->errorString());
            return;
        }

        // Update local timestamp
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral(
            "UPDATE pc_registry SET last_heartbeat = datetime('now') "
            "WHERE device_id = :did"));
        q.bindValue(QStringLiteral(":did"), m_deviceId);
        q.exec();

        emit heartbeatSent();
    });
}

// ============================================================
//  Wake-on-LAN — Magic Packet
// ============================================================

QByteArray PcWakeAgent::buildMagicPacket(const QString& macAddress) const
{
    // Parse MAC address "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF"
    QString cleanMac = macAddress;
    cleanMac.remove(QLatin1Char(':'));
    cleanMac.remove(QLatin1Char('-'));

    if (cleanMac.length() != 12)
        return QByteArray();

    QByteArray macBytes = QByteArray::fromHex(cleanMac.toLatin1());
    if (macBytes.size() != 6)
        return QByteArray();

    // Magic Packet: 6 bytes of 0xFF, then the MAC repeated 16 times
    QByteArray packet;
    packet.reserve(MAGIC_PACKET_SIZE);
    packet.append(6, static_cast<char>(0xFF));
    for (int i = 0; i < 16; ++i)
        packet.append(macBytes);

    return packet;
}

bool PcWakeAgent::sendWolPacket(const QString& macAddress,
                                 const QString& broadcastAddress,
                                 int port)
{
    const QByteArray packet = buildMagicPacket(macAddress);
    if (packet.isEmpty()) {
        const QString err = QStringLiteral("Invalid MAC address: %1").arg(macAddress);
        qWarning() << "[PcWakeAgent]" << err;
        emit wolPacketFailed(err);
        return false;
    }

    // Enable broadcast on the socket
    m_wolSocket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);

    qint64 sent = m_wolSocket->writeDatagram(
        packet,
        QHostAddress(broadcastAddress),
        static_cast<quint16>(port));

    if (sent != MAGIC_PACKET_SIZE) {
        const QString err = QStringLiteral("WoL send failed: wrote %1/%2 bytes")
            .arg(sent).arg(MAGIC_PACKET_SIZE);
        qWarning() << "[PcWakeAgent]" << err;
        emit wolPacketFailed(err);
        return false;
    }

    qDebug() << "[PcWakeAgent] WoL Magic Packet sent to" << macAddress
             << "via" << broadcastAddress << ":" << port;
    emit wolPacketSent(macAddress);
    return true;
}

// ============================================================
//  Network Discovery
// ============================================================

PcRegistration PcWakeAgent::discoverLocalNetwork() const
{
    PcRegistration reg;
    reg.deviceId = m_deviceId;
    reg.pcName   = QStringLiteral("JARVIS-PC");
    reg.status   = PcStatus::Online;
    reg.lastHeartbeat = QDateTime::currentDateTimeUtc();
    reg.wolPort  = WOL_PORT_DEFAULT;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;

        const QString mac = iface.hardwareAddress();
        if (mac.isEmpty() || mac == QStringLiteral("00:00:00:00:00:00"))
            continue;

        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            reg.macAddress       = mac;
            reg.broadcastAddress = entry.broadcast().toString();

            qDebug() << "[PcWakeAgent] Discovered:"
                     << iface.humanReadableName()
                     << "MAC:" << mac
                     << "Broadcast:" << reg.broadcastAddress;
            return reg;
        }
    }

    qDebug() << "[PcWakeAgent] No suitable network interface found";
    return reg;
}

// ============================================================
//  Message Queue (from cloud relay)
// ============================================================

void PcWakeAgent::fetchQueuedMessages()
{
    QUrl url(m_relayUrl + QStringLiteral("/messages/pending"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("device_id"), m_deviceId);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_relayApiKey.isEmpty())
        request.setRawHeader("X-API-Key", m_relayApiKey.toUtf8());
    request.setTransferTimeout(10000);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[PcWakeAgent] Fetch queued messages failed:"
                     << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        QJsonArray messages = doc.object()[QStringLiteral("messages")].toArray();
        if (messages.isEmpty()) {
            qDebug() << "[PcWakeAgent] No queued messages.";
            return;
        }

        qDebug() << "[PcWakeAgent] Delivering" << messages.size()
                 << "queued message(s)";

        QJsonArray deliveredIds;
        for (const auto& msgVal : messages) {
            QJsonObject msg = msgVal.toObject();
            qint64 chatId  = msg[QStringLiteral("chat_id")].toVariant().toLongLong();
            QString text    = msg[QStringLiteral("text")].toString();
            QString msgId   = msg[QStringLiteral("id")].toString();

            emit queuedMessageReceived(chatId, text);
            deliveredIds.append(msgId);
        }

        // Acknowledge delivered messages
        QJsonObject ackPayload;
        ackPayload[QStringLiteral("device_id")]   = m_deviceId;
        ackPayload[QStringLiteral("message_ids")] = deliveredIds;

        QNetworkRequest ackRequest(
            QUrl(m_relayUrl + QStringLiteral("/messages/ack")));
        ackRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
        if (!m_relayApiKey.isEmpty())
            ackRequest.setRawHeader("X-API-Key", m_relayApiKey.toUtf8());

        QNetworkReply* ackReply = m_network->post(
            ackRequest,
            QJsonDocument(ackPayload).toJson(QJsonDocument::Compact));
        connect(ackReply, &QNetworkReply::finished, ackReply,
                &QNetworkReply::deleteLater);
    });
}

// ============================================================
//  Query All PC Status (admin view)
// ============================================================

void PcWakeAgent::queryAllPcStatus()
{
    QNetworkRequest request(QUrl(m_relayUrl + QStringLiteral("/pc/list")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_relayApiKey.isEmpty())
        request.setRawHeader("X-API-Key", m_relayApiKey.toUtf8());
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit relayError(reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        QList<PcRegistration> result;
        QJsonArray pcs = doc.object()[QStringLiteral("pcs")].toArray();
        for (const auto& pcVal : pcs) {
            QJsonObject obj = pcVal.toObject();
            PcRegistration reg;
            reg.deviceId         = obj[QStringLiteral("device_id")].toString();
            reg.macAddress       = obj[QStringLiteral("mac")].toString();
            reg.broadcastAddress = obj[QStringLiteral("broadcast")].toString();
            reg.pcName           = obj[QStringLiteral("pc_name")].toString();
            reg.status           = pcStatusFromString(
                obj[QStringLiteral("status")].toString());
            reg.lastHeartbeat    = QDateTime::fromString(
                obj[QStringLiteral("last_heartbeat")].toString(), Qt::ISODate);
            reg.wolPort          = obj[QStringLiteral("wol_port")].toInt(9);
            result.append(reg);
        }

        emit pcStatusQueryResult(result);
    });
}

// ============================================================
//  Local Persistence
// ============================================================

void PcWakeAgent::persistLocalRegistration(const PcRegistration& reg)
{
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO pc_registry "
        "  (device_id, mac_address, broadcast_address, pc_name, status, wol_port) "
        "VALUES (:did, :mac, :bc, :name, :st, :port) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "  mac_address       = excluded.mac_address, "
        "  broadcast_address = excluded.broadcast_address, "
        "  pc_name           = excluded.pc_name, "
        "  status            = excluded.status, "
        "  last_heartbeat    = datetime('now'), "
        "  wol_port          = excluded.wol_port"));
    q.bindValue(QStringLiteral(":did"),  reg.deviceId);
    q.bindValue(QStringLiteral(":mac"),  reg.macAddress);
    q.bindValue(QStringLiteral(":bc"),   reg.broadcastAddress);
    q.bindValue(QStringLiteral(":name"), reg.pcName);
    q.bindValue(QStringLiteral(":st"),   pcStatusToString(reg.status));
    q.bindValue(QStringLiteral(":port"), reg.wolPort);

    if (!q.exec())
        qWarning() << "[PcWakeAgent] Failed to persist registration:"
                   << q.lastError().text();
}

void PcWakeAgent::loadLocalRegistration()
{
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT mac_address, broadcast_address, pc_name, status, "
        "       last_heartbeat, wol_port "
        "FROM pc_registry WHERE device_id = :did"));
    q.bindValue(QStringLiteral(":did"), m_deviceId);

    if (q.exec() && q.next()) {
        m_localReg.deviceId         = m_deviceId;
        m_localReg.macAddress       = q.value(0).toString();
        m_localReg.broadcastAddress = q.value(1).toString();
        m_localReg.pcName           = q.value(2).toString();
        m_localReg.status           = pcStatusFromString(q.value(3).toString());
        m_localReg.lastHeartbeat    = QDateTime::fromString(
            q.value(4).toString(), Qt::ISODate);
        m_localReg.wolPort          = q.value(5).toInt();
    }
}
