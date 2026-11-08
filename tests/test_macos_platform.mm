#include "overlays.h"
#include "platform.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <cmath>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

using namespace purr;

namespace {
NSWindow *nativeWindow(QWidget *widget) {
    auto *view=reinterpret_cast<NSView *>(widget->winId());
    return view.window;
}

QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QList<QWidget *> catWindows() {
    QList<QWidget *> result;
    for(auto *widget:QApplication::topLevelWidgets())
        if(widget->objectName()==QStringLiteral("catOverlay")) result.append(widget);
    return result;
}
}

// These tests must run with the Cocoa plugin in a logged-in macOS GUI session.
// They inspect application-owned NSWindows and post a process-local workspace
// notification; they never inject system input or change login/lock/sleep state.
class MacPlatformTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("cocoa"));
        QVERIFY2(!QGuiApplication::screens().isEmpty(),"A logged-in macOS GUI session is required");
        QVERIFY(platform::supportsGlobalPositioning());
    }

    void nativeOverlayPropertiesAndInputTransition() {
        QWidget overlay(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint
            |Qt::WindowDoesNotAcceptFocus|Qt::WindowTransparentForInput);
        overlay.setAttribute(Qt::WA_TranslucentBackground);
        overlay.setAttribute(Qt::WA_ShowWithoutActivating);
        overlay.setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(30,30),QSize(240,160)));
        overlay.show();
        platform::prepareOverlay(&overlay);
        QTRY_VERIFY_WITH_TIMEOUT(nativeWindow(&overlay)!=nil,3000);
        NSWindow *window=nativeWindow(&overlay);
        QCOMPARE(qint64(window.level),qint64(NSScreenSaverWindowLevel));
        QVERIFY(window.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces);
        QVERIFY(window.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary);
        QVERIFY(window.collectionBehavior & NSWindowCollectionBehaviorStationary);
        QVERIFY(!window.opaque);
        QVERIFY(!window.hasShadow);
        QVERIFY(!window.hidesOnDeactivate);
        QVERIFY(!window.canHide);
        QCOMPARE(window.animationBehavior,NSWindowAnimationBehaviorNone);
        QTRY_VERIFY_WITH_TIMEOUT(nativeWindow(&overlay).ignoresMouseEvents,3000);
        QVERIFY(overlay.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(overlay.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        const QImage pixels=overlay.grab().toImage();
        QVERIFY(!pixels.isNull());
        QVERIFY(pixels.hasAlphaChannel());
        QCOMPARE(pixels.pixelColor(pixels.width()/2,pixels.height()/2).alpha(),0);

        overlay.setWindowFlags(overlay.windowFlags() & ~Qt::WindowTransparentForInput & ~Qt::WindowDoesNotAcceptFocus);
        overlay.show();
        platform::prepareOverlay(&overlay);
        QTRY_VERIFY_WITH_TIMEOUT(!nativeWindow(&overlay).ignoresMouseEvents,3000);
        QCOMPARE(qint64(nativeWindow(&overlay).level),qint64(NSScreenSaverWindowLevel));
        QVERIFY(nativeWindow(&overlay).collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces);
    }

    void realBreakWindowsRevealNativeInputAndDismiss() {
        BreakOverlay overlay;
        QSignalSpy finished(&overlay,&BreakOverlay::dismissed);
        QSignalSpy mediaErrors(&overlay,&BreakOverlay::mediaError);
        connect(&overlay,&BreakOverlay::dismissed,&overlay,&BreakOverlay::stop);
        overlay.start({{QStringLiteral("animationMode"),QStringLiteral("fade-center")},{QStringLiteral("breakMinutes"),1}},
            QStringLiteral(PURR_TEST_ASSETS "/webm/cat-walk.webm"),QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),1);
        const auto windows=catWindows();
        QCOMPARE(windows.size(),QGuiApplication::screens().size());
        QList<QPointer<QWidget>> tracked;
        for(auto *window:windows) {
            tracked.append(window);
            QCOMPARE(window->geometry(),window->screen()->availableGeometry());
            QVERIFY(nativeWindow(window).ignoresMouseEvents);
            QVERIFY(window->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        }
        auto *button=windows.first()->findChild<QPushButton *>(QStringLiteral("dismissButton"));
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(),6500);
        QTRY_VERIFY_WITH_TIMEOUT(!nativeWindow(windows.first()).ignoresMouseEvents,2500);
        QVERIFY(!windows.first()->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY2(mediaErrors.isEmpty(),"Original alpha video must decode on the actual Cocoa backend");
        const auto pixels=windows.first()->grab().toImage();
        QVERIFY(pixels.hasAlphaChannel());
        QCOMPARE(pixels.pixelColor(0,0).alpha(),0);
        // Native QPushButton activation avoids CGEvent injection and accessibility
        // permission prompts while exercising the production dismissal path.
        button->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,2000);
        QVERIFY(!overlay.visible());
        for(const auto &window:tracked) QVERIFY(window.isNull());
    }

    void rocketRemainsNoninteractiveAndEnds() {
        RocketWindow rocket(QStringLiteral(PURR_TEST_ASSETS "/images/rocket.svg"),{},3,QStringLiteral("绘家科技"));
        QSignalSpy finished(&rocket,&RocketWindow::finished);
        QVERIFY(rocket.isVisible());
        QVERIFY(rocket.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(rocket.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(nativeWindow(&rocket).ignoresMouseEvents);
        QCOMPARE(qint64(nativeWindow(&rocket).level),qint64(NSScreenSaverWindowLevel));
        QVERIFY(nativeWindow(&rocket).collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces);
        QTest::qWait(200);
        const auto pixels=rocket.grab().toImage();
        QVERIFY(pixels.hasAlphaChannel());
        QCOMPARE(pixels.pixelColor(0,0).alpha(),0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,8000);
        QVERIFY(!rocket.isVisible());
    }

    void idleAndLockReadNativeSession() {
        const auto idle=platform::idleMilliseconds();
        const double systemIdle=CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState,kCGAnyInputEventType);
        QVERIFY(idle>=0);
        if(std::isfinite(systemIdle) && systemIdle>=0) {
            // User/runner input can reset the counter between the reads.
            QVERIFY(idle<=qint64(systemIdle*1000)+1000 || systemIdle<1);
        }
        const bool actual=platform::sessionLocked();
        CFDictionaryRef session=CGSessionCopyCurrentDictionary();
        bool expected=false;
        if(session) {
            const auto value=CFDictionaryGetValue(session,CFSTR("CGSSessionScreenIsLocked"));
            expected=value && CFGetTypeID(value)==CFBooleanGetTypeID() && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
            CFRelease(session);
        }
        QCOMPARE(actual,expected);
    }

    void watchersReleaseWithOwner() {
        auto *owner=new QObject;
        QList<bool> sleepStates;
        QList<bool> lockStates;
        platform::watchSleep(owner,[&sleepStates](bool state){sleepStates.append(state);});
        platform::watchLock(owner,[&lockStates](bool state){lockStates.append(state);});
        QCOMPARE(lockStates.size(),1);
        QCOMPARE(lockStates.first(),platform::sessionLocked());
        const auto polls=owner->findChildren<QTimer *>();
        QCOMPARE(polls.size(),1);
        QCOMPARE(polls.first()->interval(),5000);
        QVERIFY(polls.first()->isActive());
        QPointer<QTimer> timer=polls.first();
        NSNotificationCenter *center=NSWorkspace.sharedWorkspace.notificationCenter;
        // This is NSNotificationCenter, not NSDistributedNotificationCenter:
        // only observers inside this test process receive the synthetic event.
        [center postNotificationName:NSWorkspaceWillSleepNotification object:nil];
        [center postNotificationName:NSWorkspaceDidWakeNotification object:nil];
        QTRY_COMPARE_WITH_TIMEOUT(sleepStates.size(),2,1000);
        QCOMPARE(sleepStates,QList<bool>({true,false}));
        delete owner;
        QVERIFY(timer.isNull());
        [center postNotificationName:NSWorkspaceWillSleepNotification object:nil];
        [center postNotificationName:NSWorkspaceDidWakeNotification object:nil];
        QCoreApplication::processEvents();
        QCOMPARE(sleepStates.size(),2);
        QCOMPARE(lockStates.size(),1);
    }

    void unbundledAutostartRejectsWithoutWriting() {
        // CMake intentionally builds this target as a plain executable. Refuse
        // to exercise the mutation API if a caller wraps the test in an app.
        QVERIFY2(![NSBundle.mainBundle.bundlePath.pathExtension isEqualToString:@"app"],
            "Run test_macos_platform as an unbundled executable");
        const QString legacy=QDir::homePath()+QStringLiteral("/Library/LaunchAgents/com.purr-pause.native.plist");
        const bool existed=QFileInfo::exists(legacy);
        const auto before=contents(legacy);
        const auto modified=QFileInfo(legacy).lastModified();
        QString error;
        QVERIFY(!platform::setAutostart(true,&error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!platform::setAutostart(false,&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(QFileInfo::exists(legacy),existed);
        QCOMPARE(contents(legacy),before);
        QCOMPARE(QFileInfo(legacy).lastModified(),modified);
    }
};

QTEST_MAIN(MacPlatformTests)
#include "test_macos_platform.moc"
