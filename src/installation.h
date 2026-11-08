#pragma once
#include <QString>
namespace purr {
// Verifies the installed application and its resources without reading user
// configuration, trial marks or autostart settings. Runs in the GUI event loop.
void verifyInstallation(const QString &assetDirectory,const QString &docsDirectory,const QString &reportPath);
}
