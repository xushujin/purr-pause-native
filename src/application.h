#pragma once
#include "core.h"
#include "messages.h"
#include "overlays.h"
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>

namespace purr {
class SettingsDialog;
class ActivationDialog;
class MessageSettingsDialog;
class MessageListDialog;
class RulesDialog;
class Application : public QObject {
    Q_OBJECT
public:
    Application(QString dataDir, QString assetsDir, QString docsDir, QObject *parent = nullptr);
    ~Application() override;
    void showSettings();
    void testBreak();
    void showRocket(int count = 3, const QString &name = QStringLiteral("绘家科技"));
    void showRocketDemo();
private:
    void rebuildMenu();
    void updateStatus();
    void showActivation();
    void showMessages();
    void showMessageSettings();
    void showRules();
    void applyConfiguration();
    bool saveConfiguration(QJsonObject changes, QWidget *owner);
    void writeAssetReadmes();
    void log(const QString &message);
    QString asset(const QString &filename, bool notify = false) const;
    QString licenseText();
    ConfigStore config_;
    LicenseManager license_;
    BreakTimer timer_;
    MessageService messages_;
    BreakOverlay overlay_;
    QSystemTrayIcon tray_;
    QMenu menu_;
    QTimer monitor_;
    QTimer statusTicker_;
    QTimer pauseDeadline_;
    QPointer<SettingsDialog> settings_;
    QPointer<ActivationDialog> activation_;
    QPointer<MessageSettingsDialog> messageSettings_;
    QPointer<MessageListDialog> messageList_;
    QPointer<RulesDialog> rules_;
    QPointer<RocketWindow> rocket_;
    QPointer<RocketWindow> demo_;
    QPointer<QWidget> fallback_;
    QAction *remainingAction_ = nullptr;
    QAction *messagesAction_ = nullptr;
    QAction *licenseAction_ = nullptr;
    QString assetsDir_, docsDir_;
    bool licensedToRun_ = true;
    bool sleeping_ = false;
    qint64 lastSample_ = 0;
    int pendingRocketCount_ = 0;
    QString pendingRocketName_;
};
}
