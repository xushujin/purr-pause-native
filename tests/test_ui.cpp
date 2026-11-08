#include "dialogs.h"
#include "overlays.h"
#include <QApplication>
#include <QCheckBox>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTemporaryFile>
#include <cmath>

using namespace purr;
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void originalBrowserCatGeometry() {
        // Recorded from the unmodified index.html in Electron 33.2.1 at
        // 1280x800 with controlled requestAnimationFrame and media metadata.
        // Browser layout rounds to 1/64px; the native animation remains subpixel.
        struct Sample { const char *mode; double aspect,progress,x,y,width,height,opacity; };
        const Sample samples[]{
            {"walk-center",1.77777777778,0,631.119140625,395,17.77734375,10,0},
            {"walk-center",1.77777777778,0.25,303.266021729,210.578125,673.483642578,378.84375,1},
            {"walk-center",1.77777777778,1,64.0218505859,76,1151.97192383,648,1},
            {"walk-center",1,0,635,395,10,10,0},
            {"walk-center",1,0.25,441.328125,201.328125,397.34375,397.34375,1},
            {"walk-center",1,1,300,60,680,680,1},
            {"walk-center",0.5,0,637.5,395,5,10,0},
            {"walk-center",0.5,0.25,540.6640625,201.328125,198.671875,397.34375,1},
            {"walk-center",0.5,1,470,60,340,680,1},
            {"walk-center",3,0,625,395,30,10,0},
            {"walk-center",3,0.25,300.671875,286.890625,678.65625,226.21875,1},
            {"walk-center",3,1,64,208,1152,384,1},
            {"walk-zoom",1.77777777778,0,1251.1171875,612,142.21875,80,1},
            {"walk-zoom",1.77777777778,0.25,564.829223633,302.125,725.982299805,408.375,1},
            {"walk-zoom",1.77777777778,1,64.0218505859,76,1151.97192383,648,1},
            {"walk-zoom",1,0,1360,612,80,80,1},
            {"walk-zoom",1,0.25,747.1875,292.875,426.875,426.875,1},
            {"walk-zoom",1,1,300,60,680,680,1},
            {"walk-zoom",0.5,0,1430,612,40,80,1},
            {"walk-zoom",0.5,0.25,875,292.875,213.4375,426.875,1},
            {"walk-zoom",0.5,1,470,60,340,680,1},
            {"walk-zoom",3,0,1080,612,240,80,1},
            {"walk-zoom",3,0.25,492.625030518,378.4375,767.25,255.75,1},
            {"walk-zoom",3,1,64,208,1152,384,1},
            {"walk-flat",1.77777777778,0,746.240600586,76,1151.97192383,648,1},
            {"walk-flat",1.77777777778,0.25,351.834350586,76,1151.97192383,648,1},
            {"walk-flat",1.77777777778,1,64.0218505859,76,1151.97192383,648,1},
            {"walk-flat",1,0,1060,60,680,680,1},
            {"walk-flat",1,0.25,620.625,60,680,680,1},
            {"walk-flat",1,1,300,60,680,680,1},
            {"walk-flat",0.5,0,1280,60,340,680,1},
            {"walk-flat",0.5,0.25,811.71875,60,340,680,1},
            {"walk-flat",0.5,1,470,60,340,680,1},
            {"walk-flat",3,0,624,208,1152,384,1},
            {"walk-flat",3,0.25,300.25,208,1152,384,1},
            {"walk-flat",3,1,64,208,1152,384,1},
            {"fade-center",1.77777777778,0,611.200012207,383.799987793,57.5999755859,32.3999938965,0},
            {"fade-center",1.77777777778,0.25,294.850006104,205.853134155,690.300048828,388.293701172,1},
            {"fade-center",1.77777777778,1,64,76,1152,648,1},
            {"fade-center",1,0,623,383,34,34,0},
            {"fade-center",1,0.25,436.265625,196.265640259,407.46875,407.46875,1},
            {"fade-center",1,1,300,60,680,680,1},
            {"fade-center",0.5,0,631.5,383,17,34,0},
            {"fade-center",0.5,0.25,538.1328125,196.265640259,203.734375,407.46875,1},
            {"fade-center",0.5,1,470,60,340,680,1},
            {"fade-center",3,0,611.200012207,390.399993896,57.5999755859,19.200012207,0},
            {"fade-center",3,0.25,294.850006104,284.950012207,690.300048828,230.099975586,1},
            {"fade-center",3,1,64,208,1152,384,1},
        };
        for (const auto &sample:samples) {
            const auto pose=animation::catPose(QString::fromLatin1(sample.mode),QSizeF(1280,800),sample.aspect,sample.progress);
            QVERIFY2(std::abs(pose.bounds.x()-sample.x)<.1,sample.mode);
            QVERIFY2(std::abs(pose.bounds.y()-sample.y)<.1,sample.mode);
            QVERIFY2(std::abs(pose.bounds.width()-sample.width)<.1,sample.mode);
            QVERIFY2(std::abs(pose.bounds.height()-sample.height)<.1,sample.mode);
            QVERIFY(std::abs(pose.opacity-sample.opacity)<.0001);
        }
    }
    void originalBrowserRocketGeometry() {
        // Electron 33.2.1 CSS animations paused in original rocket-demo.html,
        // with startY=380, endY=0 and the original 7000ms duration.
        struct Sample { double time,y,scale,opacity,boost; };
        const Sample samples[]{
            {0,392.0,0.66,0.0,0.92},
            {120,383.25309,0.689156,0.728909,0.935763},
            {560,380.0,0.7,1.0,0.987956},
            {1000,246.124,0.7,1.0,1.01384},
            {3640,9.98599999999999,0.7,1.0,1.035},
            {5740,0.0,0.7,1.0,0.990959},
            {6500,-15.603000000000009,0.680496,0.0247935,0.990107},
            {7000,-16.0,0.68,0.0,0.99},
        };
        for(const auto &sample:samples) {
            const auto pose=animation::rocketPose(sample.time,380);
            QVERIFY(std::abs(pose.y-sample.y)<.001);
            QVERIFY(std::abs(pose.scale-sample.scale)<.00001);
            QVERIFY(std::abs(pose.opacity-sample.opacity)<.00001);
            QVERIFY(std::abs(pose.boost-sample.boost)<.00001);
        }
        QCOMPARE(animation::cubicBezier(0,.25,.1,.25,1),0.0);
        QCOMPARE(animation::cubicBezier(1,.25,.1,.25,1),1.0);
        // CSS 'ease' is already about 80% opaque halfway through a fade.
        QVERIFY(animation::cubicBezier(.5,.25,.1,.25,1)>.8);
    }
    void nativeSettingsSave() {
        SettingsDialog dialog({{QStringLiteral("thresholdMinutes"),45},{QStringLiteral("breakMinutes"),5},{QStringLiteral("animationMode"),QStringLiteral("walk-center")}},false,false);
        dialog.setAttribute(Qt::WA_DeleteOnClose,false);
        QSignalSpy saved(&dialog,&SettingsDialog::saved);
        dialog.show();
        const auto spins = dialog.findChildren<QSpinBox *>(); QVERIFY(spins.size() >= 2);
        for (auto *spin:spins) if (spin->maximum()==480) spin->setValue(25);
        QPushButton *save=nullptr;
        for (auto *button:dialog.findChildren<QPushButton *>()) if (button->text()==QStringLiteral("保存")) save=button;
        QVERIFY(save); QTest::mouseClick(save,Qt::LeftButton); QCOMPARE(saved.size(),1);
        QCOMPARE(saved[0][0].toJsonObject().value(QStringLiteral("thresholdMinutes")).toInt(),25);
    }
    void plainTextMessageContent() {
        MessageListDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose,false);
        const QString hostile=QStringLiteral("<img src='https://example.invalid/track'>");
        const QJsonObject origin{{QStringLiteral("id"),QStringLiteral("source")},{QStringLiteral("name"),QStringLiteral("测试源")},{QStringLiteral("color"),QStringLiteral("#ff8c42")}};
        QJsonObject item{{QStringLiteral("id"),QStringLiteral("a")},{QStringLiteral("title"),hostile},{QStringLiteral("targetUrl"),QStringLiteral("https://example.com/")},{QStringLiteral("origin"),origin}};
        dialog.setState({{QStringLiteral("enabled"),true},{QStringLiteral("configured"),true},{QStringLiteral("badgeCount"),1},{QStringLiteral("items"),QJsonArray{item}}}); dialog.show();
        bool found=false;
        for (auto *label:dialog.findChildren<QLabel *>()) if (label->text().contains(hostile)) { QCOMPARE(label->textFormat(),Qt::PlainText); found=true; }
        QVERIFY(found);
    }
    void transparentOverlaySnooze() {
        BreakOverlay overlay;
        QSignalSpy snoozed(&overlay,&BreakOverlay::snoozed);
        QSignalSpy errors(&overlay,&BreakOverlay::mediaError);
        overlay.start({{QStringLiteral("animationMode"),QStringLiteral("fade-center")},{QStringLiteral("breakMinutes"),1}},QStringLiteral(PURR_TEST_ASSETS "/webm/cat-walk.webm"),QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),1);
        QVERIFY(overlay.visible());
        QPushButton *button=nullptr;
        for (auto *window:QApplication::topLevelWidgets()) if (window->objectName()==QStringLiteral("catOverlay")) {
            QVERIFY(window->testAttribute(Qt::WA_TranslucentBackground));
            button=window->findChild<QPushButton *>(QStringLiteral("snoozeButton"));
        }
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(),10000);
        QVERIFY(errors.isEmpty());
        QVERIFY(button->window()->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QTRY_VERIFY_WITH_TIMEOUT(!button->window()->windowFlags().testFlag(Qt::WindowTransparentForInput),2000);
        QTest::mouseClick(button,Qt::LeftButton); QCOMPARE(snoozed.count(),1);
        overlay.stop(); QVERIFY(!overlay.visible());
    }
    void missingFadeMediaKeepsOriginalFallbackDelay() {
        BreakOverlay overlay;
        QSignalSpy errors(&overlay,&BreakOverlay::mediaError);
        overlay.start({{QStringLiteral("animationMode"),QStringLiteral("fade-center")},{QStringLiteral("breakMinutes"),1}},QString(),QStringLiteral("/missing/rest.webm"),0);
        QPushButton *button=nullptr;
        for(auto *window:QApplication::topLevelWidgets()) if(window->objectName()==QStringLiteral("catOverlay")) button=window->findChild<QPushButton *>(QStringLiteral("dismissButton"));
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),3000);
        QTest::qWait(4000);
        QVERIFY(!button->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(),4000);
        overlay.stop();
    }
    void customIdleAspectIsContained() {
        // A two-frame, 32x64 VP9 fixture, independent of the shipped 16:9 cat.
        QTemporaryFile portrait;
        QVERIFY(portrait.open());
        const auto bytes=QByteArray::fromBase64("GkXfo59ChoEBQveBAULygQRC84EIQoKEd2VibUKHgQJChYECGFOAZwEAAAAAAAIMEU2bdLpNu4tTq4QVSalmU6yBoU27i1OrhBZUrmtTrIHWTbuMU6uEElTDZ1OsggEjTbuMU6uEHFO7a1OsggH27AEAAAAAAABZAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAVSalmsCrXsYMPQkBNgIxMYXZmNjIuMy4xMDBXQYxMYXZmNjIuMy4xMDBEiYhAaQAAAAAAABZUrmvIrgEAAAAAAAA/14EBc8WI7vyGpc8WqtScgQAitZyDdW5kiIEAhoVWX1ZQOYOBASPjg4QF9eEA4JCwgSC6gUCagQJVsIRVuYEBElTDZ0B/c3OfY8CAZ8iZRaOHRU5DT0RFUkSHjExhdmY2Mi4zLjEwMHNz2mPAi2PFiO78hqXPFqrUZ8ilRaOHRU5DT0RFUkSHmExhdmM2Mi4xMS4xMDAgbGlidnB4LXZwOWfIoUWjiERVUkFUSU9ORIeTMDA6MDA6MDAuMjAwMDAwMDAwAB9DtnXJ54EAo7CBAACAgkmDQgAB8AP2ADgkHBgAAAAgAABpHP//0SAGP////wXNHD////1GNDHz9ACjkoEAZACGAECSnABAAAACAAALdBxTu2uRu4+zgQC3iveBAfGCAajwgQM=");
        QCOMPARE(portrait.write(bytes),qint64(bytes.size())); portrait.flush();
        BreakOverlay overlay;
        QSignalSpy errors(&overlay,&BreakOverlay::mediaError);
        overlay.start({{QStringLiteral("animationMode"),QStringLiteral("walk-center")},{QStringLiteral("breakMinutes"),1}},QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),portrait.fileName(),0);
        QWidget *window=nullptr;
        for(auto *candidate:QApplication::topLevelWidgets()) if(candidate->objectName()==QStringLiteral("catOverlay")) window=candidate;
        QVERIFY(window);
        const auto centerIsGreen=[window] { const auto color=window->grab().toImage().pixelColor(window->width()/2,window->height()/2); return color.green()>220 && color.red()<30 && color.blue()<30; };
        QTRY_VERIFY_WITH_TIMEOUT(centerIsGreen(),6500);
        const auto image=window->grab().toImage();
        QCOMPARE(image.pixelColor(window->width()/5,window->height()/2).alpha(),0);
        QVERIFY(errors.isEmpty()); overlay.stop();
    }
    void animationModesFinish_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"walk-center","walk-zoom","walk-flat","fade-center"}) QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void animationModesFinish() {
        QFETCH(QString,mode);
        BreakOverlay overlay;
        QSignalSpy dismissed(&overlay,&BreakOverlay::dismissed);
        QSignalSpy errors(&overlay,&BreakOverlay::mediaError);
        connect(&overlay,&BreakOverlay::dismissed,&overlay,&BreakOverlay::stop);
        // A zero-minute injected duration exercises automatic completion and fade
        // without spending four real minutes; user settings enforce >=1 minute.
        overlay.start({{QStringLiteral("animationMode"),mode},{QStringLiteral("breakMinutes"),0}},QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),2);
        QPushButton *snooze=nullptr;
        for (auto *window:QApplication::topLevelWidgets()) if (window->objectName()==QStringLiteral("catOverlay")) snooze=window->findChild<QPushButton *>(QStringLiteral("snoozeButton"));
        QVERIFY(snooze); QVERIFY(!snooze->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(),1,15000);
        QVERIFY(errors.isEmpty()); QVERIFY(!overlay.visible());
    }
};
QTEST_MAIN(UiTests)
#include "test_ui.moc"
