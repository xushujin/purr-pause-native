#include "overlays.h"
#include <QApplication>
#include <QGuiApplication>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace purr;
class ScreenTests : public QObject {
    Q_OBJECT
private slots:
    void oneOverlayPerScreenWithSharedDismissal() {
        QCOMPARE(QGuiApplication::screens().size(),2);
        BreakOverlay overlay;
        QSignalSpy finished(&overlay,&BreakOverlay::dismissed);
        QObject::connect(&overlay,&BreakOverlay::dismissed,&overlay,&BreakOverlay::stop);
        overlay.start({{"animationMode","fade-center"},{"breakMinutes",1}},
            QStringLiteral(PURR_TEST_ASSETS "/webm/cat-walk.webm"),QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),0);
        QList<QWidget *> windows;
        for (auto *window:QApplication::topLevelWidgets()) if (window->objectName()==QStringLiteral("catOverlay")) windows.append(window);
        QCOMPARE(windows.size(),2);
        QSet<QScreen *> screens;
        for (auto *window:windows) {
            screens.insert(window->screen());
            QCOMPARE(window->geometry(),window->screen()->availableGeometry());
        }
        QCOMPARE(screens.size(),2);
        for (auto *window:windows) {
            auto *button=window->findChild<QPushButton *>(QStringLiteral("dismissButton")); QVERIFY(button);
            QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(),6000);
        }
        windows.last()->findChild<QPushButton *>(QStringLiteral("dismissButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,2500);
        QVERIFY(!overlay.visible());
        for (auto *window:QApplication::topLevelWidgets()) QVERIFY(window->objectName()!=QStringLiteral("catOverlay"));
    }
};
QTEST_MAIN(ScreenTests)
#include "test_screens.moc"
