#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

class InstanceTests : public QObject {
    Q_OBJECT
private slots:
    void secondLaunchExitsAndLockReleases() {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QStringList args{QStringLiteral("--smoke-test"),QStringLiteral("--data-dir"),data.path()};
        QProcess first;
        first.start(QStringLiteral(PURR_TEST_EXECUTABLE),args);
        QVERIFY(first.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(data.filePath(QStringLiteral("native-instance.lock"))),2000);
        QProcess second;
        second.start(QStringLiteral(PURR_TEST_EXECUTABLE),args);
        QVERIFY(second.waitForStarted());
        QVERIFY(second.waitForFinished(1000));
        QCOMPARE(second.exitStatus(),QProcess::NormalExit);
        QCOMPARE(second.exitCode(),0);
        QCOMPARE(first.state(),QProcess::Running);
        QVERIFY(first.waitForFinished(8000));
        QCOMPARE(first.exitCode(),0);
        QVERIFY(!QFileInfo::exists(data.filePath(QStringLiteral("native-instance.lock"))));
        QProcess again;
        again.start(QStringLiteral(PURR_TEST_EXECUTABLE),args);
        QVERIFY(again.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(data.filePath(QStringLiteral("native-instance.lock"))),2000);
        QVERIFY(again.waitForFinished(8000));
        QCOMPARE(again.exitCode(),0);
    }
};
QTEST_GUILESS_MAIN(InstanceTests)
#include "test_instance.moc"
