#include "core.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <algorithm>
#include <cmath>
#include <memory>

namespace purr {
namespace {
constexpr auto publicKey = "-----BEGIN PUBLIC KEY-----\n"
    "MCowBQYDK2VwAyEAqxoQObh1uXH1AphIbnRj5XTQN9T+RoGk51fqNDU88HA=\n"
    "-----END PUBLIC KEY-----";

qint64 effectiveNow(qint64 now) { return now ? now : QDateTime::currentMSecsSinceEpoch(); }

QByteArray hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray internalKey() {
    return hash(QByteArray("purr-pause-internal-") + publicKey).left(32);
}

QByteArray signature(const QByteArray &bytes) {
    return QMessageAuthenticationCode::hash(bytes, internalKey(), QCryptographicHash::Sha256).toHex();
}

bool equalSignature(const QByteArray &left, const QByteArray &right) {
    return left.size() == right.size() && !left.isEmpty()
        && CRYPTO_memcmp(left.constData(), right.constData(), static_cast<size_t>(left.size())) == 0;
}

QJsonObject readObject(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject{};
}

bool writeObject(const QString &path, const QJsonObject &value, QString *error = nullptr) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建配置目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

int clampInteger(const QJsonValue &value, int minimum, int maximum, int fallback, bool timerInput = false) {
    bool ok = false;
    double parsed = fallback;
    if (value.isDouble() && std::isfinite(value.toDouble())) {
        parsed = value.toDouble();
        // parseInt first converts numbers to JavaScript's decimal/scientific
        // representation, then consumes the integral prefix.
        const auto magnitude = std::abs(parsed);
        if (magnitude >= 1e21 || (magnitude > 0 && magnitude < 1e-6))
            parsed = QString::number(parsed, 'e', 16).section('e', 0, 0).toDouble();
        parsed = std::trunc(parsed);
        ok = true;
    } else if (value.isString()) {
        // JavaScript parseInt accepts the integral prefix, including "12.5".
        const auto text = value.toString().trimmed();
        const auto hex = timerInput ? QRegularExpression(QStringLiteral("^([+-]?)0[xX]([0-9a-fA-F]+)")).match(text) : QRegularExpressionMatch();
        if (hex.hasMatch()) {
            parsed = 0;
            for (const auto digit : hex.captured(2)) {
                const auto lower = digit.toLower();
                parsed = parsed * 16 + (lower.isDigit() ? lower.unicode() - '0' : lower.unicode() - 'a' + 10);
            }
            if (hex.captured(1) == QStringLiteral("-")) parsed = -parsed;
            ok = true;
        } else {
            const auto match = QRegularExpression(QStringLiteral("^([+-]?[0-9]+)")).match(text);
            if (match.hasMatch()) parsed = match.captured(1).toDouble(&ok);
        }
    }
    // Main's timer settings use parseInt(value) || default; the message
    // settings instead only fall back when the parsed number is not finite.
    if (!ok || std::isnan(parsed) || (!timerInput && !std::isfinite(parsed)) || (timerInput && parsed == 0)) return fallback;
    return static_cast<int>(std::clamp(parsed, static_cast<double>(minimum), static_cast<double>(maximum)));
}

bool httpUrl(const QString &text) {
    const QUrl url(text);
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"));
}

QString sourceId() { return QStringLiteral("src-") + QUuid::createUuid().toString(QUuid::Id128).left(6); }

QString listUrl(const QJsonObject &source) {
    const auto direct = source.value(QStringLiteral("listUrl")).toString().trimmed();
    if (!direct.isEmpty()) return direct;
    const auto legacy = source.value(QStringLiteral("baseUrl")).toString(
        source.value(QStringLiteral("messageApiBaseUrl")).toString()).trimmed();
    if (legacy.isEmpty()) return {};
    const QUrl url(legacy);
    return httpUrl(legacy) ? url.resolved(QUrl(QStringLiteral("/purr-pause/v1/todos"))).toString() : legacy;
}

void normalizeConfiguration(QJsonObject &data) {
    data.insert(QStringLiteral("thresholdMinutes"), clampInteger(data.value(QStringLiteral("thresholdMinutes")), 1, 480, 45));
    data.insert(QStringLiteral("breakMinutes"), clampInteger(data.value(QStringLiteral("breakMinutes")), 1, 60, 5));
    data.insert(QStringLiteral("messagePollInterval"), clampInteger(data.value(QStringLiteral("messagePollInterval")), 10, 3600, 60));
    data.insert(QStringLiteral("messageMaxCacheItems"), clampInteger(data.value(QStringLiteral("messageMaxCacheItems")), 1, 30, 30));
    data.remove(QStringLiteral("messageListLimit"));
    auto sources = data.value(QStringLiteral("messageSources")).toArray();
    if (sources.isEmpty() && httpUrl(data.value(QStringLiteral("messageApiBaseUrl")).toString())) {
        sources.append(QJsonObject{
            {QStringLiteral("id"), sourceId()},
            {QStringLiteral("name"), QStringLiteral("默认接口")},
            {QStringLiteral("listUrl"), listUrl(data)},
            {QStringLiteral("authHeaders"), data.value(QStringLiteral("messageAuthHeaders"))},
            {QStringLiteral("enabled"), data.value(QStringLiteral("messageNotifyEnabled")).toBool()}
        });
        data.insert(QStringLiteral("messageApiBaseUrl"), QString());
        data.insert(QStringLiteral("messageAuthHeaders"), QStringLiteral("{}"));
    }
    QSet<QString> ids;
    QJsonArray normalized;
    for (const auto &entry : sources) {
        const auto source = entry.toObject();
        auto id = source.value(QStringLiteral("id")).toString().trimmed();
        while (id.isEmpty() || ids.contains(id)) id = sourceId();
        ids.insert(id);
        auto name = source.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) name = QStringLiteral("未命名");
        auto headers = source.value(QStringLiteral("authHeaders")).toString(QStringLiteral("{}")).trimmed();
        if (headers.isEmpty()) headers = QStringLiteral("{}");
        normalized.append(QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("listUrl"), listUrl(source)}, {QStringLiteral("authHeaders"), headers},
            {QStringLiteral("enabled"), source.value(QStringLiteral("enabled")).toBool()}
        });
    }
    data.insert(QStringLiteral("messageSources"), normalized);
    const QStringList modes{QStringLiteral("walk-center"), QStringLiteral("walk-flat"), QStringLiteral("walk-zoom"), QStringLiteral("fade-center")};
    if (!modes.contains(data.value(QStringLiteral("animationMode")).toString()))
        data.insert(QStringLiteral("animationMode"), QStringLiteral("walk-center"));
    data.insert(QStringLiteral("messageNotifyAnimation"), QStringLiteral("rocket-corner"));
}

QByteArray jsonScalar(const QJsonValue &value) {
    auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

// QJsonObject sorts keys. The original HMAC signs JSON.stringify insertion order,
// so reconstruct that precise order instead of serializing the Qt object.
QByteArray licenseSignature(const QJsonObject &data) {
    const QStringList keys{QStringLiteral("installDate"), QStringLiteral("serialKey"),
        QStringLiteral("activatedAt"), QStringLiteral("expiresAt"), QStringLiteral("type"),
        QStringLiteral("machineId"), QStringLiteral("usedSerials")};
    QByteArray payload("{");
    bool first = true;
    for (const auto &key : keys) {
        if (!data.contains(key) && key != QStringLiteral("usedSerials")) continue;
        if (!first) payload += ',';
        first = false;
        payload += jsonScalar(key) + ':';
        payload += key == QStringLiteral("usedSerials") && !data.value(key).isArray()
            ? QByteArray("[]") : jsonScalar(data.value(key));
    }
    payload += '}';
    return signature(payload);
}

QString commandOutput(const QString &program, const QStringList &arguments, bool *succeeded = nullptr) {
    if (succeeded) *succeeded = false;
    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(1000) || !process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    if (succeeded) *succeeded = true;
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString computeMachineId() {
    auto architecture = QSysInfo::currentCpuArchitecture();
    if (architecture == QStringLiteral("x86_64")) architecture = QStringLiteral("x64");
    else if (architecture == QStringLiteral("aarch64")) architecture = QStringLiteral("arm64");
    else if (architecture == QStringLiteral("i386")) architecture = QStringLiteral("ia32");
    QString platform, cpu, identifier;
    bool hasIdentifier = false;
    bool identifierFailed = false;
#ifdef Q_OS_MACOS
    platform = QStringLiteral("darwin");
    cpu = commandOutput(QStringLiteral("/usr/sbin/sysctl"), {QStringLiteral("-n"), QStringLiteral("machdep.cpu.brand_string")});
    if (cpu.isEmpty()) cpu = commandOutput(QStringLiteral("/usr/sbin/sysctl"), {QStringLiteral("-n"), QStringLiteral("hw.model")});
    bool ioregSucceeded = false;
    const auto ioreg = commandOutput(QStringLiteral("/usr/sbin/ioreg"), {QStringLiteral("-rd1"), QStringLiteral("-c"), QStringLiteral("IOPlatformExpertDevice")}, &ioregSucceeded);
    const auto match = QRegularExpression(QStringLiteral("\"IOPlatformUUID\"\\s*=\\s*\"([^\"]+)\"")).match(ioreg);
    if (match.hasMatch()) { identifier = match.captured(1); hasIdentifier = true; }
    identifierFailed = !ioregSucceeded;
#else
    platform = QStringLiteral("linux");
    cpu = QStringLiteral("unknown");
    QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        const auto contents = QString::fromUtf8(cpuInfo.readAll());
        const QString marker = architecture == QStringLiteral("arm64") ? QStringLiteral("CPU part\t: ") : QStringLiteral("model name\t: ");
        for (const auto &line : contents.split('\n')) {
            if (!line.startsWith(marker)) continue;
            cpu = line.mid(marker.size()).left(63);
            break;
        }
        if (architecture == QStringLiteral("arm64")) {
            // Electron 33 uses libuv's ARM part mapping. Using "unknown" for
            // every ARM device would invalidate existing licenses on that host.
            static const char armParts[] =
                "0x811\nARM810\n0x920\nARM920\n0x922\nARM922\n0x926\nARM926\n"
                "0x940\nARM940\n0x946\nARM946\n0x966\nARM966\n0xa20\nARM1020\n"
                "0xa22\nARM1022\n0xa26\nARM1026\n0xb02\nARM11 MPCore\n0xb36\nARM1136\n"
                "0xb56\nARM1156\n0xb76\nARM1176\n0xc05\nCortex-A5\n0xc07\nCortex-A7\n"
                "0xc08\nCortex-A8\n0xc09\nCortex-A9\n0xc0d\nCortex-A17\n0xc0f\nCortex-A15\n"
                "0xc0e\nCortex-A17\n0xc14\nCortex-R4\n0xc15\nCortex-R5\n0xc17\nCortex-R7\n"
                "0xc18\nCortex-R8\n0xc20\nCortex-M0\n0xc21\nCortex-M1\n0xc23\nCortex-M3\n"
                "0xc24\nCortex-M4\n0xc27\nCortex-M7\n0xc60\nCortex-M0+\n0xd01\nCortex-A32\n"
                "0xd03\nCortex-A53\n0xd04\nCortex-A35\n0xd05\nCortex-A55\n0xd06\nCortex-A65\n"
                "0xd07\nCortex-A57\n0xd08\nCortex-A72\n0xd09\nCortex-A73\n0xd0a\nCortex-A75\n"
                "0xd0b\nCortex-A76\n0xd0c\nNeoverse-N1\n0xd0d\nCortex-A77\n0xd0e\nCortex-A76AE\n"
                "0xd13\nCortex-R52\n0xd20\nCortex-M23\n0xd21\nCortex-M33\n0xd41\nCortex-A78\n"
                "0xd42\nCortex-A78AE\n0xd4a\nNeoverse-E1\n0xd4b\nCortex-A78C\n";
            const auto parts = QString::fromLatin1(armParts).split('\n');
            const auto part = parts.indexOf(cpu);
            cpu = part >= 0 && part % 2 == 0 ? parts.value(part + 1) : QStringLiteral("unknown");
        }
    }
    QFile machine(QStringLiteral("/etc/machine-id"));
    if (machine.open(QIODevice::ReadOnly)) { identifier = QString::fromUtf8(machine.readAll()).trimmed(); hasIdentifier = true; }
    else identifierFailed = true;
#endif
    QStringList parts{platform, architecture, cpu};
    if (identifierFailed) parts.append(QSysInfo::machineHostName() + QDir::homePath());
    else if (hasIdentifier) parts.append(identifier);
    return QString::fromLatin1(hash(parts.join('|').toUtf8()).left(16));
}

QDateTime parseDate(const QString &value) { return QDateTime::fromString(value, Qt::ISODateWithMs); }
QString isoDate(qint64 nowMs) { return QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC).toString(Qt::ISODateWithMs); }
} // namespace

QJsonObject ConfigStore::defaults() {
    return {
        {QStringLiteral("thresholdMinutes"), 45}, {QStringLiteral("breakMinutes"), 5},
        {QStringLiteral("customWebmDir"), QString()}, {QStringLiteral("walkVideo"), QStringLiteral("cat-walk.webm")},
        {QStringLiteral("idleVideo"), QStringLiteral("cat-rest.webm")}, {QStringLiteral("animationMode"), QStringLiteral("walk-center")},
        {QStringLiteral("logEnabled"), false}, {QStringLiteral("messageNotifyEnabled"), false},
        {QStringLiteral("messageApiBaseUrl"), QString()}, {QStringLiteral("messageAuthHeaders"), QStringLiteral("{}")},
        {QStringLiteral("messageSources"), QJsonArray()}, {QStringLiteral("messagePollInterval"), 60},
        {QStringLiteral("messageMaxCacheItems"), 30}, {QStringLiteral("messageNotifyDir"), QString()},
        {QStringLiteral("messageNotifyVideo"), QStringLiteral("notify-rocket.webm")},
        {QStringLiteral("messageNotifyAnimation"), QStringLiteral("rocket-corner")}
    };
}

ConfigStore::ConfigStore(QString dataDir, QObject *parent)
    : QObject(parent), m_directory(QDir::cleanPath(dataDir)), m_data(defaults()) {
    const auto existing = readObject(QDir(m_directory).filePath(QStringLiteral("config.json")));
    for (auto it = existing.begin(); it != existing.end(); ++it) m_data.insert(it.key(), it.value());
    normalizeConfiguration(m_data);
}

bool ConfigStore::save(const QJsonObject &changes, bool activated, QString *error) {
    if (error) error->clear();
    auto next = m_data;
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        if (!activated && (it.key() == QStringLiteral("animationMode") || it.key() == QStringLiteral("customWebmDir"))) continue;
        if (it.key() == QStringLiteral("animationMode")) {
            const QStringList modes{QStringLiteral("walk-center"), QStringLiteral("walk-flat"), QStringLiteral("walk-zoom"), QStringLiteral("fade-center")};
            if (!modes.contains(it.value().toString())) continue;
        }
        next.insert(it.key(), it.value());
    }
    if (changes.contains(QStringLiteral("thresholdMinutes")))
        next.insert(QStringLiteral("thresholdMinutes"), clampInteger(changes.value(QStringLiteral("thresholdMinutes")), 1, 480, 45, true));
    if (changes.contains(QStringLiteral("breakMinutes")))
        next.insert(QStringLiteral("breakMinutes"), clampInteger(changes.value(QStringLiteral("breakMinutes")), 1, 60, 5, true));
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (changes.contains(QStringLiteral("messageSources"))) {
        QJsonArray validated;
        const auto sources = changes.value(QStringLiteral("messageSources")).toArray();
        for (qsizetype index = 0; index < sources.size(); ++index) {
            auto source = sources.at(index).toObject();
            const auto name = source.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) return fail(QStringLiteral("第 %1 个接口源名称不能为空").arg(index + 1));
            if (source.value(QStringLiteral("enabled")).toBool() && !httpUrl(listUrl(source)))
                return fail(QStringLiteral("「%1」的接口地址必须是 http 或 https URL").arg(name));
            const auto text = source.value(QStringLiteral("authHeaders")).toString(QStringLiteral("{}")).trimmed();
            QJsonParseError parseError;
            const auto headers = QJsonDocument::fromJson(text.isEmpty() ? QByteArray("{}") : text.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !headers.isObject())
                return fail(QStringLiteral("「%1」的请求头必须是 JSON 对象").arg(name));
            source.insert(QStringLiteral("authHeaders"), QString::fromUtf8(headers.toJson(QJsonDocument::Compact)));
            validated.append(source);
        }
        next.insert(QStringLiteral("messageSources"), validated);
        next.insert(QStringLiteral("messageApiBaseUrl"), QString());
        next.insert(QStringLiteral("messageAuthHeaders"), QStringLiteral("{}"));
    }
    for (const auto &key : {QStringLiteral("walkVideo"), QStringLiteral("idleVideo"), QStringLiteral("messageNotifyVideo")}) {
        auto filename = next.value(key).toString().trimmed();
        if (filename.isEmpty()) filename = defaults().value(key).toString();
        if (filename.contains('/') || filename.contains('\\') || filename == QStringLiteral(".") || filename == QStringLiteral(".."))
            return fail(QStringLiteral("素材只能填写文件名，不能包含路径分隔符"));
        next.insert(key, filename);
    }
    for (const auto &key : {QStringLiteral("customWebmDir"), QStringLiteral("messageNotifyDir")}) {
        if (!changes.contains(key) || (key == QStringLiteral("customWebmDir") && !activated)) continue;
        const auto directory = next.value(key).toString().trimmed();
        if (!directory.isEmpty() && !QFileInfo(directory).isDir()) return fail(QStringLiteral("素材目录不存在或无法访问"));
        next.insert(key, directory);
    }
    normalizeConfiguration(next);
    if (!writeObject(QDir(m_directory).filePath(QStringLiteral("config.json")), next, error)) return false;
    m_data = next;
    emit changed(m_data);
    return true;
}

LicenseManager::LicenseManager(QString dataDir)
    : m_directory(QDir::cleanPath(dataDir)), m_machineId(computeMachineId()) {}

QJsonObject LicenseManager::verifySerial(const QString &serial) {
    auto stripped = serial.trimmed();
    if (stripped.startsWith(QStringLiteral("PP-"), Qt::CaseInsensitive)) stripped.remove(0, 3);
    stripped.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9+/=]")));
    const auto bytes = QByteArray::fromBase64(stripped.toLatin1());
    if (bytes.size() != 76) return {};
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(publicKey, -1), BIO_free);
    if (!bio) return {};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!key || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) return {};
    const auto *raw = reinterpret_cast<const unsigned char *>(bytes.constData());
    if (EVP_DigestVerify(context.get(), raw + 12, 64, raw, 12) != 1) return {};
    const quint32 days = (quint32(raw[0]) << 24) | (quint32(raw[1]) << 16) | (quint32(raw[2]) << 8) | quint32(raw[3]);
    return {{QStringLiteral("days"), static_cast<double>(days)},
        {QStringLiteral("machineId"), QString::fromLatin1(bytes.mid(4, 8).toHex())}};
}

QJsonObject LicenseManager::load() const {
    const auto data = readObject(QDir(m_directory).filePath(QStringLiteral("license.json")));
    const auto expected = licenseSignature(data);
    const auto stored = data.value(QStringLiteral("_sig")).toString().toLatin1();
    if (!equalSignature(stored, expected)) return {};
    const auto machine = data.value(QStringLiteral("machineId")).toString();
    if (!machine.isEmpty() && machine != m_machineId) return {};
    return data;
}

bool LicenseManager::persist(QJsonObject data, QString *error) const {
    data.insert(QStringLiteral("_sig"), QString::fromLatin1(licenseSignature(data)));
    return writeObject(QDir(m_directory).filePath(QStringLiteral("license.json")), data, error);
}

QString LicenseManager::installDate(qint64 nowMs) const {
    const QStringList paths{QDir(m_directory).filePath(QStringLiteral(".install_mark")),
        QDir::home().filePath(QStringLiteral(".local/share/.purr-pause-mark"))};
    for (const auto &path : paths) {
        const auto mark = readObject(path);
        const auto date = mark.value(QStringLiteral("date")).toString();
        const auto machine = mark.value(QStringLiteral("mid")).toString();
        if (machine == m_machineId && parseDate(date).isValid()
            && equalSignature(mark.value(QStringLiteral("sig")).toString().toLatin1(), signature((date + machine).toUtf8()).left(16)))
            return date;
    }
    const auto date = isoDate(nowMs);
    const QJsonObject mark{{QStringLiteral("date"), date}, {QStringLiteral("mid"), m_machineId},
        {QStringLiteral("sig"), QString::fromLatin1(signature((date + m_machineId).toUtf8()).left(16))}};
    for (const auto &path : paths) writeObject(path, mark);
    return date;
}

QJsonObject LicenseManager::status(qint64 nowMs) {
    nowMs = effectiveNow(nowMs);
    auto data = load();
    if (data.isEmpty()) {
        data = {{QStringLiteral("installDate"), installDate(nowMs)}, {QStringLiteral("type"), QStringLiteral("trial")},
            {QStringLiteral("serialKey"), QJsonValue::Null}, {QStringLiteral("activatedAt"), QJsonValue::Null},
            {QStringLiteral("expiresAt"), QJsonValue::Null}, {QStringLiteral("machineId"), m_machineId}};
        persist(data);
    }
    const auto type = data.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("permanent"))
        return {{QStringLiteral("status"), QStringLiteral("active")}, {QStringLiteral("type"), type}, {QStringLiteral("daysLeft"), -1}};
    const auto now = QDateTime::fromMSecsSinceEpoch(nowMs).toLocalTime();
    QDateTime end;
    if (type == QStringLiteral("trial")) {
        const auto installed = parseDate(data.value(QStringLiteral("installDate")).toString()).toLocalTime();
        end = QDateTime(installed.date().addDays(7), QTime(23, 59, 59, 999), Qt::LocalTime);
    } else {
        end = parseDate(data.value(QStringLiteral("expiresAt")).toString()).toLocalTime();
    }
    if (!end.isValid() || now >= end)
        return {{QStringLiteral("status"), QStringLiteral("expired")}, {QStringLiteral("type"), type}, {QStringLiteral("daysLeft"), 0}};
    // Calendar-day count is intentionally independent of the exact activation hour.
    const auto days = static_cast<int>(now.date().daysTo(end.date()));
    return {{QStringLiteral("status"), type == QStringLiteral("trial") ? QStringLiteral("trial") : QStringLiteral("active")},
        {QStringLiteral("type"), type}, {QStringLiteral("daysLeft"), days}};
}

bool LicenseManager::activate(const QString &serial, QString *error, qint64 nowMs) {
    if (error) error->clear();
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const auto parsed = verifySerial(serial);
    if (parsed.isEmpty()) return fail(QStringLiteral("序列号无效"));
    if (parsed.value(QStringLiteral("machineId")).toString() != m_machineId) return fail(QStringLiteral("序列号与本机不匹配"));
    nowMs = effectiveNow(nowMs);
    auto data = load();
    auto used = data.value(QStringLiteral("usedSerials")).toArray();
    const auto serialHash = QString::fromLatin1(hash(serial.trimmed().toUtf8()).left(32));
    if (used.contains(serialHash)) return fail(QStringLiteral("此序列号已使用过，请使用新的序列号"));
    used.append(serialHash);
    data.insert(QStringLiteral("usedSerials"), used);
    data.insert(QStringLiteral("serialKey"), serial.trimmed());
    data.insert(QStringLiteral("activatedAt"), isoDate(nowMs));
    data.insert(QStringLiteral("machineId"), m_machineId);
    if (data.value(QStringLiteral("installDate")).toString().isEmpty()) data.insert(QStringLiteral("installDate"), installDate(nowMs));
    const auto days = static_cast<quint32>(parsed.value(QStringLiteral("days")).toDouble());
    if (days == 0) {
        data.insert(QStringLiteral("type"), QStringLiteral("permanent"));
        data.insert(QStringLiteral("expiresAt"), QJsonValue::Null);
    } else {
        data.insert(QStringLiteral("type"), days <= 30 ? QStringLiteral("monthly") : days <= 90 ? QStringLiteral("quarterly") : QStringLiteral("yearly"));
        data.insert(QStringLiteral("expiresAt"), isoDate(nowMs + qint64(days) * 86400000));
    }
    return persist(data, error);
}

bool LicenseManager::isActivated() { return status().value(QStringLiteral("status")) == QStringLiteral("active"); }
bool LicenseManager::canUse() { return status().value(QStringLiteral("status")) != QStringLiteral("expired"); }

BreakTimer::BreakTimer(QObject *parent) : QObject(parent) {}

void BreakTimer::configure(const QJsonObject &configuration) {
    m_thresholdSeconds = clampInteger(configuration.value(QStringLiteral("thresholdMinutes")), 1, 480, 45) * 60;
    emit changed();
}

qint64 BreakTimer::secondsUntilBreak() const {
    return std::max<qint64>(0, (m_snoozeThreshold ? m_snoozeThreshold : m_thresholdSeconds) - m_activeSeconds);
}

void BreakTimer::setLocked(bool locked, qint64 nowMs) {
    if (locked == m_locked) return;
    nowMs = effectiveNow(nowMs);
    m_locked = locked;
    if (locked) m_lockedAt = nowMs;
    else {
        if (m_lockedAt > 0 && nowMs - m_lockedAt > 600000) m_activeSeconds = 0;
        m_lockedAt = 0;
        emit changed();
    }
}

void BreakTimer::tick(qint64 nowMs, qint64 idleMilliseconds, bool locked) {
    setLocked(locked, nowMs);
    if (m_paused && m_pauseUntil > 0 && nowMs >= m_pauseUntil) resume();
    if (m_breaking || m_paused || m_locked || m_suspended) return;
    if (idleMilliseconds > 600000) {
        m_activeSeconds = 0;
        emit changed();
        return;
    }
    if (idleMilliseconds < 180000) m_activeSeconds += 10;
    emit changed();
    if (secondsUntilBreak() == 0) {
        m_snoozeThreshold = 0;
        startBreak(false, nowMs);
    }
}

void BreakTimer::pause(int minutes, qint64 nowMs) {
    m_paused = true;
    m_pauseUntil = minutes > 0 ? effectiveNow(nowMs) + qint64(minutes) * 60000 : 0;
    emit changed();
}

void BreakTimer::resume() {
    m_paused = false;
    m_pauseUntil = 0;
    emit changed();
}

bool BreakTimer::startBreak(bool force, qint64 nowMs) {
    nowMs = effectiveNow(nowMs);
    if (m_breaking || (!force && m_lastDismissTime >= 0 && nowMs - m_lastDismissTime < 60000)) return false;
    m_breaking = true;
    emit changed();
    emit breakStarted();
    return true;
}

void BreakTimer::finishBreak(qint64 nowMs) {
    if (!m_breaking) return;
    m_breaking = false;
    m_activeSeconds = 0;
    m_snoozeCount = 0;
    m_snoozeThreshold = 0;
    m_lastDismissTime = effectiveNow(nowMs);
    emit changed();
    emit breakEnded();
}

bool BreakTimer::snooze() {
    if (!m_breaking || m_snoozeCount >= 2) return false;
    ++m_snoozeCount;
    m_breaking = false;
    m_activeSeconds = 0;
    m_snoozeThreshold = 300;
    emit changed();
    emit breakEnded();
    return true;
}

void BreakTimer::reset() {
    m_activeSeconds = 0;
    emit changed();
}

void BreakTimer::handleSuspend(qint64 nowMs) {
    m_suspended = true;
    m_sleepStartedAt = effectiveNow(nowMs);
}

void BreakTimer::handleResume(qint64 nowMs) {
    if (m_suspended && effectiveNow(nowMs) - m_sleepStartedAt > 600000) {
        m_activeSeconds = 0;
        m_snoozeThreshold = 0;
    }
    m_suspended = false;
    m_sleepStartedAt = 0;
    emit changed();
}

} // namespace purr
