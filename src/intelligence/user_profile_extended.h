#pragma once
// ============================================================
// user_profile_extended.h — Multi-User Identity & Preferences
//
// Extends the behavioral UserProfile with persistent identity
// fields: nickname, biometrics, psycho-matrix, UI preferences.
// Thread-safe SQLite-backed API with Q_PROPERTY for UI binding.
//
// Each user is identified by a crypto-hash userId derived from
// system hostname + OS username (stable across sessions).
//
// Fields:
//   - nickname, avatar_path
//   - bioMetrics: active_hours_start/end, preferred_break_min
//   - psychoMatrix: focus_triggers, dev_style, ui_accent_color
//   - meshRole: primary/secondary (for P2P delegation)
// ============================================================

#include <QObject>
#include <QString>
#include <QVariant>
#include <QJsonObject>
#include <QMutex>
#include <QDateTime>

#include "jarvis_core_export.h"

// ============================================================
//  ProfileField — metadata about one profile property
// ============================================================

struct ProfileField
{
    QString   key;
    QVariant  value;
    double    confidence = 1.0;   // 0.0–1.0: how sure we are
    QDateTime lastUpdated;
    QString   source;             // "user", "inferred", "mesh_sync"

    bool isLowConfidence() const { return confidence < 0.75; }
    bool isEmpty() const { return !value.isValid() || value.toString().trimmed().isEmpty(); }
};

// ============================================================
//  UserProfileExtended
// ============================================================

class JARVIS_CORE_EXPORT UserProfileExtended : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString nickname     READ nickname     WRITE setNickname     NOTIFY profileChanged)
    Q_PROPERTY(QString devStyle     READ devStyle     WRITE setDevStyle     NOTIFY profileChanged)
    Q_PROPERTY(QString uiAccentColor READ uiAccentColor WRITE setUiAccentColor NOTIFY profileChanged)
    Q_PROPERTY(int activeHoursStart READ activeHoursStart WRITE setActiveHoursStart NOTIFY profileChanged)
    Q_PROPERTY(int activeHoursEnd   READ activeHoursEnd   WRITE setActiveHoursEnd   NOTIFY profileChanged)
    Q_PROPERTY(QString meshRole     READ meshRole     WRITE setMeshRole     NOTIFY profileChanged)

public:
    static UserProfileExtended& instance();

    UserProfileExtended(const UserProfileExtended&)            = delete;
    UserProfileExtended& operator=(const UserProfileExtended&) = delete;

    // --- User ID ---
    static QString generateUserId();
    QString currentUserId() const { return m_userId; }
    void setCurrentUserId(const QString& id);

    // --- Thread-safe field access ---
    void updateProfileField(const QString& userId, const QString& key,
                             const QVariant& value,
                             double confidence = 1.0,
                             const QString& source = QStringLiteral("user"));

    QVariant profileField(const QString& userId, const QString& key) const;
    ProfileField profileFieldFull(const QString& userId, const QString& key) const;

    // --- All fields for a user ---
    QList<ProfileField> allFields(const QString& userId) const;
    QList<ProfileField> lowConfidenceFields(const QString& userId) const;
    QStringList emptyRequiredFields(const QString& userId) const;

    // --- Q_PROPERTY accessors (current user) ---
    QString nickname() const;
    void setNickname(const QString& v);

    QString devStyle() const;
    void setDevStyle(const QString& v);

    QString uiAccentColor() const;
    void setUiAccentColor(const QString& v);

    int activeHoursStart() const;
    void setActiveHoursStart(int hour);

    int activeHoursEnd() const;
    void setActiveHoursEnd(int hour);

    QString meshRole() const;
    void setMeshRole(const QString& role);

    // --- Profile summary for LLM context ---
    QString buildIdentitySummary(const QString& userId) const;

    // --- Serialization for mesh sync ---
    QJsonObject toJson(const QString& userId) const;
    void fromJson(const QString& userId, const QJsonObject& obj);

    void ensureTable();

signals:
    void profileChanged(const QString& userId);
    void fieldNeedsCalibration(const QString& userId, const QString& key,
                                const QString& currentValue);

private:
    explicit UserProfileExtended(QObject* parent = nullptr);
    ~UserProfileExtended() override;

    mutable QMutex m_mutex;
    QString m_userId;

    static const QStringList& requiredFields();
};
