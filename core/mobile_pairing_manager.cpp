// ============================================================
// mobile_pairing_manager.cpp — Zero-Config Mobile Pairing Engine
// ============================================================

#include "mobile_pairing_manager.h"
#include "database_manager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QDebug>

// ============================================================
//  Construction
// ============================================================

MobilePairingManager::MobilePairingManager(const QString& instanceId,
                                           QObject* parent)
    : QObject(parent)
    , m_instanceId(instanceId)
    , m_gatewayUrl(QStringLiteral("https://jarvis-gateway.local/api/v1/pair"))
{
    m_network = new QNetworkAccessManager(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &MobilePairingManager::onPollTimer);

    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setSingleShot(true);
    connect(m_sessionTimer, &QTimer::timeout, this, &MobilePairingManager::onSessionExpired);

    // Ensure the paired_devices table exists
    {
        QMutexLocker lock(&m_mutex);
        QSqlQuery q(QSqlDatabase::database());
        q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS paired_devices ("
            "  device_id      TEXT PRIMARY KEY,"
            "  display_name   TEXT NOT NULL,"
            "  mobile_handle  TEXT,"
            "  bound_role     TEXT DEFAULT 'Developer',"
            "  platform       TEXT DEFAULT 'unknown',"
            "  paired_at      TEXT DEFAULT (datetime('now')),"
            "  last_seen      TEXT DEFAULT (datetime('now')),"
            "  active         INTEGER DEFAULT 1"
            ")"));
    }

    loadPairedDevices();

    qDebug() << "[MobilePairing] Initialized for instance:" << m_instanceId
             << "| Paired devices:" << m_pairedDevices.size();
}

MobilePairingManager::~MobilePairingManager()
{
    stopGatewayPolling();
}

// ============================================================
//  PIN Generation Engine
// ============================================================

QString MobilePairingManager::generatePin() const
{
    static const char alphanum[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZ"   // no I/O to avoid confusion
        "23456789";                    // no 0/1 to avoid confusion
    constexpr int poolSize = sizeof(alphanum) - 1;

    QString pin;
    pin.reserve(PIN_LENGTH);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < PIN_LENGTH; ++i)
        pin.append(QLatin1Char(alphanum[rng->bounded(poolSize)]));
    return pin;
}

PairingSession MobilePairingManager::generatePairingPin(const QString& targetRole)
{
    QMutexLocker lock(&m_mutex);

    m_currentSession.pin        = generatePin();
    m_currentSession.instanceId = m_instanceId;
    m_currentSession.targetRole = targetRole;
    m_currentSession.createdAt  = QDateTime::currentDateTimeUtc();
    m_currentSession.expiresAt  = m_currentSession.createdAt.addSecs(PIN_LIFETIME_SECS);
    m_currentSession.consumed   = false;

    // Persist to settings for crash recovery
    auto& db = DatabaseManager::instance();
    db.setConfig(QStringLiteral("mobile_pairing_pin"),        m_currentSession.pin);
    db.setConfig(QStringLiteral("mobile_pairing_role"),       m_currentSession.targetRole);
    db.setConfig(QStringLiteral("mobile_pairing_expires"),    m_currentSession.expiresAt.toString(Qt::ISODate));

    // Start expiration timer
    int remainMs = static_cast<int>(QDateTime::currentDateTimeUtc().msecsTo(m_currentSession.expiresAt));
    m_sessionTimer->start(qMax(remainMs, 1000));

    QString deepLink = buildDeepLinkUri(m_currentSession);

    qDebug() << "[MobilePairing] PIN generated:" << m_currentSession.pin
             << "expires:" << m_currentSession.expiresAt.toString(Qt::ISODate)
             << "role:" << targetRole;

    lock.unlock();
    emit pairingSessionCreated(m_currentSession.pin, deepLink);

    return m_currentSession;
}

PairingSession MobilePairingManager::currentSession() const
{
    QMutexLocker lock(&m_mutex);
    return m_currentSession;
}

bool MobilePairingManager::hasActiveSession() const
{
    QMutexLocker lock(&m_mutex);
    if (m_currentSession.pin.isEmpty() || m_currentSession.consumed)
        return false;
    return QDateTime::currentDateTimeUtc() < m_currentSession.expiresAt;
}

void MobilePairingManager::cancelSession()
{
    QMutexLocker lock(&m_mutex);
    m_currentSession = PairingSession();
    m_sessionTimer->stop();
    stopGatewayPolling();

    auto& db = DatabaseManager::instance();
    db.deleteConfig(QStringLiteral("mobile_pairing_pin"));
    db.deleteConfig(QStringLiteral("mobile_pairing_role"));
    db.deleteConfig(QStringLiteral("mobile_pairing_expires"));

    qDebug() << "[MobilePairing] Session cancelled";
}

QString MobilePairingManager::buildDeepLinkUri(const PairingSession& session) const
{
    // jarvis://pair?pin=ABC123&node=xxxxxxxxxxxx&role=Developer
    QUrl url;
    url.setScheme(QStringLiteral("jarvis"));
    url.setHost(QStringLiteral("pair"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("pin"),  session.pin);
    query.addQueryItem(QStringLiteral("node"), session.instanceId);
    query.addQueryItem(QStringLiteral("role"), session.targetRole);
    url.setQuery(query);

    return url.toString();
}

// ============================================================
//  Gateway Polling
// ============================================================

void MobilePairingManager::startGatewayPolling()
{
    if (m_polling) return;
    if (!hasActiveSession()) return;

    m_polling = true;
    m_pollTimer->start();
    qDebug() << "[MobilePairing] Gateway polling started (every"
             << POLL_INTERVAL_MS << "ms)";
}

void MobilePairingManager::stopGatewayPolling()
{
    if (!m_polling) return;
    m_polling = false;
    m_pollTimer->stop();
    qDebug() << "[MobilePairing] Gateway polling stopped";
}

void MobilePairingManager::onPollTimer()
{
    if (!hasActiveSession()) {
        stopGatewayPolling();
        return;
    }

    QMutexLocker lock(&m_mutex);
    const QString pin = m_currentSession.pin;
    const QString node = m_currentSession.instanceId;
    lock.unlock();

    // Build poll request
    QUrl url(m_gatewayUrl + QStringLiteral("/poll"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("pin"), pin);
    query.addQueryItem(QStringLiteral("node"), node);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setTransferTimeout(5000);

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Silent — gateway may not be reachable yet; that's fine
            // for local-first architecture. We log at debug level only.
            qDebug() << "[MobilePairing] Gateway poll:" << reply->errorString();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;

        QJsonObject obj = doc.object();
        if (obj[QStringLiteral("status")].toString() == QStringLiteral("paired")) {
            handleGatewayResponse(obj);
        }
    });
}

void MobilePairingManager::handleGatewayResponse(const QJsonObject& response)
{
    const QString mobileHandle = response[QStringLiteral("mobile_handle")].toString();
    const QString displayName  = response[QStringLiteral("device_name")].toString(
                                     QStringLiteral("Mobile Device"));
    const QString platform     = response[QStringLiteral("platform")].toString(
                                     QStringLiteral("unknown"));

    if (mobileHandle.isEmpty()) {
        qDebug() << "[MobilePairing] Gateway response missing mobile_handle";
        return;
    }

    QMutexLocker lock(&m_mutex);
    const QString role = m_currentSession.targetRole;
    m_currentSession.consumed = true;
    lock.unlock();

    bindDeviceToRole(mobileHandle, displayName, platform, role);
    stopGatewayPolling();
}

void MobilePairingManager::onSessionExpired()
{
    QMutexLocker lock(&m_mutex);
    if (!m_currentSession.consumed && !m_currentSession.pin.isEmpty()) {
        qDebug() << "[MobilePairing] Session expired for PIN:" << m_currentSession.pin;
        m_currentSession = PairingSession();
        lock.unlock();
        stopGatewayPolling();
        emit pairingSessionExpired();
    }
}

// ============================================================
//  Device Binding
// ============================================================

void MobilePairingManager::bindDeviceToRole(const QString& mobileHandle,
                                             const QString& displayName,
                                             const QString& platform,
                                             const QString& role)
{
    MobilePairedDevice device;
    device.deviceId     = QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
    device.displayName  = displayName;
    device.mobileHandle = mobileHandle;
    device.boundRole    = role;
    device.platform     = platform;
    device.pairedAt     = QDateTime::currentDateTimeUtc();
    device.lastSeen     = device.pairedAt;
    device.active       = true;

    {
        QMutexLocker lock(&m_mutex);
        m_pairedDevices.append(device);
    }

    persistPairedDevice(device);

    qDebug() << "[MobilePairing] Device paired:" << displayName
             << "(" << platform << ") → role:" << role;

    emit devicePaired(displayName, role);
}

void MobilePairingManager::persistPairedDevice(const MobilePairedDevice& device)
{
    QMutexLocker lock(&m_mutex);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO paired_devices "
        "(device_id, display_name, mobile_handle, bound_role, platform, paired_at, last_seen, active) "
        "VALUES (:id, :name, :handle, :role, :platform, :paired, :seen, :active)"));
    q.bindValue(QStringLiteral(":id"),       device.deviceId);
    q.bindValue(QStringLiteral(":name"),     device.displayName);
    q.bindValue(QStringLiteral(":handle"),   device.mobileHandle);
    q.bindValue(QStringLiteral(":role"),     device.boundRole);
    q.bindValue(QStringLiteral(":platform"), device.platform);
    q.bindValue(QStringLiteral(":paired"),   device.pairedAt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":seen"),     device.lastSeen.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":active"),   device.active ? 1 : 0);

    if (!q.exec())
        qWarning() << "[MobilePairing] Failed to persist device:" << q.lastError().text();
}

void MobilePairingManager::loadPairedDevices()
{
    QMutexLocker lock(&m_mutex);
    m_pairedDevices.clear();

    QSqlQuery q(QSqlDatabase::database());
    q.exec(QStringLiteral("SELECT * FROM paired_devices WHERE active = 1 ORDER BY paired_at DESC"));

    while (q.next()) {
        MobilePairedDevice d;
        d.deviceId     = q.value(QStringLiteral("device_id")).toString();
        d.displayName  = q.value(QStringLiteral("display_name")).toString();
        d.mobileHandle = q.value(QStringLiteral("mobile_handle")).toString();
        d.boundRole    = q.value(QStringLiteral("bound_role")).toString();
        d.platform     = q.value(QStringLiteral("platform")).toString();
        d.pairedAt     = QDateTime::fromString(q.value(QStringLiteral("paired_at")).toString(), Qt::ISODate);
        d.lastSeen     = QDateTime::fromString(q.value(QStringLiteral("last_seen")).toString(), Qt::ISODate);
        d.active       = q.value(QStringLiteral("active")).toBool();
        m_pairedDevices.append(d);
    }
}

QList<MobilePairedDevice> MobilePairingManager::pairedDevices() const
{
    QMutexLocker lock(&m_mutex);
    return m_pairedDevices;
}

bool MobilePairingManager::removePairedDevice(const QString& deviceId)
{
    QMutexLocker lock(&m_mutex);

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral("UPDATE paired_devices SET active = 0 WHERE device_id = :id"));
    q.bindValue(QStringLiteral(":id"), deviceId);
    if (!q.exec()) return false;

    m_pairedDevices.erase(
        std::remove_if(m_pairedDevices.begin(), m_pairedDevices.end(),
                        [&](const MobilePairedDevice& d) { return d.deviceId == deviceId; }),
        m_pairedDevices.end());

    lock.unlock();
    emit deviceUnpaired(deviceId);

    qDebug() << "[MobilePairing] Device unpaired:" << deviceId;
    return true;
}

int MobilePairingManager::pairedDeviceCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_pairedDevices.size();
}

// ============================================================
//  Wake-on-LAN Exporter
// ============================================================

QList<NetworkInterfaceInfo> MobilePairingManager::discoverNetworkInterfaces() const
{
    QList<NetworkInterfaceInfo> result;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;

        const QString mac = iface.hardwareAddress();
        if (mac.isEmpty() || mac == QStringLiteral("00:00:00:00:00:00")) continue;

        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;

            NetworkInterfaceInfo info;
            info.interfaceName  = iface.humanReadableName();
            info.macAddress     = mac;
            info.ipv4Address    = entry.ip().toString();
            info.subnetMask     = entry.netmask().toString();
            info.broadcastAddress = entry.broadcast().toString();
            info.isUp           = true;
            result.append(info);
        }
    }

    qDebug() << "[MobilePairing] Discovered" << result.size() << "network interfaces for WoL";
    return result;
}

QString MobilePairingManager::generateWolShortcutUrl(const NetworkInterfaceInfo& iface) const
{
    // Universal WoL URL scheme understood by most WoL apps
    // wol://MAC@BROADCAST:9
    const QString cleanMac = QString(iface.macAddress).remove(QLatin1Char(':'));
    return QStringLiteral("wol://%1@%2:9").arg(cleanMac, iface.broadcastAddress);
}

QString MobilePairingManager::generateWolAppleShortcut(const NetworkInterfaceInfo& iface) const
{
    // Apple Shortcuts deep link that creates a WoL action
    // Uses the shortcuts:// URL scheme with pre-filled parameters
    QJsonObject shortcut;
    shortcut[QStringLiteral("name")] = QStringLiteral("Wake JARVIS (%1)").arg(iface.interfaceName);
    shortcut[QStringLiteral("mac")]  = iface.macAddress;
    shortcut[QStringLiteral("broadcast")] = iface.broadcastAddress;
    shortcut[QStringLiteral("port")] = 9;
    shortcut[QStringLiteral("ip")]   = iface.ipv4Address;

    // Build a data URI containing the WoL magic packet script
    // This can be opened on iOS to create a Shortcut
    QString script = QStringLiteral(
        "# Wake-on-LAN for JARVIS\n"
        "# Interface: %1\n"
        "# MAC: %2\n"
        "# Broadcast: %3\n"
        "# IP: %4\n\n"
        "# Import this into Apple Shortcuts or any WoL app:\n"
        "MAC_ADDRESS=%2\n"
        "BROADCAST=%3\n"
        "PORT=9\n"
    ).arg(iface.interfaceName, iface.macAddress,
          iface.broadcastAddress, iface.ipv4Address);

    return script;
}

QString MobilePairingManager::generateWolAndroidIntent(const NetworkInterfaceInfo& iface) const
{
    // Android intent URI for WoL apps
    // intent://wol?mac=XX:XX:XX:XX:XX:XX&broadcast=x.x.x.x&port=9#Intent;scheme=jarvis;end
    QUrl url;
    url.setScheme(QStringLiteral("intent"));
    url.setHost(QStringLiteral("wol"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mac"),       iface.macAddress);
    query.addQueryItem(QStringLiteral("broadcast"), iface.broadcastAddress);
    query.addQueryItem(QStringLiteral("port"),      QStringLiteral("9"));
    query.addQueryItem(QStringLiteral("name"),
                       QStringLiteral("JARVIS (%1)").arg(iface.interfaceName));
    url.setQuery(query);
    url.setFragment(QStringLiteral("Intent;scheme=jarvis;end"));

    return url.toString();
}

QString MobilePairingManager::exportAllWolProfiles() const
{
    const auto interfaces = discoverNetworkInterfaces();
    if (interfaces.isEmpty())
        return QStringLiteral("No active network interfaces found.");

    QString report;
    report += QStringLiteral("═══════════════════════════════════════\n");
    report += QStringLiteral("  JARVIS Wake-on-LAN Profiles\n");
    report += QStringLiteral("  Generated: %1\n").arg(
                  QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    report += QStringLiteral("═══════════════════════════════════════\n\n");

    for (const auto& iface : interfaces) {
        report += QStringLiteral("── %1 ──\n").arg(iface.interfaceName);
        report += QStringLiteral("  MAC:       %1\n").arg(iface.macAddress);
        report += QStringLiteral("  IP:        %1\n").arg(iface.ipv4Address);
        report += QStringLiteral("  Broadcast: %1\n").arg(iface.broadcastAddress);
        report += QStringLiteral("  Subnet:    %1\n").arg(iface.subnetMask);
        report += QStringLiteral("\n");
        report += QStringLiteral("  WoL URL:     %1\n").arg(generateWolShortcutUrl(iface));
        report += QStringLiteral("  Android:     %1\n").arg(generateWolAndroidIntent(iface));
        report += QStringLiteral("\n");
        report += generateWolAppleShortcut(iface);
        report += QStringLiteral("\n");
    }

    emit const_cast<MobilePairingManager*>(this)->wolProfileGenerated(report);
    return report;
}
