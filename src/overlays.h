#pragma once
#include "media.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QSvgRenderer>
#include <QTimer>
#include <QWidget>

class QScreen;
class QLabel;
class QPushButton;
namespace purr {
namespace animation {
struct CatPose { QRectF bounds; double opacity = 1; };
struct RocketPose { double y = 0, scale = .7, opacity = 1, boost = 1; };
double cubicBezier(double progress, double x1, double y1, double x2, double y2);
CatPose catPose(const QString &mode, QSizeF screen, double aspect, double progress);
RocketPose rocketPose(double milliseconds, double startY);
}
class CatWindow;
class BreakOverlay : public QObject {
    Q_OBJECT
public:
    explicit BreakOverlay(QObject *parent = nullptr);
    ~BreakOverlay() override;
    void start(QJsonObject config, const QString &walkPath, const QString &restPath, int snoozeCount);
    void stop();
    bool visible() const { return !windows_.isEmpty(); }
signals:
    void dismissed();
    void snoozed();
    void mediaError(const QString &message);
private:
    void syncScreens();
    void showRest();
    void update();
    void beginDismiss();
    VideoPlayer player_;
    QTimer ticker_;
    QElapsedTimer phaseClock_;
    QList<CatWindow *> windows_;
    QJsonObject config_;
    QString restPath_;
    QString phase_;
    QImage lastFrame_;
    int snoozeCount_ = 0;
    qint64 countdownEnd_ = 0;
    bool controlsShown_ = false;
    bool started_ = false;
    bool mediaReady_ = false;
    bool restFrameReady_ = false;
    bool fadeFallback_ = false;
    double walkAspect_ = 16.0/9;
};

class RocketWindow : public QWidget {
    Q_OBJECT
public:
    RocketWindow(const QString &svgPath, const QString &videoPath, int count, const QString &sourceName);
    int count() const { return count_; }
    QString sourceName() const { return sourceName_; }
signals:
    void finished();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QSvgRenderer svg_;
    VideoPlayer player_;
    QTimer ticker_;
    QElapsedTimer elapsed_;
    QImage badge_;
    QRectF badgeBounds_;
    QString sourceName_;
    int count_;
    bool mediaFailed_ = false;
};
}
