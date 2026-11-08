#include "platform.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/extensions/shape.h>
#include <unistd.h>

// Xlib's Bool macro collides with QMetaType::Bool in generated moc code.
#undef Bool

namespace purr::platform {
class SleepWatcher final : public QObject {
    Q_OBJECT
public:
    SleepWatcher(QObject *parent, std::function<void(bool)> callback)
        : QObject(parent), callback_(std::move(callback)) {}
public slots:
    void changed(bool sleeping) { callback_(sleeping); }
private:
    std::function<void(bool)> callback_;
};

class LockWatcher final : public QObject {
    Q_OBJECT
public:
    LockWatcher(QObject *parent, std::function<void(bool)> callback)
        : QObject(parent), callback_(std::move(callback)), locked_(sessionLocked()) {
        callback_(locked_);
        // Event delivery supplies exact transitions. Polling catches desktops
        // without the ScreenSaver signal and recovers after D-Bus restarts.
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] { changed(sessionLocked()); });
        poll->start(5000);
    }
public slots:
    void changed(bool locked) {
        if (locked == locked_) return;
        locked_ = locked;
        callback_(locked);
    }
private:
    std::function<void(bool)> callback_;
    bool locked_;
};

namespace {
QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/purr-pause.desktop");
}

QDBusMessage call(const QDBusConnection &bus, const QString &service, const QString &path,
                  const QString &interface, const QString &method, const QVariantList &arguments = {})
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    return bus.call(message, QDBus::Block, 150);
}

bool replyValue(const QDBusMessage &message, QVariant *value)
{
    if (message.type() != QDBusMessage::ReplyMessage || message.arguments().isEmpty())
        return false;
    *value = message.arguments().first();
    if (value->canConvert<QDBusVariant>())
        *value = value->value<QDBusVariant>().variant();
    return true;
}

qint64 mutterIdle()
{
    QVariant value;
    if (replyValue(call(QDBusConnection::sessionBus(), QStringLiteral("org.gnome.Mutter.IdleMonitor"),
                        QStringLiteral("/org/gnome/Mutter/IdleMonitor/Core"),
                        QStringLiteral("org.gnome.Mutter.IdleMonitor"), QStringLiteral("GetIdletime")), &value))
        return value.toLongLong();
    return -1;
}

qint64 x11Idle()
{
    // Use an independent connection: Qt's native X11 interfaces vary between Qt releases.
    struct Connection {
        Display *display = XOpenDisplay(nullptr);
        XScreenSaverInfo *info = nullptr;
        Connection() {
            int event = 0, error = 0;
            if (display && XScreenSaverQueryExtension(display, &event, &error))
                info = XScreenSaverAllocInfo();
        }
        ~Connection() {
            if (info) XFree(info);
            if (display) XCloseDisplay(display);
        }
    };
    static Connection connection;
    if (connection.info && XScreenSaverQueryInfo(connection.display,
            DefaultRootWindow(connection.display), connection.info))
        return qint64(connection.info->idle);
    return -1;
}

QString quoteDesktopArgument(QString argument)
{
    // Desktop Entry Exec first undergoes string unescaping, then argument parsing.
    argument.replace(QLatin1Char('%'), QStringLiteral("%%"));
    argument.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    argument.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    argument.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    argument.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return QLatin1Char('"') + argument + QLatin1Char('"');
}

void updateFrameInput(QWidget *widget)
{
    // Some reparenting WMs (including Openbox) keep a rectangular input region
    // on their frame even when Qt has cleared the client's ShapeInput region.
    // Mirror the client's intent onto its private frame; never modify the root.
    Display *display=XOpenDisplay(nullptr);
    if (!display) return;
    const Window window=static_cast<Window>(widget->winId());
    Window root=0,parent=0,*children=nullptr;
    unsigned int count=0;
    if (XQueryTree(display,window,&root,&parent,&children,&count) && parent && parent!=root) {
        int event=0,error=0;
        if (XShapeQueryExtension(display,&event,&error)) {
            if (widget->windowFlags().testFlag(Qt::WindowTransparentForInput))
                XShapeCombineRectangles(display,parent,ShapeInput,0,0,nullptr,0,ShapeSet,Unsorted);
            else XShapeCombineMask(display,parent,ShapeInput,0,0,0,ShapeSet);
        }
    }
    if (children) XFree(children);
    XFlush(display); XCloseDisplay(display);
}
} // namespace

qint64 idleMilliseconds()
{
    // XWayland only sees input delivered to X11 clients. Mutter knows about all input.
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")) {
        const qint64 idle = mutterIdle();
        if (idle >= 0) return idle;
    }
    const qint64 idle = x11Idle();
    if (idle >= 0) return idle;
    return qMax<qint64>(0, mutterIdle());
}

bool sessionLocked()
{
    static QString sessionPath;
    if (sessionPath.isEmpty()) {
        const auto reply = call(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.login1"),
                                QStringLiteral("/org/freedesktop/login1"),
                                QStringLiteral("org.freedesktop.login1.Manager"),
                                QStringLiteral("GetSessionByPID"), {uint(getpid())});
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            sessionPath = reply.arguments().first().value<QDBusObjectPath>().path();
    }
    QVariant value;
    if (!sessionPath.isEmpty() && replyValue(call(QDBusConnection::systemBus(),
            QStringLiteral("org.freedesktop.login1"), sessionPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
            {QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("LockedHint")}), &value)
        && value.toBool())
        return true;

    // Some desktop sessions do not publish logind's LockedHint.
    const QStringList services{QStringLiteral("org.gnome.ScreenSaver"),
                               QStringLiteral("org.freedesktop.ScreenSaver"),
                               QStringLiteral("org.cinnamon.ScreenSaver")};
    for (const QString &service : services) {
        const QString path = service == QLatin1String("org.freedesktop.ScreenSaver")
            ? QStringLiteral("/ScreenSaver") : QLatin1Char('/') + QString(service).replace('.', '/');
        if (replyValue(call(QDBusConnection::sessionBus(), service, path, service,
                            QStringLiteral("GetActive")), &value) && value.toBool())
            return true;
    }
    return false;
}

bool setAutostart(bool enabled, QString *error)
{
    if (error) error->clear();
    const QString path = autostartPath();
    if (!enabled) {
        if (!QFile::exists(path)) return true;
        QFile file(path);
        if (file.remove()) return true;
        if (error) *error = file.errorString();
        return false;
    }
    QString executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty()) executable = QCoreApplication::applicationFilePath();
    if (executable.contains(QLatin1Char('\n')) || executable.contains(QLatin1Char('\r'))) {
        if (error) *error = QStringLiteral("程序路径不能包含换行符");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建自动启动目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray content = QStringLiteral(
        "[Desktop Entry]\nType=Application\nName=胖猫暂停一下（PurrPause）\n"
        // GDesktopAppInfo validates argv[0] before expanding %% field codes.
        // A stable launcher keeps paths containing a literal % valid; env
        // replaces itself with the application, with no shell evaluation.
        "Exec=/usr/bin/env %1\nIcon=purr-pause\nTerminal=false\nHidden=false\n"
        "X-GNOME-Autostart-enabled=true\nCategories=Utility;\n")
        .arg(quoteDesktopArgument(executable)).toUtf8();
    if (file.write(content) != content.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool autostartEnabled()
{
    QFile file(autostartPath());
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QString content = QString::fromUtf8(file.readAll());
    const auto flags = QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption;
    return !content.contains(QRegularExpression(QStringLiteral("^Hidden\\s*=\\s*true\\s*$"), flags))
        && !content.contains(QRegularExpression(QStringLiteral("^X-GNOME-Autostart-enabled\\s*=\\s*false\\s*$"), flags));
}

void prepareOverlay(QWidget *widget)
{
    widget->setAttribute(Qt::WA_ShowWithoutActivating);
    widget->setWindowFlag(Qt::WindowStaysOnTopHint);
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;
    Display *display = XOpenDisplay(nullptr);
    if (!display) return;
    const Window window = static_cast<Window>(widget->winId());
    QGuiApplication::sync();
    const Atom desktop = XInternAtom(display, "_NET_WM_DESKTOP", False);
    const Atom state = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom sticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False);
    const unsigned long allDesktops = 0xffffffffUL;
    XChangeProperty(display, window, desktop, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&allDesktops), 1);
    // EWMH client messages update an already mapped window; the property also
    // covers a window prepared before its first mapping.
    XEvent request{};
    request.xclient.type = ClientMessage;
    request.xclient.window = window;
    request.xclient.format = 32;
    request.xclient.message_type = desktop;
    request.xclient.data.l[0] = static_cast<long>(allDesktops);
    request.xclient.data.l[1] = 1;
    XWindowAttributes attributes{};
    const Window root = XGetWindowAttributes(display,window,&attributes) ? attributes.root : DefaultRootWindow(display);
    XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &request);
    request.xclient.message_type = state;
    request.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
    request.xclient.data.l[1] = static_cast<long>(sticky);
    request.xclient.data.l[2] = 0;
    request.xclient.data.l[3] = 1;
    XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &request);
    XFlush(display);
    XCloseDisplay(display);
    updateFrameInput(widget);
    // Mapping and reparenting occur asynchronously on the WM's connection.
    QTimer::singleShot(50,widget,[widget] { updateFrameInput(widget); });
    QTimer::singleShot(250,widget,[widget] { updateFrameInput(widget); });
}

bool supportsGlobalPositioning()
{
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

void watchSleep(QObject *context, std::function<void(bool)> callback)
{
    auto *watcher = new SleepWatcher(context, std::move(callback));
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"), watcher, SLOT(changed(bool)));
}

void watchLock(QObject *context, std::function<void(bool)> callback)
{
    auto *watcher = new LockWatcher(context, std::move(callback));
    for (const auto &service : {QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("org.cinnamon.ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver")}) {
        const QString path = service == QStringLiteral("org.freedesktop.ScreenSaver")
            ? QStringLiteral("/ScreenSaver") : QLatin1Char('/') + QString(service).replace('.', '/');
        QDBusConnection::sessionBus().connect(service, path, service,
            QStringLiteral("ActiveChanged"), watcher, SLOT(changed(bool)));
    }
}
} // namespace purr::platform

#include "platform_linux.moc"
