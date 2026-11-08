#include "messages.h"

#include <QCollator>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace purr {
namespace {
constexpr qint64 MaximumResponseBytes = 1024 * 1024;
constexpr int RequestTimeoutMs = 5000;
constexpr int KnownIdLimit = 2000;
constexpr int ClearedIdLimit = 200;

bool truthy(const QJsonValue &value) {
    if (value.isNull() || value.isUndefined()) return false;
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble() != 0;
    if (value.isString()) return !value.toString().isEmpty();
    return true;
}

// The Electron API normalizes with JavaScript String(), including array joins.
QString stringValue(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    if (value.isDouble()) {
        const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        QString result = QString::fromLatin1(encoded.mid(1, encoded.size() - 2));
        const int exponentAt = result.indexOf(QLatin1Char('e'));
        if (exponentAt < 0) return result;
        const int exponent = result.mid(exponentAt + 1).toInt();
        if (exponent >= -6 && exponent < 21) {
            QString mantissa = result.left(exponentAt);
            const QString sign = mantissa.startsWith(QLatin1Char('-')) ? QStringLiteral("-") : QString();
            if (!sign.isEmpty()) mantissa.remove(0, 1);
            const int decimalAt = mantissa.contains(QLatin1Char('.')) ? mantissa.indexOf(QLatin1Char('.')) : mantissa.size();
            mantissa.remove(QLatin1Char('.'));
            const int position = decimalAt + exponent;
            if (position <= 0) return sign + QStringLiteral("0.") + QString(-position, QLatin1Char('0')) + mantissa;
            if (position >= mantissa.size()) return sign + mantissa + QString(position - mantissa.size(), QLatin1Char('0'));
            return sign + mantissa.left(position) + QLatin1Char('.') + mantissa.mid(position);
        }
        return result.left(exponentAt) + QLatin1Char('e') + (exponent >= 0 ? QStringLiteral("+") : QString()) + QString::number(exponent);
    }
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isObject()) return QStringLiteral("[object Object]");
    if (value.isArray()) {
        QStringList entries;
        for (const auto &entry : value.toArray())
            entries.append(entry.isNull() || entry.isUndefined() ? QString() : stringValue(entry));
        return entries.join(QLatin1Char(','));
    }
    return value.isNull() ? QStringLiteral("null") : QStringLiteral("undefined");
}

QString truthyString(const QJsonValue &value, const QString &fallback = {}) {
    return truthy(value) ? stringValue(value) : fallback;
}

int integerValue(const QJsonValue &value, int fallback) {
    const auto match = QRegularExpression(QStringLiteral("^\\s*([+-]?\\d+)")).match(stringValue(value));
    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok || !std::isfinite(number)) return fallback;
    return int(std::clamp(number, double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max())));
}

bool httpUrl(const QString &value) {
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
}

QString resolveUrl(const QJsonObject &source) {
    const auto directValue = source.value(QStringLiteral("listUrl"));
    const QString direct = (directValue.isNull() || directValue.isUndefined() ? QString() : stringValue(directValue)).trimmed();
    if (!direct.isEmpty()) return direct;
    const auto base = source.value(QStringLiteral("baseUrl"));
    const auto legacyValue = base.isNull() || base.isUndefined() ? source.value(QStringLiteral("messageApiBaseUrl")) : base;
    const QString legacy = (legacyValue.isNull() || legacyValue.isUndefined() ? QString() : stringValue(legacyValue)).trimmed();
    if (legacy.isEmpty()) return {};
    const QUrl baseUrl(legacy);
    return baseUrl.isValid() ? baseUrl.resolved(QUrl(QStringLiteral("/purr-pause/v1/todos"))).toString() : legacy;
}

QString colorFor(const QString &id) {
    static const QStringList palette = {
        QStringLiteral("#2f80ed"), QStringLiteral("#27ae60"), QStringLiteral("#f2994a"),
        QStringLiteral("#9b51e0"), QStringLiteral("#eb5757"), QStringLiteral("#00a7a7")
    };
    quint32 hash = 0;
    const QString text = id.isEmpty() ? QStringLiteral("unknown") : id;
    for (const QChar character : text) hash = hash * 31u + character.unicode();
    const qint64 signedHash = hash <= 0x7fffffffu ? qint64(hash) : qint64(hash) - 0x100000000LL;
    return palette.at(int(qAbs(signedHash) % palette.size()));
}

qint64 timestamp(const QJsonObject &item) {
    const QString value = item.value(QStringLiteral("createdAt")).toString();
    // ECMAScript interprets ISO dates without a time as UTC, while Qt treats
    // them as local time. ISO date-times without an offset remain local time.
    static const QRegularExpression isoDate(QStringLiteral("^(\\d{4})(?:-(\\d{2})(?:-(\\d{2}))?)?$"));
    const auto dateOnly = isoDate.match(value);
    if (dateOnly.hasMatch()) {
        const QDate date(dateOnly.captured(1).toInt(), dateOnly.captured(2).isEmpty() ? 1 : dateOnly.captured(2).toInt(),
                         dateOnly.captured(3).isEmpty() ? 1 : dateOnly.captured(3).toInt());
        return date.isValid() ? QDateTime(date, QTime(0, 0), QTimeZone("UTC")).toMSecsSinceEpoch() : 0;
    }
    auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid()) date = QDateTime::fromString(value, Qt::ISODate);
    if (!date.isValid()) date = QDateTime::fromString(value, Qt::RFC2822Date);
    return date.isValid() ? date.toMSecsSinceEpoch() : 0;
}

QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

QJsonObject normalizeSource(const QJsonObject &raw) {
    QString name = truthyString(raw.value(QStringLiteral("name"))).trimmed();
    if (name.isEmpty()) name = QStringLiteral("未命名");
    auto headersValue = raw.value(QStringLiteral("authHeaders"));
    if (headersValue.isNull() || headersValue.isUndefined()) headersValue = raw.value(QStringLiteral("messageAuthHeaders"));
    QString headers = (headersValue.isNull() || headersValue.isUndefined() ? QStringLiteral("{}") : stringValue(headersValue)).trimmed();
    if (headers.isEmpty()) headers = QStringLiteral("{}");
    return {
        {QStringLiteral("id"), truthyString(raw.value(QStringLiteral("id"))).trimmed()},
        {QStringLiteral("name"), name},
        {QStringLiteral("listUrl"), resolveUrl(raw)},
        {QStringLiteral("authHeaders"), headers},
        {QStringLiteral("enabled"), raw.value(QStringLiteral("enabled")).isUndefined() || truthy(raw.value(QStringLiteral("enabled")))}
    };
}

bool parseHeaders(const QString &text, QList<QPair<QByteArray, QByteArray>> *headers, QString *error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("请求头必须是 JSON 对象");
        return false;
    }
    const auto object = document.object();
    const QRegularExpression headerName(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString key = it.key().trimmed();
        if (key.isEmpty() || it.value().isNull()) continue;
        const QString value = stringValue(it.value());
        if (!headerName.match(key).hasMatch() || value.contains(QLatin1Char('\r'))
            || value.contains(QLatin1Char('\n')) || value.contains(QChar::Null)) {
            *error = QStringLiteral("请求头名称或内容不合法");
            return false;
        }
        headers->append({key.toLatin1(), value.toUtf8()});
    }
    return true;
}
} // namespace

struct MessageService::Source {
    QJsonObject config;
    QTimer *timer = nullptr;
    QPointer<QNetworkReply> reply;
    quint64 generation = 0;
    bool inFlight = false;
    bool firstSync = true;
    int failures = 0;
    QString error;
    QString refreshedAt;
    int total = 0;
    QJsonArray snapshot;
    QSet<QString> knownIds;
    QList<QString> knownQueue;
    QSet<QString> clearedIds;
    QList<QString> clearedQueue;
};

struct MessageService::FetchResult {
    bool ok = false;
    QJsonObject data;
    QString error;
};

MessageService::MessageService(QObject *parent) : QObject(parent), network_(this) {}

MessageService::~MessageService() {
    for (const auto &source : sources_) {
        ++source->generation;
        source->timer->stop();
        if (source->reply) source->reply->disconnect(this);
    }
}

bool MessageService::enabled(const SourcePtr &source) const {
    return enabled_ && source->config.value(QStringLiteral("enabled")).toBool()
        && httpUrl(source->config.value(QStringLiteral("listUrl")).toString());
}

void MessageService::cancel(const SourcePtr &source, bool clearSnapshot) {
    ++source->generation; // Abort can finish synchronously; invalidate it first.
    source->timer->stop();
    source->inFlight = false;
    const auto reply = source->reply;
    source->reply.clear();
    if (reply) reply->abort();
    if (clearSnapshot) {
        source->snapshot = {};
        source->total = 0;
        source->error.clear();
        source->failures = 0;
    }
}

void MessageService::setConfig(QJsonObject config) {
    const bool previousEnabled = enabled_;
    enabled_ = truthy(config.value(QStringLiteral("messageNotifyEnabled")));
    pollInterval_ = std::clamp(integerValue(config.value(QStringLiteral("messagePollInterval")), 60), 10, 3600);
    maxItems_ = std::clamp(integerValue(config.value(QStringLiteral("messageMaxCacheItems")), 30), 1, 30);

    QJsonArray rawSources = config.value(QStringLiteral("messageSources")).toArray();
    const QString legacyUrl = config.value(QStringLiteral("messageApiBaseUrl")).toString().trimmed();
    if (rawSources.isEmpty() && httpUrl(legacyUrl)) {
        rawSources.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("legacy-default")},
            {QStringLiteral("name"), QStringLiteral("默认接口")},
            {QStringLiteral("baseUrl"), legacyUrl},
            {QStringLiteral("authHeaders"), config.value(QStringLiteral("messageAuthHeaders")).toString(QStringLiteral("{}"))},
            {QStringLiteral("enabled"), enabled_}
        });
    }

    QHash<QString, SourcePtr> existing;
    for (const auto &source : sources_) existing.insert(source->config.value(QStringLiteral("id")).toString(), source);
    QList<SourcePtr> next;
    QSet<QString> ids;
    int startIndex = 0;
    for (const auto &raw : rawSources) {
        auto sourceConfig = normalizeSource(raw.toObject());
        QString id = sourceConfig.value(QStringLiteral("id")).toString();
        while (id.isEmpty() || ids.contains(id)) id = QStringLiteral("src-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        ids.insert(id);
        sourceConfig.insert(QStringLiteral("id"), id);
        auto source = existing.take(id);
        const bool isNew = !source;
        if (isNew) {
            source = SourcePtr::create();
            source->timer = new QTimer(this);
            source->timer->setSingleShot(true);
            source->timer->setTimerType(Qt::PreciseTimer);
            const QWeakPointer<Source> weak(source);
            connect(source->timer, &QTimer::timeout, this, [this, weak] {
                if (const auto current = weak.toStrongRef()) refreshSource(current, false, true);
            });
        }
        const auto previous = source->config;
        const bool endpointChanged = previous.value(QStringLiteral("listUrl")) != sourceConfig.value(QStringLiteral("listUrl"))
            || previous.value(QStringLiteral("authHeaders")) != sourceConfig.value(QStringLiteral("authHeaders"))
            || previous.value(QStringLiteral("enabled")) != sourceConfig.value(QStringLiteral("enabled"));
        source->config = sourceConfig;
        // Keep origin metadata on the existing snapshot until the next fetch.
        // The Electron runtime updates its presentation source object first but
        // does not rewrite already fetched item objects.
        if (!enabled(source)) {
            cancel(source, true);
        } else if (isNew || endpointChanged || previousEnabled != enabled_) {
            cancel(source, false);
            source->firstSync = true;
            source->failures = 0;
            source->error.clear();
            schedule(source, startIndex++ * 300);
        } else if (!source->timer->isActive() && !source->inFlight) {
            schedule(source, 0);
        }
        next.append(source);
    }
    for (const auto &removed : existing) {
        cancel(removed, true);
        removed->timer->deleteLater();
    }
    sources_ = next;
    const bool anyEnabled = std::any_of(sources_.cbegin(), sources_.cend(),
        [this](const SourcePtr &source) { return enabled(source); });
    if (!anyEnabled) {
        ++deferredGeneration_;
        pendingCount_ = 0;
        pendingName_.clear();
    }
    emit changed();
}

int MessageService::intervalMs(const SourcePtr &source) const {
    if (source->failures < 5) return pollInterval_ * 1000;
    // Match the original fifth-failure backoff and its 300-second ceiling.
    const double interval = std::min(300.0, pollInterval_ * std::pow(2.0, std::min(30, source->failures - 4)));
    return int(interval * 1000);
}

void MessageService::schedule(const SourcePtr &source, int delayMs) {
    source->timer->stop();
    if (enabled(source)) source->timer->start(std::max(0, delayMs));
}

QNetworkReply *MessageService::fetch(QJsonObject source, int limit, FetchCallback callback, bool connectionTest) {
    const QString urlText = resolveUrl(source);
    QList<QPair<QByteArray, QByteArray>> headers;
    QString error;
    if (!httpUrl(urlText)) error = QStringLiteral("接口地址必须是 http 或 https URL");
    else if (!parseHeaders(source.value(QStringLiteral("authHeaders")).toString(QStringLiteral("{}")), &headers, &error)
             && connectionTest) error.prepend(QStringLiteral("请求头解析失败："));
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [callback, error] { callback({false, {}, error}); });
        return nullptr;
    }

    QUrl url(urlText);
    QUrlQuery query(url);
    query.removeAllQueryItems(QStringLiteral("limit"));
    query.removeAllQueryItems(QStringLiteral("offset"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    url.setQuery(query);
    QNetworkRequest request(url);
    // A source's secret headers must not follow a redirect to a different origin.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    for (const auto &header : headers) request.setRawHeader(header.first, header.second);
    auto *reply = network_.get(request);
    reply->setReadBufferSize(MaximumResponseBytes + 1);
    auto *timeout = new QTimer(reply);
    timeout->setTimerType(Qt::PreciseTimer);
    timeout->setSingleShot(true);
    struct Buffer { QByteArray bytes; bool oversized = false; bool timedOut = false; };
    auto buffer = std::make_shared<Buffer>();
    connect(timeout, &QTimer::timeout, this, [reply, buffer] {
        buffer->timedOut = true;
        reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, buffer] {
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > MaximumResponseBytes) {
            buffer->oversized = true;
            reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, this, [reply, buffer] {
        buffer->bytes.append(reply->read(MaximumResponseBytes + 1 - buffer->bytes.size()));
        if (buffer->bytes.size() > MaximumResponseBytes) {
            buffer->oversized = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, timeout, buffer, callback, connectionTest] {
        timeout->stop();
        if (!buffer->oversized && !buffer->timedOut && reply->isOpen())
            buffer->bytes.append(reply->read(MaximumResponseBytes + 1 - buffer->bytes.size()));
        FetchResult result;
        if (buffer->oversized || buffer->bytes.size() > MaximumResponseBytes) {
            result.error = QStringLiteral("响应超过 1 MiB 限制");
        } else if (buffer->timedOut) {
            result.error = QStringLiteral("请求超时（5 秒）");
        } else {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 0 && reply->error() != QNetworkReply::NoError) {
                result.error = reply->errorString();
            } else {
                QJsonParseError parseError;
                // A wrapper also accepts JSON primitives, as JSON.parse does.
                const auto document = QJsonDocument::fromJson("[" + (buffer->bytes.isEmpty() ? QByteArray("null") : buffer->bytes) + "]", &parseError);
                if (parseError.error != QJsonParseError::NoError || document.array().size() != 1) {
                    result.error = QStringLiteral("响应不是合法 JSON");
                } else {
                    const auto response = document.array().at(0);
                    const auto body = response.toObject();
                    const auto code = body.value(QStringLiteral("code"));
                    const auto message = body.value(QStringLiteral("message"));
                    if (status != 0 && (status < 200 || status >= 300)) {
                        result.error = QStringLiteral("HTTP %1").arg(status);
                        if (connectionTest && truthy(message)) result.error += QStringLiteral("：") + stringValue(message);
                    } else if (reply->error() != QNetworkReply::NoError) {
                        result.error = reply->errorString();
                    } else if (!code.isDouble() || code.toDouble() != 0) {
                        result.error = truthyString(message, connectionTest
                            ? QStringLiteral("第三方接口返回失败 (code=%1)").arg(stringValue(truthy(response) ? code : response))
                            : QStringLiteral("第三方接口返回失败"));
                    } else {
                        result.ok = true;
                        result.data = body.value(QStringLiteral("data")).toObject();
                    }
                }
            }
        }
        reply->deleteLater();
        callback(result);
    });
    timeout->start(RequestTimeoutMs);
    return reply;
}

void MessageService::refreshSource(const SourcePtr &source, bool silent, bool scheduled) {
    if (!enabled(source)) return;
    if (source->inFlight) {
        if (scheduled) schedule(source, intervalMs(source));
        return;
    }
    source->inFlight = true;
    const quint64 generation = source->generation;
    const bool baseline = source->firstSync;
    emit changed();
    source->reply = fetch(source->config, maxItems_, [this, source, generation, silent, scheduled, baseline](FetchResult result) {
        if (source->generation != generation || !enabled(source)) return;
        source->reply.clear();
        source->inFlight = false;
        if (result.ok) {
            source->failures = 0;
            source->error.clear();
            applyItems(source, result.data, silent || baseline);
            if (!silent || scheduled) source->firstSync = false;
        } else {
            source->failures = std::min(source->failures + 1, 1000000);
            source->error = (silent ? QStringLiteral("刷新待办列表失败：") : QStringLiteral("拉取待办列表失败：")) + result.error;
        }
        if (scheduled) schedule(source, intervalMs(source));
        emit changed();
    });
}

void MessageService::applyItems(const SourcePtr &source, const QJsonObject &data, bool silent) {
    QList<QJsonObject> fetched;
    for (const auto &entry : data.value(QStringLiteral("items")).toArray()) {
        if (!entry.isObject()) continue;
        const auto raw = entry.toObject();
        const QString id = truthyString(raw.value(QStringLiteral("id"))).trimmed();
        const QString title = truthyString(raw.value(QStringLiteral("title"))).trimmed();
        const QString target = truthyString(raw.value(QStringLiteral("targetUrl"))).trimmed();
        if (id.isEmpty() || title.isEmpty() || !httpUrl(target)) continue;
        const QString sourceId = source->config.value(QStringLiteral("id")).toString();
        const QString level = truthyString(raw.value(QStringLiteral("level")), QStringLiteral("normal"));
        fetched.append(QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("title"), title},
            {QStringLiteral("summary"), truthyString(raw.value(QStringLiteral("summary")))},
            {QStringLiteral("level"), level},
            {QStringLiteral("createdAt"), truthyString(raw.value(QStringLiteral("createdAt")))},
            {QStringLiteral("targetUrl"), target},
            {QStringLiteral("uid"), sourceId + QStringLiteral("::") + id},
            {QStringLiteral("origin"), QJsonObject{
                {QStringLiteral("id"), sourceId}, {QStringLiteral("name"), source->config.value(QStringLiteral("name"))},
                {QStringLiteral("color"), colorFor(sourceId)}}}
        });
    }
    std::stable_sort(fetched.begin(), fetched.end(), [](const auto &a, const auto &b) { return timestamp(a) > timestamp(b); });
    if (fetched.size() > maxItems_) fetched = fetched.mid(0, maxItems_);
    QSet<QString> fetchedIds;
    for (const auto &item : fetched) fetchedIds.insert(item.value(QStringLiteral("id")).toString());
    QList<QString> keptCleared;
    for (const auto &id : source->clearedQueue) if (fetchedIds.contains(id)) keptCleared.append(id);
    source->clearedQueue = keptCleared;
    source->clearedIds = QSet<QString>(keptCleared.begin(), keptCleared.end());

    source->snapshot = {};
    int newCount = 0;
    for (const auto &item : fetched) {
        const auto id = item.value(QStringLiteral("id")).toString();
        if (source->clearedIds.contains(id)) continue;
        source->snapshot.append(item);
        if (!source->knownIds.contains(id)) ++newCount;
    }
    for (const auto &entry : source->snapshot) {
        const QString id = entry.toObject().value(QStringLiteral("id")).toString();
        if (!source->knownIds.contains(id)) {
            source->knownIds.insert(id);
            source->knownQueue.append(id);
        }
    }
    while (source->knownQueue.size() > KnownIdLimit) source->knownIds.remove(source->knownQueue.takeFirst());
    const int total = integerValue(data.value(QStringLiteral("total")), fetched.size());
    source->total = std::max(0, total == 0 ? int(fetched.size()) : total);
    source->refreshedAt = now();
    if (!silent && newCount > 0) notify(newCount, source->config.value(QStringLiteral("name")).toString());
}

void MessageService::refresh(bool silent) {
    for (const auto &source : sources_) refreshSource(source, silent, false);
}

void MessageService::dismiss(QString sourceId, QString id) {
    for (const auto &source : sources_) {
        if (source->config.value(QStringLiteral("id")).toString() != sourceId) continue;
        QJsonArray next;
        for (const auto &entry : source->snapshot)
            if (entry.toObject().value(QStringLiteral("id")).toString() != id) next.append(entry);
        if (next.size() == source->snapshot.size()) return;
        source->snapshot = next;
        if (!source->clearedIds.contains(id)) {
            source->clearedIds.insert(id);
            source->clearedQueue.append(id);
        }
        while (source->clearedQueue.size() > ClearedIdLimit) source->clearedIds.remove(source->clearedQueue.takeFirst());
        source->refreshedAt = now();
        emit changed();
        return;
    }
}

void MessageService::notify(int count, const QString &name) {
    if (!enabled_ || count <= 0) return;
    if (breakVisible_) {
        if (pendingCount_ == 0) pendingName_ = name;
        else if (pendingName_ != name) pendingName_ = QStringLiteral("待办消息");
        pendingCount_ += count;
    } else emit notifyRequested(count, name);
}

void MessageService::setBreakVisible(bool visible) {
    if (breakVisible_ == visible) return;
    breakVisible_ = visible;
    const auto generation = ++deferredGeneration_;
    if (!visible && pendingCount_ > 0) {
        QTimer::singleShot(500, this, [this, generation] {
            if (generation != deferredGeneration_ || breakVisible_ || !enabled_ || pendingCount_ <= 0) return;
            const int count = pendingCount_;
            const QString name = pendingName_;
            pendingCount_ = 0;
            pendingName_.clear();
            emit notifyRequested(count, name);
        });
    }
}

void MessageService::testSource(QJsonObject source) {
    const auto explicitLimit = source.value(QStringLiteral("maxCacheItems"));
    const int limit = std::clamp(integerValue(explicitLimit.isNull() || explicitLimit.isUndefined()
        ? source.value(QStringLiteral("messageMaxCacheItems")) : explicitLimit, 30), 1, 30);
    source = normalizeSource(source);
    const QString id = source.value(QStringLiteral("id")).toString();
    fetch(source, limit, [this, id](FetchResult result) {
        const int count = result.data.value(QStringLiteral("items")).toArray().size();
        emit connectionTested(id, result.ok,
            result.ok ? QStringLiteral("接通：列表返回 %1 条").arg(count) : result.error, count);
    }, true);
}

QJsonArray MessageService::items() const {
    QList<QJsonObject> merged;
    for (const auto &source : sources_) if (enabled(source))
        for (const auto &item : source->snapshot) merged.append(item.toObject());
    QLocale locale;
    // JavaScript's Intl default is en-US in a C locale; Qt otherwise compares
    // raw character values there, which reverses case and punctuation ordering.
    if (locale.language() == QLocale::C) locale = QLocale(QLocale::English, QLocale::UnitedStates);
    const QCollator collator(locale);
    std::stable_sort(merged.begin(), merged.end(), [&collator](const auto &a, const auto &b) {
        if (timestamp(a) != timestamp(b)) return timestamp(a) > timestamp(b);
        return collator.compare(a.value(QStringLiteral("uid")).toString(), b.value(QStringLiteral("uid")).toString()) < 0;
    });
    QJsonArray result;
    for (const auto &item : merged) result.append(item);
    return result;
}

int MessageService::unreadCount() const {
    int count = 0;
    for (const auto &source : sources_) if (enabled(source)) count += source->snapshot.size();
    return count;
}

QJsonObject MessageService::state() const {
    const auto allItems = items();
    int total = 0;
    int hidden = 0;
    bool configured = false;
    bool loading = false;
    bool backoff = false;
    QString refreshed;
    QString error;
    QJsonArray sourceStates;
    for (const auto &source : sources_) {
        const QString id = source->config.value(QStringLiteral("id")).toString();
        const QString name = source->config.value(QStringLiteral("name")).toString();
        if (enabled(source)) {
            configured = true;
            total += source->total;
            hidden += std::max(0, source->total - maxItems_);
            loading = loading || source->inFlight;
            backoff = backoff || source->failures >= 5;
            if (source->refreshedAt > refreshed) refreshed = source->refreshedAt;
            if (error.isEmpty() && !source->error.isEmpty()) error = name + QStringLiteral(": ") + source->error;
        }
        sourceStates.append(QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("color"), colorFor(id)},
            {QStringLiteral("enabled"), enabled_ && source->config.value(QStringLiteral("enabled")).toBool()},
            {QStringLiteral("configured"), httpUrl(source->config.value(QStringLiteral("listUrl")).toString())},
            {QStringLiteral("total"), source->total}, {QStringLiteral("badgeCount"), source->snapshot.size()},
            {QStringLiteral("itemCount"), source->snapshot.size()}, {QStringLiteral("lastError"), source->error},
            {QStringLiteral("loading"), source->inFlight}, {QStringLiteral("backoff"), source->failures >= 5},
            {QStringLiteral("lastRefreshAt"), source->refreshedAt}, {QStringLiteral("consecutiveFailures"), source->failures},
            {QStringLiteral("effectivePollInterval"), intervalMs(source) / 1000}
        });
    }
    return {
        {QStringLiteral("enabled"), enabled_}, {QStringLiteral("configured"), configured},
        {QStringLiteral("total"), std::max(total, int(allItems.size()))}, {QStringLiteral("badgeCount"), unreadCount()},
        {QStringLiteral("items"), allItems}, {QStringLiteral("itemCount"), allItems.size()},
        {QStringLiteral("hiddenCount"), hidden}, {QStringLiteral("maxCacheItems"), maxItems_},
        {QStringLiteral("pollInterval"), pollInterval_}, {QStringLiteral("lastRefreshAt"), refreshed},
        {QStringLiteral("lastError"), error}, {QStringLiteral("loading"), loading},
        {QStringLiteral("backoff"), backoff}, {QStringLiteral("sources"), sourceStates}
    };
}

} // namespace purr
