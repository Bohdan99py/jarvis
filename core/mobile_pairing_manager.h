#pragma once
// ============================================================
// mobile_pairing_manager.h — Zero-Config Mobile Pairing Engine
//
// Provides seamless "WhatsApp Web-style" pairing for mobile
// devices without manual bot creation or token configuration.
//
// Architecture:
//   1. Dynamic PIN Generator — short-lived 6-char alphanumeric
//      codes tied to the local instance ID
//   2. Gateway Poller — async polling loop that detects when a
//      mobile device submits the PIN to the shared gateway
//   3. Wake-on-LAN Exporter — one-click generation of WoL
//      shortcut URLs for iOS Shortcuts / Android widgets
//
// Thread safety: all DB writes serialized via QMutex.
// Network I/O: fully async (QNetworkAccessManager).
// ============================================================

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QTimer>
#include <QMutex>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>

class QNetworkAccessManager;
class QNetworkReply;

// Represents a single paired mobile device
struct MobilePairedDevice {
    QString   deviceId;       // unique ID assigned on pairing
    QString   displayName;    // "Bohdan's iPhone", "Anna's Galaxy"
    QString   mobileHandle;   // opaque handle from gateway
    QString   boundRole;      // "Developer", "QA_Tester", etc.
    QString   platform;       // "ios" | "android" | "unknown"
    QDateTime pairedAt;
    QDateTime lastSeen;
    bool      active = true;
};

// Active pairing session (short-lived)
struct PairingSession {
    QString   pin;            // 6-char alphanumeric
    QString   instanceId;     // local J2J node ID
    QString   targetRole;     // role to bind on success
    QDateTime createdAt;
    QDateTime expiresAt;
    bool      consumed = false;
};

// Network interface snapshot for WoL
struct NetworkInterfaceInfo {
    QString interfaceName;
    QString macAddress;       // "AA:BB:CC:DD:EE:FF"
    QString ipv4Address;
    QString subnetMask;
    QString broadcastAddress;
    bool    isUp = false;
};

class MobilePairingManager : public QObject
{
    Q_OBJECT

public:
    explicit MobilePairingManager(const QString& instanceId,
                                  QObject* parent = nullptr);
    ~MobilePairingManager() override;

    // --- PIN Pairing Engine ---
    PairingSession generatePairingPin(const QString& targetRole = QStringLiteral("Developer"));
    PairingSession currentSession() const;
    bool           hasActiveSession() const;
    void           cancelSession();
    QString        buildDeepLinkUri(const PairingSession& session) const;

    // --- Gateway Polling ---
    void startGatewayPolling();
    void stopGatewayPolling();
    bool isPolling() const { return m_polling; }

    // --- Paired Devices ---
    QList<MobilePairedDevice> pairedDevices() const;
    bool removePairedDevice(const QString& deviceId);
    int  pairedDeviceCount() const;

    // --- Wake-on-LAN Exporter ---
    QList<NetworkInterfaceInfo> discoverNetworkInterfaces() const;
    QString generateWolShortcutUrl(const NetworkInterfaceInfo& iface) const;
    QString generateWolAppleShortcut(const NetworkInterfaceInfo& iface) const;
    QString generateWolAndroidIntent(const NetworkInterfaceInfo& iface) const;
    QString exportAllWolProfiles() const;

    // --- Configuration ---
    void setGatewayBaseUrl(const QString& url) { m_gatewayUrl = url; }
    QString gatewayBaseUrl() const { return m_gatewayUrl; }

    static constexpr int PIN_LENGTH        = 6;
    static constexpr int PIN_LIFETIME_SECS = 300;  // 5 minutes
    static constexpr int POLL_INTERVAL_MS  = 3000;  // 3 seconds
    static constexpr int MAX_PAIRED        = 10;

signals:
    void pairingSessionCreated(const QString& pin, const QString& deepLink);
    void pairingSessionExpired();
    void devicePaired(const QString& deviceName, const QString& boundRole);
    void deviceUnpaired(const QString& deviceId);
    void gatewayError(const QString& message);
    void wolProfileGenerated(const QString& profileData);

private slots:
    void onPollTimer();
    void onSessionExpired();

private:
    QString generatePin() const;
    void    persistPairedDevice(const MobilePairedDevice& device);
    void    loadPairedDevices();
    void    handleGatewayResponse(const QJsonObject& response);
    void    bindDeviceToRole(const QString& mobileHandle,
                             const QString& displayName,
                             const QString& platform,
                             const QString& role);

    QString m_instanceId;
    QString m_gatewayUrl;

    QNetworkAccessManager* m_network  = nullptr;
    QTimer*  m_pollTimer              = nullptr;
    QTimer*  m_sessionTimer           = nullptr;
    bool     m_polling                = false;

    PairingSession              m_currentSession;
    QList<MobilePairedDevice>   m_pairedDevices;

    mutable QMutex m_mutex;
};
