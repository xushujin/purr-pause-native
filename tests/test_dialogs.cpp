#include "dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

using namespace purr;

namespace {
QJsonObject source(const char *id) {
    return {{"id", id}, {"name", id}, {"listUrl", "https://example.com/todos"}, {"authHeaders", "{}"}, {"enabled", true}};
}
QJsonObject config() {
    return {{"messageNotifyEnabled", true}, {"messageSources", QJsonArray{source("one"), source("two")}},
            {"messageMaxCacheItems", 7}, {"messageNotifyVideo", "notify-rocket.webm"}};
}
QPushButton *saveButton(QWidget &dialog) {
    for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == QStringLiteral("保存")) return button;
    return nullptr;
}
}

class DialogTests : public QObject {
    Q_OBJECT
private slots:
    void connectionTestsAreSerializedAndUseCurrentLimit() {
        MessageSettingsDialog dialog(config());
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        const auto buttons = dialog.findChildren<QPushButton *>("sourceTest");
        const auto results = dialog.findChildren<QLabel *>("sourceTestResult");
        QCOMPARE(buttons.size(), 2);
        QSignalSpy tests(&dialog, &MessageSettingsDialog::testRequested);
        dialog.findChild<QSpinBox *>("messageMaxCacheItems")->setValue(11);
        buttons[0]->click();
        QCOMPARE(tests.size(), 1);
        QCOMPARE(tests[0][0].toJsonObject().value("maxCacheItems").toInt(), 11);
        QVERIFY(!buttons[0]->isEnabled());
        buttons[1]->click();
        QCOMPARE(tests.size(), 1);
        QCOMPARE(results[1]->text(), QStringLiteral("请等待当前测试完成"));
        dialog.connectionResult("two", true, {}, 9);
        QVERIFY(!buttons[0]->isEnabled());
        dialog.connectionResult("one", true, {}, 3);
        QVERIFY(buttons[0]->isEnabled());
        QCOMPARE(results[0]->text(), QStringLiteral("✓ 接通：列表返回 3 条"));
        buttons[1]->click();
        QCOMPARE(tests.size(), 2);
        dialog.connectionResult("two", false, "HTTP 401", 0);
        QCOMPARE(results[1]->text(), QStringLiteral("✗ 测试失败：HTTP 401"));
    }

    void invalidHeadersExpandWithoutSendingRequests() {
        MessageSettingsDialog dialog(config());
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        const auto headers = dialog.findChildren<QPlainTextEdit *>("sourceHeaders");
        const auto toggles = dialog.findChildren<QPushButton *>("sourceHeadersToggle");
        const auto tests = dialog.findChildren<QPushButton *>("sourceTest");
        QVERIFY(!toggles[0]->isChecked());
        QSignalSpy requested(&dialog, &MessageSettingsDialog::testRequested);
        headers[0]->setPlainText("[]");
        tests[0]->click();
        QCOMPARE(requested.size(), 0);
        QVERIFY(toggles[0]->isChecked());
        QVERIFY(headers[0]->isVisible());
        QVERIFY(headers[0]->property("invalid").toBool());
        headers[0]->setPlainText(R"({"Authorization":"Bearer fixture"})");
        QVERIFY(!headers[0]->property("invalid").toBool());
        toggles[0]->click();
        QVERIFY(!headers[0]->isVisible());
        toggles[0]->click();
        QCOMPARE(headers[0]->toPlainText(), QStringLiteral(R"({"Authorization":"Bearer fixture"})"));
        tests[0]->click();
        QCOMPARE(requested.size(), 1);
    }

    void saveValidationIsInlineAndPreservesForm() {
        MessageSettingsDialog dialog(config());
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        QSignalSpy saved(&dialog, &MessageSettingsDialog::saved);
        auto *name = dialog.findChildren<QLineEdit *>("sourceName").first();
        auto *headers = dialog.findChildren<QPlainTextEdit *>("sourceHeaders").first();
        name->clear();
        headers->setPlainText("null");
        auto *save = saveButton(dialog);
        QVERIFY(save);
        save->click();
        QCOMPARE(saved.size(), 0);
        QVERIFY(dialog.isVisible());
        QVERIFY(name->property("invalid").toBool());
        QVERIFY(headers->isVisible());
        QCOMPARE(dialog.findChild<QLabel *>("saveStatus")->text(), QStringLiteral("第 1 个接口源名称不能为空"));
        name->setText("First");
        headers->setPlainText("{}");
        auto *video = dialog.findChild<QLineEdit *>("messageNotifyVideo");
        video->setText("../rocket.webm");
        save->click();
        QCOMPARE(saved.size(), 0);
        QVERIFY(dialog.findChild<QPushButton *>("globalSettingsToggle")->isChecked());
        QVERIFY(video->property("invalid").toBool());
        video->clear();
        save->click();
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved[0][0].toJsonObject().value("messageNotifyVideo").toString(), QStringLiteral("notify-rocket.webm"));
        dialog.showSaveResult(true);
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.findChild<QLabel *>("saveStatus")->isVisible());
    }

    void saveStatusLeavesSettingsOpenAndExpires() {
        SettingsDialog dialog({}, true, false);
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        QSignalSpy saved(&dialog, &SettingsDialog::saved);
        saveButton(dialog)->click();
        QCOMPARE(saved.size(), 1);
        dialog.showSaveResult(true);
        auto *status = dialog.findChild<QLabel *>("saveStatus");
        QVERIFY(status->isVisible());
        QCOMPARE(status->text(), QStringLiteral("保存成功 ✓"));
        QVERIFY(dialog.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!status->isVisible(), 3000);
        QVERIFY(dialog.isVisible());
        dialog.showSaveResult(false, "disk full");
        QCOMPARE(status->text(), QStringLiteral("disk full"));
        QVERIFY(status->isVisible());
    }

    void messageListHasOriginalMinimumSize() {
        MessageListDialog dialog;
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        QCOMPARE(dialog.minimumSize(), QSize(460, 420));
    }

    void sourceControlsUpdateCountAndEmptyState() {
        MessageSettingsDialog dialog({});
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        QCOMPARE(dialog.findChild<QLabel *>("sourceCount")->text(), QStringLiteral("0"));
        QVERIFY(dialog.findChild<QLabel *>("emptySources")->isVisible());
        dialog.findChild<QPushButton *>("addSource")->click();
        QCOMPARE(dialog.findChild<QLabel *>("sourceCount")->text(), QStringLiteral("1"));
        QVERIFY(!dialog.findChild<QLabel *>("emptySources")->isVisible());
        QTRY_VERIFY(dialog.findChild<QLineEdit *>("sourceName")->hasFocus());
        auto *enabled = dialog.findChild<QCheckBox *>("messageNotifyEnabled");
        QCOMPARE(enabled->size(), QSize(40, 22));
        QPushButton *remove = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == QStringLiteral("×")) remove = button;
        QVERIFY(remove);
        remove->click();
        QCOMPARE(dialog.findChild<QLabel *>("sourceCount")->text(), QStringLiteral("0"));
        QVERIFY(dialog.findChild<QLabel *>("emptySources")->isVisible());
    }

    void activationStatusCopyAndSuccessFeedback() {
        ActivationDialog dialog({{"status", "expired"}, {"type", "trial"}}, "0123456789abcdef");
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        QCOMPARE(dialog.findChild<QLabel *>("licenseStatus")->text(), QStringLiteral("试用已到期"));
        QPushButton *later = nullptr;
        QPushButton *copy = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("稍后再说")) later = button;
            if (button->text() == QStringLiteral("复制")) copy = button;
        }
        QVERIFY(later && copy);
        QVERIFY(!later->isVisible());
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0123456789abcdef"));
        dialog.showSuccess({{"status", "active"}, {"type", "permanent"}});
        QCOMPARE(dialog.findChild<QLabel *>("activationError")->text(), QStringLiteral("激活成功！（永久授权）"));
        QVERIFY(later->isVisible());
        QCOMPARE(later->text(), QStringLiteral("稍后再说"));
    }

    void messageFilterAndErrorBranchesStayConsistent() {
        MessageListDialog dialog;
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        QJsonObject state{{"enabled", true}, {"configured", true}, {"badgeCount", 1},
            {"sources", QJsonArray{QJsonObject{{"id", "one"}, {"name", "First"}, {"badgeCount", 1}, {"color", "#2f80ed"}}}},
            {"items", QJsonArray{QJsonObject{{"id", "a"}, {"title", "Task"}, {"targetUrl", "https://example.com"},
                {"origin", QJsonObject{{"id", "one"}, {"name", "First"}, {"color", "#2f80ed"}}}}}},
            {"hiddenCount", 5}, {"maxCacheItems", 7}};
        dialog.setState(state);
        dialog.show();
        auto *items = dialog.findChild<QListWidget *>("messageItems");
        QCOMPARE(items->count(), 1);
        // Upstream CSS accidentally shows the empty label even with items;
        // native deliberately preserves a truthful empty state.
        QVERIFY(items->item(0)->text() != QStringLiteral("暂无待办消息"));
        auto *notice = dialog.findChild<QLabel *>("messageNotice");
        QVERIFY(notice->isVisible());
        dialog.findChild<QPushButton *>("filter-one")->click();
        QVERIFY(!notice->isVisible());
        state.insert("lastError", "First: HTTP 500");
        state.insert("backoff", true);
        dialog.setState(state);
        QCOMPARE(dialog.findChild<QLabel *>("messageError")->text(), QStringLiteral("First: HTTP 500"));
        QCOMPARE(notice->text(), QStringLiteral("最近同步失败，已降低轮询频率（First: HTTP 500）"));
        state.insert("sources", QJsonArray{});
        dialog.setState(state);
        QVERIFY(dialog.findChild<QPushButton *>("filter-all")->isChecked());
    }
};

QTEST_MAIN(DialogTests)
#include "test_dialogs.moc"
