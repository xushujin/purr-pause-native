#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

class InstallationTests : public QObject {
    Q_OBJECT
private:
    static bool copyDirectory(const QString &source,const QString &destination) {
        if (!QDir().mkpath(destination)) return false;
        for (const auto &entry:QDir(source).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot)) {
            const auto target=QDir(destination).filePath(entry.fileName());
            if (entry.isDir()) { if (!copyDirectory(entry.absoluteFilePath(),target)) return false; }
            else if (!QFile::copy(entry.absoluteFilePath(),target)) return false;
        }
        return true;
    }
private slots:
    void rejectsMissingAndCorruptResourcesWithoutSourceFallback() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString prefix=temporary.filePath(QStringLiteral("installed path with spaces"));
#ifdef Q_OS_MACOS
        const QString binary=prefix+QStringLiteral("/Contents/MacOS/purr-pause");
        const QString resources=prefix+QStringLiteral("/Contents/Resources");
#else
        const QString binary=prefix+QStringLiteral("/bin/purr-pause");
        const QString resources=prefix+QStringLiteral("/share/purr-pause");
#endif
        QVERIFY(QDir().mkpath(QFileInfo(binary).absolutePath()));
        QVERIFY(QFile::copy(QStringLiteral(PURR_TEST_EXECUTABLE),binary));
        QVERIFY(QFile::setPermissions(binary,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const QString report=temporary.filePath(QStringLiteral("report.json"));
        const auto run=[&](int expectedCode) {
            QProcess process; process.start(binary,{QStringLiteral("--verify-install"),QStringLiteral("--verification-report"),report});
            if (!process.waitForFinished(20000)) { process.kill(); process.waitForFinished(); return QJsonObject(); }
            if (process.exitCode()!=expectedCode || process.exitStatus()!=QProcess::NormalExit) return QJsonObject();
            QFile input(report); if (!input.open(QIODevice::ReadOnly)) return QJsonObject();
            return QJsonDocument::fromJson(input.readAll()).object();
        };
        const auto missing=run(2); QVERIFY(!missing.isEmpty()); QVERIFY(!missing.value(QStringLiteral("ok")).toBool());
        QVERIFY(copyDirectory(QStringLiteral(PURR_TEST_ASSETS),resources+QStringLiteral("/assets")));
        QVERIFY(QDir().mkpath(resources+QStringLiteral("/docs")));
        QVERIFY(QFile::copy(QStringLiteral(PURR_TEST_SOURCE "/docs/message-api-spec.md"),resources+QStringLiteral("/docs/message-api-spec.md")));
        const auto success=run(0); QVERIFY(!success.isEmpty()); QVERIFY(success.value(QStringLiteral("ok")).toBool());
        QCOMPARE(success.value(QStringLiteral("videos")).toArray().size(),2);
        QCOMPARE(success.value(QStringLiteral("assets")).toString(),resources+QStringLiteral("/assets"));
        QFile broken(resources+QStringLiteral("/assets/webm/cat-rest.webm")); QVERIFY(broken.open(QIODevice::WriteOnly|QIODevice::Truncate)); broken.write("invalid webm"); broken.close();
        const auto invalid=run(2); QVERIFY(!invalid.isEmpty()); QVERIFY(!invalid.value(QStringLiteral("ok")).toBool());
        QVERIFY(invalid.value(QStringLiteral("error")).toString().contains(QStringLiteral("视频")));
        QVERIFY(!QFile::exists(prefix+QStringLiteral("/license.json")));
    }
};
QTEST_GUILESS_MAIN(InstallationTests)
#include "test_installation.moc"
