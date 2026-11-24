// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

// AnimatedPicture: one clock, both views of a pair from one frame, the file's frames, delays and loop count, 16-bit
// frames handed on as 16-bit, and a reason instead of a picture when a frame cannot be produced. Fixtures:
// readers/jxlstereo (generate.cpp).
#include "animatedpicture.h"

#include <QImageReader>
#include <QRgba64>
#include <QStandardPaths>
#include <QTest>

using Stereo::AnimatedPicture;
using Stereo::Picture;

namespace
{
QString fixture(const QString &name) { return QFINDTESTDATA(QStringLiteral("readers/jxlstereo/") + name); }

QRgba64 at(const QImage &image, int x, int y)
{
    return image.format() == QImage::Format_RGBA64 ? reinterpret_cast<const QRgba64 *>(image.constScanLine(y))[x]
                                                   : QRgba64::fromArgb32(image.pixel(x, y));
}

// The green sample both views of the fixtures carry: 1000 for frame 0, 2000 for frame 1, ...; -1 when the view is not
// all one frame, or not red (left) or blue (right).
int frameGreen(const QImage &view, bool left)
{
    int green = -1;
    for (int y = 0; y < view.height(); ++y) {
        for (int x = 0; x < view.width(); ++x) {
            const QRgba64 sample = at(view, x, y);
            if ((left ? sample.red() : sample.blue()) != 60000 || (green >= 0 && sample.green() != green)) {
                return -1;
            }
            green = sample.green();
        }
    }
    return green;
}

// The first column of a view where the drawing's marker (red on the left, blue on the right) is, or -1.
int marker(const QImage &view, bool left)
{
    for (int x = 0; x < view.width(); ++x) {
        const QColor colour = view.pixelColor(x, view.height() / 2);
        if ((left ? colour.red() : colour.blue()) > 128 && colour.alpha() > 128) {
            return x;
        }
    }
    return -1;
}
}

class AnimatedPictureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(QImageReader::supportedImageFormats().contains("jxl"), "the JPEG XL image reader (kimageformat6-plugins) is needed");
    }

    void jpegXlDeclares()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair.jxl")));
        QVERIFY(!animation.isNull());
        QVERIFY(animation.isStereo());
        QVERIFY(animation.reason().isEmpty());
        QCOMPARE(animation.frameCount(), 3);
        QCOMPARE(animation.delays(), QList<int>({100, 200, 300}));
        // two plays (JPEG XL num_loops 2): one more after the first
        QCOMPARE(animation.loopCount(), 1);
        QCOMPARE(animation.duration(), qint64(600));
    }

    void jpegXlClock_data()
    {
        QTest::addColumn<qint64>("time");
        QTest::addColumn<int>("frame");
        QTest::newRow("start") << qint64(0) << 0;
        QTest::newRow("end of the first frame") << qint64(99) << 0;
        QTest::newRow("second frame") << qint64(100) << 1;
        QTest::newRow("end of the second frame") << qint64(299) << 1;
        QTest::newRow("third frame") << qint64(300) << 2;
        QTest::newRow("second play") << qint64(650) << 0;
        QTest::newRow("second play, third frame") << qint64(1199) << 2;
        QTest::newRow("after the last play") << qint64(1200) << 2;
        QTest::newRow("long after") << qint64(50000) << 2;
    }

    void frameDeadlines()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair.jxl")));
        QCOMPARE(animation.nextFrameTime(0), 100);
        QCOMPARE(animation.nextFrameTime(99), 100);
        QCOMPARE(animation.nextFrameTime(100), 300);
        QCOMPARE(animation.nextFrameTime(300), 600);
        QCOMPARE(animation.nextFrameTime(600), 700);
        QCOMPARE(animation.nextFrameTime(1199), -1);
        QCOMPARE(animation.nextFrameTime(1200), -1);
    }

    // Both views from the one frame the clock names, at the file's 16 bits.
    void jpegXlClock()
    {
        QFETCH(qint64, time);
        QFETCH(int, frame);
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair.jxl")));
        QCOMPARE(animation.frameAt(time), frame);
        const Picture pair = animation.pairAt(time);
        QVERIFY(pair.isStereo());
        QCOMPARE(pair.left.format(), QImage::Format_RGBA64);
        QCOMPARE(pair.right.format(), QImage::Format_RGBA64);
        QCOMPARE(pair.left.size(), QSize(8, 4));
        QCOMPARE(frameGreen(pair.left, true), 1000 * (frame + 1));
        QCOMPARE(frameGreen(pair.right, false), 1000 * (frame + 1));
    }

    // Premultiplication and packing preserve the 16-bit samples of both views.
    void jpegXlPackedPrecision()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair.jxl")));
        const Picture pair = animation.pairAt(100);
        const QImage sbs = pair.sideBySide(QImage::Format_RGBA64_Premultiplied);
        QCOMPARE(sbs.format(), QImage::Format_RGBA64_Premultiplied);
        QCOMPARE(sbs.size(), QSize(2 * pair.left.width(), pair.left.height()));
        QCOMPARE(sbs.copy(0, 0, pair.left.width(), pair.left.height()), pair.left.convertToFormat(sbs.format()));
        QCOMPARE(sbs.copy(pair.left.width(), 0, pair.right.width(), pair.right.height()), pair.right.convertToFormat(sbs.format()));
    }

    // A frame that cannot be decoded gives no picture and a reason; the frames before it are still the right ones.
    void truncated()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair-truncated.jxl")));
        const Picture late = animation.pairAt(400);
        QVERIFY(late.left.isNull() && late.right.isNull() && late.original.isNull());
        QVERIFY(!animation.reason().isEmpty());
        for (qint64 time = 0; time < 300; time += 50) {
            const Picture pair = animation.pairAt(time);
            if (!pair.left.isNull()) {
                QCOMPARE(frameGreen(pair.left, true), 1000 * (animation.frameAt(time) + 1));
                QCOMPARE(frameGreen(pair.right, false), 1000 * (animation.frameAt(time) + 1));
            }
        }
    }

    // One document time for both views (SVGStereo 0.4): the marker stands at the same place in each view, and moves.
    void drawing()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-pair.svg")));
        QVERIFY(!animation.isNull());
        QVERIFY(animation.isStereo());
        QCOMPARE(animation.frameCount(), 0);
        QCOMPARE(animation.duration(), qint64(300));
        QSet<int> places;
        for (qint64 time : {0, 50, 150, 250, 350, 1234}) {
            const Picture pair = animation.pairAt(time, QSize(16, 8));
            QVERIFY(pair.isStereo());
            QCOMPARE(pair.left.size(), QSize(16, 8));
            const int left = marker(pair.left, true);
            QVERIFY(left >= 0);
            QCOMPARE(marker(pair.right, false), left);
            places.insert(left);
        }
        QVERIFY(places.size() >= 2);
    }

    void unmarkedDrawing()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-unmarked.svg")));
        QVERIFY(!animation.isNull());
        QVERIFY(!animation.isStereo());
        const Picture shown = animation.pairAt(50);
        QVERIFY(!shown.isStereo());
        QCOMPARE(shown.original.size(), QSize(32, 8));
    }

    // An unmarked timed AVIF of 4:1 plays as one picture, 12-bit samples handed on in 16 bits.
    void unmarkedSequence()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-sequence.avif")));
        QVERIFY(!animation.isNull());
        QVERIFY(!animation.isStereo());
        QCOMPARE(animation.frameCount(), 3);
        const Picture shown = animation.pairAt(150);
        QVERIFY(!shown.isStereo());
        QCOMPARE(shown.original.size(), QSize(64, 16));
        QCOMPARE(shown.original.format(), QImage::Format_RGBA64);
    }

    // The ISOBMFF stereo video scheme in an AVIF sequence: its reader (libavif 1.4.2) gives each frame one view wide, so
    // the file says why and gives no picture, never a half-width one.
    void stereoVideoTrack()
    {
        AnimatedPicture animation(fixture(QStringLiteral("animated-stvi.avif")));
        QVERIFY(animation.isNull());
        QVERIFY(!animation.reason().isEmpty());
        const Picture shown = animation.pairAt(0);
        QVERIFY(shown.left.isNull() && shown.right.isNull() && shown.original.isNull());
        QVERIFY(animation.frame(0).original.isNull());
    }
};

QTEST_MAIN(AnimatedPictureTest)
#include "animatedpicturetest.moc"
