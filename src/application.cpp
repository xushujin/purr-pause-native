#include "application.h"
#include "dialogs.h"
#include "platform.h"
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>

namespace purr {
namespace {
void bringForward(QWidget *w) { w->show(); w->raise(); w->activateWindow(); }
bool httpUrl(const QUrl &url) { return url.isValid() && !url.host().isEmpty() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https")); }
}
Application::Application(QString dataDir, QString assetsDir, QString docsDir, QObject *parent)
    : QObject(parent), config_(dataDir, this), license_(dataDir), timer_(this), messages_(this), overlay_(this), tray_(this), assetsDir_(std::move(assetsDir)), docsDir_(std::move(docsDir)) {
    tray_.setContextMenu(&menu_);
    tray_.setIcon(QIcon(assetsDir_+QStringLiteral("/images/tray-icon.png")));
    tray_.setToolTip(QStringLiteral("胖猫暂停一下（PurrPause）"));
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) { if (reason == QSystemTrayIcon::DoubleClick) showMessages(); });
    connect(&menu_, &QMenu::aboutToShow, this, &Application::rebuildMenu);
    connect(&timer_, &BreakTimer::changed, this, &Application::updateStatus);
    connect(&timer_, &BreakTimer::breakStarted, this, [this] {
        log(QStringLiteral("开始休息"));
        const auto conf = config_.data();
        overlay_.start(conf, asset(conf.value(QStringLiteral("walkVideo")).toString()), asset(conf.value(QStringLiteral("idleVideo")).toString()), timer_.snoozeCount());
        updateStatus();
    });
    connect(&timer_, &BreakTimer::breakEnded, this, [this] {
        overlay_.stop(); log(QStringLiteral("休息结束或延后")); updateStatus();
        if (pendingRocketCount_) QTimer::singleShot(500, this, [this] {
            if (!timer_.breaking() && pendingRocketCount_) { const int count = pendingRocketCount_; const auto name = pendingRocketName_; pendingRocketCount_ = 0; showRocket(count, name); }
        });
    });
    connect(&overlay_, &BreakOverlay::dismissed, this, [this] { timer_.finishBreak(); });
    connect(&overlay_, &BreakOverlay::snoozed, this, [this] { timer_.snooze(); });
    connect(&overlay_, &BreakOverlay::mediaError, this, [this](const QString &message) { log(message); });
    connect(&messages_, &MessageService::notifyRequested, this, &Application::showRocket);
    connect(&messages_, &MessageService::changed, this, [this] {
        updateStatus(); if (messageList_) messageList_->setState(messages_.state());
    });
    connect(&messages_, &MessageService::connectionTested, this, [this](const QString &id, bool ok, const QString &message, int count) {
        if (messageSettings_) messageSettings_->connectionResult(id, ok, message, count);
    });
    connect(&config_, &ConfigStore::changed, this, [this] { applyConfiguration(); writeAssetReadmes(); });
    pauseDeadline_.setSingleShot(true);
    pauseDeadline_.setTimerType(Qt::PreciseTimer);
    connect(&pauseDeadline_, &QTimer::timeout, &timer_, &BreakTimer::resume);
    monitor_.setInterval(10000);
    monitor_.setTimerType(Qt::PreciseTimer);
    connect(&monitor_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastSample_ && now-lastSample_ > 30000 && !sleeping_) { timer_.handleSuspend(lastSample_); timer_.handleResume(now); }
        lastSample_ = now;
        if (licensedToRun_ && !sleeping_) timer_.tick(now, platform::idleMilliseconds(), platform::sessionLocked());
    });
    platform::watchSleep(this, [this](bool sleeping) {
        sleeping_ = sleeping;
        if (sleeping) timer_.handleSuspend();
        else { timer_.handleResume(); lastSample_ = QDateTime::currentMSecsSinceEpoch(); }
    });
    platform::watchLock(this, [this](bool locked) { timer_.setLocked(locked); });
    licensedToRun_ = license_.canUse();
    applyConfiguration(); writeAssetReadmes(); rebuildMenu();
    tray_.show(); monitor_.start(); statusTicker_.setInterval(30000);
    connect(&statusTicker_, &QTimer::timeout, this, &Application::updateStatus); statusTicker_.start();
    log(QStringLiteral("原生版启动，版本 ") + QStringLiteral(PURR_VERSION));
    if (!licensedToRun_) QTimer::singleShot(0, this, &Application::showActivation);
    // GNOME without AppIndicator still needs a persistent, usable entry point.
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        fallback_ = new QWidget;
        fallback_->setWindowTitle(QStringLiteral("胖猫暂停一下（PurrPause）"));
        auto *layout = new QVBoxLayout(fallback_);
        auto *label = new QLabel(QStringLiteral("胖猫正在提醒你适时休息。\n当前桌面未提供系统托盘，请从这里打开菜单。")); label->setWordWrap(true); layout->addWidget(label);
        auto *button = new QPushButton(QStringLiteral("胖猫菜单")); button->setMenu(&menu_); layout->addWidget(button);
        auto *quit = new QPushButton(QStringLiteral("退出")); connect(quit, &QPushButton::clicked, qApp, &QApplication::quit); layout->addWidget(quit);
        fallback_->setWindowFlag(Qt::WindowCloseButtonHint, false); fallback_->resize(380, 150); fallback_->show();
    }
}
Application::~Application() {
    overlay_.stop();
    if (rocket_) delete rocket_;
    if (demo_) delete demo_;
    if (fallback_) delete fallback_;
    // Top-level windows have no QWidget parent but belong to this controller.
    for (QWidget *window : {static_cast<QWidget *>(settings_.data()), static_cast<QWidget *>(activation_.data()), static_cast<QWidget *>(messageSettings_.data()), static_cast<QWidget *>(messageList_.data()), static_cast<QWidget *>(rules_.data())}) {
        if (window) { window->disconnect(this); delete window; }
    }
    tray_.hide();
}
void Application::applyConfiguration() {
    auto config = config_.data(); timer_.configure(config);
    if (!licensedToRun_) config.insert(QStringLiteral("messageNotifyEnabled"), false);
    messages_.setConfig(config);
    if (!messages_.state().value(QStringLiteral("configured")).toBool()) {
        pendingRocketCount_ = 0; pendingRocketName_.clear(); if (rocket_) { delete rocket_; rocket_ = nullptr; }
    }
    updateStatus();
}
QString Application::licenseText() {
    const auto state = license_.status(); const auto status = state.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("expired")) return QStringLiteral("未激活");
    if (status == QStringLiteral("trial")) return QStringLiteral("试用中（剩余 %1 天）").arg(state.value(QStringLiteral("daysLeft")).toInt());
    return state.value(QStringLiteral("type")) == QStringLiteral("permanent") ? QStringLiteral("已激活（永久）") : QStringLiteral("已激活（剩余 %1 天）").arg(state.value(QStringLiteral("daysLeft")).toInt());
}
void Application::updateStatus() {
    const qint64 seconds = timer_.secondsUntilBreak();
    const QString timing = timer_.paused() ? QStringLiteral("监控已暂停") : QStringLiteral("距下次休息: %1 分钟").arg((seconds+59)/60);
    if (remainingAction_) { remainingAction_->setText(timing); remainingAction_->setVisible(timer_.paused() || !timer_.breaking()); }
    if (licenseAction_) licenseAction_->setText(licenseText());
    const int count = messages_.unreadCount();
    if (messagesAction_) messagesAction_->setText(QStringLiteral("待办消息（%1）").arg(count));
}
void Application::rebuildMenu() {
    remainingAction_ = messagesAction_ = licenseAction_ = nullptr; menu_.clear();
    licenseAction_=menu_.addAction(licenseText()); licenseAction_->setEnabled(false);
    remainingAction_=menu_.addAction(QString()); remainingAction_->setEnabled(false);
    messagesAction_=menu_.addAction(QString(), this, &Application::showMessages);
    menu_.addAction(QStringLiteral("消息提醒设置"), this, &Application::showMessageSettings); menu_.addSeparator();
    if (timer_.paused()) menu_.addAction(QStringLiteral("恢复监控"), this, [this] { pauseDeadline_.stop(); timer_.resume(); });
    else {
        auto *pause=menu_.addMenu(QStringLiteral("暂停监控"));
        for (int minutes : {30,60,120,0}) {
            const auto label = !minutes ? QStringLiteral("直到手动恢复") : minutes == 30 ? QStringLiteral("30 分钟") : QStringLiteral("%1 小时").arg(minutes/60);
            pause->addAction(label, this, [this,minutes] { pauseDeadline_.stop(); timer_.pause(minutes); if (minutes) pauseDeadline_.start(minutes*60000); });
        }
    }
    menu_.addAction(QStringLiteral("激活/续期"), this, &Application::showActivation);
    menu_.addAction(QStringLiteral("设置"), this, &Application::showSettings);
    menu_.addAction(QStringLiteral("计时规则"), this, &Application::showRules);
    menu_.addAction(QStringLiteral("查看日志"), this, [this] {
        const QString path = config_.directory()+QStringLiteral("/logs/purr-pause.log");
        if (QFile::exists(path)) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else QMessageBox::information(nullptr, QStringLiteral("运行日志"), QStringLiteral("尚无日志。可在设置中启用“运行日志”。"));
    });
    menu_.addAction(QStringLiteral("小火箭演示"), this, &Application::showRocketDemo);
    menu_.addAction(QStringLiteral("立即测试"), this, &Application::testBreak);
    menu_.addAction(QStringLiteral("重置计时"), this, [this] { timer_.reset(); });
    menu_.addSeparator(); auto *version=menu_.addAction(QStringLiteral("v") + QStringLiteral(PURR_VERSION)); version->setEnabled(false);
    menu_.addAction(QStringLiteral("退出"), qApp, &QApplication::quit); updateStatus();
}
bool Application::saveConfiguration(QJsonObject changes, QWidget *owner) {
    QString error;
    const auto report = [owner](const QString &message) {
        if (auto *dialog=qobject_cast<SettingsDialog *>(owner)) dialog->showSaveResult(false,message);
        else if (auto *dialog=qobject_cast<MessageSettingsDialog *>(owner)) dialog->showSaveResult(false,message);
        else QMessageBox::warning(owner,QStringLiteral("保存失败"),message);
    };
    const bool hasAuto = changes.contains(QStringLiteral("autoLaunch"));
    const bool previousAuto = platform::autostartEnabled();
    if (hasAuto && !platform::setAutostart(changes.take(QStringLiteral("autoLaunch")).toBool(), &error)) { report(error); return false; }
    if (!config_.save(changes, license_.isActivated(), &error)) {
        if (hasAuto) platform::setAutostart(previousAuto);
        report(error); return false;
    }
    log(QStringLiteral("设置已保存")); return true;
}
void Application::showSettings() {
    if (settings_) { bringForward(settings_); return; }
    settings_=new SettingsDialog(config_.data(), license_.isActivated(), platform::autostartEnabled());
    connect(settings_, &SettingsDialog::saved, this, [this](QJsonObject changes) { if (settings_ && saveConfiguration(changes, settings_)) settings_->showSaveResult(true); });
    bringForward(settings_);
}
void Application::showActivation() {
    if (activation_) { activation_->setStatus(license_.status()); bringForward(activation_); return; }
    activation_=new ActivationDialog(license_.status(), license_.machineId());
    connect(activation_, &ActivationDialog::activationRequested, this, [this](const QString &serial) {
        QString error;
        if (!license_.activate(serial, &error)) { if (activation_) activation_->showError(error); return; }
        licensedToRun_=true; applyConfiguration();
        if (activation_) {
            auto status=license_.status();
            QFile file(config_.directory()+QStringLiteral("/license.json"));
            if (file.open(QIODevice::ReadOnly)) status.insert(QStringLiteral("expiresAt"),QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("expiresAt")));
            activation_->setStatus(status);
            activation_->showSuccess(status);
            QTimer::singleShot(2000, activation_, &QDialog::accept);
        }
        log(QStringLiteral("激活成功"));
    });
    connect(activation_, &QDialog::finished, this, [this] { if (!license_.canUse()) qApp->quit(); });
    bringForward(activation_);
}
void Application::showMessages() {
    if (!messageList_) {
        messageList_=new MessageListDialog;
        connect(messageList_, &MessageListDialog::refreshRequested, this, [this] { messages_.refresh(true); });
        connect(messageList_, &MessageListDialog::settingsRequested, this, &Application::showMessageSettings);
        connect(messageList_, &MessageListDialog::openRequested, this, [this](const QString &source, const QString &id, const QUrl &url) {
            if (httpUrl(url) && QDesktopServices::openUrl(url)) messages_.dismiss(source,id);
        });
    }
    messageList_->setState(messages_.state()); bringForward(messageList_); messages_.refresh(true);
}
void Application::showMessageSettings() {
    if (messageSettings_) { bringForward(messageSettings_); return; }
    messageSettings_=new MessageSettingsDialog(config_.data());
    connect(messageSettings_, &MessageSettingsDialog::saved, this, [this](QJsonObject changes) { if (messageSettings_ && saveConfiguration(changes, messageSettings_)) messageSettings_->showSaveResult(true); });
    connect(messageSettings_, &MessageSettingsDialog::testRequested, &messages_, &MessageService::testSource);
    connect(messageSettings_, &MessageSettingsDialog::exportRequested, this, [this] {
        const auto path=QFileDialog::getSaveFileName(messageSettings_, QStringLiteral("下载消息提醒接口规范"), QDir::homePath()+QStringLiteral("/PurrPause-消息提醒接口规范-v1.md"), QStringLiteral("Markdown (*.md)"));
        if (path.isEmpty()) return;
        QFile input(docsDir_+QStringLiteral("/message-api-spec.md")); QSaveFile output(path);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) { QMessageBox::warning(messageSettings_, QStringLiteral("导出失败"), QStringLiteral("无法读取接口规范或写入目标文件。")); return; }
        const QByteArray bytes=input.readAll();
        if (output.write(bytes) != bytes.size() || !output.commit()) QMessageBox::warning(messageSettings_, QStringLiteral("导出失败"), output.errorString());
    });
    bringForward(messageSettings_);
}
void Application::showRules() { if (!rules_) rules_=new RulesDialog; bringForward(rules_); }
void Application::testBreak() { timer_.startBreak(true); }
void Application::showRocketDemo() {
    if (demo_) delete demo_;
    demo_=new RocketWindow(assetsDir_+QStringLiteral("/images/rocket.svg"), asset(config_.data().value(QStringLiteral("messageNotifyVideo")).toString(),true),3,QStringLiteral("绘家科技"));
    connect(demo_, &RocketWindow::finished, demo_, &QObject::deleteLater);
}
void Application::showRocket(int count, const QString &name) {
    if (count <= 0) return;
    if (rocket_ || timer_.breaking()) {
        if (!pendingRocketCount_) pendingRocketName_=name; else if (pendingRocketName_!=name) pendingRocketName_=QStringLiteral("待办消息");
        pendingRocketCount_+=count; return;
    }
    rocket_=new RocketWindow(assetsDir_+QStringLiteral("/images/rocket.svg"), asset(config_.data().value(QStringLiteral("messageNotifyVideo")).toString(),true),count,name);
    connect(rocket_, &RocketWindow::finished, this, [this] {
        if (rocket_) rocket_->deleteLater(); rocket_=nullptr;
        if (pendingRocketCount_) QTimer::singleShot(1500, this, [this] {
            if (pendingRocketCount_ && !rocket_ && !timer_.breaking()) { const auto count=pendingRocketCount_; const auto name=pendingRocketName_; pendingRocketCount_=0; showRocket(count,name); }
        });
    });
}
QString Application::asset(const QString &filename, bool notify) const {
    if (filename.isEmpty() || filename.contains('/') || filename.contains('\\') || filename==QStringLiteral("..")) return {};
    const auto config=config_.data(); QStringList directories;
    if (notify) directories << config.value(QStringLiteral("messageNotifyDir")).toString();
    directories << config.value(QStringLiteral("customWebmDir")).toString() << config_.directory()+QStringLiteral("/webm") << assetsDir_+QStringLiteral("/webm");
    for (const auto &dir : directories) if (!dir.isEmpty() && QFileInfo(dir+QLatin1Char('/')+filename).isFile()) return dir+QLatin1Char('/')+filename;
    return {};
}
void Application::writeAssetReadmes() {
    QDir().mkpath(config_.directory()+QStringLiteral("/webm"));
    QStringList directories{config_.directory()+QStringLiteral("/webm"),config_.data().value(QStringLiteral("customWebmDir")).toString(),config_.data().value(QStringLiteral("messageNotifyDir")).toString()};
    for (int i=0;i<directories.size();++i) {
        const auto dir=directories[i]; if (dir.isEmpty() || !QDir(dir).exists()) continue;
        const QString name=i==2?QStringLiteral("purr-pause-消息提醒素材说明.txt"):QStringLiteral("purr-pause-素材说明.txt");
        QFile reference(assetsDir_+QStringLiteral("/readme/")+name);
        QFile file(dir+QLatin1Char('/')+name);
        if (reference.open(QIODevice::ReadOnly) && file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) file.write(reference.readAll());
    }
}
void Application::log(const QString &message) {
    if (!config_.data().value(QStringLiteral("logEnabled")).toBool()) return;
    const QString dir=config_.directory()+QStringLiteral("/logs"); QDir().mkpath(dir);
    const QString path=dir+QStringLiteral("/purr-pause.log");
    if (QFileInfo(path).size()>=128*1024) {
        QFile::remove(dir+QStringLiteral("/purr-pause.4.log"));
        for (int i=3;i>=1;--i) QFile::rename(dir+QStringLiteral("/purr-pause.%1.log").arg(i),dir+QStringLiteral("/purr-pause.%1.log").arg(i+1));
        QFile::rename(path,dir+QStringLiteral("/purr-pause.1.log"));
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly|QIODevice::Append)) file.write((QStringLiteral("[%1] [INFO] %2\n").arg(QDateTime::currentDateTimeUtc().addSecs(8*3600).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),message)).toUtf8());
}
}
