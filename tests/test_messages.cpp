#include "messages.h"

#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <functional>

using purr::MessageService;

namespace {
QJsonObject todo(const QString &id, const QString &time = QStringLiteral("2026-05-26T10:00:00Z"),
                 const QString &target = QStringLiteral("https://example.com/todo")) {
    return {{"id", id}, {"title", QStringLiteral("待办 ") + id}, {"createdAt", time},
            {"summary", QStringLiteral("<b>plain text</b>")}, {"targetUrl", target}};
}

QByteArray success(QJsonArray items, int total = -1) {
    if (total < 0) total = items.size();
    return QJsonDocument(QJsonObject{{"code", 0}, {"message", "ok"},
        {"data", QJsonObject{{"total", total}, {"items", items}}}}).toJson(QJsonDocument::Compact);
}

class HttpFixture : public QObject {
public:
    struct Response {
        QByteArray body = success({});
        int status = 200;
        int delayMs = 0;
        QByteArray extraHeaders;
    } response;
    QTcpServer server;
    QList<QByteArray> requests;
    std::function<Response(const QByteArray &)> handler;

    HttpFixture() {
        const bool listening = server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(listening);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                auto input = QSharedPointer<QByteArray>::create();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QIODevice::readyRead, this, [this, socket, input] {
                    input->append(socket->readAll());
                    if (!input->contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                    socket->setProperty("answered", true);
                    requests.append(*input);
                    const Response result = handler ? handler(*input) : response;
                    const QPointer<QTcpSocket> target(socket);
                    QTimer::singleShot(result.delayMs, this, [target, result] {
                        if (!target || target->state() != QAbstractSocket::ConnectedState) return;
                        const QByteArray header = "HTTP/1.1 " + QByteArray::number(result.status)
                            + " Fixture\r\nContent-Type: application/json\r\nContent-Length: "
                            + QByteArray::number(result.body.size()) + "\r\nConnection: close\r\n"
                            + result.extraHeaders + "\r\n";
                        target->write(header);
                        target->write(result.body);
                        target->disconnectFromHost();
                    });
                });
            }
        });
    }

    QString url(const QString &path = QStringLiteral("/todos")) const {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path);
    }

    QJsonObject source(const QString &id = QStringLiteral("oa"), const QString &path = QStringLiteral("/todos")) const {
        return {{"id", id}, {"name", id}, {"listUrl", url(path)},
                {"authHeaders", "{}"}, {"enabled", true}};
    }
};

QJsonObject config(const QJsonArray &sources, int limit = 30) {
    return {{"messageNotifyEnabled", true}, {"messageSources", sources},
            {"messageMaxCacheItems", limit}, {"messagePollInterval", 60}};
}

QJsonObject firstSource(const MessageService &service) {
    return service.state().value("sources").toArray().at(0).toObject();
}
} // namespace

class MessageServiceTest : public QObject {
    Q_OBJECT
private slots:
    void baselineQueryAndLocalDismissal() {
        HttpFixture http;
        http.response.body = success({todo("a"), todo("b", "2026-05-26T11:00:00Z")}, 20);
        auto source = http.source("oa", "/integration/todos?tenant=cat&limit=99&offset=7");
        source.insert("authHeaders", R"({"Authorization":"Bearer fixture-token","X-Number":7})");
        MessageService service;
        QSignalSpy notifications(&service, &MessageService::notifyRequested);
        service.setConfig(config({source}, 2));
        QTRY_COMPARE(service.unreadCount(), 2);
        QCOMPARE(notifications.size(), 0);
        QCOMPARE(service.items().first().toObject().value("id").toString(), QString("b"));
        QCOMPARE(service.state().value("total").toInt(), 20);
        QCOMPARE(service.state().value("hiddenCount").toInt(), 18);
        QCOMPARE(http.requests.size(), 1);
        const QByteArray request = http.requests.first();
        const auto target = QUrl::fromEncoded(request.split(' ').at(1));
        const QUrlQuery query(target);
        QCOMPARE(target.path(), QString("/integration/todos"));
        QCOMPARE(query.queryItemValue("tenant"), QString("cat"));
        QCOMPARE(query.queryItemValue("limit"), QString("2"));
        QCOMPARE(query.queryItemValue("offset"), QString("0"));
        QVERIFY(request.contains("Authorization: Bearer fixture-token\r\n"));
        QVERIFY(request.contains("Accept: application/json\r\n"));
        QVERIFY(request.contains("X-Number: 7\r\n"));

        service.dismiss("oa", "b");
        QCOMPARE(service.unreadCount(), 1);
        service.refresh(false);
        QTRY_COMPARE(http.requests.size(), 2);
        QTRY_VERIFY(!service.state().value("loading").toBool());
        QCOMPARE(service.unreadCount(), 1);
        QCOMPARE(notifications.size(), 0);
        // A removed server item loses its local dismissal, but remains a known ID.
        http.response.body = success({todo("a")});
        service.refresh(false);
        QTRY_COMPARE(http.requests.size(), 3);
        QTRY_VERIFY(!service.state().value("loading").toBool());
        http.response.body = success({todo("a"), todo("b")});
        service.refresh(false);
        QTRY_COMPARE(service.unreadCount(), 2);
        QCOMPARE(notifications.size(), 0);
        http.response.body = success({todo("c", "2026-05-26T12:00:00Z"), todo("b")});
        service.refresh(false);
        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications.first().at(0).toInt(), 1);
        QCOMPARE(notifications.first().at(1).toString(), QString("oa"));
        // Nothing writes back to the remote source.
        for (const auto &entry : http.requests) QVERIFY(entry.startsWith("GET "));
    }

    void filteringCappingAndIndependentSources() {
        HttpFixture http;
        http.handler = [](const QByteArray &request) {
            HttpFixture::Response result;
            if (request.startsWith("GET /one")) {
                result.body = success({todo("shared"), todo("unsafe", {}, "javascript:alert(1)"),
                    QJsonObject{{"id", "no-title"}, {"targetUrl", "https://example.com"}},
                    todo("newest", "2026-05-27T12:00:00Z"), todo("overflow", "2026-01-01T00:00:00Z")}, 8);
            } else result.body = success({todo("shared")});
            return result;
        };
        MessageService service;
        service.setConfig(config({http.source("one", "/one"), http.source("two", "/two")}, 2));
        QTRY_COMPARE(service.unreadCount(), 3);
        QCOMPARE(service.items().first().toObject().value("id").toString(), QString("newest"));
        QSet<QString> identities;
        for (const auto &item : service.items()) identities.insert(item.toObject().value("uid").toString());
        QVERIFY(identities.contains("one::shared"));
        QVERIFY(identities.contains("two::shared"));
        QCOMPARE(service.state().value("hiddenCount").toInt(), 6);
        service.dismiss("one", "shared");
        QCOMPARE(service.unreadCount(), 2);
        QVERIFY(service.items().last().toObject().value("uid").toString() == "two::shared");
    }

    void silentRefreshAndRenamePreserveBaseline() {
        HttpFixture http;
        http.response.body = success({todo("a")});
        MessageService service;
        QSignalSpy notifications(&service, &MessageService::notifyRequested);
        auto source = http.source();
        service.setConfig(config({source}));
        QTRY_COMPARE(service.unreadCount(), 1);
        http.response.body = success({todo("a"), todo("b")});
        service.refresh();
        QTRY_COMPARE(service.unreadCount(), 2);
        QCOMPARE(notifications.size(), 0);
        source.insert("name", "renamed");
        service.setConfig(config({source}));
        // The Electron runtime keeps the already-fetched item object until the
        // next successful fetch; only the source filter name changes now.
        QCOMPARE(service.items().first().toObject().value("origin").toObject().value("name").toString(), QString("oa"));
        http.response.body = success({todo("a"), todo("b"), todo("c")});
        service.refresh(false);
        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications.first().at(0).toInt(), 1);
        QCOMPARE(notifications.first().at(1).toString(), QString("renamed"));
    }

    void failuresKeepBaselineAndBackoffRecovers() {
        HttpFixture http;
        http.response.status = 500;
        MessageService service;
        QSignalSpy notifications(&service, &MessageService::notifyRequested);
        service.setConfig(config({http.source()}));
        QTRY_COMPARE(firstSource(service).value("consecutiveFailures").toInt(), 1);
        for (int failure = 2; failure <= 7; ++failure) {
            service.refresh(false);
            QTRY_COMPARE(firstSource(service).value("consecutiveFailures").toInt(), failure);
        }
        QVERIFY(service.state().value("backoff").toBool());
        QCOMPARE(firstSource(service).value("effectivePollInterval").toInt(), 300);
        http.response.status = 200;
        http.response.body = success({todo("first")});
        service.refresh(false);
        QTRY_COMPARE(service.unreadCount(), 1);
        QCOMPARE(notifications.size(), 0);
        QVERIFY(!service.state().value("backoff").toBool());
        QVERIFY(service.state().value("lastError").toString().isEmpty());
        QCOMPARE(firstSource(service).value("effectivePollInterval").toInt(), 60);
        http.response.body = "{\"code\":\"0\",\"data\":{\"items\":[]}}";
        service.refresh(false);
        QTRY_COMPARE(firstSource(service).value("consecutiveFailures").toInt(), 1);
        QCOMPARE(service.unreadCount(), 1);
        http.response.body = success({todo("first"), todo("second")});
        service.refresh(false);
        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications.first().at(0).toInt(), 1);
    }

    void breakDefersAndCombinesNotifications() {
        HttpFixture http;
        http.response.body = success({});
        MessageService service;
        QSignalSpy notifications(&service, &MessageService::notifyRequested);
        service.setConfig(config({http.source("one", "/one"), http.source("two", "/two")}));
        QTRY_COMPARE(http.requests.size(), 2);
        QTRY_VERIFY(!service.state().value("loading").toBool());
        service.setBreakVisible(true);
        http.response.body = success({todo("new")});
        service.refresh(false);
        QTRY_COMPARE(service.unreadCount(), 2);
        QCOMPARE(notifications.size(), 0);
        service.setBreakVisible(false);
        QTRY_COMPARE(notifications.size(), 1);
        QCOMPARE(notifications.first().at(0).toInt(), 2);
        QCOMPARE(notifications.first().at(1).toString(), QString("待办消息"));
        service.setBreakVisible(true);
        http.response.body = success({todo("new"), todo("later")});
        service.refresh(false);
        QTRY_COMPARE(service.unreadCount(), 4);
        // Removing all sources discards their deferred notifications as well.
        service.setConfig(config({}));
        service.setBreakVisible(false);
        QTest::qWait(600);
        QCOMPARE(notifications.size(), 1);
    }

    void disabledAndChangedSourcesRejectStaleReplies() {
        HttpFixture http;
        http.handler = [](const QByteArray &request) {
            HttpFixture::Response result;
            if (request.startsWith("GET /slow")) {
                result.body = success({todo("stale")});
                result.delayMs = 250;
            } else result.body = success({todo("current")});
            return result;
        };
        MessageService service;
        QSignalSpy notifications(&service, &MessageService::notifyRequested);
        service.setConfig(config({http.source("oa", "/slow")}));
        QTRY_COMPARE(http.requests.size(), 1);
        // Refreshing an active request cannot create concurrent calls for the source.
        service.refresh();
        service.refresh();
        QCOMPARE(http.requests.size(), 1);
        auto disabled = config({http.source("oa", "/slow")});
        disabled.insert("messageNotifyEnabled", false);
        service.setConfig(disabled);
        QTest::qWait(300);
        QCOMPARE(service.unreadCount(), 0);
        QVERIFY(!service.state().value("loading").toBool());
        QVERIFY(service.state().value("lastError").toString().isEmpty());
        service.setConfig(config({http.source("oa", "/slow")}));
        QTRY_COMPARE(http.requests.size(), 2);
        service.setConfig(config({http.source("oa", "/current")}));
        QTRY_COMPARE(service.unreadCount(), 1);
        QTest::qWait(300);
        QCOMPARE(service.items().first().toObject().value("id").toString(), QString("current"));
        QCOMPARE(notifications.size(), 0);
        service.setConfig(config({}));
        QCOMPARE(service.unreadCount(), 0);
        QVERIFY(service.state().value("sources").toArray().isEmpty());
    }

    void connectionValidationAndResponseLimits() {
        HttpFixture http;
        MessageService service;
        QSignalSpy tests(&service, &MessageService::connectionTested);
        auto source = http.source();
        source.insert("listUrl", "file:///etc/passwd");
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(!tests.takeFirst().at(1).toBool());
        QCOMPARE(http.requests.size(), 0);
        source = http.source();
        source.insert("authHeaders", "[]");
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(!tests.takeFirst().at(1).toBool());
        source.insert("authHeaders", R"({"Authorization":"safe\r\nX-Injected: bad"})");
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(!tests.takeFirst().at(1).toBool());
        QCOMPARE(http.requests.size(), 0);

        source = http.source();
        http.response.body = success({todo("valid")});
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        const auto successResult = tests.takeFirst();
        QCOMPARE(successResult.at(0).toString(), QString("oa"));
        QVERIFY(successResult.at(1).toBool());
        QCOMPARE(successResult.at(3).toInt(), 1);
        QCOMPARE(service.unreadCount(), 0);
        http.response.body = "not-json";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(tests.takeFirst().at(2).toString().contains("JSON"));
        http.response.body = QByteArray(1024 * 1024 + 1, 'x');
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(tests.takeFirst().at(2).toString().contains("1 MiB"));
    }

    void javascriptItemCoercionMatchesElectron() {
        HttpFixture http;
        const QString target = QStringLiteral("https://example.com");
        http.response.body = success({
            QJsonObject{{"id", 0}, {"title", "zero"}, {"targetUrl", target}},
            QJsonObject{{"id", false}, {"title", "false"}, {"targetUrl", target}},
            QJsonObject{{"id", true}, {"title", true},
                {"summary", QJsonArray{1, QJsonValue::Null, QJsonObject{{"x", 2}}, QJsonArray{3, 4}}},
                {"level", false}, {"createdAt", 0}, {"targetUrl", target}},
            QJsonObject{{"id", "object"}, {"title", QJsonObject{{"x", 1}}},
                {"summary", false}, {"level", 0}, {"targetUrl", target}},
            QJsonObject{{"id", "array"}, {"title", QJsonArray{"a", "b"}},
                {"summary", QJsonArray{}}, {"level", QJsonArray{}}, {"createdAt", QJsonArray{}}, {"targetUrl", target}},
            QJsonObject{{"id", 1e-7}, {"title", 1e-6}, {"targetUrl", target}}
        });
        MessageService service;
        service.setConfig(config({http.source()}));
        QTRY_COMPARE(service.unreadCount(), 4);
        // Expected values were obtained by running the same payload through
        // the original Electron message-notify.js, not by copying native conversion code.
        const QJsonArray expected{
            QJsonObject{{"id", "1e-7"}, {"title", "0.000001"}, {"summary", ""}, {"level", "normal"}, {"createdAt", ""}},
            QJsonObject{{"id", "array"}, {"title", "a,b"}, {"summary", ""}, {"level", ""}, {"createdAt", ""}},
            QJsonObject{{"id", "object"}, {"title", "[object Object]"}, {"summary", ""}, {"level", "normal"}, {"createdAt", ""}},
            QJsonObject{{"id", "true"}, {"title", "true"}, {"summary", "1,,[object Object],3,4"}, {"level", "normal"}, {"createdAt", ""}}
        };
        for (int i = 0; i < expected.size(); ++i) {
            const auto actual = service.items().at(i).toObject();
            const auto wanted = expected.at(i).toObject();
            for (auto field = wanted.begin(); field != wanted.end(); ++field)
                QCOMPARE(actual.value(field.key()), field.value());
        }
    }

    void equalTimestampsUseLocaleOrdering() {
        HttpFixture http;
        QJsonArray incoming;
        for (const auto &id : {"Z", "a", "A", "_", "-", "ä", "z"}) incoming.append(todo(QString::fromUtf8(id)));
        http.response.body = success(incoming);
        MessageService service;
        service.setConfig(config({http.source()}));
        QTRY_COMPARE(service.unreadCount(), 7);
        QStringList actual;
        for (const auto &entry : service.items()) actual.append(entry.toObject().value("id").toString());
        // Original getPublicState() sorts tied dates with uid.localeCompare().
        QCOMPARE(actual, (QStringList{"_", "-", "a", "A", QString::fromUtf8("ä"), "z", "Z"}));
    }

    void isoDateOnlyUsesUtc() {
        HttpFixture http;
        http.response.body = success({todo("b", "2026-05-26"), todo("a", "2026-05-25T23:00:00Z")});
        MessageService service;
        service.setConfig(config({http.source()}));
        QTRY_COMPARE(service.unreadCount(), 2);
        QCOMPARE(service.items().first().toObject().value("id").toString(), QStringLiteral("b"));
    }

    void connectionLimitAliasesAndDetailedErrors() {
        HttpFixture http;
        MessageService service;
        service.setConfig(config({}, 2));
        QSignalSpy tests(&service, &MessageService::connectionTested);
        auto source = http.source();
        // Connection tests default to 30 regardless of the live service limit.
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(http.requests.last().contains("limit=30&offset=0"));
        source.insert("messageMaxCacheItems", 7);
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 2);
        QVERIFY(http.requests.last().contains("limit=7&offset=0"));
        source.insert("maxCacheItems", QJsonArray{3});
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 3);
        QVERIFY(http.requests.last().contains("limit=3&offset=0"));

        http.response.status = 401;
        http.response.body = R"({"code":42,"message":"token expired"})";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 4);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("HTTP 401：token expired"));
        http.response.body = "not-json";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 5);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("响应不是合法 JSON"));
        http.response.status = 200;
        http.response.body = R"({"code":42,"message":""})";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 6);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("第三方接口返回失败 (code=42)"));
        http.response.body.clear();
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 7);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("第三方接口返回失败 (code=null)"));
        http.response.body = "[]";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 8);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("第三方接口返回失败 (code=undefined)"));
        http.response.body = "false";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 9);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("第三方接口返回失败 (code=false)"));
        http.response.body = R"({"code":0},{"code":0})";
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 10);
        QCOMPARE(tests.last().at(2).toString(), QStringLiteral("响应不是合法 JSON"));
    }

    void credentialsDoNotFollowCrossOriginRedirect() {
        HttpFixture origin;
        HttpFixture other;
        origin.response.status = 302;
        origin.response.extraHeaders = "Location: " + other.url().toUtf8() + "\r\n";
        MessageService service;
        QSignalSpy tests(&service, &MessageService::connectionTested);
        auto source = origin.source();
        source.insert("authHeaders", R"({"Authorization":"Bearer private-token","X-Api-Key":"also-secret"})");
        service.testSource(source);
        QTRY_COMPARE(tests.size(), 1);
        QVERIFY(!tests.first().at(1).toBool());
        QCOMPARE(origin.requests.size(), 1);
        QCOMPARE(other.requests.size(), 0);
    }

    void requestsHaveFiveSecondDeadline() {
        HttpFixture http;
        http.response.delayMs = 7000;
        MessageService service;
        QSignalSpy tests(&service, &MessageService::connectionTested);
        service.testSource(http.source());
        QTRY_COMPARE_WITH_TIMEOUT(tests.size(), 1, 6500);
        QVERIFY(!tests.first().at(1).toBool());
        QVERIFY(tests.first().at(2).toString().contains("5 秒"));
    }
};

QTEST_GUILESS_MAIN(MessageServiceTest)
#include "test_messages.moc"
