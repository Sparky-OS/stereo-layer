// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include "render.h"
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
using namespace Stereo;
class PictureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void marksAndChoices();
    void mpoFallback();
    void padding();
    void outputPolicy();
    void viewControls();
    void detector_data();
    void detector();
    void geometry();
    void panorama();
    void shape_data();
    void shape();
    void readerIntegration_data();
    void readerIntegration();
    void loadPair_data();
    void loadPair();
    void loadPairPng();
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
    for (const QString &layout : { QStringLiteral("mono"), QStringLiteral("sideBySideLeftFirst"), QStringLiteral("topBottomRightFirst") }) {
        const auto picture = Picture::fromImages(image, {}, layout, "jps", {}, options, OutputMode::Stereo);
        QCOMPARE(picture.source, Picture::Source::FileMark);
        QCOMPARE(picture.isStereo(), layout != QLatin1String("mono"));
    }
    options = {};
    const auto ratio = Picture::fromImages(image, {}, {}, "png", {}, options, OutputMode::Stereo);
    QCOMPARE(ratio.source, Picture::Source::Shape);
    QCOMPARE(ratio.right.pixelColor(0, 0), QColor(Qt::blue));
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
void PictureTest::shape_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<bool>("pair");
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
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::gray);
    const auto picture = Picture::fromImages(image, {}, {}, "png", {}, {}, OutputMode::Stereo);
    QCOMPARE(picture.isStereo(), pair);
    QCOMPARE(picture.source, pair ? Picture::Source::Shape : Picture::Source::None);
    if (pair) {
        QCOMPARE(picture.layout, Picture::Layout::SideBySide);
    }
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
    // The same pixels without the chunk are a picture 3:1 wide, a pair by its shape; at 4:1 they are a panorama.
    QImage wide(1600, 400, QImage::Format_RGB32);
    wide.fill(Qt::gray);
    QVERIFY(wide.save(dir.filePath(QStringLiteral("panorama.png"))));
    QVERIFY(!Picture::loadPair(dir.filePath(QStringLiteral("panorama.png")), QSize(256, 256), {}).isStereo());
    QVERIFY(image.save(dir.filePath(QStringLiteral("shape.png"))));
    QCOMPARE(Picture::loadPair(dir.filePath(QStringLiteral("shape.png")), QSize(256, 256), {}).source, Picture::Source::Shape);
    QVERIFY(Picture().sideBySide().isNull());
}

QTEST_MAIN(PictureTest)
#include "picturetest.moc"
