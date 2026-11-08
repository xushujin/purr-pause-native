#pragma once
#include <QImage>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QSize>
#include <atomic>

namespace purr {
// One decoder shared by all displays. A single latest-frame mailbox bounds memory
// even if the GUI cannot paint as fast as the video decoder.
class VideoPlayer : public QThread {
    Q_OBJECT
public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer() override;
    void play(const QString &path, bool loop);
    void stop();
    QImage frame() const;
    double duration() const { return durationSeconds_.load(); }
signals:
    void playbackReady(QSize size, double duration);
    void frameAvailable();
    void playbackFinished();
    void failed(const QString &reason);
protected:
    void run() override;
private:
    QString path_;
    bool loop_ = false;
    mutable QMutex mutex_;
    QImage latest_;
    std::atomic<double> durationSeconds_{3.0};
    std::atomic<bool> framePending_{false};
    std::atomic<quint64> generation_{0};
};
}
