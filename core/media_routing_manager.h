#pragma once
// ============================================================
// media_routing_manager.h — Dual-Route Media Dispatcher
//
// Singleton that bridges Telegram delivery and local preview.
// When media is routed, it is simultaneously:
//   1. Displayed locally in MediaPreviewWidget
//   2. Sent to the paired mobile device via Telegram Bot API
// ============================================================

#include <QObject>
#include <QString>

class MediaPreviewWidget;
class J2JTelegramGateway;

class MediaRoutingManager : public QObject
{
    Q_OBJECT

public:
    static MediaRoutingManager& instance();

    MediaRoutingManager(const MediaRoutingManager&)            = delete;
    MediaRoutingManager& operator=(const MediaRoutingManager&) = delete;

    void setTelegramGateway(J2JTelegramGateway* gw) { m_gateway = gw; }

    void routeMedia(const QString& filePath,
                    const QString& caption,
                    qint64 targetChatId);

    void previewImage(const QString& filePath);
    void previewImageBuffer(const QByteArray& data, const QString& title);

signals:
    void mediaRouted(const QString& filePath, const QString& mediaType);

private:
    explicit MediaRoutingManager(QObject* parent = nullptr);
    ~MediaRoutingManager() override;

    static QString detectMediaType(const QString& filePath);

    MediaPreviewWidget*   m_preview = nullptr;
    J2JTelegramGateway*   m_gateway = nullptr;
};
