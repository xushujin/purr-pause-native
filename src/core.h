#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace purr {

class ConfigStore final : public QObject {
    Q_OBJECT
public:
    explicit ConfigStore(QString dataDir, QObject *parent = nullptr);
    QJsonObject data() const { return m_data; }
    QString directory() const { return m_directory; }
    bool save(const QJsonObject &changes, bool activated, QString *error = nullptr);
    static QJsonObject defaults();
signals:
    void changed(const QJsonObject &configuration);
private:
    QString m_directory;
    QJsonObject m_data;
};

class LicenseManager final {
public:
    explicit LicenseManager(QString dataDir);
    QJsonObject status(qint64 nowMs = 0);
    QString machineId() const { return m_machineId; }
    bool activate(const QString &serial, QString *error = nullptr, qint64 nowMs = 0);
    bool isActivated();
    bool canUse();
    static QJsonObject verifySerial(const QString &serial);
private:
    QJsonObject load() const;
    bool persist(QJsonObject data, QString *error = nullptr) const;
    QString installDate(qint64 nowMs) const;
    QString m_directory;
    QString m_machineId;
};

// The caller supplies a ten-second sample. Time spent asleep is never counted
// as activity; pause/lock deadlines use the supplied wall clock for testability.
class BreakTimer final : public QObject {
    Q_OBJECT
public:
    explicit BreakTimer(QObject *parent = nullptr);
    void configure(const QJsonObject &configuration);
    void tick(qint64 nowMs, qint64 idleMilliseconds, bool locked);
    void setLocked(bool locked, qint64 nowMs = 0);
    void pause(int minutes, qint64 nowMs = 0);
    void resume();
    bool startBreak(bool force = false, qint64 nowMs = 0);
    void finishBreak(qint64 nowMs = 0);
    bool snooze();
    void reset();
    void handleSuspend(qint64 nowMs = 0);
    void handleResume(qint64 nowMs = 0);
    bool breaking() const { return m_breaking; }
    bool paused() const { return m_paused; }
    int snoozeCount() const { return m_snoozeCount; }
    qint64 secondsUntilBreak() const;
    qint64 activeSeconds() const { return m_activeSeconds; }
signals:
    void breakStarted();
    void breakEnded();
    void changed();
private:
    qint64 m_thresholdSeconds = 45 * 60;
    qint64 m_activeSeconds = 0;
    qint64 m_snoozeThreshold = 0;
    qint64 m_pauseUntil = 0;
    qint64 m_lockedAt = 0;
    qint64 m_sleepStartedAt = 0;
    qint64 m_lastDismissTime = -1;
    int m_snoozeCount = 0;
    bool m_breaking = false;
    bool m_paused = false;
    bool m_locked = false;
    bool m_suspended = false;
};

} // namespace purr
