#include "platform.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/shape.h>
#undef Bool
#undef None
#undef Status
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut

using namespace purr;
class ScreenSaverFixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.ScreenSaver")
public slots:
    bool GetActive() const { return active_; }
    void SetActive(bool active) { active_=active; emit ActiveChanged(active); }
signals:
    void ActiveChanged(bool active);
private:
    bool active_=false;
};

class PowerFixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")
public slots:
    void SimulateSleep(bool sleeping) { emit PrepareForSleep(sleeping); }
signals:
    void PrepareForSleep(bool sleeping);
};

class PlatformTests : public QObject {
    Q_OBJECT
private:
    Display *display_=nullptr;
    void click(QPoint position) {
        XTestFakeMotionEvent(display_,-1,position.x(),position.y(),CurrentTime);
        XTestFakeButtonEvent(display_,1,True,CurrentTime);
        XTestFakeButtonEvent(display_,1,False,CurrentTime);
        XFlush(display_);
    }
    unsigned long property(Window window,const char *name) {
        Atom actual=0; int format=0; unsigned long count=0,remaining=0; unsigned char *data=nullptr;
        XGetWindowProperty(display_,window,XInternAtom(display_,name,False),0,1024,False,AnyPropertyType,&actual,&format,&count,&remaining,&data);
        unsigned long value=(data && count && format==32) ? (reinterpret_cast<unsigned long *>(data)[0] & 0xffffffffUL) : 0;
        if (data) XFree(data);
        return value;
    }
private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("xcb"));
        display_=XOpenDisplay(nullptr); QVERIFY(display_);
        // Wait for the real window manager to publish its EWMH support window.
        QTRY_VERIFY_WITH_TIMEOUT(property(DefaultRootWindow(display_),"_NET_SUPPORTING_WM_CHECK")!=0,5000);
    }
    void cleanupTestCase() { if (display_) XCloseDisplay(display_); }
    void globalIdleTracksActualXInput() {
        XTestFakeMotionEvent(display_,-1,30,30,CurrentTime); XSync(display_,False);
        QTest::qWait(100);
        QVERIFY(platform::idleMilliseconds()<1000);
        QTest::qWait(1100);
        QVERIFY(platform::idleMilliseconds()>=1000);
        XTestFakeMotionEvent(display_,-1,60,60,CurrentTime); XSync(display_,False);
        QTRY_VERIFY(platform::idleMilliseconds()<500);
    }
    void overlayStaysOnEveryDesktopAndClickThroughWorks() {
        QWidget base;
        base.setGeometry(100,100,420,240);
        QPushButton behind(QStringLiteral("behind"),&base); behind.setGeometry(100,80,180,60);
        base.show(); QVERIFY(QTest::qWaitForWindowExposed(&base));
        QSignalSpy backgroundClicks(&behind,&QPushButton::clicked);
        QTest::qWait(100);
        click(behind.mapToGlobal(behind.rect().center()));
        QTRY_COMPARE(backgroundClicks.size(),1);
        QWidget overlay(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
        overlay.setAttribute(Qt::WA_TranslucentBackground);
        overlay.setGeometry(base.geometry());
        QPushButton front(QStringLiteral("front"),&overlay); front.setGeometry(100,80,180,60);
        overlay.show(); platform::prepareOverlay(&overlay);
        QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        QTest::qWait(100);
        const auto window=static_cast<Window>(overlay.winId());
        QTRY_COMPARE(property(window,"_NET_WM_DESKTOP"),0xffffffffUL);
        QSignalSpy foregroundClicks(&front,&QPushButton::clicked);
        int rectangleCount=0,ordering=0;
        XRectangle *input=XShapeGetRectangles(display_,window,ShapeInput,&rectangleCount,&ordering);
        if (input) XFree(input);
        QCOMPARE(rectangleCount,0);
        // Two clicks on the same underlying widget must not be coalesced into
        // a double-click, which QPushButton intentionally handles differently.
        QTest::qWait(QApplication::doubleClickInterval()+50);
        click(behind.mapToGlobal(behind.rect().center()));
        QTRY_COMPARE(backgroundClicks.size(),2);
        QCOMPARE(foregroundClicks.size(),0);
        overlay.setWindowFlags(overlay.windowFlags() & ~Qt::WindowTransparentForInput & ~Qt::WindowDoesNotAcceptFocus);
        overlay.show(); platform::prepareOverlay(&overlay);
        QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        click(front.mapToGlobal(front.rect().center()));
        QTRY_COMPARE(foregroundClicks.size(),1);
        QCOMPARE(backgroundClicks.size(),2);
        // The same window is still shown when switching to the next workspace.
        XEvent request{}; request.xclient.type=ClientMessage;
        request.xclient.window=DefaultRootWindow(display_); request.xclient.format=32;
        request.xclient.message_type=XInternAtom(display_,"_NET_CURRENT_DESKTOP",False);
        request.xclient.data.l[0]=1; request.xclient.data.l[1]=CurrentTime;
        XSendEvent(display_,DefaultRootWindow(display_),False,SubstructureRedirectMask|SubstructureNotifyMask,&request); XFlush(display_);
        QTRY_COMPARE(property(DefaultRootWindow(display_),"_NET_CURRENT_DESKTOP"),1UL);
        XWindowAttributes attrs{}; XGetWindowAttributes(display_,static_cast<Window>(overlay.winId()),&attrs);
        QCOMPARE(attrs.map_state,IsViewable);
        request.xclient.data.l[0]=0; XSendEvent(display_,DefaultRootWindow(display_),False,SubstructureRedirectMask|SubstructureNotifyMask,&request); XFlush(display_);
        QTRY_COMPARE(property(DefaultRootWindow(display_),"_NET_CURRENT_DESKTOP"),0UL);
    }
    void lockSignalsReachWatcherImmediately() {
        QProcess fixture;
        fixture.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--screen-saver-fixture")});
        QVERIFY(fixture.waitForStarted()); QVERIFY(fixture.waitForReadyRead(5000));
        QCOMPARE(fixture.readAllStandardOutput().trimmed(),QByteArray("ready"));
        QObject context;
        QList<bool> events;
        platform::watchLock(&context,[&events](bool locked) { events.append(locked); });
        QDBusInterface remote(QStringLiteral("org.gnome.ScreenSaver"),QStringLiteral("/org/gnome/ScreenSaver"),QStringLiteral("org.gnome.ScreenSaver"),QDBusConnection::sessionBus());
        QVERIFY(remote.isValid());
        QCOMPARE(events,QList<bool>{false});
        auto reply=remote.call(QStringLiteral("SetActive"),true);
        QCOMPARE(reply.type(),QDBusMessage::ReplyMessage);
        QTRY_COMPARE_WITH_TIMEOUT(events.size(),2,1500);
        QCOMPARE(events.last(),true); QVERIFY(platform::sessionLocked());
        remote.call(QStringLiteral("SetActive"),false);
        QTRY_COMPARE_WITH_TIMEOUT(events.size(),3,1500);
        QVERIFY(!events.last()); QVERIFY(!platform::sessionLocked());
        fixture.terminate(); QVERIFY(fixture.waitForFinished());
    }
    void desktopAutostartEscapingRoundTrips() {
        const auto gio=QStandardPaths::findExecutable(QStringLiteral("gio"));
        QVERIFY2(!gio.isEmpty(),"Install libglib2.0-bin for actual Desktop Entry launch verification");
        QTemporaryDir temporary;
        const QString directory=temporary.filePath(QStringLiteral("name with spaces ' \" $ ` % back\\slash"));
        QVERIFY(QDir().mkpath(directory));
        const QString executable=directory+QStringLiteral("/purr-pause");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),executable));
        QFile::setPermissions(executable,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"),temporary.filePath(QStringLiteral("config")));
        environment.insert(QStringLiteral("PURR_AUTOSTART_PROBE"),temporary.filePath(QStringLiteral("launched")));
        QProcess setup;
        setup.setProcessEnvironment(environment);
        setup.start(executable,{QStringLiteral("--autostart-write")});
        QVERIFY(setup.waitForFinished(5000)); QCOMPARE(setup.exitCode(),0);
        QProcess launch;
        launch.setProcessEnvironment(environment);
        launch.start(gio,{QStringLiteral("launch"),temporary.filePath(QStringLiteral("config/autostart/purr-pause.desktop"))});
        QVERIFY(launch.waitForFinished(5000));
        if (launch.exitCode()!=0) { temporary.setAutoRemove(false); qWarning() << "Preserved desktop entry fixture:" << temporary.path(); }
        QVERIFY2(launch.exitCode()==0,launch.readAllStandardError().constData());
        const auto deadline=QDeadlineTimer(5000);
        while (!QFile::exists(temporary.filePath(QStringLiteral("launched"))) && !deadline.hasExpired()) QTest::qWait(20);
        if (!QFile::exists(temporary.filePath(QStringLiteral("launched")))) {
            temporary.setAutoRemove(false);
            qWarning() << "Preserved launch fixture:" << temporary.path() << launch.readAllStandardError();
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(temporary.filePath(QStringLiteral("launched"))),5000);
        setup.start(executable,{QStringLiteral("--autostart-remove")});
        QVERIFY(setup.waitForFinished(5000)); QCOMPARE(setup.exitCode(),0);
        QVERIFY(!QFile::exists(temporary.filePath(QStringLiteral("config/autostart/purr-pause.desktop"))));
    }
    void sleepEventsUseTheSystemBusAndRespectLifetime() {
        QProcess fixture;
        fixture.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--power-fixture")});
        QVERIFY(fixture.waitForStarted()); QVERIFY(fixture.waitForReadyRead(5000));
        QCOMPARE(fixture.readAllStandardOutput().trimmed(),QByteArray("ready"));
        QList<bool> events;
        auto *context=new QObject;
        platform::watchSleep(context,[&events](bool sleeping) { events.append(sleeping); });
        QDBusInterface remote(QStringLiteral("org.freedesktop.login1"),QStringLiteral("/org/freedesktop/login1"),QStringLiteral("org.freedesktop.login1.Manager"),QDBusConnection::systemBus());
        QVERIFY(remote.isValid());
        remote.call(QStringLiteral("SimulateSleep"),true);
        QTRY_COMPARE_WITH_TIMEOUT(events.size(),1,1500);
        QVERIFY(events.last());
        remote.call(QStringLiteral("SimulateSleep"),false);
        QTRY_COMPARE_WITH_TIMEOUT(events.size(),2,1500);
        QVERIFY(!events.last());
        delete context;
        remote.call(QStringLiteral("SimulateSleep"),true);
        QTest::qWait(100); QCOMPARE(events.size(),2);
        fixture.terminate(); QVERIFY(fixture.waitForFinished());
    }
};

int main(int argc,char **argv) {
    const QString mode=argc>1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode==QStringLiteral("--screen-saver-fixture")) {
        QCoreApplication app(argc,argv);
        ScreenSaverFixture fixture;
        auto bus=QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.gnome.ScreenSaver")) || !bus.registerObject(QStringLiteral("/org/gnome/ScreenSaver"),&fixture,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals)) return 2;
        puts("ready"); fflush(stdout);
        return app.exec();
    }
    if (mode==QStringLiteral("--power-fixture")) {
        QCoreApplication app(argc,argv);
        PowerFixture fixture;
        auto bus=QDBusConnection::systemBus();
        if (!bus.registerService(QStringLiteral("org.freedesktop.login1")) || !bus.registerObject(QStringLiteral("/org/freedesktop/login1"),&fixture,QDBusConnection::ExportAllSlots|QDBusConnection::ExportAllSignals)) return 2;
        puts("ready"); fflush(stdout);
        return app.exec();
    }
    if (mode==QStringLiteral("--autostart-write") || mode==QStringLiteral("--autostart-remove")) {
        QCoreApplication app(argc,argv);
        QString error;
        return platform::setAutostart(mode==QStringLiteral("--autostart-write"),&error) ? 0 : 3;
    }
    if (!qEnvironmentVariableIsEmpty("PURR_AUTOSTART_PROBE")) {
        QCoreApplication app(argc,argv);
        QFile file(qEnvironmentVariable("PURR_AUTOSTART_PROBE"));
        return file.open(QIODevice::WriteOnly) && file.write("launched")==8 ? 0 : 4;
    }
    QApplication app(argc,argv);
    PlatformTests test;
    return QTest::qExec(&test,argc,argv);
}
#include "test_platform.moc"
