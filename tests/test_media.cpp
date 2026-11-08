#include "media.h"
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryFile>

using namespace purr;
class MediaTests : public QObject {
    Q_OBJECT
private slots:
    void decodesOriginalAlpha() {
        VideoPlayer player;
        QSignalSpy frames(&player, &VideoPlayer::frameAvailable);
        QSignalSpy errors(&player, &VideoPlayer::failed);
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"), true);
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 2 || !errors.isEmpty(), 15000);
        QVERIFY2(errors.isEmpty(), "Original transparent video must decode with libvpx-vp9");
        const QImage image = player.frame(); QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        int transparent = 0, opaque = 0;
        for (int y = 0; y < image.height(); y += 8) for (int x = 0; x < image.width(); x += 8) {
            const int alpha = image.pixelColor(x, y).alpha();
            if (alpha < 10) ++transparent;
            if (alpha > 245) ++opaque;
        }
        QVERIFY2(transparent > 100, "Black backgrounds indicate missing WebM alpha decoding");
        QVERIFY(opaque > 100);
        QVERIFY(player.duration() > 3 && player.duration() < 4);
        QElapsedTimer shutdown; shutdown.start(); player.stop();
        QVERIFY(shutdown.elapsed() < 2000);
    }
    void loopsAndRestarts() {
        VideoPlayer player;
        QSignalSpy done(&player, &VideoPlayer::playbackFinished);
        QSignalSpy errors(&player, &VideoPlayer::failed);
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"), true);
        QTest::qWait(3700);
        QVERIFY(player.isRunning()); QVERIFY(done.isEmpty()); QVERIFY(errors.isEmpty());
        player.stop();
        QElapsedTimer duration; duration.start();
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"), false);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 15000);
        QVERIFY2(duration.elapsed()>=3150,"A 3.2-second video must retain its final frame before ending");
        QVERIFY(!player.frame().isNull());
    }
    void corruptMediaFailsSafely() {
        QTemporaryFile file; QVERIFY(file.open()); file.write("not a webm video"); file.flush();
        VideoPlayer player; QSignalSpy errors(&player, &VideoPlayer::failed);
        player.play(file.fileName(), false);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 5000);
        player.stop(); QVERIFY(player.frame().isNull());
    }
    void smallCustomMediaKeepsItsResolutionAndTiming() {
        QTemporaryFile portrait; QVERIFY(portrait.open());
        const auto bytes=QByteArray::fromBase64("GkXfo59ChoEBQveBAULygQRC84EIQoKEd2VibUKHgQJChYECGFOAZwEAAAAAAAIMEU2bdLpNu4tTq4QVSalmU6yBoU27i1OrhBZUrmtTrIHWTbuMU6uEElTDZ1OsggEjTbuMU6uEHFO7a1OsggH27AEAAAAAAABZAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAVSalmsCrXsYMPQkBNgIxMYXZmNjIuMy4xMDBXQYxMYXZmNjIuMy4xMDBEiYhAaQAAAAAAABZUrmvIrgEAAAAAAAA/14EBc8WI7vyGpc8WqtScgQAitZyDdW5kiIEAhoVWX1ZQOYOBASPjg4QF9eEA4JCwgSC6gUCagQJVsIRVuYEBElTDZ0B/c3OfY8CAZ8iZRaOHRU5DT0RFUkSHjExhdmY2Mi4zLjEwMHNz2mPAi2PFiO78hqXPFqrUZ8ilRaOHRU5DT0RFUkSHmExhdmM2Mi4xMS4xMDAgbGlidnB4LXZwOWfIoUWjiERVUkFUSU9ORIeTMDA6MDA6MDAuMjAwMDAwMDAwAB9DtnXJ54EAo7CBAACAgkmDQgAB8AP2ADgkHBgAAAAgAABpHP//0SAGP////wXNHD////1GNDHz9ACjkoEAZACGAECSnABAAAACAAALdBxTu2uRu4+zgQC3iveBAfGCAajwgQM=");
        QCOMPARE(portrait.write(bytes),qint64(bytes.size())); portrait.flush();
        VideoPlayer player;
        QSignalSpy ready(&player,&VideoPlayer::playbackReady);
        QSignalSpy done(&player,&VideoPlayer::playbackFinished);
        QSignalSpy errors(&player,&VideoPlayer::failed);
        QElapsedTimer elapsed; elapsed.start();
        player.play(portrait.fileName(),false);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,3000);
        QCOMPARE(ready.count(),1);
        QCOMPARE(ready.first().at(0).toSize(),QSize(32,64));
        QCOMPARE(player.frame().size(),QSize(32,64));
        QVERIFY(elapsed.elapsed()>=190);
        QVERIFY(errors.isEmpty());
    }
    void immediateStopDiscardsCallbacks() {
        VideoPlayer player;
        QSignalSpy errors(&player,&VideoPlayer::failed);
        QSignalSpy ready(&player,&VideoPlayer::playbackReady);
        for(int index=0;index<20;++index) {
            player.play(QStringLiteral("/missing/video.webm"),false);
            player.stop();
            QCoreApplication::processEvents();
        }
        QVERIFY(errors.isEmpty()); QVERIFY(ready.isEmpty());
    }
    void customHighResolutionMediaIsPreserved() {
        const auto file=QFINDTESTDATA("fixtures/wide-vp9.webm"); QVERIFY(!file.isEmpty());
        VideoPlayer player;
        QSignalSpy done(&player,&VideoPlayer::playbackFinished);
        player.play(file,false);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,3000);
        QCOMPARE(player.frame().size(),QSize(2048,32));
    }
    void staleEventsCannotAffectNewPlayback() {
        VideoPlayer player;
        QSignalSpy done(&player,&VideoPlayer::playbackFinished);
        QSignalSpy errors(&player,&VideoPlayer::failed);
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),false);
        // Wait without processing GUI events: old completion remains queued.
        QVERIFY(player.wait(15000));
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),true);
        QTest::qWait(100);
        QVERIFY(done.isEmpty());
        player.play(QStringLiteral("/missing/purr-video.webm"),false);
        QVERIFY(player.wait(5000));
        player.play(QStringLiteral(PURR_TEST_ASSETS "/webm/cat-rest.webm"),true);
        QTest::qWait(100);
        QVERIFY(errors.isEmpty()); QVERIFY(player.isRunning()); player.stop();
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "test_media.moc"
