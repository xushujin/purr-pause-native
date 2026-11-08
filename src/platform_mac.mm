#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>
#include <QXmlStreamWriter>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreServices/CoreServices.h>
#import <ServiceManagement/ServiceManagement.h>
#include <cmath>

namespace purr::platform {
namespace {
QString autostartPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/com.purr-pause.native.plist");
}

// Electron 33 uses the system main-app login service on macOS 13+, and
// LSSharedFileList on Monterey. Keep the same login-item identity/path so an
// installed original app and its native replacement share the user's setting.
bool legacyLoginItem(bool remove, QString *error=nullptr) {
    LSSharedFileListRef list=LSSharedFileListCreate(kCFAllocatorDefault,kLSSharedFileListSessionLoginItems,nullptr);
    if (!list) { if (remove && error) *error=QStringLiteral("无法访问系统登录项"); return false; }
    CFArrayRef snapshot=LSSharedFileListCopySnapshot(list,nullptr);
    bool found=false;
    if (snapshot) {
        for (CFIndex i=0;i<CFArrayGetCount(snapshot);++i) {
            auto item=static_cast<LSSharedFileListItemRef>(const_cast<void *>(CFArrayGetValueAtIndex(snapshot,i)));
            CFURLRef url=LSSharedFileListItemCopyResolvedURL(item,kLSSharedFileListNoUserInteraction|kLSSharedFileListDoNotMountVolumes,nullptr);
            if (url) {
                NSURL *resolved=(__bridge NSURL *)url;
                if ([resolved.path.stringByStandardizingPath isEqualToString:NSBundle.mainBundle.bundlePath.stringByStandardizingPath]) {
                    found=true;
                    if (remove) {
                        const OSStatus result=LSSharedFileListItemRemove(list,item);
                        if (result!=noErr && error) *error=QStringLiteral("无法移除系统登录项（错误 %1）").arg(result);
                    }
                }
                CFRelease(url);
            }
        }
        CFRelease(snapshot);
    }
    CFRelease(list);
    return found;
}
}

qint64 idleMilliseconds()
{
    const double idle = CGEventSourceSecondsSinceLastEventType(
        kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
    return std::isfinite(idle) && idle >= 0 ? qint64(idle * 1000) : 0;
}

bool sessionLocked()
{
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session) return false;
    const auto value = CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked"));
    const bool locked = value && CFGetTypeID(value) == CFBooleanGetTypeID()
        && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    CFRelease(session);
    return locked;
}

bool setAutostart(bool enabled, QString *error)
{
    if (error) error->clear();
    if (![[NSBundle.mainBundle.bundlePath pathExtension] isEqualToString:@"app"]) {
        if (error) *error=QStringLiteral("请先将应用安装到 Applications，再设置开机启动");
        return false;
    }
    if (@available(macOS 13.0,*)) {
        SMAppService *service=SMAppService.mainAppService;
        // Electron treats the old login item as active only when the new
        // service has not been found, then removes it before registration.
        if (service.status==SMAppServiceStatusNotFound && legacyLoginItem(false)) {
            QString removalError;
            legacyLoginItem(true,&removalError);
            if (!removalError.isEmpty()) { if (error) *error=removalError; return false; }
        }
        NSError *failure=nil;
        bool ok=true;
        if (enabled && service.status!=SMAppServiceStatusEnabled) ok=[service registerAndReturnError:&failure];
        else if (!enabled && service.status!=SMAppServiceStatusNotRegistered && service.status!=SMAppServiceStatusNotFound)
            ok=[service unregisterAndReturnError:&failure];
        if (!ok) {
            if (error) *error=QString::fromUtf8(failure.localizedDescription.UTF8String);
            return false;
        }
    } else if (enabled) {
        if (!legacyLoginItem(false)) {
            LSSharedFileListRef list=LSSharedFileListCreate(kCFAllocatorDefault,kLSSharedFileListSessionLoginItems,nullptr);
            if (!list) { if (error) *error=QStringLiteral("无法访问系统登录项"); return false; }
            LSSharedFileListItemRef item=LSSharedFileListInsertItemURL(list,kLSSharedFileListItemLast,nullptr,nullptr,(__bridge CFURLRef)NSBundle.mainBundle.bundleURL,nullptr,nullptr);
            CFRelease(list);
            if (!item) { if (error) *error=QStringLiteral("无法添加系统登录项"); return false; }
            CFRelease(item);
        }
    } else {
        QString removalError;
        legacyLoginItem(true,&removalError);
        if (!removalError.isEmpty()) { if (error) *error=removalError; return false; }
    }
    // Retire the earlier native port's LaunchAgent after a successful change.
    QFile::remove(autostartPath());
    return true;
}

bool autostartEnabled()
{
    if (QFile::exists(autostartPath())) return true;
    if (@available(macOS 13.0,*)) {
        const auto state=SMAppService.mainAppService.status;
        return state==SMAppServiceStatusEnabled || (state==SMAppServiceStatusNotFound && legacyLoginItem(false));
    }
    return legacyLoginItem(false);
}

void prepareOverlay(QWidget *widget)
{
    widget->setAttribute(Qt::WA_ShowWithoutActivating);
    widget->setWindowFlag(Qt::WindowStaysOnTopHint);
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) return;
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    NSWindow *window = view.window;
    if (!window) return;
    window.level = NSScreenSaverWindowLevel;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorStationary;
    window.hidesOnDeactivate = NO;
    window.canHide = NO;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    window.backgroundColor = NSColor.clearColor;
    window.opaque = NO;
    window.hasShadow = NO;
}

bool supportsGlobalPositioning() { return true; }

void watchSleep(QObject *context, std::function<void(bool)> callback)
{
    NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
    id sleepObserver = [center addObserverForName:NSWorkspaceWillSleepNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { callback(true); }];
    id wakeObserver = [center addObserverForName:NSWorkspaceDidWakeNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { callback(false); }];
    QObject::connect(context, &QObject::destroyed, [center, sleepObserver, wakeObserver]() {
        [center removeObserver:sleepObserver];
        [center removeObserver:wakeObserver];
    });
}

void watchLock(QObject *context, std::function<void(bool)> callback)
{
    NSDistributedNotificationCenter *center = NSDistributedNotificationCenter.defaultCenter;
    id locked = [center addObserverForName:@"com.apple.screenIsLocked" object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { callback(true); }];
    id unlocked = [center addObserverForName:@"com.apple.screenIsUnlocked" object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { callback(false); }];
    QObject::connect(context, &QObject::destroyed, [center, locked, unlocked]() {
        [center removeObserver:locked];
        [center removeObserver:unlocked];
    });
    auto *poll = new QTimer(context);
    QObject::connect(poll, &QTimer::timeout, context, [callback] { callback(sessionLocked()); });
    poll->start(5000);
    callback(sessionLocked());
}
} // namespace purr::platform
