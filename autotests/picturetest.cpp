// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include "render.h"
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>
using namespace Stereo;
class PictureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void marksAndChoices();
    void padding();
    void outputPolicy();
    void viewControls();
    void detector_data();
    void detector();
    void geometry();
    void panorama();
    void readerIntegration_data();
    void readerIntegration();
};
static QImage packed()
{
    QImage image(16, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int y = 0; y < 8; ++y)
        for (int x = 8; x < 16; ++x)
            image.setPixelColor(x, y, Qt::blue);
    return image;
}
void PictureTest::marksAndChoices()
{
    const QImage image = packed();
    Picture::Options options;
    options.inputLayout = Picture::Layout::TopAndBottom;
    auto marked = Picture::fromImages(image, {}, QStringLiteral("sideBySideLeftFirst"), "png", {}, options, OutputMode::Stereo);
    QCOMPARE(marked.source, Picture::Source::FileMark);
    QCOMPARE(marked.left.size(), QSize(8, 8));
    QCOMPARE(marked.left.pixelColor(0, 0), QColor(Qt::red));
    const auto user = Picture::fromImages(image, {}, QStringLiteral("sideBySideRightFirst"), "jps", {}, options, OutputMode::Stereo);
    QCOMPARE(user.source, Picture::Source::User);
    QCOMPARE(user.left.size(), QSize(16, 4));
    options = {};
    const auto ratio = Picture::fromImages(image, {}, {}, "png", {}, options, OutputMode::Stereo);
    QCOMPARE(ratio.source, Picture::Source::Shape);
    QCOMPARE(ratio.right.pixelColor(0, 0), QColor(Qt::blue));
    const auto mono = Picture::fromImages(image, {}, QStringLiteral("mono"), "jpg", {}, options, OutputMode::Stereo);
    QVERIFY(!mono.isStereo());
}
void PictureTest::padding()
{
    QImage image(13, 5, QImage::Format_RGB32);
    image.fill(Qt::green);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            image.setPixelColor(x, y, Qt::red);
            image.setPixelColor(x + 8, y, Qt::blue);
        }
    const auto picture = Picture::fromImages(image, {}, QStringLiteral("sideBySideLeftFirst"), "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(picture.left.size(), QSize(5, 5));
    QCOMPARE(picture.right.size(), QSize(5, 5));
    QCOMPARE(picture.left.pixelColor(4, 4), QColor(Qt::red));
    QCOMPARE(picture.right.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(picture.original, image);
}
void PictureTest::outputPolicy()
{
    const QImage image = packed();
    Picture::Options options;
    options.anaglyph = Picture::AnaglyphColorPair::RedCyan;
    for (auto output : { OutputMode::Anaglyph, OutputMode::Mono, OutputMode::Unknown }) {
        const auto picture = Picture::fromImages(image, {}, {}, "png", {}, options, output);
        QVERIFY(!picture.isStereo());
        QVERIFY(!picture.approximateColor);
        QCOMPARE(picture.original, image);
        QCOMPARE(picture.anaglyph, options.anaglyph);
    }
    const auto stereo = Picture::fromImages(image, {}, {}, "png", {}, options, OutputMode::Stereo);
    QVERIFY(stereo.isStereo());
    QVERIFY(stereo.approximateColor);
    QCOMPARE(stereo.original, image);
    QVERIFY(Picture::colorDisclaimer().contains(QStringLiteral("approximate")));
    QVERIFY(Picture::colorDisclaimer().contains(QStringLiteral("never changed")));
}
void PictureTest::viewControls()
{
    Picture::Options options;
    options.swapEyes = true;
    options.view = Picture::View::LeftOnly;
    const auto left = Picture::fromImages(packed(), {}, {}, "png", {}, options, OutputMode::Stereo);
    QVERIFY(!left.isStereo());
    QCOMPARE(left.original.pixelColor(0, 0), QColor(Qt::blue));
    options.view = Picture::View::Mono;
    QCOMPARE(Picture::fromImages(packed(), {}, {}, "png", {}, options, OutputMode::Stereo).original, packed());
}

void PictureTest::detector_data()
{
    QTest::addColumn<int>("pair");
    QTest::newRow("red-cyan") << int(Picture::AnaglyphColorPair::RedCyan);
    QTest::newRow("green-magenta") << int(Picture::AnaglyphColorPair::GreenMagenta);
    QTest::newRow("amber-blue") << int(Picture::AnaglyphColorPair::AmberBlue);
}
void PictureTest::detector()
{
    QFETCH(int, pair);
    const auto colorPair = Picture::AnaglyphColorPair(pair);
    QImage image(32, 16, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const int left = (x * 9 + y * 3) % 220;
            const int right = ((x + 3) * 9 + y * 3) % 220;
            image.setPixelColor(x, y,
                colorPair == Picture::AnaglyphColorPair::RedCyan            ? QColor(left, right, right)
                    : colorPair == Picture::AnaglyphColorPair::GreenMagenta ? QColor(right, left, right)
                                                                            : QColor(left, left, right));
        }
    const auto direct = Picture::fromAnaglyph(image, colorPair);
    QCOMPARE(direct.original, image);
    QCOMPARE(direct.anaglyph, colorPair);
    QVERIFY(direct.approximateColor);
    QCOMPARE(Picture::detectAnaglyph(image), colorPair);
    QCOMPARE(Picture::detectAnaglyph(image.scaled(1024, 512)), colorPair);
    const auto stereo = Picture::fromImages(image, {}, {}, {}, {}, {}, OutputMode::Stereo);
    QVERIFY(stereo.isStereo());
    QVERIFY(stereo.approximateColor);
    const auto anaglyph = Picture::fromImages(image, {}, {}, {}, {}, {}, OutputMode::Anaglyph);
    QVERIFY(!anaglyph.isStereo());
    QCOMPARE(anaglyph.original, image);
    image.fill(Qt::gray);
    QCOMPARE(Picture::detectAnaglyph(image), Picture::AnaglyphColorPair::None);
}
void PictureTest::geometry()
{
    Picture picture;
    picture.left = QImage(20, 10, QImage::Format_RGB32);
    picture.left.fill(Qt::red);
    picture.left.setPixelColor(8, 4, Qt::white);
    picture.right = picture.left;
    picture.layout = Picture::Layout::SideBySide;
    for (qreal zoom : { qreal(1), qreal(2) }) {
        const QImage frame = renderEyes(picture, QSize(20, 10), zoom, QPointF(2, -1));
        QCOMPARE(frame.size(), QSize(40, 10));
        QCOMPARE(frame.copy(0, 0, 20, 10), frame.copy(20, 0, 20, 10));
        // With zoom 1 the white marker at (8,4) moves to (10,3).
        if (zoom == 1)
            QCOMPARE(frame.pixelColor(10, 3), QColor(Qt::white));
        else {
            double weight = 0, xSum = 0, ySum = 0;
            for (int y = 0; y < 10; ++y)
                for (int x = 0; x < 20; ++x) {
                    const int green = frame.pixelColor(x, y).green();
                    weight += green;
                    xSum += (x + .5) * green;
                    ySum += (y + .5) * green;
                }
            QVERIFY(weight > 0);
            QVERIFY(qAbs(xSum / weight - 9) < .03);
            QVERIFY(qAbs(ySum / weight - 3) < .03);
        }
    }
    QVERIFY(renderEyes(picture, QSize(100000, 10)).isNull());
    QVERIFY(renderEyes(picture, QSize(20, 10), -1).isNull());
}
void PictureTest::panorama()
{
    const QByteArray xml = QByteArrayLiteral("<x xmlns:p='http://ns.google.com/photos/1.0/panorama/' p:ProjectionType='equirectangular'/>");
    QByteArray payload("http://ns.adobe.com/xap/1.0/\0", 29);
    payload += xml;
    const int length = payload.size() + 2;
    QByteArray header = QByteArray::fromHex("ffd8ffe1");
    header += char(length >> 8);
    header += char(length);
    header += payload;
    QVERIFY(!Picture::fromImages(packed(), {}, {}, "jpg", header, {}, OutputMode::Stereo).isStereo());
    Picture::Options manual;
    manual.inputLayout = Picture::Layout::SideBySide;
    QVERIFY(Picture::fromImages(packed(), {}, {}, "jpg", header, manual, OutputMode::Stereo).isStereo());
    const auto marked = Picture::fromImages(packed(), {}, QStringLiteral("sideBySideLeftFirst"), "jpg", header, {}, OutputMode::Stereo);
    QVERIFY(marked.isStereo());
    QCOMPARE(marked.source, Picture::Source::FileMark);
}
void PictureTest::readerIntegration_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("rightFirst");
    QTest::addColumn<bool>("multiView");
    for (const QString &file : { QStringLiteral("test_jps_2_0.jpg"), QStringLiteral("test_jps_2_1.jpg"), QStringLiteral("test_jps_unmarked.jps") }) {
        QTest::addRow("%s", qPrintable(file)) << file << !file.contains(QLatin1String("2_1")) << false;
    }
    for (const QString &file : { QStringLiteral("test_mpo_1_0.mpo"), QStringLiteral("test_mpo_1_1.mpo"), QStringLiteral("test_mpo_0_0.mpo"),
             QStringLiteral("test_mpo_0_1.mpo") }) {
        QTest::addRow("%s", qPrintable(file)) << file << file.endsWith(QLatin1String("_1.mpo")) << true;
    }
}
void PictureTest::readerIntegration()
{
    QFETCH(QString, file);
    QFETCH(bool, rightFirst);
    QFETCH(bool, multiView);
    const QString path = QFINDTESTDATA(QStringLiteral("readers/") + file);
    QVERIFY(!path.isEmpty());
    QImageReader reference(path);
    const QImage first = reference.read();
    QVERIFY(!first.isNull());
    QImage second;
    if (multiView) {
        QVERIFY(reference.jumpToImage(1));
        second = reference.read();
    }
    const auto picture = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(picture.isStereo());
    const QImage expectedFirst = multiView ? first : first.copy(0, 0, first.width() / 2, first.height());
    const QImage expectedSecond = multiView ? second : first.copy(first.width() / 2, 0, first.width() / 2, first.height());
    QCOMPARE(picture.left, rightFirst ? expectedSecond : expectedFirst);
    QCOMPARE(picture.right, rightFirst ? expectedFirst : expectedSecond);
}

QTEST_MAIN(PictureTest)
#include "picturetest.moc"
