#include "overlays.h"
#include "platform.h"
#include <QApplication>
#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QFile>
#include <QFontMetricsF>
#include <QEvent>
#include <QPainterPath>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace purr {
namespace animation {
double cubicBezier(double progress, double x1, double y1, double x2, double y2) {
    if (progress <= 0) return 0;
    if (progress >= 1) return 1;
    const auto coordinate = [](double t, double a, double b) {
        return 3*(1-t)*(1-t)*t*a + 3*(1-t)*t*t*b + t*t*t;
    };
    double low = 0, high = 1;
    for (int i=0; i<28; ++i) {
        const double mid = (low+high)/2;
        if (coordinate(mid,x1,x2) < progress) low=mid; else high=mid;
    }
    return coordinate((low+high)/2,y1,y2);
}
CatPose catPose(const QString &mode, QSizeF screen, double aspect, double progress) {
    if (!std::isfinite(aspect) || aspect <= 0) aspect = 16.0/9;
    const double baseW = 200 * aspect;
    const double finalScale = std::min({screen.height() * .85 / 200, screen.width() * .9 / baseW, 5.0});
    const double eased = 1 - std::pow(1 - std::clamp(progress,0.0,1.0),3);
    double scale = finalScale, cx = screen.width()/2, cy = screen.height()/2, opacity = 1;
    if (mode == QStringLiteral("walk-center") || mode == QStringLiteral("fade-center")) {
        scale = mode == QStringLiteral("fade-center") ? finalScale*(.05+.95*eased) : .05+(finalScale-.05)*eased;
        opacity = std::min(eased*2,1.0);
    } else {
        const double startX = screen.width()+220-baseW/2;
        cx = startX+(cx-startX)*eased;
        if (mode == QStringLiteral("walk-zoom")) {
            cy = screen.height()-148+(cy-(screen.height()-148))*eased;
            scale = .4+(finalScale-.4)*eased;
        }
    }
    return {QRectF(cx-baseW*scale/2,cy-100*scale,baseW*scale,200*scale),opacity};
}
RocketPose rocketPose(double milliseconds, double startY) {
    const double t = std::clamp(milliseconds/7000.0,0.0,1.0);
    const auto ease = [](double p) { return cubicBezier(p,.2,.82,.18,1); };
    const auto mix = [](double a,double b,double p) { return a+(b-a)*p; };
    RocketPose result;
    if (t<.08) {
        const double p=ease(t/.08);
        result.opacity=p; result.y=startY+mix(12,0,p); result.scale=mix(.66,.7,p);
    } else if (t<.82) result.y=mix(startY,0,ease((t-.08)/.74));
    else {
        const double p=ease((t-.82)/.18);
        result.opacity=1-p; result.y=mix(0,-16,p); result.scale=mix(.7,.68,p);
    }
    result.boost=t<.52 ? mix(.92,1.035,ease(t/.52)) : mix(1.035,.99,ease((t-.52)/.48));
    return result;
}
} // namespace animation

namespace {
// The original button transforms do not participate in flex layout. An effect
// preserves that behavior while the underlying control remains a native button.
class ControlEffect final : public QGraphicsDropShadowEffect {
public:
    ControlEffect(QWidget *button, bool green, bool interactive=true) : QGraphicsDropShadowEffect(button), button_(button), green_(green) {
        setOffset(0,interactive ? 4 : 2); setBlurRadius(interactive ? 20 : 8);
        setColor(green ? QColor(34,197,94,102) : QColor(0,0,0,interactive ? 77 : 178));
        if (interactive) button->installEventFilter(this);
        transition_.setDuration(200);
        connect(&transition_,&QVariantAnimation::valueChanged,this,[this](const QVariant &value) {
            const double p=value.toDouble();
            scale_=1+.08*p;
            if (green_) { setOffset(0,4+2*p); setBlurRadius(20+10*p); setColor(QColor(34,197,94,qRound(102+51*p))); }
            updateBoundingRect(); update();
        });
    }
    void setOpacity(double opacity) { opacity_=opacity; update(); }
    QRectF boundingRectFor(const QRectF &rect) const override {
        return QGraphicsDropShadowEffect::boundingRectFor(rect).adjusted(-12,-12,12,12);
    }
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        if (object==button_ && (event->type()==QEvent::Enter || event->type()==QEvent::Leave || event->type()==QEvent::EnabledChange)) {
            transition_.stop(); transition_.setStartValue((scale_-1)/.08);
            transition_.setEndValue(button_->isEnabled() && event->type()==QEvent::Enter ? 1.0 : 0.0);
            transition_.start();
        }
        return QGraphicsDropShadowEffect::eventFilter(object,event);
    }
    void draw(QPainter *painter) override {
        painter->save();
        painter->setOpacity(painter->opacity()*opacity_*(button_->isEnabled() ? 1 : .4));
        const auto center=sourceBoundingRect(Qt::LogicalCoordinates).center();
        painter->translate(center); painter->scale(scale_,scale_); painter->translate(-center);
        QGraphicsDropShadowEffect::draw(painter);
        painter->restore();
    }
private:
    QWidget *button_;
    bool green_;
    double scale_=1;
    double opacity_=1;
    QVariantAnimation transition_;
};
}

class CatWindow : public QWidget {
public:
    QPointer<QScreen> display;
    QWidget *controls;
    QLabel *countdown;
    QLabel *error;
    QPushButton *dismiss;
    QPushButton *snooze;
    QImage image;
    QRectF target;
    double opacity = 1;
    explicit CatWindow(QScreen *screen, int snoozes) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput), display(screen) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setObjectName(QStringLiteral("catOverlay"));
        setScreen(screen);
        setGeometry(screen->availableGeometry());
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 40);
        layout->addStretch();
        error = new QLabel(this);
        error->setAlignment(Qt::AlignCenter);
        error->setStyleSheet(QStringLiteral("color:white;background:rgba(0,0,0,160);border-radius:8px;padding:8px;"));
        error->hide(); layout->addWidget(error);
        controls = new QWidget(this);
        auto *row = new QHBoxLayout(controls);
        row->setContentsMargins(0,0,0,0);
        row->setSpacing(24); row->addStretch();
        countdown = new QLabel(controls);
        countdown->setObjectName(QStringLiteral("breakCountdown"));
        countdown->setTextFormat(Qt::PlainText);
        countdown->setStyleSheet(QStringLiteral("color:white;font-weight:600;background:transparent;"));
        auto countdownFont=countdown->font(); countdownFont.setPointSizeF(19.2*72/countdown->logicalDpiY()); countdown->setFont(countdownFont);
        countdown->setGraphicsEffect(new ControlEffect(countdown,false,false));
        row->addWidget(countdown);
        snooze = new QPushButton(snoozes >= 2 ? QStringLiteral("已用完") : QStringLiteral("再等 5 分钟（%1次）").arg(2-snoozes), controls);
        snooze->setObjectName(QStringLiteral("snoozeButton"));
        snooze->setEnabled(snoozes < 2);
        // Qt does not clamp an oversized CSS radius to half the button height.
        snooze->setStyleSheet(QStringLiteral("QPushButton{padding:12px 32px;background:rgba(0,0,0,102);color:white;border:2px solid rgba(255,255,255,204);border-radius:24px;font-size:16px;font-weight:600;} QPushButton:hover:enabled{background:rgba(0,0,0,128);}"));
        snooze->setCursor(Qt::PointingHandCursor);
        snooze->setFixedHeight(52);
        snooze->setGraphicsEffect(new ControlEffect(snooze,false));
        row->addWidget(snooze);
        dismiss = new QPushButton(QStringLiteral("精力充沛！"), controls);
        dismiss->setObjectName(QStringLiteral("dismissButton"));
        dismiss->setStyleSheet(QStringLiteral("QPushButton{padding:12px 32px;background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4ade80,stop:1 #22c55e);color:white;border:0;border-radius:24px;font-size:18px;font-weight:700;}"));
        auto dismissFont=dismiss->font(); dismissFont.setLetterSpacing(QFont::AbsoluteSpacing,.5); dismiss->setFont(dismissFont);
        dismiss->setCursor(Qt::PointingHandCursor);
        dismiss->setFixedHeight(49);
        dismiss->setGraphicsEffect(new ControlEffect(dismiss,true));
        row->addWidget(dismiss); row->addStretch();
        layout->addWidget(controls); controls->hide();
        show(); platform::prepareOverlay(this);
    }
    void enableControls() {
        if (controls->isVisible()) return;
        setControlsOpacity(0);
        controls->show();
    }
    void setControlsOpacity(double opacity) {
        for (auto *widget : {static_cast<QWidget *>(countdown),static_cast<QWidget *>(snooze),static_cast<QWidget *>(dismiss)})
            static_cast<ControlEffect *>(widget->graphicsEffect())->setOpacity(opacity);
    }
    void enableInput() {
        if (!windowFlags().testFlag(Qt::WindowTransparentForInput)) return;
        setWindowFlags(windowFlags() & ~Qt::WindowTransparentForInput & ~Qt::WindowDoesNotAcceptFocus);
        show(); platform::prepareOverlay(this);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform); p.setOpacity(opacity);
        if (!image.isNull()) {
            // A video is a replaced element with object-fit: contain in Chromium.
            // The idle element keeps the walk rectangle, but differently shaped
            // custom media is letterboxed inside it rather than stretched.
            const auto fitted=QSizeF(image.size()).scaled(target.size(),Qt::KeepAspectRatio);
            p.drawImage(QRectF(target.center()-QPointF(fitted.width()/2,fitted.height()/2),fitted),image);
        }
    }
};

BreakOverlay::BreakOverlay(QObject *parent) : QObject(parent), player_(this) {
    ticker_.setInterval(16);
    connect(&ticker_, &QTimer::timeout, this, &BreakOverlay::update);
    connect(&player_, &VideoPlayer::playbackReady, this, [this](QSize size, double) {
        if (!started_) return;
        if (phase_==QStringLiteral("walk")) {
            mediaReady_=true;
            walkAspect_=double(size.width())/std::max(1,size.height());
            phaseClock_.restart();
        } else if (phase_==QStringLiteral("fade") && !fadeFallback_) {
            walkAspect_=double(size.width())/std::max(1,size.height());
        }
    });
    connect(&player_, &VideoPlayer::frameAvailable, this, [this] {
        if (started_ && phase_==QStringLiteral("rest") && !restFrameReady_) {
            restFrameReady_=true;
            phaseClock_.restart();
        } else if (started_ && phase_==QStringLiteral("fade") && !mediaReady_) {
            // fade-center starts on canplay, whereas walk modes start at metadata.
            mediaReady_=true;
            phaseClock_.restart();
        }
    });
    connect(&player_, &VideoPlayer::playbackFinished, this, [this] { if (phase_ == QStringLiteral("walk")) showRest(); });
    connect(&player_, &VideoPlayer::failed, this, [this](const QString &message) {
        if (!started_) return;
        emit mediaError(message);
        if (phase_ == QStringLiteral("walk")) showRest();
        else { for (auto *w : windows_) { w->error->setText(message + QStringLiteral("。仍可使用休息倒计时。")); w->error->show(); } }
    });
    connect(qApp, &QGuiApplication::screenAdded, this, [this] { if (started_) syncScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { if (started_) syncScreens(); });
}
BreakOverlay::~BreakOverlay() { stop(); }
void BreakOverlay::start(QJsonObject config, const QString &walkPath, const QString &restPath, int count) {
    stop(); config_ = config; restPath_ = restPath; snoozeCount_ = count; started_ = true;
    controlsShown_ = false; countdownEnd_ = 0;
    mediaReady_=false; restFrameReady_=false; fadeFallback_=false; walkAspect_=16.0/9;
    phase_ = config_.value(QStringLiteral("animationMode")) == QStringLiteral("fade-center") ? QStringLiteral("fade") : QStringLiteral("walk");
    phaseClock_.start(); syncScreens();
    player_.play(phase_ == QStringLiteral("fade") ? restPath : walkPath, phase_ == QStringLiteral("fade"));
    ticker_.start();
}
void BreakOverlay::stop() {
    started_ = false; ticker_.stop(); player_.stop();
    qDeleteAll(windows_); windows_.clear(); lastFrame_ = {}; phase_.clear();
}
void BreakOverlay::syncScreens() {
    const auto screens = qApp->screens();
    for (int i = windows_.size() - 1; i >= 0; --i) {
        if (!windows_[i]->display || !screens.contains(windows_[i]->display)) delete windows_.takeAt(i);
    }
    for (auto *screen : screens) {
        auto found = std::find_if(windows_.cbegin(), windows_.cend(), [screen](CatWindow *w) { return w->display == screen; });
        if (found != windows_.cend()) continue;
        auto *w = new CatWindow(screen, snoozeCount_); windows_.append(w);
        connect(screen, &QScreen::availableGeometryChanged, w, [w](const QRect &rect) { w->setGeometry(rect); });
        connect(w->dismiss, &QPushButton::clicked, this, &BreakOverlay::beginDismiss);
        connect(w->snooze, &QPushButton::clicked, this, [this] { if (snoozeCount_ < 2) emit snoozed(); });
        if (controlsShown_) w->enableControls();
    }
}
void BreakOverlay::showRest() {
    if (!started_ || phase_ != QStringLiteral("walk")) return;
    lastFrame_ = player_.frame();
    phase_ = QStringLiteral("rest"); phaseClock_.restart();
    restFrameReady_=false;
    player_.play(restPath_, true);
}
void BreakOverlay::beginDismiss() {
    if (!started_ || phase_ == QStringLiteral("dismiss")) return;
    phase_ = QStringLiteral("dismiss"); phaseClock_.restart();
    for (auto *w : windows_) { w->snooze->setAttribute(Qt::WA_TransparentForMouseEvents); w->dismiss->setAttribute(Qt::WA_TransparentForMouseEvents); }
}
void BreakOverlay::update() {
    if (!started_) return;
    qint64 elapsed = phaseClock_.elapsed();
    QImage frame = player_.frame();
    if (!frame.isNull()) lastFrame_ = frame;
    if (phase_ == QStringLiteral("walk") && !mediaReady_ && elapsed >= 5000) { showRest(); elapsed = 0; }
    if (phase_ == QStringLiteral("fade") && !mediaReady_ && elapsed >= 3000) {
        mediaReady_=true; fadeFallback_=true; phaseClock_.restart(); elapsed=0;
    }
    if (phase_ == QStringLiteral("dismiss") && elapsed >= 800) { emit dismissed(); return; }
    if (phase_==QStringLiteral("rest") && !restFrameReady_ && elapsed>=2000) { restFrameReady_=true; phaseClock_.restart(); elapsed=0; }
    const qint64 reveal = phase_ == QStringLiteral("fade") ? 3800 : 800;
    if (!controlsShown_ && ((phase_==QStringLiteral("fade") && mediaReady_) || (phase_==QStringLiteral("rest") && restFrameReady_)) && elapsed >= reveal) {
        controlsShown_ = true;
        countdownEnd_ = QDateTime::currentMSecsSinceEpoch() + config_.value(QStringLiteral("breakMinutes")).toInt(5) * 60000;
        for (auto *w : windows_) w->enableControls();
    }
    const qint64 remaining = std::max<qint64>(0, (countdownEnd_ - QDateTime::currentMSecsSinceEpoch() + 500) / 1000);
    if (controlsShown_ && countdownEnd_ && QDateTime::currentMSecsSinceEpoch() >= countdownEnd_ + 1500 && phase_ != QStringLiteral("dismiss")) { beginDismiss(); elapsed = 0; }
    const QString mode = config_.value(QStringLiteral("animationMode")).toString();
    for (auto *w : windows_) {
        // showIdle in the original reuses the walk element's rectangle, even
        // when custom idle media has a different aspect ratio.
        double progress=1;
        if (phase_ == QStringLiteral("walk") || phase_ == QStringLiteral("fade")) {
            const double duration = phase_ == QStringLiteral("fade") ? 3000 : std::max(100.0, player_.duration() * 1000);
            progress=mediaReady_ ? std::clamp(elapsed/duration,0.0,1.0) : 0;
        }
        auto pose=animation::catPose(mode,w->size(),walkAspect_,progress);
        if (phase_ == QStringLiteral("dismiss")) pose.opacity=std::max(0.0,1-elapsed/800.0);
        w->image = lastFrame_; w->target=pose.bounds; w->opacity=pose.opacity;
        if (controlsShown_ && phase_ != QStringLiteral("dismiss")) {
            w->countdown->setText(remaining > 0 ? QStringLiteral("休息中 %1:%2").arg(remaining/60, 2, 10, QLatin1Char('0')).arg(remaining%60, 2, 10, QLatin1Char('0')) : QStringLiteral("休息结束！"));
            w->setControlsOpacity(animation::cubicBezier((elapsed-reveal)/1000.0,.25,.1,.25,1));
            if (elapsed >= reveal + 1000) w->enableInput();
        }
        if (phase_==QStringLiteral("dismiss")) {
            w->setControlsOpacity(1-animation::cubicBezier(elapsed/1000.0,.25,.1,.25,1));
        }
        w->update();
    }
}

RocketWindow::RocketWindow(const QString &svgPath, const QString &videoPath, int count, const QString &sourceName)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus), svg_(svgPath), player_(this), sourceName_(sourceName), count_(count) {
    setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_ShowWithoutActivating);
    connect(&player_,&VideoPlayer::failed,this,[this] { mediaFailed_=true; update(); });
    const auto *screen = qApp->primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const int startX = qRound(std::max(double(area.x()+24), double(area.x()+area.width()-240-24)));
    const int startY = qRound(std::max(double(area.y()+24), double(area.y()+area.height()-240-24)));
    const int endY = qRound(std::max(double(area.y()+24), area.y()+area.height()*.14));
    const int localStartY = std::max(0, startY-endY);
    setProperty("rocketStartY", localStartY);
    setGeometry(startX, endY, 240, std::max(240, localStartY+240));
    QFile artwork(svgPath);
    if (artwork.open(QIODevice::ReadOnly)) setProperty("rocketArtwork", artwork.readAll());
    QFont badgeFont=font(); badgeFont.setWeight(QFont::Black); badgeFont.setPixelSize(14);
    const QFontMetricsF metrics(badgeFont);
    const auto badgeText=QString::number(std::max(1,count_));
    QPainterPath badgePath;
    badgePath.addText(QPointF(-metrics.horizontalAdvance(badgeText)/2,(metrics.ascent()-metrics.descent())/2),badgeFont,badgeText);
    QGraphicsScene badgeScene;
    auto *badgeItem=badgeScene.addPath(badgePath,QPen(QColor(255,255,255,240),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin),Qt::NoBrush);
    auto *badgeShadow=new QGraphicsDropShadowEffect;
    badgeShadow->setOffset(0,2); badgeShadow->setBlurRadius(5); badgeShadow->setColor(QColor(20,25,38,71));
    badgeItem->setGraphicsEffect(badgeShadow);
    // CSS paint-order: stroke places the red glyph over the white outline.
    badgeScene.addPath(badgePath,Qt::NoPen,QColor("#ff3d57"))->setZValue(1);
    badgeBounds_=badgePath.boundingRect().adjusted(-8,-6,8,10);
    badge_=QImage((badgeBounds_.size()*3).toSize(),QImage::Format_ARGB32_Premultiplied);
    badge_.fill(Qt::transparent);
    { QPainter badgePainter(&badge_); badgePainter.setRenderHint(QPainter::Antialiasing); badgeScene.render(&badgePainter,QRectF(QPointF(),badge_.size()),badgeBounds_); }
    if (!videoPath.isEmpty()) player_.play(videoPath, true);
    ticker_.setInterval(16);
    connect(&ticker_, &QTimer::timeout, this, [this] {
        if (elapsed_.elapsed() >= 7160) { ticker_.stop(); hide(); emit finished(); return; }
        update();
    });
    elapsed_.start(); ticker_.start(); show(); platform::prepareOverlay(this);
}
void RocketWindow::paintEvent(QPaintEvent *) {
    // CSS timing functions apply independently to every keyframe interval.
    const auto bezier = animation::cubicBezier;
    const auto mix = [](double a, double b, double t) { return a+(b-a)*t; };
    const double milliseconds = elapsed_.elapsed();
    const double startY = property("rocketStartY").toInt();
    const auto pose=animation::rocketPose(milliseconds,startY);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(pose.opacity);
    painter.translate(120,pose.y+120); painter.scale(pose.scale,pose.scale); painter.translate(-120,-120);
    const QImage frame=player_.frame();
    if (!mediaFailed_ && !frame.isNull()) {
        const QSizeF fitted=QSizeF(frame.size()).scaled(QSizeF(240,240),Qt::KeepAspectRatio);
        painter.drawImage(QRectF((240-fitted.width())/2,(240-fitted.height())/2,fitted.width(),fitted.height()),frame);
    } else {
        QString artwork=QString::fromUtf8(property("rocketArtwork").toByteArray());
        const double boost=pose.boost;
        const auto alternate = [&bezier,milliseconds](double duration) {
            const double cycles=milliseconds/duration;
            const int cycle=int(cycles);
            const double progress=cycles-cycle;
            return bezier(cycle%2 ? 1-progress : progress,.42,0,.58,1);
        };
        const auto transform = [](double originY, double sx, double sy, double dy=0.0) {
            return QStringLiteral("translate(120 %1) scale(%2 %3) translate(0 %4) translate(-120 %5)")
                .arg(originY).arg(sx).arg(sy).arg(dy).arg(-originY);
        };
        const auto animate = [&artwork](const QString &className, const QString &value, double alpha=1) {
            artwork.replace(QStringLiteral("class=\"%1\"").arg(className),
                QStringLiteral("class=\"%1\" transform=\"%2\" opacity=\"%3\"").arg(className,value).arg(alpha));
        };
        artwork.replace(QStringLiteral("class=\"rocket-group rocket-shadow\""),
            QStringLiteral("transform=\"translate(120 42) scale(%1) translate(-120 -42)\"").arg(boost));
        artwork.replace(QStringLiteral("class=\"soft-glow\""),QStringLiteral("opacity=\"%1\"").arg(mix(.66,1,alternate(1400))));
        double p=alternate(160);
        animate(QStringLiteral("flame-halo"),transform(205,mix(.92,1.08,p),mix(.94,1.06,p)),mix(.34,.56,p));
        p=alternate(120);
        animate(QStringLiteral("outer-flame"),transform(187,mix(1.08,.96,p),mix(.92,1.18,p)),mix(.88,1,p));
        p=alternate(92);
        animate(QStringLiteral("middle-flame"),transform(188,mix(.9,1.08,p),mix(1.1,.92,p),mix(-1,2,p)));
        p=alternate(70);
        animate(QStringLiteral("inner-flame"),transform(189,mix(1,.84,p),mix(.84,1.08,p)),mix(.78,1,p));
        // The static asset's core-jet presentation opacity is superseded by CSS.
        artwork.replace(QStringLiteral("fill=\"#fffef2\" opacity=\"0.92\""),QStringLiteral("fill=\"#fffef2\""));
        p=alternate(76);
        animate(QStringLiteral("core-jet"),transform(188,mix(.9,1.12,p),mix(1.04,.96,p)),mix(.9,1,p));
        const QRegularExpression spark(QStringLiteral("class=\"spark\" style=\"--duration:([0-9]+)ms;--delay:([0-9]+)ms\""));
        auto matches=spark.globalMatch(artwork);
        QList<QRegularExpressionMatch> sparks;
        while (matches.hasNext()) sparks.append(matches.next());
        for (auto it=sparks.crbegin(); it!=sparks.crend(); ++it) {
            const double duration=it->captured(1).toDouble(), delay=it->captured(2).toDouble();
            const double progress=milliseconds<delay ? 0 : std::fmod((milliseconds-delay)/duration,1.0);
            const double alpha=milliseconds<delay ? 0 : progress<.14 ? .94*progress/.14 : .94*(1-progress)/.86;
            const double sparkScale=mix(.22,1.36,progress);
            const QString value=QStringLiteral("transform=\"translate(0 %1) translate(120 120) scale(%2) translate(-120 -120)\" opacity=\"%3\"")
                .arg(mix(-8,52,progress)).arg(sparkScale).arg(alpha);
            artwork.replace(it->capturedStart(),it->capturedLength(),value);
        }
        if (!artwork.isEmpty()) svg_.load(artwork.toUtf8());
        svg_.render(&painter,QRectF(0,0,240,240));
        painter.save();
        painter.translate(120,42); painter.scale(boost,boost); painter.translate(-120,-42);
        const auto codepoints=sourceName_.toUcs4();
        const int characters=std::min(6,int(codepoints.size()));
        QFont labelFont=painter.font(); labelFont.setWeight(QFont::ExtraBold); labelFont.setPixelSize(characters>4?9:11);
        const QFontMetricsF labelMetrics(labelFont);
        const double gap=characters>4?9:10.5, labelStart=149+(4-characters)*2;
        for (int i=0;i<characters;++i) {
            QPainterPath glyph;
            const char32_t codepoint=codepoints.at(i);
            const auto text=QString::fromUcs4(&codepoint,1);
            // SVG text-anchor uses the advance, and dominant-baseline: middle
            // aligns the alphabetic baseline by half the Latin x-height.
            glyph.addText(QPointF(120-labelMetrics.horizontalAdvance(text)/2,labelStart+i*gap+labelMetrics.xHeight()/2),labelFont,text);
            painter.setPen(QPen(QColor(19,36,54,199),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush); painter.drawPath(glyph);
            painter.fillPath(glyph,QColor("#d9fbff"));
        }
        painter.restore();
    }
    // The badge is plain red type with a white outline; it is also shown on video.
    const double badgeTime=std::clamp((milliseconds-120)/720.0,0.0,1.0);
    double badgeScale=1,badgeY=0,badgeOpacity=1;
    if (badgeTime<.58) {
        const double p=bezier(badgeTime/.58,.2,1.25,.3,1);
        badgeScale=mix(.2,1.12,p); badgeY=mix(8,0,p); badgeOpacity=std::clamp(p,0.0,1.0);
    } else badgeScale=mix(1.12,1,bezier((badgeTime-.58)/.42,.2,1.25,.3,1));
    painter.setOpacity(pose.opacity*badgeOpacity);
    painter.translate(145,34+badgeY); painter.scale(badgeScale,badgeScale);
    painter.drawImage(badgeBounds_,badge_);
}
}
