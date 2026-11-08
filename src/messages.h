#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSharedPointer>
#include <functional>

class QNetworkReply;

namespace purr {

// Message contents and local dismissals deliberately live only in memory.
class MessageService final : public QObject {
    Q_OBJECT
public:
    explicit MessageService(QObject *parent = nullptr);
    ~MessageService() override;

    void setConfig(QJsonObject config);
    void setBreakVisible(bool visible);
    QJsonArray items() const;
    QJsonObject state() const;
    int unreadCount() const;
    void dismiss(QString sourceId, QString id);
    void refresh(bool silent = true);
    void testSource(QJsonObject source);

signals:
    void changed();
    void notifyRequested(int count, QString sourceName);
    void connectionTested(QString sourceId, bool ok, QString message, int count);

private:
    struct Source;
    struct FetchResult;
    using SourcePtr = QSharedPointer<Source>;
    using FetchCallback = std::function<void(FetchResult)>;

    bool enabled(const SourcePtr &source) const;
    void cancel(const SourcePtr &source, bool clearSnapshot);
    void schedule(const SourcePtr &source, int delayMs);
    void refreshSource(const SourcePtr &source, bool silent, bool scheduled);
    void applyItems(const SourcePtr &source, const QJsonObject &data, bool silent);
    void notify(int count, const QString &name);
    int intervalMs(const SourcePtr &source) const;
    QNetworkReply *fetch(QJsonObject source, int limit, FetchCallback callback, bool connectionTest = false);

    QNetworkAccessManager network_;
    QList<SourcePtr> sources_;
    bool enabled_ = false;
    bool breakVisible_ = false;
    int pollInterval_ = 60;
    int maxItems_ = 30;
    int pendingCount_ = 0;
    QString pendingName_;
    quint64 deferredGeneration_ = 0;
};

} // namespace purr
