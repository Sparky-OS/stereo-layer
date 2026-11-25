// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

// The JXLStereo mark read by Picture::load() and loadPair(): the property in a plain and a Brotli-compressed XML box, in
// attribute and element form, the orientation applied before the views are taken, and every declaration that is not a
// mark left as authored. Fixtures: readers/jxlstereo (generate.cpp).
#include "picture.h"

#include <QImageReader>
#include <QRgba64>
#include <QStandardPaths>
#include <QTest>

using Stereo::OutputMode;
using Stereo::Picture;

namespace
{
QString fixture(const QString &name) { return QFINDTESTDATA(QStringLiteral("readers/jxlstereo/") + name); }

QRgba64 at(const QImage &image, int x, int y) { return reinterpret_cast<const QRgba64 *>(image.constScanLine(y))[x]; }

// Every sample of a view: red on the left, blue on the right, green the frame's number (1000 for the first), 16 bits.
bool isView(const QImage &view, bool left, int green)
{
    if (view.format() != QImage::Format_RGBA64) {
        return false;
    }
    for (int y = 0; y < view.height(); ++y) {
        for (int x = 0; x < view.width(); ++x) {
            const QRgba64 sample = at(view, x, y);
            if (sample.red() != (left ? 60000 : 4000) || sample.blue() != (left ? 4000 : 60000) || sample.green() != green) {
                return false;
            }
        }
    }
    return true;
}
}

class JxlMarkTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY2(QImageReader::supportedImageFormats().contains("jxl"), "the JPEG XL image reader (kimageformat6-plugins) is needed");
    }

    void marked_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("xml box, attribute") << QStringLiteral("pair-xml-attribute.jxl");
        QTest::newRow("xml box, element") << QStringLiteral("pair-xml-element.jxl");
        QTest::newRow("brob box, attribute") << QStringLiteral("pair-brob-attribute.jxl");
        QTest::newRow("brob box, element") << QStringLiteral("pair-brob-element.jxl");
        // stored 4 x 16 with orientation 6: presented 16 x 4, red left
        QTest::newRow("orientation applied first") << QStringLiteral("pair-rotate90.jxl");
    }

    void marked()
    {
        QFETCH(QString, file);
        const Picture picture = Picture::load(fixture(file), {}, OutputMode::Unknown);
        QVERIFY(picture.isStereo());
        QCOMPARE(picture.source, Picture::Source::FileMark);
        QCOMPARE(picture.layout, Picture::Layout::SideBySide);
        QCOMPARE(picture.left.size(), QSize(8, 4));
        QVERIFY(isView(picture.left, true, 1000));
        QVERIFY(isView(picture.right, false, 1000));
        const Picture small = Picture::loadPair(fixture(file), QSize(16, 4), {});
        QVERIFY(small.isStereo());
        QCOMPARE(small.source, Picture::Source::FileMark);
    }

    void asAuthored_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("odd presented width") << QStringLiteral("pair-odd-width.jxl");
        QTest::newRow("declared twice") << QStringLiteral("pair-declared-twice.jxl");
        QTest::newRow("unfamiliar value") << QStringLiteral("pair-unfamiliar-value.jxl");
    }

    // A declaration that is not a mark leaves the picture as authored: one picture, never guessed from its shape.
    void asAuthored()
    {
        QFETCH(QString, file);
        const Picture picture = Picture::load(fixture(file), {}, OutputMode::Unknown);
        QVERIFY(!picture.isStereo());
        QCOMPARE(picture.source, Picture::Source::FileMark);
        QVERIFY(picture.left.isNull());
    }

    // The property is matched by its namespace: a layout property of another namespace says nothing.
    void otherNamespace()
    {
        const Picture picture = Picture::load(fixture(QStringLiteral("pair-other-namespace.jxl")), {}, OutputMode::Unknown);
        QVERIFY(picture.source != Picture::Source::FileMark);
        QVERIFY(!picture.isStereo());
    }

    // A 2:1 picture without the mark is never taken for a marked pair; what the layer does with an unmarked picture of a
    // pair's shape (Picture::pairByShape(), the last safety net) is its rule for every format, and stays.
    void unmarkedShape()
    {
        const Picture picture = Picture::load(fixture(QStringLiteral("unmarked-2to1.jxl")), {}, OutputMode::Unknown);
        QVERIFY(picture.source != Picture::Source::FileMark);
        QCOMPARE(picture.source, Picture::Source::Shape);
    }

    // The still of an animated file is its first composed frame, as a pair, at the file's precision.
    void animatedStill()
    {
        const Picture picture = Picture::load(fixture(QStringLiteral("animated-pair.jxl")), {}, OutputMode::Unknown);
        QVERIFY(picture.isStereo());
        QCOMPARE(picture.source, Picture::Source::FileMark);
        QVERIFY(isView(picture.left, true, 1000));
        QVERIFY(isView(picture.right, false, 1000));
    }
};

QTEST_GUILESS_MAIN(JxlMarkTest)
#include "jxlmarktest.moc"
