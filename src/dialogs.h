#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QUrl>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QHBoxLayout;
class QTimer;

namespace purr {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(QJsonObject config, bool activated, bool autostart, QWidget *parent = nullptr);
    void showSaveResult(bool ok, QString error = {});
signals:
    void saved(QJsonObject configuration);
private:
    QLabel *saveStatus_ = nullptr;
    QTimer *saveStatusTimer_ = nullptr;
};

class ActivationDialog final : public QDialog {
    Q_OBJECT
public:
    ActivationDialog(QJsonObject status, QString machineId, QWidget *parent = nullptr);
    void setStatus(QJsonObject status);
    void showError(QString error);
    void showSuccess(QJsonObject status);
signals:
    void activationRequested(QString serial);
private:
    QLabel *status_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *later_ = nullptr;
};

class SourceEditor;

class MessageSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    MessageSettingsDialog(QJsonObject config, QWidget *parent = nullptr);
    void connectionResult(QString id, bool ok, QString message, int count);
    void showSaveResult(bool ok, QString error = {});
signals:
    void saved(QJsonObject configuration);
    void testRequested(QJsonObject source);
    void exportRequested();
private:
    void addSource(QJsonObject source = {});
    void finishConnectionTest();
    QVBoxLayout *sourcesLayout_ = nullptr;
    QList<SourceEditor *> sources_;
    SourceEditor *activeTest_ = nullptr;
    QLabel *sourceCount_ = nullptr;
    QLabel *emptySources_ = nullptr;
    QTimer *connectionTimer_ = nullptr;
    QLabel *saveStatus_ = nullptr;
    QTimer *saveStatusTimer_ = nullptr;
};

class MessageListDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MessageListDialog(QWidget *parent = nullptr);
    void setState(QJsonObject state);
signals:
    void refreshRequested();
    void settingsRequested();
    void openRequested(QString sourceId, QString id, QUrl url);
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    void render();
    QJsonObject state_;
    QString filter_;
    QLabel *count_ = nullptr;
    QLabel *meta_ = nullptr;
    QLabel *messageError_ = nullptr;
    QLabel *notice_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QHBoxLayout *sources_ = nullptr;
    QListWidget *items_ = nullptr;
};

class RulesDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RulesDialog(QWidget *parent = nullptr);
};

} // namespace purr
