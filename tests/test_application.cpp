#include "application.h"
#include "dialogs.h"
#include <QApplication>
#include <QJsonDocument>
#include <QFile>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace purr;
class ApplicationTests : public QObject {
    Q_OBJECT
private:
    QList<RocketWindow *> rockets() const {
        QList<RocketWindow *> result;
        for (auto *window : QApplication::topLevelWidgets()) if (auto *rocket=qobject_cast<RocketWindow *>(window)) result.append(rocket);
        return result;
    }
private slots:
    void saveKeepsSettingsOpenAndPersists() {
        QTemporaryDir data;
        Application controller(data.path(),QStringLiteral(PURR_TEST_ASSETS),QStringLiteral(PURR_TEST_SOURCE "/docs"));
        controller.showSettings();
        SettingsDialog *dialog=nullptr;
        for (auto *window : QApplication::topLevelWidgets()) if (auto *settings=qobject_cast<SettingsDialog *>(window)) dialog=settings;
        QVERIFY(dialog);
        // Exercise the complete save handler, not just the dialog's signal.
        QMetaObject::invokeMethod(dialog,"saved",Qt::DirectConnection,Q_ARG(QJsonObject,QJsonObject({{QStringLiteral("thresholdMinutes"),27}})));
        QVERIFY(dialog->isVisible());
        QFile file(data.filePath(QStringLiteral("config.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("thresholdMinutes")).toInt(),27);
    }
    void notificationsDuringRocketAndBreakMergeOnce() {
        QTemporaryDir data;
        Application controller(data.path(),QStringLiteral(PURR_TEST_ASSETS),QStringLiteral(PURR_TEST_SOURCE "/docs"));
        auto *timer=controller.findChild<BreakTimer *>();
        QVERIFY(timer);
        controller.showRocket(2,QStringLiteral("工单"));
        controller.showRocket(3,QStringLiteral("工单"));
        controller.testBreak();
        controller.showRocket(4,QStringLiteral("审批"));
        QTRY_VERIFY_WITH_TIMEOUT(rockets().isEmpty(),10000);
        QVERIFY(timer->breaking());
        timer->finishBreak();
        QTRY_COMPARE_WITH_TIMEOUT(rockets().size(),1,2000);
        QCOMPARE(rockets().first()->count(),7);
        QCOMPARE(rockets().first()->sourceName(),QStringLiteral("待办消息"));
        // The original demo is independent and replaces itself on repeat clicks.
        controller.showRocketDemo();
        QCOMPARE(rockets().size(),2);
        controller.showRocketDemo();
        QCOMPARE(rockets().size(),2);
    }
    void activationWindowUsesOriginalSerialAndClosesAfterSuccess() {
        const auto node=QStandardPaths::findExecutable(QStringLiteral("node"));
        if (node.isEmpty()) QSKIP("Original key generator requires Node for reference verification");
        const auto keygen=QFINDTESTDATA("reference/tools/keygen.js");
        QVERIFY(!keygen.isEmpty());
        QTemporaryDir data;
        Application controller(data.path(),QStringLiteral(PURR_TEST_ASSETS),QStringLiteral(PURR_TEST_SOURCE "/docs"));
        auto *tray=controller.findChild<QSystemTrayIcon *>(); QVERIFY(tray);
        QAction *activateAction=nullptr;
        for (auto *action:tray->contextMenu()->actions()) if (action->text()==QStringLiteral("激活/续期")) activateAction=action;
        QVERIFY(activateAction); activateAction->trigger();
        QPointer<ActivationDialog> dialog;
        for (auto *window:QApplication::topLevelWidgets()) if (auto *candidate=qobject_cast<ActivationDialog *>(window)) dialog=candidate;
        QVERIFY(dialog); QVERIFY(dialog->isVisible());
        auto *machine=dialog->findChild<QLineEdit *>(QStringLiteral("machineId")); QVERIFY(machine);
        QProcess generator;
        generator.start(node,{keygen,QStringLiteral("--mid"),machine->text(),QStringLiteral("--permanent")});
        QVERIFY(generator.waitForFinished(5000)); QCOMPARE(generator.exitCode(),0);
        const auto match=QRegularExpression(QStringLiteral("PP-[A-Za-z0-9+/=\\-]+")).match(QString::fromUtf8(generator.readAllStandardOutput()));
        QVERIFY(match.hasMatch());
        auto *serial=dialog->findChild<QPlainTextEdit *>(QStringLiteral("serialKey")); QVERIFY(serial);
        serial->setPlainText(match.captured());
        QPushButton *activate=nullptr;
        for (auto *button:dialog->findChildren<QPushButton *>()) if (button->text()==QStringLiteral("激活")) activate=button;
        QVERIFY(activate); QVERIFY(activate->isEnabled());
        activate->click();
        auto *feedback=dialog->findChild<QLabel *>(QStringLiteral("activationError")); QVERIFY(feedback);
        QVERIFY(feedback->text().contains(QStringLiteral("激活成功")));
        QVERIFY(dialog->isVisible());
        QFile file(data.filePath(QStringLiteral("license.json"))); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto license=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(license.value(QStringLiteral("type")).toString(),QStringLiteral("permanent"));
        QCOMPARE(license.value(QStringLiteral("machineId")).toString(),machine->text());
        QTRY_VERIFY_WITH_TIMEOUT(dialog.isNull(),3500);
    }
};
QTEST_MAIN(ApplicationTests)
#include "test_application.moc"
