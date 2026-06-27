// ============================================================
// media_routing_manager.cpp — Dual-Route Media Dispatcher
// ============================================================
#include "media_routing_manager.h"
#include "media_preview_widget.h"
#include "j2j_telegram_gateway.h"

#include <QFileInfo>
#include <QDebug>

// ============================================================
//  Singleton
// ============================================================

MediaRoutingManager& MediaRoutingManager::instance()
{
    static MediaRoutingManager inst;
    return inst;
}

MediaRoutingManager::MediaRoutingManager(QObject* parent)
    : QObject(parent)
{}

MediaRoutingManager::~MediaRoutingManager()
{
    delete m_preview;
    m_preview = nullptr;
}

// ============================================================
//  Media type detection
// ============================================================

QString MediaRoutingManager::detectMediaType(const QString& filePath)
{
    static const QStringList kImageExts = {
        QStringLiteral("png"),  QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("bmp"),
        QStringLiteral("gif"),  QStringLiteral("webp"),
        QStringLiteral("svg"),
    };
    static const QStringList kVideoExts = {
        QStringLiteral("mp4"),  QStringLiteral("mkv"),
        QStringLiteral("avi"),  QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("wmv"),
    };

    const QString ext = QFileInfo(filePath).suffix().toLower();

    if (kImageExts.contains(ext)) return QStringLiteral("image");
    if (kVideoExts.contains(ext)) return QStringLiteral("video");

    return QStringLiteral("document");
}

// ============================================================
//  Route: local preview + Telegram delivery
// ============================================================

void MediaRoutingManager::routeMedia(const QString& filePath,
                                      const QString& caption,
                                      qint64 targetChatId)
{
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[MediaRouter] File not found:" << filePath;
        return;
    }

    const QString mediaType = detectMediaType(filePath);

    // ── 1. Local route: display in preview window (images/videos only) ──
    if (mediaType != QStringLiteral("document")) {
        if (!m_preview)
            m_preview = new MediaPreviewWidget;
        m_preview->displayMedia(filePath, mediaType);
    }

    // ── 2. Remote route: send to Telegram ──────────────────
    if (m_gateway && targetChatId != 0) {
        if (mediaType == QStringLiteral("image")) {
            m_gateway->sendImageToMobile(targetChatId, filePath, caption);
        } else if (mediaType == QStringLiteral("video")) {
            m_gateway->sendVideoToMobile(targetChatId, filePath, caption);
        } else {
            m_gateway->sendDocumentToMobile(targetChatId, filePath, caption);
        }
    } else {
        qDebug() << "[MediaRouter] No gateway or chatId — local preview only";
    }

    emit mediaRouted(filePath, mediaType);
    qDebug() << "[MediaRouter] Routed" << mediaType
             << "to local + chat" << targetChatId;
}

// ============================================================
//  Local-only preview (no Telegram send)
// ============================================================

void MediaRoutingManager::previewImage(const QString& filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return;

    if (!m_preview)
        m_preview = new MediaPreviewWidget;

    m_preview->displayMedia(filePath, QStringLiteral("image"));
}

void MediaRoutingManager::previewImageBuffer(const QByteArray& data,
                                              const QString& title)
{
    if (data.isEmpty()) return;

    if (!m_preview)
        m_preview = new MediaPreviewWidget;

    m_preview->displayImageBuffer(data, title);
}
