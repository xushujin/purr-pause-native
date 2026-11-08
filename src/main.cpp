#include "application.h"
#include "installation.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QStandardPaths>
#include <QSslSocket>
#include <cstdio>

namespace {
QString resources(const QString &name, const QString &development,bool installedOnly=false) {
    const QDir executable(QCoreApplication::applicationDirPath());
    QStringList candidates{executable.filePath(QStringLiteral("../Resources/")+name), executable.filePath(QStringLiteral("../share/purr-pause/")+name), executable.filePath(name)};
    if (!installedOnly) candidates.append(development);
    for (const auto &candidate : candidates) if (QDir(candidate).exists()) return QDir(candidate).absolutePath();
    return installedOnly?QString():development;
}
}
int main(int argc, char **argv) {
#ifdef Q_OS_LINUX
    // Wayland forbids globally positioned overlay windows. Ubuntu's XWayland
    // supplies positioning while Mutter's native D-Bus API supplies idle time.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc,argv);
    app.setApplicationName(QStringLiteral("purr-pause"));
    app.setApplicationDisplayName(QStringLiteral("胖猫暂停一下（PurrPause）"));
    app.setApplicationVersion(QStringLiteral(PURR_VERSION));
    app.setOrganizationDomain(QStringLiteral("purr-pause.com"));
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription(QStringLiteral("胖猫暂停一下 — 原生休息与待办提醒")); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("data-dir"),QStringLiteral("使用独立数据目录（测试或便携运行）"),QStringLiteral("directory")});
    parser.addOption({QStringLiteral("settings"),QStringLiteral("打开设置")});
    parser.addOption({QStringLiteral("test-break"),QStringLiteral("立即测试休息动画")});
    parser.addOption({QStringLiteral("rocket-demo"),QStringLiteral("演示小火箭")});
    parser.addOption({QStringLiteral("smoke-test"),QStringLiteral("打开设置后退出，用于构建冒烟测试")});
    parser.addOption({QStringLiteral("verify-install"),QStringLiteral("验证安装目录资源、TLS、原生窗口及透明视频解码后退出")});
    parser.addOption({QStringLiteral("verification-report"),QStringLiteral("保存安装验证 JSON 报告"),QStringLiteral("path")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("verify-install"))) {
        purr::verifyInstallation(resources(QStringLiteral("assets"),{},true),resources(QStringLiteral("docs"),{},true),parser.value(QStringLiteral("verification-report")));
        return app.exec();
    }
    if (parser.isSet(QStringLiteral("smoke-test")) && !QSslSocket::supportsSsl()) {
        std::fputs("No working TLS backend; HTTPS notification sources would fail.\n",stderr);
        return 2;
    }
    QString dataDir=parser.value(QStringLiteral("data-dir"));
    if (dataDir.isEmpty()) {
#ifdef Q_OS_MACOS
        dataDir=QDir::homePath()+QStringLiteral("/Library/Application Support/purr-pause");
#else
        dataDir=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+QStringLiteral("/purr-pause");
#endif
    }
    if (!QDir().mkpath(dataDir)) { std::fputs("Cannot create the application data directory.\n",stderr); return 1; }
    QLockFile lock(QDir(dataDir).filePath(QStringLiteral("native-instance.lock"))); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return lock.error()==QLockFile::LockFailedError ? 0 : 1;
    const QString assets=resources(QStringLiteral("assets"),QStringLiteral(PURR_SOURCE_ASSETS));
    const QString docs=resources(QStringLiteral("docs"),QStringLiteral(PURR_SOURCE_DOCS));
    app.setWindowIcon(QIcon(assets+QStringLiteral("/images/logo.png")));
    purr::Application controller(dataDir,assets,docs);
    if (parser.isSet(QStringLiteral("settings")) || parser.isSet(QStringLiteral("smoke-test"))) controller.showSettings();
    if (parser.isSet(QStringLiteral("test-break"))) QTimer::singleShot(100,&controller,&purr::Application::testBreak);
    if (parser.isSet(QStringLiteral("rocket-demo"))) QTimer::singleShot(800,&controller,&purr::Application::showRocketDemo);
    if (parser.isSet(QStringLiteral("smoke-test"))) QTimer::singleShot(1500,&app,&QApplication::quit);
    return app.exec();
}
