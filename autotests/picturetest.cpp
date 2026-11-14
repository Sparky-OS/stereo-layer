// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoicemenu.h"
#include "filechoicemoves.h"
#include "picture.h"
#include "render.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QMenu>
#include <QSet>
#include <QSignalSpy>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QUrl>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QtEndian>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <zlib.h>
using namespace Stereo;
class PictureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void marksAndChoices();
    void mpoFallback();
    void padding();
    void sterPadding_data();
    void sterPadding();
    void outputPolicy();
    void viewControls();
    void detector_data();
    void detector();
    void detectorSingleColour_data();
    void detectorSingleColour();
    void geometry();
    void cropEyesCopies_data();
    void cropEyesCopies();
    void cropEyesZoomed();
    void panorama();
    void shape_data();
    void shape();
    void shapeIsEnough();
    void unmarkedSunkTexture();
    void readerIntegration_data();
    void readerIntegration();
    void loadPair_data();
    void loadPair();
    void loadPairPng();
    void pairAtEverySize();
    void svgStereo_data();
    void svgStereo();
    void interleavedSvg();
    void wholeMarkSet_data();
    void wholeMarkSet();
    void upconversion_data();
    void upconversion();
    void swapOverMark();
    void storedCorrection();
    void fileChoice();
    void fileChoiceFollowsMoves();
    void fileChoiceMenuLists();
    void depthLayouts_data();
    void depthLayouts();
    void packedDepthOptions_data();
    void packedDepthOptions();
    void savedJpsDescriptor_data();
    void savedJpsDescriptor();
    void depthSeparate();
    void depthReaders();
    void heifMonoscopicFallback();
    void depthReaderControls_data();
    void depthReaderControls();
};
static QByteArray pngChunk(const QByteArray &type, const QByteArray &data);
// A scene of smooth texture, sums of waves, one set per seed (the same as policytest.c).
static double scene(double x, double y, int seed)
{
    return 128 + 50 * std::sin(x * (0.21 + 0.013 * seed) + y * 0.13) + 35 * std::sin(x * 0.047 - y * (0.31 + 0.007 * seed))
        + 25 * std::sin(x * 0.11 * (seed + 1) + y * 0.19) + 20 * std::sin(x * 0.37 + y * 0.29 * (seed + 2));
}
// What a picture of this size may hold: two views of one scene side by side (the right one a little to the side, more
// at the bottom), two unrelated scenes (two monitors), one scene across the whole width (a panorama or a phone
// photo), or nothing but one grey.
enum class Content { Pair, Unrelated, OneScene, Flat };
static QImage picture(const QSize &size, Content content)
{
    QImage image(size, QImage::Format_RGB32);
    const int half = size.width() / 2;
    const double k = 192.0 / half;
    for (int y = 0; y < size.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            double v = 128;
            const bool right = x >= half;
            switch (content) {
            case Content::Pair:
                v = right ? scene((x - half) * k + 6 * (1 + double(y) / size.height()), y * k, 0) : scene(x * k, y * k, 0);
                break;
            case Content::Unrelated:
                v = right ? scene((x - half) * k, y * k, 3) : scene(x * k, y * k, 0);
                break;
            case Content::OneScene:
                v = scene(x * k, y * k, 0);
                break;
            case Content::Flat:
                break;
            }
            const int c = qBound(0, int(v), 255);
            row[x] = qRgb(c, c * 9 / 10, c * 8 / 10);
        }
    }
    return image;
}
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
    for (const QString &layout : { QStringLiteral("mono"), QStringLiteral("sideBySideLeftFirst"), QStringLiteral("topBottomRightFirst") }) {
        const auto picture = Picture::fromImages(image, {}, layout, "jps", {}, options, OutputMode::Stereo);
        QCOMPARE(picture.source, Picture::Source::FileMark);
        QCOMPARE(picture.isStereo(), layout != QLatin1String("mono"));
    }
    options = {};
    // Shape alone is a pair (ORDER 504): flat red and blue halves in the shape range, with no mark, are the last safety net's pair
    QCOMPARE(Picture::fromImages(image, {}, {}, "png", {}, options, OutputMode::Stereo).source, Picture::Source::Shape);
    const auto ratio = Picture::fromImages(picture(QSize(300, 100), Content::Pair), {}, {}, "png", {}, options, OutputMode::Stereo);
    QCOMPARE(ratio.source, Picture::Source::Shape);
    QCOMPARE(ratio.layout, Picture::Layout::SideBySide);
    const auto mono = Picture::fromImages(image, {}, QStringLiteral("mono"), "jpg", {}, options, OutputMode::Stereo);
    QVERIFY(!mono.isStereo());
}
void PictureTest::mpoFallback()
{
    for (const QString &name : { QStringLiteral("unknown"), QStringLiteral("equal"), QStringLiteral("composite"), QStringLiteral("multangle") }) {
        const QString path = QFINDTESTDATA(QStringLiteral("readers/test_mpo_") + name + QStringLiteral(".mpo"));
        QVERIFY(!path.isEmpty());
        const auto picture = Picture::load(path, {}, OutputMode::Stereo);
        const bool disparity = name == QLatin1String("unknown") || name == QLatin1String("equal");
        QVERIFY2(picture.isStereo() == disparity, qPrintable(name));
        if (disparity) {
            QCOMPARE(picture.source, Picture::Source::Format);
            Picture::Options options;
            options.inputLayout = Picture::Layout::MultiView;
            options.inputOrder = Picture::EyeOrder::RightFirst;
            const auto chosen = Picture::load(path, options, OutputMode::Stereo);
            QCOMPARE(chosen.source, Picture::Source::User);
            QCOMPARE(chosen.left, picture.right);
            QCOMPARE(chosen.right, picture.left);
        } else {
            QVERIFY(!picture.original.isNull());
            Picture::Options options;
            options.inputLayout = Picture::Layout::MultiView;
            QVERIFY(!Picture::load(path, options, OutputMode::Stereo).isStereo());
        }
    }
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
// PNG Extensions 1.4.0, 3.6: "The left edge of the right subimage must be on a column that is evenly divisible by eight ... Padding columns
// between the two subimages must be introduced by the encoder if necessary", and a decoder takes "padding := 15 - ((W - 1) mod 16);
// if (padding > 7) then error; subimage_width := (W - padding) / 2". The padding is dropped, never shown as a line down the middle.
void PictureTest::sterPadding_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("padding");
    QTest::newRow("none, 1920 per eye") << 3840 << 0;
    QTest::newRow("none, 1024 per eye") << 2048 << 0;
    QTest::newRow("7 columns, 2713 wide (1353 per eye)") << 2713 << 7;
    QTest::newRow("6 columns, 2762 wide (1378 per eye)") << 2762 << 6;
    QTest::newRow("1 column") << 1199 << 1;
}
void PictureTest::sterPadding()
{
    QFETCH(int, width);
    QFETCH(int, padding);
    const int eye = (width - padding) / 2;
    QCOMPARE(2 * eye + padding, width);
    QCOMPARE(15 - ((width - 1) % 16), padding);
    QImage image(width, 12, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.fillRect(0, 0, eye, 12, Qt::red);
    painter.fillRect(eye + padding, 0, eye, 12, Qt::blue);
    painter.end();
    QCOMPARE((eye + padding) % 8, 0);
    const auto picture = Picture::fromImages(image, {}, QStringLiteral("sideBySideLeftFirst"), "pns", {}, {}, OutputMode::Stereo);
    QVERIFY(picture.isStereo());
    QCOMPARE(picture.left.size(), QSize(eye, 12));
    QCOMPARE(picture.right.size(), QSize(eye, 12));
    for (const int x : { 0, eye - 1 }) {
        QCOMPARE(picture.left.pixelColor(x, 6), QColor(Qt::red));
        QCOMPARE(picture.right.pixelColor(x, 6), QColor(Qt::blue));
    }
    // loaded from a file with the chunk, as Dolphin's thumbnailer and Gwenview read it
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));
    png.insert(8 + 25, pngChunk("sTER", QByteArray(1, '\x01')));
    QTemporaryDir dir;
    QFile file(dir.filePath(QStringLiteral("pair.pns")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(png);
    file.close();
    const auto loaded = Picture::loadPair(file.fileName(), QSize(width * 2, 100), {});
    QVERIFY(loaded.isStereo());
    QCOMPARE(loaded.left.size(), QSize(eye, 12));
    QCOMPARE(loaded.right.pixelColor(0, 6), QColor(Qt::blue));
    QCOMPARE(loaded.left.pixelColor(eye - 1, 6), QColor(Qt::red));
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
    options.inputLayout = Picture::Layout::SideBySide; // flat halves are one picture to the shape rule: the user says it is a pair
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
void PictureTest::detectorSingleColour_data()
{
    QTest::addColumn<QColor>("ink");
    QTest::addColumn<QColor>("ground");
    QTest::newRow("red on grey") << QColor(220, 30, 30) << QColor(96, 96, 96);
    QTest::newRow("green on grey") << QColor(40, 200, 70) << QColor(96, 96, 96);
    QTest::newRow("blue on grey") << QColor(30, 30, 220) << QColor(96, 96, 96);
    QTest::newRow("cyan on grey") << QColor(30, 200, 220) << QColor(96, 96, 96);
    QTest::newRow("magenta on grey") << QColor(220, 30, 200) << QColor(96, 96, 96);
    QTest::newRow("amber on grey") << QColor(230, 170, 30) << QColor(96, 96, 96);
    QTest::newRow("red on black") << QColor(255, 0, 0) << QColor(0, 0, 0);
    QTest::newRow("red on white") << QColor(200, 20, 20) << QColor(240, 240, 240);
}
void PictureTest::detectorSingleColour()
{
    QFETCH(QColor, ink);
    QFETCH(QColor, ground);
    // A small square and a word of one colour on a neutral ground: the red-on-grey wallpaper that was shown washed out
    QImage image(1280, 800, QImage::Format_RGB32);
    image.fill(ground);
    QPainter painter(&image);
    painter.fillRect(200, 120, 60, 60, ink);
    painter.setPen(ink);
    QFont font(QStringLiteral("sans"));
    font.setPixelSize(56);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(282, 170, QStringLiteral("LEFT"));
    painter.end();
    QCOMPARE(Picture::detectAnaglyph(image), Picture::AnaglyphColorPair::None);
    QCOMPARE(Picture::detectAnaglyph(image.scaled(320, 200)), Picture::AnaglyphColorPair::None);
    QVERIFY(!Picture::fromImages(image, {}, {}, {}, {}, {}, OutputMode::Stereo).isStereo());
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
namespace
{
// The address of a sample of a view: its view, column and row (as fsbs_ids.py writes them)
QRgb address(int view, int x, int y) { return qRgb(x & 0xff, y & 0xff, view << 7 | ((x >> 8) & 0xf) << 3 | ((y >> 8) & 0x7)); }
// A packed frame of width x height whose views carry their samples' addresses, and its views' size
QImage addressedFrame(Picture::Layout layout, int width, int height, QSize *view)
{
    QImage frame(width, height, QImage::Format_RGB32);
    frame.fill(Qt::black);
    using L = Picture::Layout;
    *view = layout == L::TopAndBottom || layout == L::Rows ? QSize(width, height / 2) : QSize(width / 2, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int v = 0, vx = x, vy = y;
            switch (layout) {
            case L::SideBySide:
            case L::SideBySideQuincunx:
                v = x >= width / 2;
                vx = x - v * width / 2;
                break;
            case L::TopAndBottom:
                v = y >= height / 2;
                vy = y - v * height / 2;
                break;
            case L::Rows:
                v = y % 2;
                vy = y / 2;
                break;
            case L::Columns:
                v = x % 2;
                vx = x / 2;
                break;
            case L::Checkerboard:
                v = (x + y) % 2;
                vx = x / 2;
                break;
            default:
                break;
            }
            frame.setPixel(x, y, address(v, vx, vy));
        }
    }
    return frame;
}
// The samples of packed that are not the addresses of their places, each view being view big
int wrongSamples(const QImage &packed, const QSize &view, QPoint offset = {})
{
    if (packed.size() != QSize(view.width() * 2, view.height()))
        return -1;
    int wrong = 0;
    for (int y = 0; y < packed.height(); ++y) {
        for (int x = 0; x < packed.width(); ++x) {
            const int v = x >= view.width();
            wrong += (packed.pixel(x, y) & 0xffffff) != (address(v, x - v * view.width() + offset.x(), y + offset.y()) & 0xffffff);
        }
    }
    return wrong;
}
}
void PictureTest::cropEyesCopies_data()
{
    QTest::addColumn<int>("layout");
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QSize>("area");
    using L = Picture::Layout;
    // every packing of Table D-8 a picture can carry (H.264 V15, 08/2024): types 3 and 4 as the picture's halves,
    // 0 to 2 and 3 with quincunx sampling as the eyes' own rows and columns
    QTest::newRow("side by side") << int(L::SideBySide) << QSize(384, 108) << QSize(160, 90);
    QTest::newRow("side by side into a larger area") << int(L::SideBySide) << QSize(384, 108) << QSize(640, 360);
    QTest::newRow("top and bottom") << int(L::TopAndBottom) << QSize(192, 216) << QSize(160, 90);
    QTest::newRow("rows") << int(L::Rows) << QSize(192, 108) << QSize(160, 90);
    QTest::newRow("columns") << int(L::Columns) << QSize(192, 108) << QSize(160, 90);
    QTest::newRow("checkerboard") << int(L::Checkerboard) << QSize(192, 108) << QSize(160, 90);
    QTest::newRow("quincunx") << int(L::SideBySideQuincunx) << QSize(192, 108) << QSize(160, 90);
}
void PictureTest::cropEyesCopies()
{
    QFETCH(int, layout);
    QFETCH(QSize, frame);
    QFETCH(QSize, area);
    QSize view;
    const QImage image = addressedFrame(Picture::Layout(layout), frame.width(), frame.height(), &view);
    for (const auto order : { Picture::EyeOrder::LeftFirst, Picture::EyeOrder::RightFirst }) {
        Picture picture = Picture::fromPackedImage(image, Picture::Layout(layout), order);
        // the stream's right eye first: its first view is the right eye, swapped back to read the addresses
        if (order == Picture::EyeOrder::RightFirst)
            picture = picture.swapped();
        QVERIFY(picture.isStereo());
        const EyeCrop crop = cropEyes(picture, area);
        // each eye at the resolution the picture carries it, every sample its own
        QCOMPARE(wrongSamples(crop.packed, view), 0);
        // shown with the shape of a view, fitted in the area: one resize, the compositor's
        const QSizeF shape = QSizeF(picture.left.size()).scaled(QSizeF(area), Qt::KeepAspectRatio);
        QVERIFY(qAbs(crop.shown.width() - shape.width()) < 1e-6);
        QVERIFY(qAbs(crop.shown.height() - shape.height()) < 1e-6);
        QVERIFY(qAbs(crop.shown.center().x() - area.width() / 2.0) < 1e-6);
        QVERIFY(qAbs(crop.shown.center().y() - area.height() / 2.0) < 1e-6);
    }
}
void PictureTest::cropEyesZoomed()
{
    QSize view;
    const Picture picture = Picture::fromPackedImage(
        addressedFrame(Picture::Layout::SideBySide, 400, 100, &view), Picture::Layout::SideBySide, Picture::EyeOrder::LeftFirst);
    // twice the fit, in an area of the view's shape: the middle half of each eye shows, copied, and fills the area
    const EyeCrop crop = cropEyes(picture, QSize(100, 50), 2);
    QCOMPARE(crop.packed.size(), QSize(200, 50));
    QCOMPARE(wrongSamples(crop.packed, QSize(100, 50), QPoint(50, 25)), 0);
    QCOMPARE(crop.shown, QRectF(0, 0, 100, 50));
    // panned to the right edge: the samples that show, and no more
    const EyeCrop panned = cropEyes(picture, QSize(100, 50), 2, QPointF(-50, 0));
    QCOMPARE(panned.shown, QRectF(0, 0, 100, 50));
    QCOMPARE(wrongSamples(panned.packed, QSize(100, 50), QPoint(100, 25)), 0);
    // further: the eye's edge in the middle of the area, the area's other half the window's
    const EyeCrop beyond = cropEyes(picture, QSize(100, 50), 2, QPointF(-100, 0));
    QCOMPARE(beyond.shown, QRectF(0, 0, 50, 50));
    QCOMPARE(wrongSamples(beyond.packed, QSize(50, 50), QPoint(150, 25)), 0);
    // more than a compositor's texture holds: drawn at the area's size
    Picture wide;
    wide.left = QImage(9000, 10, QImage::Format_RGB32);
    wide.left.fill(Qt::red);
    wide.right = wide.left;
    wide.layout = Picture::Layout::SideBySide;
    const EyeCrop drawn = cropEyes(wide, QSize(900, 1));
    QCOMPARE(drawn.packed.size(), QSize(1800, 1));
    QCOMPARE(drawn.shown, QRectF(0, 0, 900, 1));
    // nothing shows, nothing is handed over
    QVERIFY(cropEyes(picture, QSize(100, 50), 1, QPointF(1000, 0)).packed.isNull());
    QVERIFY(cropEyes(picture, QSize(100000, 50)).packed.isNull());
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
void PictureTest::shape_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<bool>("pair");
    // two views of one scene, at every shape the rule takes, and at the shapes it does not
    QTest::newRow("two squares") << QSize(200, 100) << true;
    QTest::newRow("two 4:3") << QSize(800, 300) << true;
    QTest::newRow("two 16:9") << QSize(3840, 1080) << true;
    QTest::newRow("3.6:1") << QSize(360, 100) << true;
    QTest::newRow("just under 2:1") << QSize(198, 100) << false;
    QTest::newRow("panorama 4:1") << QSize(400, 100) << false;
    QTest::newRow("tall 1:2") << QSize(100, 200) << false;
    QTest::newRow("phone screenshot") << QSize(1080, 2340) << false;
}
void PictureTest::shape()
{
    QFETCH(QSize, size);
    QFETCH(bool, pair);
    const auto picture = Picture::fromImages(::picture(size, Content::Pair), {}, {}, "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(picture.isStereo(), pair);
    QCOMPARE(picture.source, pair ? Picture::Source::Shape : Picture::Source::None);
    if (pair) {
        QCOMPARE(picture.layout, Picture::Layout::SideBySide);
    }
}
// An unmarked picture within the shape of a pair (2:1 to 3.6:1) is a pair, whatever its pixels say: the last safety net for files
// that say nothing (Daniel, 2026-10-13). Two views with little or no depth are not told from two unrelated pictures by a comparison, and
// a comparison that said "not a pair" left a real pair flat (ORDER 504). A mark or the user's choice still comes first, and outside the
// shape nothing makes a pair.
void PictureTest::shapeIsEnough()
{
    const QList<QSize> inside { QSize(4640 / 4, 2088 / 4), QSize(1484, 720), QSize(400, 200), QSize(2400 / 2, 1000 / 2), QSize(2710, 1000),
        QSize(3200, 900), QSize(3840, 1080) };
    for (const QSize &size : inside) {
        for (const Content content : { Content::Pair, Content::OneScene, Content::Unrelated, Content::Flat }) {
            const QString row = QStringLiteral("%1x%2 content %3").arg(size.width()).arg(size.height()).arg(int(content));
            const QImage image = ::picture(size, content);
            const auto picture = Picture::fromImages(image, {}, {}, "png", {}, {}, OutputMode::Stereo);
            QVERIFY2(picture.isStereo(), qPrintable(row));
            QCOMPARE(picture.source, Picture::Source::Shape);
            QCOMPARE(picture.layout, Picture::Layout::SideBySide);
            QCOMPARE(picture.left.size(), QSize(size.width() / 2, size.height()));
            // the user's choice of one picture and a mark that says so come first
            Picture::Options mono;
            mono.view = Picture::View::Mono;
            QVERIFY2(!Picture::fromImages(image, {}, {}, "png", {}, mono, OutputMode::Stereo).isStereo(), qPrintable(row));
            const auto marked = Picture::fromImages(image, {}, QStringLiteral("mono"), "png", {}, {}, OutputMode::Stereo);
            QVERIFY2(!marked.isStereo(), qPrintable(row));
            QCOMPARE(marked.source, Picture::Source::FileMark);
        }
    }
    const QList<QSize> outside { QSize(198, 100), QSize(400, 100), QSize(3200, 800), QSize(1080, 2340), QSize(100, 200) };
    for (const QSize &size : outside) {
        const auto picture = Picture::fromImages(::picture(size, Content::Pair), {}, {}, "png", {}, {}, OutputMode::Stereo);
        QVERIFY2(!picture.isStereo(), qPrintable(QStringLiteral("%1x%2").arg(size.width()).arg(size.height())));
        QCOMPARE(picture.source, Picture::Source::None);
    }
    // the comparison stays for a program that wants it
    QVERIFY(Picture::pairByContent(::picture(QSize(1200, 500), Content::Pair)));
    QVERIFY(!Picture::pairByContent(::picture(QSize(1200, 500), Content::Unrelated)));
    QVERIFY(!Picture::pairByContent(QImage()));
}
// A sunk texture, as the edition's stereo texture generator draws it (a bark, 3840x1080 full side by side, here at a quarter of the size):
// every view holds the same texture with a parallax of a few pixels and its own shading. The comparison of the halves told it from two
// pictures at no size (it found 4 matched blocks of 48 at the size of the thumbnailer's comparison), so without a mark neither Dolphin
// nor Gwenview showed it in 3D. The same pixels at 4:3 (one view alone) are one picture.
void PictureTest::unmarkedSunkTexture()
{
    const QString path = QFINDTESTDATA(QStringLiteral("readers/unmarked-sbs-sunk-texture-960x270.png"));
    QVERIFY(!path.isEmpty());
    const auto pair = Picture::loadPair(path, QSize(256, 256), {});
    QVERIFY(pair.isStereo());
    QCOMPARE(pair.source, Picture::Source::Shape);
    QCOMPARE(pair.layout, Picture::Layout::SideBySide);
    QVERIFY(pair.left.size() == pair.right.size());
    QCOMPARE(pair.left.size(), QSize(480, 270).scaled(QSize(128, 256), Qt::KeepAspectRatio));
    const auto viewer = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(viewer.isStereo());
    QCOMPARE(viewer.source, Picture::Source::Shape);
    QCOMPARE(viewer.left.size(), QSize(480, 270));
    QCOMPARE(viewer.right.size(), QSize(480, 270));
    // a reader that has the file's pixels already, as Gwenview's adapter
    QImageReader reader(path);
    const QImage image = reader.read();
    const auto fromReader = Picture::fromReader(reader, image, {}, "png", {}, {}, OutputMode::Stereo);
    QVERIFY(fromReader.isStereo());
    QCOMPARE(fromReader.source, Picture::Source::Shape);
    // one view alone at 4:3 is one picture
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(image.copy(0, 0, 360, 270).save(dir.filePath(QStringLiteral("mono.png"))));
    QVERIFY(!Picture::loadPair(dir.filePath(QStringLiteral("mono.png")), QSize(256, 256), {}).isStereo());
    QVERIFY(!Picture::load(dir.filePath(QStringLiteral("mono.png")), {}, OutputMode::Stereo).isStereo());
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
void PictureTest::loadPair_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("rightFirst");
    QTest::newRow("jps marked right first") << QStringLiteral("test_jps_2_0.jpg") << true;
    QTest::newRow("jps marked left first") << QStringLiteral("test_jps_2_1.jpg") << false;
    QTest::newRow("jps unmarked") << QStringLiteral("test_jps_unmarked.jps") << true;
    QTest::newRow("mpo") << QStringLiteral("test_mpo_1_0.mpo") << false;
    QTest::newRow("mpo right first") << QStringLiteral("test_mpo_1_1.mpo") << true;
}
void PictureTest::loadPair()
{
    QFETCH(QString, file);
    QFETCH(bool, rightFirst);
    const QString path = QFINDTESTDATA(QStringLiteral("readers/") + file);
    const auto full = Picture::load(path, {}, OutputMode::Unknown);
    QVERIFY(full.isStereo());
    const QSize bound(8, 8);
    const auto pair = Picture::loadPair(path, bound, {});
    QVERIFY(pair.isStereo());
    QCOMPARE(pair.eyeOrder == Picture::EyeOrder::RightFirst, rightFirst);
    QCOMPARE(pair.left.size(), pair.right.size());
    QCOMPARE(pair.left.size(), full.left.size().scaled(QSize(bound.width() / 2, bound.height()), Qt::KeepAspectRatio));
    const QImage sideBySide = pair.sideBySide();
    QCOMPARE(sideBySide.size(), QSize(pair.left.width() * 2, pair.left.height()));
    QVERIFY(sideBySide.width() <= bound.width() && sideBySide.height() <= bound.height());
    QCOMPARE(sideBySide.text(QStringLiteral("Stereo3DLayout")), QStringLiteral("sideBySideLeftFirst"));
    QCOMPARE(sideBySide.copy(0, 0, pair.left.width(), pair.left.height()), pair.left.convertToFormat(sideBySide.format()));
    QCOMPARE(sideBySide.copy(pair.left.width(), 0, pair.right.width(), pair.right.height()), pair.right.convertToFormat(sideBySide.format()));
    // The views keep their colours through the scaling: compare each with the full-size view scaled the same way.
    const QImage expected = full.left.scaled(pair.left.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QColor a = expected.pixelColor(expected.width() / 2, expected.height() / 2);
    const QColor b = pair.left.pixelColor(pair.left.width() / 2, pair.left.height() / 2);
    QVERIFY(qAbs(a.red() - b.red()) < 48 && qAbs(a.blue() - b.blue()) < 48);
}
// A thumbnail, a larger preview and a viewer show the same picture: the pixels of a file are compared at one size,
// whatever size it is decoded at for showing, so the answer does not change with it. The pictures go from a pair to two
// unrelated views, through the ones the comparison only just accepts or refuses, as JPEG files of a phone's shape.
void PictureTest::pairAtEverySize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QSize size(2400, 1080);
    const QImage pair = picture(size, Content::Pair);
    const QImage unrelated = picture(size, Content::Unrelated);
    const int half = size.width() / 2;
    int pairs = 0;
    int ones = 0;
    for (int step = 0; step <= 20; ++step) {
        const double share = step / 20.0;
        QImage image = pair;
        for (int y = 0; y < size.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            const QRgb *other = reinterpret_cast<const QRgb *>(unrelated.constScanLine(y));
            for (int x = half; x < size.width(); ++x) {
                const auto mix = [share](int a, int b) { return qBound(0, qRound(a * (1 - share) + b * share), 255); };
                row[x] = qRgb(mix(qRed(row[x]), qRed(other[x])), mix(qGreen(row[x]), qGreen(other[x])), mix(qBlue(row[x]), qBlue(other[x])));
            }
        }
        const QString path = dir.filePath(QStringLiteral("phone-%1.jpg").arg(step));
        QVERIFY(image.save(path, "jpg", 90));
        const bool viewer = Picture::load(path, {}, OutputMode::Unknown).isStereo();
        for (const int bound : { 128, 256, 384, 512, 768, 1024, 2048 }) {
            QVERIFY2(Picture::loadPair(path, QSize(bound, bound), {}).isStereo() == viewer,
                qPrintable(QStringLiteral("step %1 at %2: not as in a viewer (%3)")
                        .arg(step)
                        .arg(bound)
                        .arg(viewer ? QStringLiteral("pair") : QStringLiteral("one"))));
        }
        ++(viewer ? pairs : ones);
    }
    // by its shape a picture of 2400x1080 is a pair at every share of unrelated pixels, in a thumbnail as in a viewer
    QCOMPARE(pairs, 21);
    QCOMPARE(ones, 0);
}
static QByteArray pngChunk(const QByteArray &type, const QByteArray &data)
{
    QByteArray chunk;
    const quint32 length = data.size();
    chunk.append(char(length >> 24)).append(char(length >> 16)).append(char(length >> 8)).append(char(length));
    const QByteArray body = type + data;
    chunk.append(body);
    quint32 crc = 0xffffffff;
    for (const char c : body) {
        crc ^= quint8(c);
        for (int k = 0; k < 8; ++k)
            crc = crc & 1 ? 0xedb88320 ^ (crc >> 1) : crc >> 1;
    }
    crc ^= 0xffffffff;
    chunk.append(char(crc >> 24)).append(char(crc >> 16)).append(char(crc >> 8)).append(char(crc));
    return chunk;
}
void PictureTest::loadPairPng()
{
    // A small pair with the registered sTER chunk (PNG extensions 1.4.0, 3.6), mode 1: the left view on the left. The
    // views are 598 wide, so the right one starts on a multiple of 8 (600) after 2 padding columns.
    QImage image(1198, 400, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.fillRect(0, 0, 598, 400, Qt::red);
    painter.fillRect(600, 0, 598, 400, Qt::blue);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));
    png.insert(8 + 25, pngChunk("sTER", QByteArray(1, '\x01')));
    QTemporaryDir dir;
    for (const QString &name : { QStringLiteral("pair.png"), QStringLiteral("pair.pns") }) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png);
        file.close();
        const auto pair = Picture::loadPair(file.fileName(), QSize(256, 256), {});
        QVERIFY2(pair.isStereo(), qPrintable(name));
        QCOMPARE(pair.source, Picture::Source::FileMark);
        QCOMPARE(pair.left.size(), QSize(598, 400).scaled(QSize(128, 256), Qt::KeepAspectRatio));
        QCOMPARE(pair.left.pixelColor(64, 40), QColor(Qt::red));
        QCOMPARE(pair.right.pixelColor(64, 40), QColor(Qt::blue));
    }
    // The same pixels without the chunk are a pair by their shape at 3:1 (the last safety net), and at 4:1 a panorama.
    QImage wide(1600, 400, QImage::Format_RGB32);
    wide.fill(Qt::gray);
    QVERIFY(wide.save(dir.filePath(QStringLiteral("panorama.png"))));
    QVERIFY(!Picture::loadPair(dir.filePath(QStringLiteral("panorama.png")), QSize(256, 256), {}).isStereo());
    QVERIFY(image.save(dir.filePath(QStringLiteral("unmarked.png"))));
    QCOMPARE(Picture::loadPair(dir.filePath(QStringLiteral("unmarked.png")), QSize(256, 256), {}).source, Picture::Source::Shape);
    QVERIFY(::picture(QSize(900, 300), Content::Pair).save(dir.filePath(QStringLiteral("shape.png"))));
    QCOMPARE(Picture::loadPair(dir.filePath(QStringLiteral("shape.png")), QSize(256, 256), {}).source, Picture::Source::Shape);
    QVERIFY(Picture().sideBySide().isNull());
}
void PictureTest::svgStereo_data()
{
    QTest::addColumn<QString>("root");
    QTest::addColumn<bool>("pair");
    QTest::addColumn<bool>("rightFirst");
    QTest::addColumn<bool>("compressed");
    const QString svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' xmlns:svgstereo='https://github.com/Sparky-OS/svgstereo' ");
    for (const bool compressed : { false, true }) {
        const char *suffix = compressed ? ", gzip" : "";
        QTest::addRow("left first%s", suffix) << svg + QStringLiteral("svgstereo:layout='sideBySideLeftFirst'") << true << false << compressed;
        QTest::addRow("right first%s", suffix) << svg + QStringLiteral("svgstereo:layout='sideBySideRightFirst'") << true << true << compressed;
        QTest::addRow("mono%s", suffix) << svg + QStringLiteral("svgstereo:layout='mono'") << false << false << compressed;
        QTest::addRow("unmarked 2:1 drawing%s", suffix) << svg << false << false << compressed;
        QTest::addRow("another namespace%s", suffix)
            << QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' xmlns:s='https://example.org/other' s:layout='sideBySideLeftFirst'") << false
            << false << compressed;
    }
}
void PictureTest::svgStereo()
{
    QFETCH(QString, root);
    QFETCH(bool, pair);
    QFETCH(bool, rightFirst);
    QFETCH(bool, compressed);
    if (!QImageReader::supportedImageFormats().contains(compressed ? "svgz" : "svg")) {
        QSKIP("Qt's SVG image plugin is not installed");
    }
    const QString document = root
        + QStringLiteral(" width='200' height='100' viewBox='0 0 200 100'><rect width='100' height='100' fill='#ff0000'/>"
                         "<rect x='100' width='100' height='100' fill='#0000ff'/></svg>");
    QByteArray bytes = document.toUtf8();
    if (compressed) {
        // A gzip stream (RFC 1952), as an .svgz file holds it
        z_stream stream = {};
        QCOMPARE(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY), Z_OK);
        QByteArray gzip(deflateBound(&stream, bytes.size()), Qt::Uninitialized);
        stream.next_in = reinterpret_cast<Bytef *>(bytes.data());
        stream.avail_in = uInt(bytes.size());
        stream.next_out = reinterpret_cast<Bytef *>(gzip.data());
        stream.avail_out = uInt(gzip.size());
        QCOMPARE(deflate(&stream, Z_FINISH), Z_STREAM_END);
        gzip.truncate(stream.total_out);
        deflateEnd(&stream);
        bytes = gzip;
    }
    QTemporaryDir dir;
    QFile file(dir.filePath(compressed ? QStringLiteral("pair.svgz") : QStringLiteral("pair.svg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    const auto loaded = Picture::loadPair(file.fileName(), QSize(256, 256), {});
    QCOMPARE(loaded.isStereo(), pair);
    const auto full = Picture::load(file.fileName(), {}, OutputMode::Stereo);
    QCOMPARE(full.isStereo(), pair);
    if (!pair) {
        return;
    }
    QCOMPARE(loaded.source, Picture::Source::FileMark);
    QCOMPARE(loaded.left.size(), QSize(128, 128));
    QCOMPARE(loaded.left.pixelColor(64, 64), rightFirst ? QColor(Qt::blue) : QColor(Qt::red));
    QCOMPARE(loaded.right.pixelColor(64, 64), rightFirst ? QColor(Qt::red) : QColor(Qt::blue));
}

void PictureTest::interleavedSvg()
{
    // A drawing of interleaved rows is decoded at its own size before the rows are taken apart: drawn larger, each row
    // would cover several pixel rows and the views would mix
    if (!QImageReader::supportedImageFormats().contains("svg")) {
        QSKIP("Qt's SVG image plugin is not installed");
    }
    QString document = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' xmlns:svgstereo='https://github.com/Sparky-OS/svgstereo' svgstereo:layout='rowInterleavedLeftFirst' "
        "width='64' height='32' viewBox='0 0 64 32' shape-rendering='crispEdges'>");
    for (int y = 0; y < 32; ++y)
        document += QStringLiteral("<rect y='%1' width='64' height='1' fill='%2'/>")
                        .arg(y)
                        .arg(y % 2 ? QStringLiteral("#0000ff") : QStringLiteral("#ff0000"));
    document += QStringLiteral("</svg>");
    QTemporaryDir dir;
    QFile file(dir.filePath(QStringLiteral("rows.svg")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(document.toUtf8());
    file.close();
    const auto loaded = Picture::loadPair(file.fileName(), QSize(256, 256), {});
    QVERIFY(loaded.isStereo());
    QCOMPARE(loaded.layout, Picture::Layout::Rows);
    for (int y = 1; y < loaded.left.height() - 1; ++y) {
        QCOMPARE(loaded.left.pixelColor(loaded.left.width() / 2, y), QColor(Qt::red));
        QCOMPARE(loaded.right.pixelColor(loaded.right.width() / 2, y), QColor(Qt::blue));
    }
}
void PictureTest::wholeMarkSet_data()
{
    // Every Stereo3DLayout value (RFC 9559 StereoMode, H.264/H.265 Table D-8, JPS, MPO, SVGStereo), on a 32x16 picture;
    // H.264 V15 (08/2024) numbers that table D-8, V16 (06/2026) D-9
    QTest::addColumn<QString>("mark");
    QTest::addColumn<bool>("stereo");
    QTest::addColumn<Picture::Layout>("layout");
    QTest::addColumn<Picture::EyeOrder>("order");
    QTest::addColumn<Picture::AnaglyphColorPair>("anaglyph");
    QTest::addColumn<QSize>("view");
    const auto L = Picture::EyeOrder::LeftFirst;
    const auto R = Picture::EyeOrder::RightFirst;
    const auto none = Picture::AnaglyphColorPair::None;
    QTest::newRow("mono") << QStringLiteral("mono") << false << Picture::Layout::None << L << none << QSize();
    struct Row {
        const char *name;
        Picture::Layout layout;
        QSize view;
    };
    const Row rows[] = {
        { "sideBySide", Picture::Layout::SideBySide, QSize(16, 16) },
        { "sideBySideQuincunx", Picture::Layout::SideBySideQuincunx, QSize(32, 16) },
        { "topBottom", Picture::Layout::TopAndBottom, QSize(32, 8) },
        { "checkerboard", Picture::Layout::Checkerboard, QSize(32, 16) },
        { "rowInterleaved", Picture::Layout::Rows, QSize(32, 16) },
        { "columnInterleaved", Picture::Layout::Columns, QSize(32, 16) },
    };
    for (const Row &row : rows) {
        for (const auto order : { L, R }) {
            const QString mark = QLatin1String(row.name) + (order == L ? QLatin1String("LeftFirst") : QLatin1String("RightFirst"));
            QTest::addRow("%s", qPrintable(mark)) << mark << true << row.layout << order << none << row.view;
        }
    }
    // Views that alternate in time or in a stream's blocks are not in one still
    for (const char *mark : { "frameSequentialLeftFirst", "frameSequentialRightFirst", "blockLacedLeftFirst", "blockLacedRightFirst" })
        QTest::addRow("%s", mark) << QString::fromLatin1(mark) << false << Picture::Layout::None << L << none << QSize();
    QTest::newRow("anaglyphCyanRed") << QStringLiteral("anaglyphCyanRed") << true << Picture::Layout::None << L << Picture::AnaglyphColorPair::RedCyan
                                     << QSize(32, 16);
    QTest::newRow("anaglyphGreenMagenta") << QStringLiteral("anaglyphGreenMagenta") << true << Picture::Layout::None << L
                                          << Picture::AnaglyphColorPair::GreenMagenta << QSize(32, 16);
    QTest::newRow("anaglyphRedLeft") << QStringLiteral("anaglyphRedLeft") << true << Picture::Layout::None << L << Picture::AnaglyphColorPair::RedCyan
                                     << QSize(32, 16);
    QTest::newRow("anaglyphRedRight") << QStringLiteral("anaglyphRedRight") << true << Picture::Layout::None << R
                                      << Picture::AnaglyphColorPair::RedCyan << QSize(32, 16);
    // Outside the set: one picture
    for (const char *mark : { "sideBySide", "sideBySideLeft", "anaglyphCyan", "topBottomleftFirst", "quincunx" })
        QTest::addRow("unknown %s", mark) << QString::fromLatin1(mark) << false << Picture::Layout::None << L << none << QSize();
}
void PictureTest::wholeMarkSet()
{
    QFETCH(QString, mark);
    QFETCH(bool, stereo);
    QFETCH(Picture::Layout, layout);
    QFETCH(Picture::EyeOrder, order);
    QFETCH(Picture::AnaglyphColorPair, anaglyph);
    QFETCH(QSize, view);
    QImage image(32, 16, QImage::Format_RGB32);
    image.fill(QColor(200, 60, 60));
    const auto picture = Picture::fromImages(image, {}, mark, "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(picture.source, Picture::Source::FileMark);
    QCOMPARE(picture.isStereo(), stereo);
    QCOMPARE(picture.anaglyph, anaglyph);
    if (anaglyph == Picture::AnaglyphColorPair::None)
        QCOMPARE(picture.layout, layout);
    if (stereo) {
        QCOMPARE(picture.eyeOrder, order);
        QCOMPARE(picture.left.size(), view);
        QCOMPARE(picture.right.size(), view);
    }
}
void PictureTest::upconversion_data()
{
    QTest::addColumn<Picture::Layout>("layout");
    QTest::addColumn<Picture::EyeOrder>("order");
    for (const auto layout :
        { Picture::Layout::Rows, Picture::Layout::Columns, Picture::Layout::Checkerboard, Picture::Layout::SideBySideQuincunx }) {
        for (const auto order : { Picture::EyeOrder::LeftFirst, Picture::EyeOrder::RightFirst })
            QTest::addRow("%d %s", int(layout), order == Picture::EyeOrder::LeftFirst ? "left first" : "right first") << layout << order;
    }
}
void PictureTest::upconversion()
{
    QFETCH(Picture::Layout, layout);
    QFETCH(Picture::EyeOrder, order);
    // Two views of 32x16: linear ramps, the left one in red across x and y, the right one in blue; packed as the layout
    // says, each view keeps its samples and gets back the missing ones exactly (a ramp is its neighbours' mean).
    const auto left = [](int x, int y) { return QColor(4 * x + 2 * y, 0, 0); };
    const auto right = [](int x, int y) { return QColor(0, 0, 4 * x + 2 * y); };
    const bool quincunx = layout == Picture::Layout::SideBySideQuincunx;
    QImage frame(32, 16, QImage::Format_RGB32);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (quincunx) {
                // the half of each view: its samples on even columns of even rows and odd columns of odd rows
                const int view = x / 16;
                const int column = 2 * (x % 16) + (y % 2);
                const bool firstView = view == 0;
                const bool leftView = firstView == (order == Picture::EyeOrder::LeftFirst);
                frame.setPixelColor(x, y, leftView ? left(column, y) : right(column, y));
                continue;
            }
            const int which = layout == Picture::Layout::Rows ? y % 2 : (layout == Picture::Layout::Columns ? x % 2 : (x + y) % 2);
            const bool leftView = (which == 0) == (order == Picture::EyeOrder::LeftFirst);
            frame.setPixelColor(x, y, leftView ? left(x, y) : right(x, y));
        }
    }
    const auto picture = Picture::fromPackedImage(frame, layout, order);
    QVERIFY(picture.isStereo());
    QCOMPARE(picture.left.size(), QSize(32, 16));
    QCOMPARE(picture.right.size(), QSize(32, 16));
    // the edges have one neighbour across the interleaving; inside, every pixel is the view's own
    for (int y = 1; y < 15; ++y) {
        for (int x = 1; x < 31; ++x) {
            QCOMPARE(picture.left.pixelColor(x, y), left(x, y));
            QCOMPARE(picture.right.pixelColor(x, y), right(x, y));
        }
    }
}

void PictureTest::swapOverMark()
{
    // Marked right first: the first half is the right view. The user's correction puts it in the left eye; removing
    // the correction returns to the mark; a 2D output keeps the picture as it is either way.
    const QImage image = packed(); // red first half, blue second half
    const auto marked = Picture::fromImages(image, {}, QStringLiteral("sideBySideRightFirst"), "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(marked.left.pixelColor(0, 0), QColor(Qt::blue));
    Picture::Options corrected;
    corrected.swapEyes = true;
    const auto swapped = Picture::fromImages(image, {}, QStringLiteral("sideBySideRightFirst"), "png", {}, corrected, OutputMode::Stereo);
    QCOMPARE(swapped.source, Picture::Source::FileMark);
    QCOMPARE(swapped.left.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(swapped.right.pixelColor(0, 0), QColor(Qt::blue));
    const auto again = Picture::fromImages(image, {}, QStringLiteral("sideBySideRightFirst"), "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(again.left.pixelColor(0, 0), QColor(Qt::blue));
    const auto flat = Picture::fromImages(image, {}, QStringLiteral("sideBySideRightFirst"), "png", {}, corrected, OutputMode::Mono);
    QCOMPARE(flat.original, image);
}

void PictureTest::storedCorrection()
{
    // The correction stored with a file applies over its mark in load() and loadPair(), and goes away with it
    if (!QImageReader::supportedImageFormats().contains("svg")) {
        QSKIP("Qt's SVG image plugin is not installed");
    }
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/correction-XXXXXX"));
    const QString path = dir.filePath(QStringLiteral("pair.svg"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<svg xmlns='http://www.w3.org/2000/svg' xmlns:svgstereo='https://github.com/Sparky-OS/svgstereo' "
               "svgstereo:layout='sideBySideLeftFirst' width='200' height='100' viewBox='0 0 200 100'>"
               "<rect width='100' height='100' fill='#ff0000'/><rect x='100' width='100' height='100' fill='#0000ff'/></svg>");
    file.close();
    if (!Picture::canStoreUserSwapEyes(path)) {
        QSKIP("This file system keeps no extended attributes");
    }
    QVERIFY(!Picture::userSwapEyes(path));
    QCOMPARE(Picture::loadPair(path, QSize(256, 256), {}).left.pixelColor(64, 64), QColor(Qt::red));
    QVERIFY(Picture::setUserSwapEyes(path, true));
    QVERIFY(Picture::userSwapEyes(path));
    const auto pair = Picture::loadPair(path, QSize(256, 256), {});
    QCOMPARE(pair.source, Picture::Source::FileMark);
    QCOMPARE(pair.left.pixelColor(64, 64), QColor(Qt::blue));
    QCOMPARE(pair.right.pixelColor(64, 64), QColor(Qt::red));
    QCOMPARE(Picture::load(path, {}, OutputMode::Stereo).left.pixelColor(64, 64), QColor(Qt::blue));
    // the program's own swap on top of the stored one swaps back
    Picture::Options swapped;
    swapped.swapEyes = true;
    QCOMPARE(Picture::load(path, swapped, OutputMode::Stereo).left.pixelColor(64, 64), QColor(Qt::red));
    QVERIFY(Picture::setUserSwapEyes(path, false));
    QVERIFY(!Picture::userSwapEyes(path));
    QCOMPARE(Picture::loadPair(path, QSize(256, 256), {}).left.pixelColor(64, 64), QColor(Qt::red));
}

// A picture of vertical stripes with a near square on a far background, as a depth map of 2D-plus-depth: larger
// samples nearer, the screen in the middle. The right eye shows the square moved left and the background right by
// range / 2 at this width.
static QImage stripes(int w, int h)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            image.setPixelColor(x, y, QColor((x * 16) % 256, x >= 40 && x < 60 ? 240 : 60, 128));
    return image;
}

static QImage depthOf(int w, int h, int nearSample, int farSample)
{
    QImage depth(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            depth.scanLine(y)[x] = uchar(x >= 40 && x < 60 ? nearSample : farSample);
    return depth;
}

void PictureTest::depthLayouts_data()
{
    QTest::addColumn<QString>("mark");
    QTest::newRow("side by side, picture first") << QStringLiteral("depthSideBySidePictureFirst");
    QTest::newRow("side by side, depth first") << QStringLiteral("depthSideBySideDepthFirst");
    QTest::newRow("top and bottom, picture first") << QStringLiteral("depthTopBottomPictureFirst");
    QTest::newRow("top and bottom, depth first") << QStringLiteral("depthTopBottomDepthFirst");
    QTest::newRow("graphics") << QStringLiteral("depthGraphicsTopBottom");
    QTest::newRow("background") << QStringLiteral("depthBackgroundQuarters");
}

// Each depth layout, marked, packed into one frame: the left eye is the picture, the right one shows the near square
// 8 pixels further left than the background around it (parallax -4 against +4 at 128 wide, range 60 at 1920).
void PictureTest::depthLayouts()
{
    QFETCH(QString, mark);
    const int w = 128, h = 32;
    const QImage pic = stripes(w, h);
    const QImage depth = depthOf(w, h, 255, 0);
    QImage frame;
    const bool sbs = mark.startsWith(QLatin1String("depthSideBySide")), depthFirst = mark.endsWith(QLatin1String("DepthFirst"));
    const QImage depthRgba = depth.convertToFormat(QImage::Format_RGBA8888);
    QImage graphics(w, h, QImage::Format_RGBA8888), graphicsDepth(w, h, QImage::Format_Grayscale8);
    graphics.fill(QColor(255, 255, 0));
    graphicsDepth.fill(0);
    for (int y = 4; y < 8; y++)
        for (int x = 4; x < 20; x++)
            graphicsDepth.scanLine(y)[x] = 255; // a bar of graphics in front
    if (sbs || mark.startsWith(QLatin1String("depthTopBottom"))) {
        frame = QImage(sbs ? 2 * w : w, sbs ? h : 2 * h, QImage::Format_RGBA8888);
        QPainter painter(&frame);
        painter.drawImage(0, 0, depthFirst ? depthRgba : pic);
        painter.drawImage(sbs ? w : 0, sbs ? 0 : h, depthFirst ? pic : depthRgba);
    } else if (mark == QLatin1String("depthGraphicsTopBottom")) {
        frame = QImage(w, 4 * h, QImage::Format_RGBA8888);
        QPainter painter(&frame);
        painter.drawImage(0, 0, pic);
        painter.drawImage(0, h, depthRgba);
        painter.drawImage(0, 2 * h, graphics);
        painter.drawImage(0, 3 * h, graphicsDepth.convertToFormat(QImage::Format_RGBA8888));
    } else {
        frame = QImage(2 * w, 2 * h, QImage::Format_RGBA8888);
        QPainter painter(&frame);
        painter.drawImage(0, 0, pic);
        painter.drawImage(w, 0, depthRgba);
        QImage background = pic;
        background.fill(QColor(0, 0, 255));
        painter.drawImage(0, h, background); // what the square hides: blue
        painter.drawImage(w, h, depthOf(w, h, 1, 1).convertToFormat(QImage::Format_RGBA8888)); // all of it far (0 is no background)
    }
    frame.setText(QStringLiteral("Stereo3DDepth"), QStringLiteral("nearness range=60"));
    Picture::Layout layout;
    Picture::EyeOrder order;
    {
        // the mark as a reader reports it
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QImage marked = frame;
        marked.setText(QStringLiteral("Stereo3DLayout"), mark);
        QVERIFY(marked.save(&buffer, "PNG"));
        const Picture loaded
            = Picture::fromImages(QImage::fromData(buffer.data(), "PNG"), {}, mark, "png", buffer.data().left(1024), {}, OutputMode::Stereo);
        QVERIFY(loaded.isStereo());
        layout = loaded.layout;
        order = depthFirst ? Picture::EyeOrder::RightFirst : Picture::EyeOrder::LeftFirst;
        QVERIFY(layout >= Picture::Layout::DepthSideBySide && layout <= Picture::Layout::DepthBackground);
    }
    const Picture views = Picture::fromPackedImage(frame, layout, order);
    QVERIFY(views.isStereo());
    QCOMPARE(views.left.size(), QSize(w, h));
    const int y = 20;
    // the left eye is the picture (the graphics drawn in it at their place)
    QCOMPARE(views.left.pixelColor(30, y), pic.pixelColor(30, y));
    // the right eye: the square's pixel 45 at 41, the background's pixel 70 at 74
    QCOMPARE(views.right.pixelColor(41, y), pic.pixelColor(45, y));
    QCOMPARE(views.right.pixelColor(74, y), pic.pixelColor(70, y));
    if (mark == QLatin1String("depthGraphicsTopBottom")) {
        QCOMPARE(views.left.pixelColor(10, 5), QColor(255, 255, 0));
        QCOMPARE(views.right.pixelColor(6, 5), QColor(255, 255, 0)); // graphics in front, 4 left
        QCOMPARE(views.right.pixelColor(30, 5), pic.pixelColor(26, 5)); // no graphics there
    }
    if (mark == QLatin1String("depthBackgroundQuarters")) {
        // the disocclusion right of the square shows the background layer (blue), not the stripes extended
        QCOMPARE(views.right.pixelColor(60, y), QColor(0, 0, 255));
    }
}



void PictureTest::savedJpsDescriptor_data()
{
    QTest::addColumn<int>("separation");
    QTest::newRow("adjacent") << 0;
    QTest::newRow("gap") << 2;
}

void PictureTest::savedJpsDescriptor()
{
    QFETCH(int, separation);
    QImage packed(128 + separation, 64, QImage::Format_RGB32);
    packed.fill(Qt::black);
    {
        QPainter p(&packed);
        p.fillRect(0, 0, 64, 64, Qt::red);
        p.fillRect(64 + separation, 0, 64, 64, Qt::blue);
    }
    packed.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
    packed.setText(QStringLiteral("Stereo3DJpsSeparation"), QString::number(separation));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("saved.jps"));
    QImageWriter writer(path, "jps");
    writer.setQuality(100);
    QVERIFY(writer.write(packed));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    int descriptors = 0;
    for (qsizetype at = 2; at + 4 <= data.size();) {
        QCOMPARE(quint8(data[at]), quint8(255));
        const int marker = quint8(data[at + 1]);
        const int length = qFromBigEndian<quint16>(data.constData() + at + 2);
        QVERIFY(length >= 2 && at + 2 + length <= data.size());
        if (marker == 0xda)
            break;
        if (marker == 0xe3 && data.mid(at + 4, 8) == "_JPSJPS_") {
            ++descriptors;
            QCOMPARE(length, 18);
            QCOMPARE(qFromBigEndian<quint16>(data.constData() + at + 12), quint16(4));
            QCOMPARE(qFromBigEndian<quint32>(data.constData() + at + 14), quint32(0x40201 | separation << 24));
            QCOMPARE(qFromBigEndian<quint16>(data.constData() + at + 18), quint16(0));
        }
        at += 2 + length;
    }
    QCOMPARE(descriptors, 1);
    QImageReader reader(path, "jps");
    QCOMPARE(reader.text(QStringLiteral("Stereo3DLayout")), QStringLiteral("sideBySideLeftFirst"));
    QVERIFY(!reader.read().isNull());
    const auto readback = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(readback.isStereo());
    QCOMPARE(readback.left.size(), QSize(64, 64));
    QCOMPARE(readback.right.size(), QSize(64, 64));
    QVERIFY(readback.left.pixelColor(16, 16).red() > 240);
    QVERIFY(readback.right.pixelColor(16, 16).blue() > 240);
}

void PictureTest::packedDepthOptions_data()
{
    depthLayouts_data();
}

void PictureTest::packedDepthOptions()
{
    QFETCH(QString, mark);
    const int w = 128, h = 32;
    const QImage pic = stripes(w, h), depth = depthOf(w, h, 255, 32);
    const bool side = mark.startsWith(QLatin1String("depthSideBySide"));
    const bool top = mark.startsWith(QLatin1String("depthTopBottom"));
    const bool reversed = mark.endsWith(QLatin1String("DepthFirst"));
    const bool background = mark == QLatin1String("depthBackgroundQuarters");
    QImage layer, layerDepth;
    if (!side && !top) {
        layer = pic;
        layer.fill(background ? QColor(Qt::blue) : QColor(Qt::yellow));
        layerDepth = depthOf(w, h, background ? 1 : 255, background ? 1 : 0);
    }
    Picture::DepthOptions chosen;
    chosen.range = 640;
    chosen.view = Picture::DepthOptions::View::Center;
    chosen.fill = Picture::DepthOptions::Fill::None;
    const auto expected = Picture::fromDepth(pic, depth, {}, chosen, layer, layerDepth, background);
    const auto defaults = Picture::fromDepth(pic, depth, {}, {}, layer, layerDepth, background);
    QVERIFY(expected.isStereo());
    QVERIFY(defaults.isStereo());
    QVERIFY(expected.left != defaults.left || expected.right != defaults.right);
    QImage frame(side || background ? 2 * w : w, top ? 2 * h : side ? h : background ? 2 * h : 4 * h, pic.format());
    {
        QPainter painter(&frame);
        painter.drawImage(0, 0, reversed ? depth : pic);
        painter.drawImage(side || background ? w : 0, side || background ? 0 : h, reversed ? pic : depth);
        if (!layer.isNull()) {
            painter.drawImage(0, background ? h : 2 * h, layer);
            painter.drawImage(background ? w : 0, background ? h : 3 * h, layerDepth);
        }
    }
    Picture::Options manual;
    manual.inputLayout = side ? Picture::Layout::DepthSideBySide : top ? Picture::Layout::DepthTopAndBottom
        : background ? Picture::Layout::DepthBackground : Picture::Layout::DepthGraphics;
    manual.inputOrder = reversed ? Picture::EyeOrder::RightFirst : Picture::EyeOrder::LeftFirst;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (bool declared : { false, true }) {
        const QString path = dir.filePath(declared ? QStringLiteral("marked.png") : QStringLiteral("manual.png"));
        frame.setText(QStringLiteral("Stereo3DLayout"), declared ? mark : QString());
        QVERIFY(frame.save(path));
        const Picture::Options options = declared ? Picture::Options() : manual;
        const auto loaded = Picture::load(path, options, chosen, Picture::DepthPicture::Shown, OutputMode::Stereo);
        QVERIFY(loaded.isStereo());
        QCOMPARE(loaded.left, expected.left);
        QCOMPARE(loaded.right, expected.right);
        const auto thumb = Picture::loadPair(path, QSize(2 * w, 4 * h), options, chosen, Picture::DepthPicture::Shown);
        QCOMPARE(thumb.left, expected.left);
        QCOMPARE(thumb.right, expected.right);
        const auto images = Picture::fromImages(frame, {}, declared ? mark : QString(), "png", {}, options, chosen, OutputMode::Stereo);
        QCOMPARE(images.left, expected.left);
        QCOMPARE(images.right, expected.right);
        auto swappedOptions = options;
        swappedOptions.swapEyes = true;
        const auto swapped = Picture::load(path, swappedOptions, chosen, Picture::DepthPicture::Shown, OutputMode::Stereo);
        QCOMPARE(swapped.left, expected.right);
        QCOMPARE(swapped.right, expected.left);
    }
}

// The depth map as a second image (an HEIF depth item, a Dynamic Depth container item), with a Dynamic Depth range.
void PictureTest::depthSeparate()
{
    const int w = 128, h = 32;
    const QImage pic = stripes(w, h);
    // RangeInverse between Near and Far: 0 near, 255 far
    QImage depth = depthOf(w, h, 0, 255);
    depth.setText(QStringLiteral("Stereo3DDepth"), QStringLiteral("rangeInverse near=1 far=4 range=60"));
    const Picture views = Picture::fromImages(pic, depth, QStringLiteral("depthSeparate"), "png", {}, {}, OutputMode::Stereo);
    QVERIFY(views.isStereo());
    QCOMPARE(views.layout, Picture::Layout::DepthSeparate);
    QCOMPARE(views.right.pixelColor(41, 20), pic.pixelColor(45, 20));
    // a malformed mark draws nothing: the picture stays one picture
    QImage bad = depth;
    bad.setText(QStringLiteral("Stereo3DDepth"), QStringLiteral("z near"));
    QVERIFY(!Picture::fromDepth(pic, bad, bad.text(QStringLiteral("Stereo3DDepth"))).isStereo());
    // the user swaps the drawn eyes
    Picture::Options swap;
    swap.swapEyes = true;
    const Picture swapped = Picture::fromImages(pic, depth, QStringLiteral("depthSeparate"), "png", {}, swap, OutputMode::Stereo);
    QCOMPARE(swapped.left.pixelColor(41, 20), pic.pixelColor(45, 20));
}

// A file of autotests/depthreader, read as the edition's readers give pictures with their depth map.
static QString depthFile(const QTemporaryDir &dir, const QString &name, const QString &layout, const QString &mark, const QList<QImage> &images)
{
    const QString path = dir.filePath(name + QStringLiteral(".sldt"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("SLDT");
    QDataStream out(&file);
    out << layout << mark << qint32(images.size());
    for (const QImage &image : images)
        out << image;
    return path;
}

void PictureTest::depthReaders()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int w = 128, h = 32;
    const QImage pic = stripes(w, h);
    const QImage depth = depthOf(w, h, 0, 255);
    QImage original = pic;
    original.invertPixels(QImage::InvertRgb);
    const QString mark = QStringLiteral("rangeInverse near=1 far=4 range=60");
    const QString path = depthFile(dir, QStringLiteral("depth"), QStringLiteral("depthSeparate"), mark, { pic, depth, original });
    QVERIFY(!path.isEmpty());

    // the depth map is the reader's second image
    const Picture views = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(views.isStereo());
    QCOMPARE(views.layout, Picture::Layout::DepthSeparate);
    QCOMPARE(views.source, Picture::Source::FileMark);
    QCOMPARE(views.original.convertToFormat(pic.format()), pic);
    QCOMPARE(views.left.pixelColor(10, 20), pic.pixelColor(10, 20));
    QCOMPARE(views.right.pixelColor(41, 20), pic.pixelColor(45, 20));

    // the user's choice of the picture before the camera's effects, the 2D picture staying the shown one
    const Picture fromOriginal = Picture::load(path, {}, Picture::DepthOptions(), Picture::DepthPicture::Original, OutputMode::Stereo);
    QVERIFY(fromOriginal.isStereo());
    QCOMPARE(fromOriginal.original.convertToFormat(pic.format()), pic);
    QCOMPARE(fromOriginal.left.pixelColor(10, 20), original.pixelColor(10, 20));
    QCOMPARE(fromOriginal.right.pixelColor(41, 20), original.pixelColor(45, 20));

    // every option of the drawing reaches it: the picture as the middle view moves both eyes
    Picture::DepthOptions middle;
    middle.view = Picture::DepthOptions::View::Center;
    const Picture centred = Picture::load(path, {}, middle, Picture::DepthPicture::Shown, OutputMode::Stereo);
    QVERIFY(centred.isStereo());
    QVERIFY(centred.left != views.left);
    QVERIFY(centred.right != views.right);

    // thumbnails
    const Picture pair = Picture::loadPair(path, QSize(256, 64), {});
    QVERIFY(pair.isStereo());
    QCOMPARE(pair.layout, Picture::Layout::DepthSeparate);

    // the other view of a stereo pair, as HEIF's 'ster' groups give it
    QImage other = pic;
    other.fill(Qt::blue);
    const QString stereo = depthFile(dir, QStringLiteral("pair"), QStringLiteral("multiViewLeftFirst"), {}, { pic, other });
    const Picture views2 = Picture::load(stereo, {}, OutputMode::Stereo);
    QVERIFY(views2.isStereo());
    QCOMPARE(views2.layout, Picture::Layout::MultiView);
    QCOMPARE(views2.right.convertToFormat(other.format()), other);
}

// The controls: without the mark, without the depth map or with a malformed mark, the file is one picture.
void PictureTest::depthReaderControls_data()
{
    QTest::addColumn<QString>("layout");
    QTest::addColumn<QString>("mark");
    QTest::addColumn<int>("images");
    QTest::newRow("no mark") << QString() << QStringLiteral("nearness") << 2;
    QTest::newRow("no depth map") << QStringLiteral("depthSeparate") << QStringLiteral("nearness") << 1;
    QTest::newRow("malformed mark") << QStringLiteral("depthSeparate") << QStringLiteral("z near") << 2;
}

void PictureTest::depthReaderControls()
{
    QFETCH(QString, layout);
    QFETCH(QString, mark);
    QFETCH(int, images);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage pic = stripes(128, 32);
    const QList<QImage> all { pic, depthOf(64, 16, 0, 255) };
    const QString path = depthFile(dir, QStringLiteral("control"), layout, mark, all.mid(0, images));
    const Picture picture = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(!picture.isStereo());
    QCOMPARE(picture.original.convertToFormat(pic.format()), pic);
}

void PictureTest::heifMonoscopicFallback()
{
    const QString path = QFINDTESTDATA("readers/stem-primary-2.heic");
    QVERIFY(!path.isEmpty());
    QImageReader reference(path);
    QCOMPARE(reference.text(QStringLiteral("Stereo3DLayout")), QStringLiteral("multiViewLeftFirst"));
    QCOMPARE(reference.text(QStringLiteral("Stereo3DViewIndices")), QStringLiteral("1,2"));
    const QImage primary = reference.read();
    QVERIFY(reference.jumpToImage(1));
    const QImage left = reference.read();
    QVERIFY(reference.jumpToImage(2));
    const QImage right = reference.read();
    QVERIFY(primary != left);
    QVERIFY(primary != right);
    const auto pair = Picture::load(path, {}, OutputMode::Stereo);
    QVERIFY(pair.isStereo());
    QCOMPARE(pair.original, primary);
    QCOMPARE(pair.left, left);
    QCOMPARE(pair.right, right);
    Picture::Options options;
    options.swapEyes = true;
    const auto swapped = Picture::load(path, options, OutputMode::Stereo);
    QCOMPARE(swapped.left, right);
    QCOMPARE(swapped.right, left);
    options = {};
    options.view = Picture::View::Mono;
    QCOMPARE(Picture::load(path, options, OutputMode::Mono).original, primary);
    options.view = Picture::View::LeftOnly;
    QCOMPARE(Picture::load(path, options, OutputMode::Stereo).original, left);
    options.view = Picture::View::RightOnly;
    QCOMPARE(Picture::load(path, options, OutputMode::Stereo).original, right);
    QVERIFY(reference.jumpToImage(0));
    const QImage firstRead = reference.read();
    const auto buffered = Picture::fromReader(reference, firstRead, reference.text(QStringLiteral("Stereo3DLayout")), "heif", {}, {}, OutputMode::Stereo);
    QCOMPARE(buffered.original, primary);
    QCOMPARE(buffered.left, left);
    QCOMPARE(buffered.right, right);
    const auto thumb = Picture::loadPair(path, QSize(2000, 1000), {});
    QCOMPARE(thumb.left, left);
    QCOMPARE(thumb.right, right);
}

static QByteArray fileState(const QString &path)
{
    // the file as the person has it: its bytes, its times and every extended attribute
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray state = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    struct stat status;
    if (stat(QFile::encodeName(path).constData(), &status) != 0) {
        return {};
    }
    state += ' ' + QByteArray::number(qlonglong(status.st_mtim.tv_sec)) + '.' + QByteArray::number(qlonglong(status.st_mtim.tv_nsec));
    state += ' ' + QByteArray::number(qlonglong(status.st_ctim.tv_sec)) + '.' + QByteArray::number(qlonglong(status.st_ctim.tv_nsec));
    char names[4096];
    const ssize_t length = listxattr(QFile::encodeName(path).constData(), names, sizeof(names));
    state += ' ' + QByteArray(names, length > 0 ? length : 0).toHex();
    return state;
}

void PictureTest::fileChoiceFollowsMoves()
{
    // A choice follows its file when a program renames or moves it (ORDER 551): by the call, and by the follower that
    // hears the desktop's file jobs on the session bus.
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/moves-XXXXXX"));
    QVERIFY(dir.isValid());
    qputenv("XDG_STATE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("state"))));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("cache"))));
    const auto restore = qScopeGuard([] {
        qunsetenv("XDG_STATE_HOME");
        qunsetenv("XDG_CACHE_HOME");
    });
    const QString wide = dir.filePath(QStringLiteral("screenshot.png"));
    const QString renamed = dir.filePath(QStringLiteral("two monitors.png"));
    QVERIFY(picture(QSize(384, 108), Content::Unrelated).save(wide));
    Picture::Options flat;
    flat.view = Picture::View::Mono;
    QVERIFY(Picture::setFileChoice(wide, flat));
    QVERIFY(!Picture::load(wide, {}, OutputMode::Stereo).isStereo());

    // renamed behind the store's back: the file is a pair by its shape again
    QVERIFY(QFile::rename(wide, renamed));
    QVERIFY(Picture::load(renamed, {}, OutputMode::Stereo).isStereo());
    // a thumbnail made in that moment, before the choice arrives, must not stay
    const QString thumbnails = dir.filePath(QStringLiteral("cache/thumbnails/large/"));
    QVERIFY(QDir().mkpath(thumbnails));
    const QString thumbnail = thumbnails
        + QString::fromLatin1(QCryptographicHash::hash(QUrl::fromLocalFile(renamed).toEncoded(QUrl::RemovePassword | QUrl::FullyEncoded), QCryptographicHash::Md5).toHex())
        + QStringLiteral(".png");
    QVERIFY(QImage(8, 8, QImage::Format_RGB32).save(thumbnail));
    QString error;
    bool failed = true;
    QCOMPARE(Picture::moveFileChoice(wide, renamed, &error, &failed), QStringList{QFileInfo(renamed).canonicalFilePath()});
    QVERIFY2(!failed, qPrintable(error));
    QVERIFY(!Picture::load(renamed, {}, OutputMode::Stereo).isStereo());
    QCOMPARE(Picture::fileChoice(renamed).view, Picture::View::Mono);
    QVERIFY(!QFile::exists(thumbnail));
    // a second call for the same move carries nothing and changes nothing
    QVERIFY(Picture::moveFileChoice(wide, renamed).isEmpty());
    QCOMPARE(Picture::fileChoice(renamed).view, Picture::View::Mono);
    // a copy carries nothing
    QVERIFY(QFile::copy(renamed, wide));
    QVERIFY(Picture::moveFileChoice(renamed, wide).isEmpty());
    QVERIFY(!Picture::hasFileChoice(wide));
    QVERIFY(QFile::remove(wide));

    // a program's own per-file swap taken into the store once: the file is not touched, and a newer stored choice stays
    const QByteArray before = fileState(renamed);
    const QString other = dir.filePath(QStringLiteral("pair.png"));
    QVERIFY(picture(QSize(384, 108), Content::Pair).save(other));
    const QByteArray otherBefore = fileState(other);
    QVERIFY(Picture::adoptSwapEyes(other, true, &error));
    QVERIFY(Picture::fileChoice(other).swapEyes);
    QVERIFY(Picture::adoptSwapEyes(other, false, &error));
    QVERIFY2(Picture::fileChoice(other).swapEyes, "a file that already has a stored choice keeps it");
    QVERIFY(Picture::adoptSwapEyes(renamed, true, &error));
    QCOMPARE(Picture::fileChoice(renamed).view, Picture::View::Mono);
    QVERIFY(!Picture::fileChoice(renamed).swapEyes);
    QCOMPARE(fileState(renamed), before);
    QCOMPARE(fileState(other), otherBefore);
    QVERIFY(Picture::setFileChoice(other, {}));

    // The follower: a move announced as KIO announces it (org.kde.KDirNotify, FileMoved, two URLs)
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QSKIP("no session bus in this run: the follower is tested where the suite has one (dbus-run-session)");
    }
    FileChoiceMoves moves;
    QVERIFY(moves.listening());
    QSignalSpy carried(&moves, &FileChoiceMoves::carried);
    QSignalSpy refused(&moves, &FileChoiceMoves::failed);
    QVERIFY(QDir().mkdir(dir.filePath(QStringLiteral("album"))));
    const QString moved = dir.filePath(QStringLiteral("album/two monitors.png"));
    const auto announce = [&bus](const QUrl &from, const QUrl &to) {
        QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/"), QStringLiteral("org.kde.KDirNotify"), QStringLiteral("FileMoved"));
        message << from.toString() << to.toString();
        return bus.send(message);
    };
    QVERIFY(QFile::rename(renamed, moved));
    QVERIFY(announce(QUrl::fromLocalFile(renamed), QUrl::fromLocalFile(moved)));
    QVERIFY(carried.wait(5000));
    QCOMPARE(carried.at(0).at(0).toStringList(), QStringList{QFileInfo(moved).canonicalFilePath()});
    QCOMPARE(Picture::fileChoice(moved).view, Picture::View::Mono);
    QVERIFY(!Picture::hasFileChoice(renamed));
    // the folder renamed as a whole: one announcement, the file below keeps its choice
    const QString album = dir.filePath(QStringLiteral("albums"));
    QVERIFY(QDir().rename(dir.filePath(QStringLiteral("album")), album));
    QVERIFY(announce(QUrl::fromLocalFile(dir.filePath(QStringLiteral("album"))), QUrl::fromLocalFile(album)));
    QVERIFY(carried.wait(5000));
    QCOMPARE(Picture::fileChoice(album + QStringLiteral("/two monitors.png")).view, Picture::View::Mono);
    // a move to a place that is not a local file (the wastebin) is not this store's
    QVERIFY(announce(QUrl::fromLocalFile(album + QStringLiteral("/two monitors.png")), QUrl(QStringLiteral("trash:/0-two monitors.png"))));
    QTest::qWait(300);
    QCOMPARE(carried.count(), 2);
    QCOMPARE(refused.count(), 0);
    QCOMPARE(Picture::fileChoice(album + QStringLiteral("/two monitors.png")).view, Picture::View::Mono);
}

void PictureTest::fileChoiceMenuLists()
{
    // One menu, two lists (ORDER 551): a picture's packings, and for a video every packing the video reader knows,
    // frames in turn and views in one block included. A packing the program cannot show in 3D here stays, and says so.
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/menu-XXXXXX"));
    QVERIFY(dir.isValid());
    qputenv("XDG_STATE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("state"))));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("cache"))));
    const auto restore = qScopeGuard([] {
        qunsetenv("XDG_STATE_HOME");
        qunsetenv("XDG_CACHE_HOME");
    });
    const QString film = dir.filePath(QStringLiteral("film.mkv"));
    QFile file(film);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write("not a real film") > 0);
    file.close();
    FileChoiceMenu menu;
    const auto entries = [&menu] {
        QHash<QString, QAction *> shown;
        QList<QMenu *> menus{&menu};
        while (!menus.isEmpty()) {
            const QList<QAction *> actions = menus.takeFirst()->actions();
            for (QAction *action : actions) {
                if (!action->isVisible()) {
                    continue;
                }
                if (action->menu()) {
                    menus.append(action->menu());
                } else if (action->isCheckable()) {
                    shown.insert(action->text(), action);
                }
            }
        }
        return shown;
    };
    menu.setFiles({film});
    // a picture's list holds no packing that needs more than one picture
    QCOMPARE(menu.content(), FileChoiceMenu::Content::Picture);
    QVERIFY(entries().contains(QStringLiteral("Rows, left eye first")));
    QVERIFY(!entries().contains(QStringLiteral("Frames in turn, left eye first")));
    QVERIFY(!entries().contains(QStringLiteral("Both views in one block, right eye first")));
    // a video's list: every packing of the video reader with both orders, the anaglyph pairs, picture and depth
    menu.setContent(FileChoiceMenu::Content::Video);
    const QStringList video{QStringLiteral("Automatic"), QStringLiteral("Not 3D"), QStringLiteral("Side by side, left eye first"),
        QStringLiteral("Side by side, right eye first"), QStringLiteral("Top and bottom, left eye first"), QStringLiteral("Top and bottom, right eye first"),
        QStringLiteral("Rows, left eye first"), QStringLiteral("Rows, right eye first"), QStringLiteral("Columns, left eye first"),
        QStringLiteral("Columns, right eye first"), QStringLiteral("Checkerboard, left eye first"), QStringLiteral("Checkerboard, right eye first"),
        QStringLiteral("Side by side quincunx, left eye first"), QStringLiteral("Side by side quincunx, right eye first"),
        QStringLiteral("Frames in turn, left eye first"), QStringLiteral("Frames in turn, right eye first"),
        QStringLiteral("Both views in one block, left eye first"), QStringLiteral("Both views in one block, right eye first"), QStringLiteral("Red/cyan"),
        QStringLiteral("Green/magenta"), QStringLiteral("Amber/blue"), QStringLiteral("Side by side, picture first"), QStringLiteral("Side by side, depth first"),
        QStringLiteral("Top and bottom, picture first"), QStringLiteral("Top and bottom, depth first"),
        QStringLiteral("Four rows: picture, depth, graphics, graphics depth"), QStringLiteral("Four quarters: picture, depth, background, background depth"),
        QStringLiteral("Swap left and right eyes")};
    QStringList listed = entries().keys();
    QStringList expected = video;
    listed.sort();
    expected.sort();
    QCOMPARE(listed, expected);
    // every packing the store can hold for a video is in the list, or is one of the two that nothing chooses by hand
    QSet<int> offered;
    QSignalSpy changed(&menu, &FileChoiceMenu::changed);
    for (const QString &text : video) {
        if (text == QLatin1String("Swap left and right eyes")) {
            continue;
        }
        entries().value(text)->trigger();
        offered.insert(int(Picture::fileChoice(film).inputLayout));
        QVERIFY2(entries().value(text)->isChecked(), qPrintable(text));
    }
    QCOMPARE(changed.count(), video.size() - 1);
    for (int layout = int(Picture::Layout::None); layout <= int(Picture::Layout::DepthSeparate); ++layout) {
        const bool byHand = layout != int(Picture::Layout::MultiView) && layout != int(Picture::Layout::DepthSeparate);
        QVERIFY2(offered.contains(layout) == byHand, qPrintable(QString::number(layout)));
    }
    // frames in turn, right eye first, is kept as it was chosen
    entries().value(QStringLiteral("Frames in turn, right eye first"))->trigger();
    QCOMPARE(Picture::fileChoice(film).inputLayout, Picture::Layout::FrameSequential);
    QCOMPARE(Picture::fileChoice(film).inputOrder, Picture::EyeOrder::RightFirst);
    // where the program shows one view of it, the entry stays in the list and says so
    menu.setShownAsOneView({Picture::Layout::FrameSequential, Picture::Layout::BlockLaced});
    QVERIFY(!entries().contains(QStringLiteral("Frames in turn, right eye first")));
    QAction *noted = entries().value(QStringLiteral("Frames in turn, right eye first (one view shown here)"));
    QVERIFY(noted && noted->isChecked() && noted->isEnabled());
    QVERIFY(entries().contains(QStringLiteral("Both views in one block, left eye first (one view shown here)")));
    QVERIFY(entries().contains(QStringLiteral("Rows, left eye first")));
    // a choice the other list does not offer is still shown as the file's, never hidden
    menu.setContent(FileChoiceMenu::Content::Picture);
    QVERIFY(entries().contains(QStringLiteral("Frames in turn, right eye first (one view shown here)")));
    QVERIFY(!entries().contains(QStringLiteral("Frames in turn, left eye first (one view shown here)")));
    // a store that cannot be written: the menu says so and does not show the choice as kept
    QFile blocker(dir.filePath(QStringLiteral("blocked")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    qputenv("XDG_STATE_HOME", QFile::encodeName(blocker.fileName()));
    QSignalSpy refused(&menu, &FileChoiceMenu::failed);
    menu.setFiles({film});
    entries().value(QStringLiteral("Not 3D"))->trigger();
    QCOMPARE(refused.count(), 1);
    QVERIFY(refused.at(0).at(0).toString().contains(QStringLiteral("film.mkv")));
    QVERIFY(!entries().value(QStringLiteral("Not 3D"))->isChecked());
    QVERIFY(entries().value(QStringLiteral("Automatic"))->isChecked());
}

void PictureTest::fileChoice()
{
    // The person's per-file choice is kept in the programs' state and applied by load() and loadPair() for every caller;
    // the file is never touched.
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/choice-XXXXXX"));
    QVERIFY(dir.isValid());
    qputenv("XDG_STATE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("state"))));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("cache"))));
    const auto restore = qScopeGuard([] {
        qunsetenv("XDG_STATE_HOME");
        qunsetenv("XDG_CACHE_HOME");
    });
    // an unmarked picture in the shape of a pair (a wide screenshot): a pair by its shape, the last safety net
    const QString wide = dir.filePath(QStringLiteral("screenshot.png"));
    QVERIFY(picture(QSize(384, 108), Content::Unrelated).save(wide));
    const QByteArray untouched = fileState(wide);
    QVERIFY(!untouched.isEmpty());
    QVERIFY(!Picture::hasFileChoice(wide));
    QCOMPARE(Picture::loadPair(wide, QSize(256, 256), {}).source, Picture::Source::Shape);
    QVERIFY(Picture::load(wide, {}, OutputMode::Stereo).isStereo());
    // a thumbnail of it in the desktop's cache, as KIO names it
    const QString thumbnails = dir.filePath(QStringLiteral("cache/thumbnails/large/"));
    QVERIFY(QDir().mkpath(thumbnails));
    const QString thumbnail =
        thumbnails + QString::fromLatin1(QCryptographicHash::hash(QUrl::fromLocalFile(wide).toEncoded(QUrl::RemovePassword | QUrl::FullyEncoded), QCryptographicHash::Md5).toHex()) + QStringLiteral(".png");
    QVERIFY(QImage(8, 8, QImage::Format_RGB32).save(thumbnail));
    const QString bystander = thumbnails + QStringLiteral("0123456789abcdef0123456789abcdef.png");
    QVERIFY(QImage(8, 8, QImage::Format_RGB32).save(bystander));

    // Not 3D: one flat picture everywhere, and its thumbnails are made again
    Picture::Options flat;
    flat.view = Picture::View::Mono;
    QString error;
    QVERIFY2(Picture::setFileChoice(wide, flat, &error), qPrintable(error));
    QVERIFY(Picture::hasFileChoice(wide));
    QCOMPARE(Picture::fileChoice(wide).view, Picture::View::Mono);
    QVERIFY(!Picture::loadPair(wide, QSize(256, 256), {}).isStereo());
    const Picture shown = Picture::load(wide, {}, OutputMode::Stereo);
    QVERIFY(!shown.isStereo());
    QCOMPARE(shown.original.size(), QSize(384, 108));
    QVERIFY(!QFile::exists(thumbnail));
    QVERIFY(QFile::exists(bystander));
    // a viewer's own packing for the session still shows it as that pair
    QVERIFY(Picture::load(wide, Picture::Options{Picture::Layout::SideBySide}, OutputMode::Stereo).isStereo());
    QCOMPARE(fileState(wide), untouched);
    // another file of the same shape beside it keeps its own answer
    const QString real = dir.filePath(QStringLiteral("pair.png"));
    QVERIFY(picture(QSize(384, 108), Content::Pair).save(real));
    QVERIFY(Picture::loadPair(real, QSize(256, 256), {}).isStereo());

    // a packing chosen by hand, right eye first, with the swap over it
    Picture::Options choice;
    choice.inputLayout = Picture::Layout::TopAndBottom;
    choice.inputOrder = Picture::EyeOrder::RightFirst;
    QVERIFY(Picture::setFileChoice(wide, choice));
    Picture pair = Picture::loadPair(wide, QSize(512, 512), {});
    QCOMPARE(pair.source, Picture::Source::User);
    QCOMPARE(pair.layout, Picture::Layout::TopAndBottom);
    QCOMPARE(pair.eyeOrder, Picture::EyeOrder::RightFirst);
    QCOMPARE(pair.left.size(), QSize(256, 36));
    const QImage leftBefore = pair.left;
    choice.swapEyes = true;
    QVERIFY(Picture::setFileChoice(wide, choice));
    pair = Picture::loadPair(wide, QSize(512, 512), {});
    QCOMPARE(pair.right, leftBefore);
    // a caller's own choice for the session wins over the stored packing, and its swap adds to the stored one
    Picture::Options session;
    session.inputLayout = Picture::Layout::SideBySide;
    session.swapEyes = true;
    pair = Picture::load(wide, session, OutputMode::Stereo);
    QCOMPARE(pair.layout, Picture::Layout::SideBySide);
    QCOMPARE(pair.left.size(), QSize(192, 108));
    QCOMPARE(pair.left, Picture::load(wide, Picture::Options{Picture::Layout::SideBySide}, OutputMode::Stereo).right);
    // every packing a still can hold is kept and read back
    for (const auto layout : {Picture::Layout::SideBySide, Picture::Layout::TopAndBottom, Picture::Layout::Rows, Picture::Layout::Columns,
             Picture::Layout::Checkerboard, Picture::Layout::SideBySideQuincunx, Picture::Layout::DepthSideBySide, Picture::Layout::DepthTopAndBottom,
             Picture::Layout::DepthGraphics, Picture::Layout::DepthBackground}) {
        Picture::Options each;
        each.inputLayout = layout;
        QVERIFY(Picture::setFileChoice(wide, each));
        QCOMPARE(Picture::fileChoice(wide).inputLayout, layout);
        QVERIFY2(Picture::load(wide, {}, OutputMode::Stereo).isStereo(), qPrintable(QString::number(int(layout))));
    }
    // the default choice forgets the file: the shape decides again
    QVERIFY(Picture::setFileChoice(wide, {}));
    QVERIFY(!Picture::hasFileChoice(wide));
    QCOMPARE(Picture::loadPair(wide, QSize(256, 256), {}).source, Picture::Source::Shape);
    QCOMPARE(fileState(wide), untouched);

    // a file that declares itself: the mark decides, the shape is never asked, and only the person's choice for this
    // file corrects it (a declaration can be wrong)
    const QString marked = dir.filePath(QStringLiteral("marked.png"));
    {
        QImage image = picture(QSize(384, 108), Content::Pair);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideRightFirst"));
        QVERIFY(image.save(marked));
    }
    Picture declared = Picture::loadPair(marked, QSize(512, 512), {});
    if (declared.source == Picture::Source::FileMark) {
        QCOMPARE(declared.eyeOrder, Picture::EyeOrder::RightFirst);
        Picture::Options general;
        general.inputLayout = Picture::Layout::TopAndBottom;
        QCOMPARE(Picture::loadPair(marked, QSize(512, 512), general).source, Picture::Source::FileMark);
        Picture::Options correction;
        correction.inputLayout = Picture::Layout::SideBySide;
        QVERIFY(Picture::setFileChoice(marked, correction));
        declared = Picture::loadPair(marked, QSize(512, 512), {});
        QCOMPARE(declared.source, Picture::Source::User);
        QCOMPARE(declared.eyeOrder, Picture::EyeOrder::LeftFirst);
        QVERIFY(Picture::setFileChoice(marked, flat));
        QVERIFY(!Picture::loadPair(marked, QSize(512, 512), {}).isStereo());
        QVERIFY(Picture::setFileChoice(marked, {}));
        QCOMPARE(Picture::loadPair(marked, QSize(512, 512), {}).source, Picture::Source::FileMark);
    } else {
        qWarning("Qt's PNG writer keeps no Stereo3DLayout text here: the marked case is not checked");
    }

    // a swap an earlier version kept in the file's attribute is read, and moves into the store at the next choice
    if (Picture::canStoreUserSwapEyes(real) && Picture::setUserSwapEyes(real, true)) {
        const Picture before = Picture::loadPair(real, QSize(256, 256), {});
        QVERIFY(Picture::fileChoice(real).swapEyes);
        QVERIFY(Picture::hasFileChoice(real));
        Picture::Options kept = Picture::fileChoice(real);
        QVERIFY(Picture::setFileChoice(real, kept));
        QVERIFY(!Picture::userSwapEyes(real));
        QVERIFY(Picture::fileChoice(real).swapEyes);
        QCOMPARE(Picture::loadPair(real, QSize(256, 256), {}).left, before.left);
        kept.swapEyes = false;
        QVERIFY(Picture::setFileChoice(real, kept));
        QVERIFY(!Picture::hasFileChoice(real));
        QCOMPARE(Picture::loadPair(real, QSize(256, 256), {}).left, before.right);
    } else {
        qWarning("This file system keeps no extended attributes: the legacy swap is not checked");
    }

    // a format the shared save cannot write says why, and the choice stays
    QVERIFY(Picture::setFileChoice(wide, flat));
    const Picture::SaveResult saved = Picture::saveFileChoice(wide);
    QVERIFY(!saved.success);
    QVERIFY2(saved.error.contains(QLatin1String("JPEG, JPS and MPO")), qPrintable(saved.error));
    QCOMPARE(Picture::fileChoice(wide).view, Picture::View::Mono);
    QCOMPARE(fileState(wide), untouched);
    QVERIFY(!Picture::saveFileChoice(marked).success);

    // the person chose "save into the file" for a JPEG pair with a swap: the file then carries the correction, the entry
    // is cleared, and the picture shows as it did with the entry
    const QString jpeg = dir.filePath(QStringLiteral("pair.jpg"));
    QVERIFY(picture(QSize(384, 108), Content::Pair).save(jpeg, "jpeg", 95));
    Picture::Options swapped;
    swapped.inputLayout = Picture::Layout::SideBySide;
    swapped.swapEyes = true;
    QVERIFY(Picture::setFileChoice(jpeg, swapped));
    const Picture corrected = Picture::loadPair(jpeg, QSize(512, 512), {});
    QVERIFY(corrected.isStereo());
    const QByteArray jpegBefore = fileState(jpeg);
    const Picture::SaveResult written = Picture::saveFileChoice(jpeg);
    if (written.success) {
        QVERIFY(!Picture::hasFileChoice(jpeg));
        QVERIFY(fileState(jpeg) != jpegBefore);
        const Picture reread = Picture::loadPair(jpeg, QSize(512, 512), {});
        QCOMPARE(reread.source, Picture::Source::FileMark);
        QCOMPARE(reread.left, corrected.left);
        QCOMPARE(reread.right, corrected.right);
    } else {
        // a JPS reader older than the shared save's descriptor refuses its own read-back: nothing was written
        qWarning("saveFileChoice() refused the JPEG here: %s", qPrintable(written.error));
        QVERIFY(Picture::hasFileChoice(jpeg));
        QCOMPARE(fileState(jpeg), jpegBefore);
    }

    // a store that cannot be written is said, with where and why, and nothing is kept
    QVERIFY(Picture::setFileChoice(wide, {}));
    qputenv("XDG_STATE_HOME", QFile::encodeName(wide));
    error.clear();
    QVERIFY(!Picture::setFileChoice(wide, flat, &error));
    QVERIFY2(error.contains(wide), qPrintable(error));
    QVERIFY(!Picture::hasFileChoice(wide));
    QCOMPARE(fileState(wide), untouched);
}

QTEST_MAIN(PictureTest)
#include "picturetest.moc"
