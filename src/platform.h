#pragma once

#include <QString>
#include <QtGlobal>
#include <functional>

class QObject;
class QWidget;

namespace purr::platform {
qint64 idleMilliseconds();
bool sessionLocked();
bool setAutostart(bool enabled, QString *error = nullptr);
bool autostartEnabled();
void prepareOverlay(QWidget *widget);
bool supportsGlobalPositioning();
void watchSleep(QObject *context, std::function<void(bool sleeping)> callback);
void watchLock(QObject *context, std::function<void(bool locked)> callback);
} // namespace purr::platform
