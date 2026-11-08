#include "dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSpinBox>
#include <QSysInfo>
#include <QTimer>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok, const QString &message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}

QJsonValue value(QWidget *widget) {
    if (auto *spin = qobject_cast<QSpinBox *>(widget)) return spin->value();
    if (auto *combo = qobject_cast<QComboBox *>(widget)) return combo->currentData().toString();
    if (auto *check = qobject_cast<QCheckBox *>(widget)) return check->isChecked();
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) return edit->text();
    if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) return edit->toPlainText();
    if (auto *label = qobject_cast<QLabel *>(widget)) return label->text();
    if (auto *button = qobject_cast<QPushButton *>(widget)) return button->text();
    throw std::runtime_error("Unsupported visual binding widget");
}

QJsonObject describe(QWidget *widget, QDialog *dialog) {
    const QPoint position = widget->mapTo(dialog, QPoint());
    const QFontInfo font(widget->font());
    return {{"value", value(widget)}, {"visible", widget->isVisible()}, {"enabled", widget->isEnabled()},
        {"rect", QJsonObject{{"x", position.x()}, {"y", position.y()}, {"width", widget->width()}, {"height", widget->height()}}},
        {"font", QJsonObject{{"family", font.family()}, {"pixelSize", font.pixelSize()}, {"weight", font.weight()}, {"italic", font.italic()}}}};
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Capture actual native dialogs for comparison with the original renderer."));
    parser.addHelpOption();
    parser.addOption({"config", "Shared visual fixture JSON", "file"});
    parser.addOption({"output", "Output directory", "directory"});
    parser.process(app);
    try {
        require(parser.isSet("config") && parser.isSet("output"), "--config and --output are required");
        QFile fixtureFile(parser.value("config"));
        require(fixtureFile.open(QIODevice::ReadOnly), "Cannot read fixture");
        const QByteArray fixtureBytes = fixtureFile.readAll();
        const auto document = QJsonDocument::fromJson(fixtureBytes);
        require(document.isObject(), "Fixture must be a JSON object");
        const auto fixture = document.object();
        require(fixture.value("schemaVersion").toInt() == 1, "Unsupported fixture schema");
        require(QString::fromUtf8(qgetenv("TZ")) == fixture.value("timezone").toString(), "TZ must match the fixture timezone");
        const auto pages = fixture.value("pages").toArray();
        require(!pages.isEmpty(), "Fixture has no pages");
        app.setFont(QFont(fixture.value("nativeFont").toString(), 10));
        const auto config = fixture.value("config").toObject();
        const QString output = QDir(parser.value("output")).absolutePath();
        require(QDir().mkpath(output), "Cannot create output directory");
        QSet<QString> ids;
        QList<QPair<QDialog *, QJsonObject>> windows;
        for (const auto &entry : pages) {
            const auto page = entry.toObject();
            const auto id = page.value("id").toString();
            const int width = page.value("width").toInt();
            const int height = page.value("height").toInt();
            require(!ids.contains(id), "Duplicate page id");
            require(width >= 320 && width <= 4096 && height >= 320 && height <= 4096, "Invalid page viewport");
            ids.insert(id);
            QDialog *dialog = nullptr;
            if (id == "settings") dialog = new purr::SettingsDialog(config, config.value("_isActivated").toBool(), config.value("_autoLaunch").toBool());
            else if (id == "message-settings") dialog = new purr::MessageSettingsDialog(config);
            else if (id == "message-list") {
                auto *messages = new purr::MessageListDialog;
                messages->setState(fixture.value("state").toObject());
                dialog = messages;
            } else if (id == "activation") {
                const auto license = fixture.value("license").toObject();
                dialog = new purr::ActivationDialog(license, license.value("machineId").toString());
            } else if (id == "rules") dialog = new purr::RulesDialog;
            else throw std::runtime_error("Unknown visual page");
            dialog->setAttribute(Qt::WA_DeleteOnClose, false);
            dialog->resize(width, height);
            dialog->show();
            windows.append({dialog, page});
        }
        QTimer::singleShot(600, &app, [&app, windows, output, fixture, fixtureBytes] {
            int result = 0;
            try {
                QJsonArray captured;
                for (const auto &window : windows) {
                    auto *dialog = window.first;
                    const auto page = window.second;
                    const QString id = page.value("id").toString();
                    require(dialog->size() == QSize(page.value("width").toInt(), page.value("height").toInt()), "Native viewport changed unexpectedly");
                    require(qFuzzyCompare(dialog->devicePixelRatioF(), 1.0), "Capture requires device pixel ratio 1");
                    const QString filename = "native-" + id + ".png";
                    require(dialog->grab().save(QDir(output).filePath(filename)), "Cannot save native screenshot");
                    QJsonObject bindings;
                    QSet<QString> keys;
                    for (const auto &entry : page.value("bindings").toArray()) {
                        const auto binding = entry.toObject();
                        const QString key = binding.value("key").toString();
                        require(!key.isEmpty() && !keys.contains(key), "Empty or duplicate binding key");
                        keys.insert(key);
                        auto *widget = dialog->findChild<QWidget *>(binding.value("native").toString());
                        require(widget, "Missing native binding: " + key);
                        auto description = describe(widget, dialog);
                        require(description.value("value") == binding.value("expected"), "Unexpected native field value: " + key);
                        bindings.insert(key, description);
                    }
                    QJsonArray text;
                    for (auto *label : dialog->findChildren<QLabel *>())
                        if (label->isVisible() && !label->text().isEmpty()) text.append(label->text());
                    captured.append(QJsonObject{{"id", id}, {"width", dialog->width()}, {"height", dialog->height()},
                        {"image", filename}, {"bindings", bindings}, {"visibleLabels", text}});
                }
                QJsonObject manifest{{"schemaVersion", 1}, {"kind", "native"}, {"qtVersion", qVersion()},
                    {"platform", QGuiApplication::platformName()}, {"os", QSysInfo::prettyProductName()},
                    {"architecture", QSysInfo::currentCpuArchitecture()}, {"devicePixelRatio", 1},
                    {"timezone", fixture.value("timezone")}, {"requestedFont", fixture.value("nativeFont")},
                    {"fixtureSha256", QString::fromLatin1(QCryptographicHash::hash(fixtureBytes, QCryptographicHash::Sha256).toHex())},
                    {"pages", captured}};
                QSaveFile file(QDir(output).filePath("native-manifest.json"));
                require(file.open(QIODevice::WriteOnly), "Cannot write native manifest");
                require(file.write(QJsonDocument(manifest).toJson()) > 0 && file.commit(), "Cannot commit native manifest");
            } catch (const std::exception &error) { qCritical("%s", error.what()); result = 1; }
            for (const auto &window : windows) delete window.first;
            app.exit(result);
        });
        return app.exec();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
}
