#include "core.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace purr;

namespace {
constexpr auto serialFixture = "PP-AAAAHgEj-RWeJq83v-WntkAwKx-tx1IPuCK-bLvI7keh-gYjoMx/q-7x4xTVhx-JudZ/Gq7-Y2oEBZPb-KfgF9sOJ-M1VjMcjW-2Qoqaab3-daheDw==";

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Use a primary mark to keep the tests away from the real user's secondary mark.
bool seedInstallMark(const QString &directory, const QString &machine, const QString &date) {
    const QByteArray publicKey("-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEAqxoQObh1uXH1AphIbnRj5XTQN9T+RoGk51fqNDU88HA=\n-----END PUBLIC KEY-----");
    const auto key = QCryptographicHash::hash(QByteArray("purr-pause-internal-") + publicKey, QCryptographicHash::Sha256).toHex().left(32);
    const auto signature = QMessageAuthenticationCode::hash((date + machine).toUtf8(), key, QCryptographicHash::Sha256).toHex().left(16);
    return writeFile(QDir(directory).filePath(QStringLiteral(".install_mark")), QJsonDocument(QJsonObject{
        {QStringLiteral("date"), date}, {QStringLiteral("mid"), machine}, {QStringLiteral("sig"), QString::fromLatin1(signature)}
    }).toJson(QJsonDocument::Compact));
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void configurationMigrationAndPersistence();
    void originalConfigurationSaveParity();
    void invalidConfigurationIsAtomic();
    void trialCannotChangeLicensedSettings();
    void timerIdleBoundaries();
    void timerPauseAndLock();
    void originalTimerIdleBoundaries();
    void lockEventsBetweenTimerSamples();
    void snoozeAndSleepRules();
    void dismissalCooldown();
    void originalEd25519Fixture();
    void originalLicenseIntegrityFixture();
    void trialCalendarAndTampering();
    void originalTrialCalendarParity();
    void originalKeygenActivationAndRenewalParity();
    void machineIdMatchesNode();
};

void CoreTests::configurationMigrationAndPersistence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("config.json"));
    const QByteArray originalConfiguration(R"({"thresholdMinutes":999,"messageApiBaseUrl":"https://example.test/old/path","messageAuthHeaders":"{\"Authorization\":\"test-token\"}","messageNotifyEnabled":true,"messageMaxCacheItems":99,"messageListLimit":80,"futureOption":{"preserve":true}})");
    QVERIFY(writeFile(path, originalConfiguration));
    ConfigStore store(temporary.path());
    QCOMPARE(store.data().value(QStringLiteral("thresholdMinutes")).toInt(), 480);
    QCOMPARE(store.data().value(QStringLiteral("messageMaxCacheItems")).toInt(), 30);
    QVERIFY(!store.data().contains(QStringLiteral("messageListLimit")));
    auto sources = store.data().value(QStringLiteral("messageSources")).toArray();
    QCOMPARE(sources.size(), 1);
    QCOMPARE(sources.first().toObject().value(QStringLiteral("listUrl")).toString(), QStringLiteral("https://example.test/purr-pause/v1/todos"));
    QVERIFY(sources.first().toObject().value(QStringLiteral("enabled")).toBool());
    QCOMPARE(store.data().value(QStringLiteral("messageApiBaseUrl")).toString(), QString());
    QString error;
    QVERIFY2(store.save({{QStringLiteral("breakMinutes"), 12}, {QStringLiteral("messageSources"), QJsonArray()}}, false, &error), qPrintable(error));
    ConfigStore reloaded(temporary.path());
    QCOMPARE(reloaded.data().value(QStringLiteral("breakMinutes")).toInt(), 12);
    QVERIFY(reloaded.data().value(QStringLiteral("messageSources")).toArray().isEmpty());
    QVERIFY(reloaded.data().value(QStringLiteral("futureOption")).toObject().value(QStringLiteral("preserve")).toBool());
}

void CoreTests::originalConfigurationSaveParity() {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) QSKIP("Node.js not installed; original behavior comparison skipped");
    const auto original = QFINDTESTDATA("reference/behavior/save-config.js");
    QVERIFY2(!original.isEmpty(), "Required original reference fixture is missing");
    const QJsonArray inputs{0, QStringLiteral("0"), 0.9, -0.9, 12.5, QStringLiteral("12.5"),
        QStringLiteral("999999999999999999999999"), QStringLiteral("0x10"), 1e21, 2e-7, QJsonValue::Null};
    const QString script = QString::fromUtf8(R"JS(
const fs = require('fs'), vm = require('vm');
const code = fs.readFileSync(process.argv[1], 'utf8');
const results = JSON.parse(process.argv[2]).map(value => {
  const config = { animationMode: 'fade-center' };
  let handler;
  const context = { config, ipcMain: { on: (_, fn) => handler = fn }, configLib: { save() {} }, assets: {},
    license: { checkStatus: () => ({ status: 'active' }) }, app: { getPath: () => '' },
    logger: { info() {}, setEnabled() {} }, process: { argv: [] }, USER_CONFIG_PATH: '',
    messageNotify: null, updateTray() {}, rebuildTrayMenu() {} };
  vm.runInNewContext(code, context);
  handler({ sender: { send() {} } }, { thresholdMinutes: value, breakMinutes: value, animationMode: 'invalid' });
  return config;
});
process.stdout.write(JSON.stringify(results));
)JS");
    QProcess process;
    process.start(node, {QStringLiteral("-e"), script, original,
        QString::fromUtf8(QJsonDocument(inputs).toJson(QJsonDocument::Compact))});
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitCode(), 0);
    const auto expected = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
    QCOMPARE(expected.size(), inputs.size());
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        QTemporaryDir temporary;
        ConfigStore store(temporary.path());
        QVERIFY(store.save({{QStringLiteral("animationMode"), QStringLiteral("fade-center")}}, true));
        QVERIFY(store.save({{QStringLiteral("thresholdMinutes"), inputs.at(index)},
            {QStringLiteral("breakMinutes"), inputs.at(index)}, {QStringLiteral("animationMode"), QStringLiteral("invalid")}}, true));
        const auto oracle = expected.at(index).toObject();
        for (auto it = oracle.begin(); it != oracle.end(); ++it)
            QCOMPARE(store.data().value(it.key()), it.value());
    }
}

void CoreTests::invalidConfigurationIsAtomic() {
    QTemporaryDir temporary;
    ConfigStore store(temporary.path());
    QVERIFY(store.save({}, false));
    const auto before = store.data();
    QFile file(temporary.filePath(QStringLiteral("config.json")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto beforeBytes = file.readAll();
    file.close();
    QString error;
    const QJsonObject source{{QStringLiteral("name"), QStringLiteral("任务")},
        {QStringLiteral("listUrl"), QStringLiteral("file:///etc/passwd")},
        {QStringLiteral("enabled"), true}, {QStringLiteral("authHeaders"), QStringLiteral("{}")}};
    QVERIFY(!store.save({{QStringLiteral("messageSources"), QJsonArray{source}}, {QStringLiteral("breakMinutes"), 20}}, false, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.data(), before);
    QVERIFY(!store.save({{QStringLiteral("messageNotifyVideo"), QStringLiteral("../secret.webm")}}, true, &error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), beforeBytes);
    auto invalidHeader = source;
    invalidHeader.insert(QStringLiteral("listUrl"), QStringLiteral("https://example.test/todos"));
    invalidHeader.insert(QStringLiteral("authHeaders"), QStringLiteral("[]"));
    QVERIFY(!store.save({{QStringLiteral("messageSources"), QJsonArray{invalidHeader}}}, false, &error));
}

void CoreTests::trialCannotChangeLicensedSettings() {
    QTemporaryDir temporary;
    ConfigStore store(temporary.path());
    const QJsonObject changes{{QStringLiteral("animationMode"), QStringLiteral("fade-center")},
        {QStringLiteral("customWebmDir"), temporary.path()}, {QStringLiteral("thresholdMinutes"), 30}};
    QVERIFY(store.save(changes, false));
    QCOMPARE(store.data().value(QStringLiteral("animationMode")).toString(), QStringLiteral("walk-center"));
    QCOMPARE(store.data().value(QStringLiteral("customWebmDir")).toString(), QString());
    QCOMPARE(store.data().value(QStringLiteral("thresholdMinutes")).toInt(), 30);
    QVERIFY(store.save(changes, true));
    QCOMPARE(store.data().value(QStringLiteral("animationMode")).toString(), QStringLiteral("fade-center"));
    QCOMPARE(store.data().value(QStringLiteral("customWebmDir")).toString(), temporary.path());
}

void CoreTests::timerIdleBoundaries() {
    BreakTimer timer;
    timer.configure({{QStringLiteral("thresholdMinutes"), 1}});
    QSignalSpy started(&timer, &BreakTimer::breakStarted);
    timer.tick(1000000, 179999, false);
    QCOMPARE(timer.activeSeconds(), 10);
    timer.tick(1010000, 180000, false);
    timer.tick(1020000, 600000, false);
    QCOMPARE(timer.activeSeconds(), 10);
    timer.tick(1030000, 600001, false);
    QCOMPARE(timer.activeSeconds(), 0);
    for (int i = 0; i < 6; ++i) timer.tick(1040000 + i * 10000, 0, false);
    QCOMPARE(started.size(), 1);
    QVERIFY(timer.breaking());
    QCOMPARE(timer.activeSeconds(), 60);
    timer.tick(1100000, 0, false);
    QCOMPARE(timer.activeSeconds(), 60);
    QCOMPARE(started.size(), 1);
}

void CoreTests::timerPauseAndLock() {
    BreakTimer timer;
    timer.tick(1000000, 0, false);
    timer.pause(1, 1000000);
    timer.tick(1059999, 0, false);
    QCOMPARE(timer.activeSeconds(), 10);
    QVERIFY(timer.paused());
    timer.tick(1060000, 0, false);
    QVERIFY(!timer.paused());
    QCOMPARE(timer.activeSeconds(), 20);
    timer.pause(0, 1070000);
    timer.tick(100000000, 0, false);
    QVERIFY(timer.paused());
    QCOMPARE(timer.activeSeconds(), 20);
    timer.resume();
    timer.tick(100010000, 0, true);
    timer.tick(100610000, 180000, false); // Exactly ten minutes keeps accumulated time.
    QCOMPARE(timer.activeSeconds(), 20);
    timer.tick(100620000, 0, true);
    timer.tick(101220001, 180000, false);
    QCOMPARE(timer.activeSeconds(), 0);
}

void CoreTests::originalTimerIdleBoundaries() {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) QSKIP("Node.js not installed; original behavior comparison skipped");
    const auto original = QFINDTESTDATA("reference/behavior/start-monitoring.js");
    QVERIFY2(!original.isEmpty(), "Required original reference fixture is missing");
    const QJsonArray samples{0, 179999, 180000, 600000, 600001, 0, 0, 0, 0, 0, 0};
    const QString script = QString::fromUtf8(R"JS(
const fs = require('fs'), vm = require('vm');
const code = fs.readFileSync(process.argv[1], 'utf8');
let tick, idle;
const context = { monitorInterval: null, isOverlayShowing: false, isPaused: false, isScreenLocked: false,
  idleCheckPending: false, activeSeconds: 0, snoozeThreshold: 0, config: { thresholdMinutes: 1 },
  detectScreenLock() {}, setInterval: fn => (tick = fn, 1), getIdleTimeMs: fn => fn(idle),
  logger: { info() {} }, updateTray() {}, triggerCat() { context.isOverlayShowing = true; } };
vm.runInNewContext(code + '\nstartMonitoring();', context);
process.stdout.write(JSON.stringify(JSON.parse(process.argv[2]).map(value => {
  idle = value; tick();
  return { activeSeconds: context.activeSeconds, breaking: context.isOverlayShowing };
})));
)JS");
    QProcess process;
    process.start(node, {QStringLiteral("-e"), script, original,
        QString::fromUtf8(QJsonDocument(samples).toJson(QJsonDocument::Compact))});
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitCode(), 0);
    const auto expected = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
    QCOMPARE(expected.size(), samples.size());
    BreakTimer timer;
    timer.configure({{QStringLiteral("thresholdMinutes"), 1}});
    for (qsizetype index = 0; index < samples.size(); ++index) {
        timer.tick(1000000 + index * 10000, samples.at(index).toInt(), false);
        QCOMPARE(timer.activeSeconds(), expected.at(index).toObject().value(QStringLiteral("activeSeconds")).toInt());
        QCOMPARE(timer.breaking(), expected.at(index).toObject().value(QStringLiteral("breaking")).toBool());
    }
}

void CoreTests::lockEventsBetweenTimerSamples() {
    BreakTimer timer;
    timer.tick(1000000, 0, false);
    timer.pause(0, 1000000);
    timer.setLocked(true, 1000001);
    timer.setLocked(false, 1600001);
    QCOMPARE(timer.activeSeconds(), 10); // Exactly ten minutes preserves time.
    timer.setLocked(true, 1600002);
    timer.setLocked(false, 2200003);
    QCOMPARE(timer.activeSeconds(), 0); // Unlock resets even while monitoring is paused.
    timer.resume();
    timer.tick(2210000, 0, false);
    QCOMPARE(timer.activeSeconds(), 10);
    QVERIFY(timer.startBreak(true, 2210001));
    timer.setLocked(true, 2210002);
    timer.setLocked(false, 2810003);
    QCOMPARE(timer.activeSeconds(), 0); // Lock notifications also apply during an overlay.
    QVERIFY(timer.breaking());
}

void CoreTests::snoozeAndSleepRules() {
    BreakTimer timer;
    QVERIFY(timer.startBreak(true, 1000000));
    QVERIFY(timer.snooze());
    QCOMPARE(timer.secondsUntilBreak(), 300);
    QCOMPARE(timer.snoozeCount(), 1);
    timer.tick(1010000, 0, false);
    timer.tick(1020000, 600001, false);
    QCOMPARE(timer.secondsUntilBreak(), 300); // Idle reset preserves a snooze.
    timer.handleSuspend(1030000);
    timer.tick(1040000, 0, false);
    QCOMPARE(timer.activeSeconds(), 0);
    timer.handleResume(1630000);
    QCOMPARE(timer.secondsUntilBreak(), 300); // Exactly ten minutes preserves it.
    timer.handleSuspend(1640000);
    timer.handleResume(2240001);
    QCOMPARE(timer.secondsUntilBreak(), 45 * 60);
    QCOMPARE(timer.snoozeCount(), 1);
    QVERIFY(timer.startBreak(true, 2250000));
    QVERIFY(timer.snooze());
    QCOMPARE(timer.snoozeCount(), 2);
    QVERIFY(timer.startBreak(true, 2260000));
    QVERIFY(!timer.snooze());
    QVERIFY(timer.breaking());
    timer.finishBreak(2270000);
    QCOMPARE(timer.snoozeCount(), 0);
    QCOMPARE(timer.secondsUntilBreak(), 45 * 60);
}

void CoreTests::dismissalCooldown() {
    BreakTimer timer;
    QVERIFY(timer.startBreak(true, 1000000));
    QVERIFY(!timer.startBreak(true, 1000001));
    timer.finishBreak(1000000);
    QVERIFY(!timer.startBreak(false, 1059999));
    QVERIFY(timer.startBreak(false, 1060000));
    timer.finishBreak(1060000);
    QVERIFY(timer.startBreak(true, 1060001));
}

void CoreTests::originalEd25519Fixture() {
    const auto result = LicenseManager::verifySerial(QString::fromLatin1(serialFixture));
    QCOMPARE(result.value(QStringLiteral("days")).toInt(), 30);
    QCOMPARE(result.value(QStringLiteral("machineId")).toString(), QStringLiteral("0123456789abcdef"));
    auto damaged = QString::fromLatin1(serialFixture);
    damaged.replace(10, 1, QStringLiteral("A"));
    QVERIFY(LicenseManager::verifySerial(damaged).isEmpty());
    QVERIFY(LicenseManager::verifySerial(QStringLiteral("invalid")).isEmpty());
    QTemporaryDir temporary;
    LicenseManager manager(temporary.path());
    QString error;
    QVERIFY(!manager.activate(QString::fromLatin1(serialFixture), &error));
    QCOMPARE(error, QStringLiteral("序列号与本机不匹配"));
}

void CoreTests::originalLicenseIntegrityFixture() {
    QTemporaryDir temporary;
    // Signatures generated by the original Node.js implementation, not this port.
    const QByteArray permanent(R"({"installDate":"2026-01-01T00:00:00.000Z","serialKey":"original-electron-fixture","activatedAt":"2026-01-02T08:00:00.000Z","expiresAt":null,"type":"permanent","usedSerials":[],"_sig":"0c9b83b895e060f082b7bc7794b9abf4e3454b6471f7791e4056a7f5f5a2a385"})");
    QVERIFY(writeFile(temporary.filePath(QStringLiteral("license.json")), permanent));
    LicenseManager manager(temporary.path());
    QVERIFY(seedInstallMark(temporary.path(), manager.machineId(), QStringLiteral("2026-01-01T00:00:00.000Z")));
    QCOMPARE(manager.status(1768000000000).value(QStringLiteral("status")).toString(), QStringLiteral("active"));
    QCOMPARE(manager.status().value(QStringLiteral("type")).toString(), QStringLiteral("permanent"));
    const QByteArray monthly(R"({"installDate":"2026-01-01T00:00:00.000Z","serialKey":"original-electron-fixture","activatedAt":"2026-01-02T08:00:00.000Z","expiresAt":"2026-02-01T08:00:00.000Z","type":"monthly","usedSerials":[],"_sig":"2d8e15155c7ebd91f5aa875b53b66a2c57dda9be377c0a2ee72398c8dbf44e5a"})");
    QVERIFY(writeFile(temporary.filePath(QStringLiteral("license.json")), monthly));
    const auto expiry = QDateTime::fromString(QStringLiteral("2026-02-01T08:00:00.000Z"), Qt::ISODateWithMs).toMSecsSinceEpoch();
    QCOMPARE(manager.status(expiry - 1).value(QStringLiteral("status")).toString(), QStringLiteral("active"));
    QCOMPARE(manager.status(expiry).value(QStringLiteral("status")).toString(), QStringLiteral("expired"));
}

void CoreTests::trialCalendarAndTampering() {
    QTemporaryDir temporary;
    LicenseManager manager(temporary.path());
    const QDateTime installed(QDate(2026, 1, 4), QTime(12, 30), Qt::LocalTime);
    QVERIFY(seedInstallMark(temporary.path(), manager.machineId(), installed.toUTC().toString(Qt::ISODateWithMs)));
    auto state = manager.status(installed.toMSecsSinceEpoch());
    QCOMPARE(state.value(QStringLiteral("status")).toString(), QStringLiteral("trial"));
    QCOMPARE(state.value(QStringLiteral("daysLeft")).toInt(), 7);
    const QDateTime end(QDate(2026, 1, 11), QTime(23, 59, 59, 999), Qt::LocalTime);
    QCOMPARE(manager.status(end.toMSecsSinceEpoch() - 1).value(QStringLiteral("status")).toString(), QStringLiteral("trial"));
    QCOMPARE(manager.status(end.toMSecsSinceEpoch()).value(QStringLiteral("status")).toString(), QStringLiteral("expired"));
    const auto licensePath = temporary.filePath(QStringLiteral("license.json"));
    QFile file(licensePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto forged = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    forged.insert(QStringLiteral("type"), QStringLiteral("permanent"));
    QVERIFY(writeFile(licensePath, QJsonDocument(forged).toJson()));
    QCOMPARE(manager.status(end.toMSecsSinceEpoch() + 1).value(QStringLiteral("status")).toString(), QStringLiteral("expired"));
    QVERIFY(QFile::remove(licensePath));
    QCOMPARE(manager.status(end.toMSecsSinceEpoch() + 1).value(QStringLiteral("status")).toString(), QStringLiteral("expired"));
}

void CoreTests::originalTrialCalendarParity() {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) QSKIP("Node.js not installed; original behavior comparison skipped");
    const auto original = QFINDTESTDATA("reference/lib/license.js");
    QVERIFY2(!original.isEmpty(), "Required original reference fixture is missing");
    const QString script = QString::fromUtf8(R"JS(
const RealDate = Date;
let current;
global.Date = class extends RealDate {
  constructor(...args) { super(...(args.length ? args : [current])); }
  static now() { return current; }
};
const original = require(process.argv[1]);
process.stdout.write(JSON.stringify(JSON.parse(process.argv[3]).map(time => {
  current = time;
  return original.checkStatus(process.argv[2]);
})));
)JS");
    // These windows cross the spring/fall daylight-saving changes when run
    // under America/New_York as well as ordinary calendar days in UTC/Shanghai.
    for (const auto &date : {QDate(2026, 3, 6), QDate(2026, 10, 30)}) {
        QTemporaryDir temporary;
        LicenseManager manager(temporary.path());
        const QDateTime installed(date, QTime(12, 30), Qt::LocalTime);
        const QDateTime end(date.addDays(7), QTime(23, 59, 59, 999), Qt::LocalTime);
        QVERIFY(seedInstallMark(temporary.path(), manager.machineId(), installed.toUTC().toString(Qt::ISODateWithMs)));
        // Exercise both directions: original reads a native file in spring,
        // then native reads the original's newly created file in autumn.
        if (date.month() == 3) manager.status(installed.toMSecsSinceEpoch());
        const QJsonArray times{installed.toMSecsSinceEpoch(), installed.addDays(2).toMSecsSinceEpoch(),
            installed.addDays(7).toMSecsSinceEpoch(), end.toMSecsSinceEpoch() - 1,
            end.toMSecsSinceEpoch(), end.toMSecsSinceEpoch() + 1};
        QProcess process;
        process.start(node, {QStringLiteral("-e"), script, original, temporary.path(),
            QString::fromUtf8(QJsonDocument(times).toJson(QJsonDocument::Compact))});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 0);
        const auto expected = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
        QCOMPARE(expected.size(), times.size());
        for (qsizetype index = 0; index < times.size(); ++index)
            QCOMPARE(manager.status(times.at(index).toVariant().toLongLong()), expected.at(index).toObject());
    }
}

void CoreTests::machineIdMatchesNode() {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) QSKIP("Node.js not installed; original interoperability test skipped");
    const auto original = QFINDTESTDATA("reference/lib/license.js");
    QVERIFY2(!original.isEmpty(), "Required original reference fixture is missing");
    QProcess process;
    process.start(node, {QStringLiteral("-e"), QStringLiteral("process.stdout.write(require(process.argv[1]).getMachineId())"), original});
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitCode(), 0);
    QTemporaryDir temporary;
    LicenseManager manager(temporary.path());
    QCOMPARE(manager.machineId(), QString::fromUtf8(process.readAllStandardOutput()));
}

void CoreTests::originalKeygenActivationAndRenewalParity() {
    const auto node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) QSKIP("Node.js not installed; original keygen comparison skipped");
    const auto keygen = QFINDTESTDATA("reference/tools/keygen.js");
    const auto original = QFINDTESTDATA("reference/lib/license.js");
    QVERIFY(!keygen.isEmpty()); QVERIFY(!original.isEmpty());
    QTemporaryDir nativeDirectory, originalDirectory;
    LicenseManager manager(nativeDirectory.path());
    const auto installed = QDateTime::fromString(QStringLiteral("2026-01-01T12:00:00.000Z"), Qt::ISODateWithMs);
    for (const auto &directory : {nativeDirectory.path(), originalDirectory.path()})
        QVERIFY(seedInstallMark(directory, manager.machineId(), installed.toString(Qt::ISODateWithMs)));
    const QString script=QString::fromUtf8(R"JS(
const [modulePath, directory, serial, instant] = process.argv.slice(1);
const RealDate=Date;
global.Date=class extends RealDate {
  constructor(...args) { super(...(args.length ? args : [Number(instant)])); }
  static now() { return Number(instant); }
};
const license=require(modulePath);
const result=license.activate(directory,serial);
const status=license.checkStatus(directory);
if (status.daysLeft===Infinity) status.daysLeft=-1;
process.stdout.write(JSON.stringify({result,status}));
)JS");
    for (int days : {30,90,0,365}) {
        QProcess generator;
        QStringList arguments{keygen,QStringLiteral("--mid"),manager.machineId()};
        if (days) arguments << QStringLiteral("--days") << QString::number(days);
        else arguments << QStringLiteral("--permanent");
        generator.start(node,arguments);
        QVERIFY(generator.waitForFinished(5000)); QCOMPARE(generator.exitCode(),0);
        const auto match=QRegularExpression(QStringLiteral("PP-[A-Za-z0-9+/=\\-]+")).match(QString::fromUtf8(generator.readAllStandardOutput()));
        QVERIFY(match.hasMatch());
        const auto serial=match.captured();
        const qint64 now=installed.addDays(10+days).toMSecsSinceEpoch();
        for (bool reuse : {false,true}) {
            QString error;
            const bool activated=manager.activate(serial,&error,now);
            QProcess reference;
            reference.start(node,{QStringLiteral("-e"),script,original,originalDirectory.path(),serial,QString::number(now)});
            QVERIFY(reference.waitForFinished(5000)); QCOMPARE(reference.exitCode(),0);
            const auto expected=QJsonDocument::fromJson(reference.readAllStandardOutput()).object();
            QCOMPARE(activated,expected.value(QStringLiteral("result")).toObject().value(QStringLiteral("success")).toBool());
            QCOMPARE(activated,!reuse);
            if (reuse) QCOMPARE(error,expected.value(QStringLiteral("result")).toObject().value(QStringLiteral("error")).toString());
            QCOMPARE(manager.status(now),expected.value(QStringLiteral("status")).toObject());
            QFile nativeFile(nativeDirectory.filePath(QStringLiteral("license.json")));
            QFile originalFile(originalDirectory.filePath(QStringLiteral("license.json")));
            QVERIFY(nativeFile.open(QIODevice::ReadOnly)); QVERIFY(originalFile.open(QIODevice::ReadOnly));
            // Compare the full persisted license, including the HMAC, timestamps
            // and used-serial list produced by the real original key generator.
            QCOMPARE(QJsonDocument::fromJson(nativeFile.readAll()).object(),QJsonDocument::fromJson(originalFile.readAll()).object());
        }
        if (days) {
            QCOMPARE(manager.status(now+qint64(days)*86400000-1).value(QStringLiteral("status")).toString(),QStringLiteral("active"));
            QCOMPARE(manager.status(now+qint64(days)*86400000).value(QStringLiteral("status")).toString(),QStringLiteral("expired"));
        }
    }
}

QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
