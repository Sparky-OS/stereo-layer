// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include "stereo-jpeg.h"
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>
#include <QtEndian>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
using Stereo::Picture;
class PictureSaveTest : public QObject
{
    Q_OBJECT
    // Independent fixture/parser for MPF fields; no calls to the production codec.
    static bool mpoFields(QByteArray &bytes, bool convert, QList<QPair<qsizetype, bool>> &fields)
    {
        qsizetype search = 0;
        for (int image = 0; image < 2; ++image) {
            const auto id = bytes.indexOf(QByteArray("MPF\0", 4), search);
            if (id < 4)
                return false;
            const auto base = id + 4;
            search = base;
            const bool little = bytes.mid(base, 2) == "II";
            auto u16 = [&](qsizetype p) {
                return little ? qFromLittleEndian<quint16>(bytes.constData() + base + p) : qFromBigEndian<quint16>(bytes.constData() + base + p);
            };
            auto u32 = [&](qsizetype p) {
                return little ? qFromLittleEndian<quint32>(bytes.constData() + base + p) : qFromBigEndian<quint32>(bytes.constData() + base + p);
            };
            quint32 dir = u32(4);
            if (convert) {
                bytes.replace(base, 4, QByteArray::fromHex("4d4d002a"));
                qToBigEndian<quint32>(dir, bytes.data() + base + 4);
            }
            for (int chain = 0; dir && chain < 8; ++chain) {
                const auto count = u16(dir);
                const auto next = u32(dir + 2 + 12 * count);
                for (unsigned i = 0; i < count; ++i) {
                    const auto at = dir + 2 + 12 * i;
                    const auto tag = u16(at), type = u16(at + 2);
                    const auto number = u32(at + 4), value = u32(at + 8);
                    if (tag == 0xb101 || tag == 0xb204)
                        fields.append({ base + at + 8, convert ? false : little });
                    if (convert) {
                        if (tag == 0xb002)
                            for (unsigned entry = 0; entry < number / 16; ++entry) {
                                const auto pos = value + entry * 16;
                                const quint32 av = u32(pos), size = u32(pos + 4), offset = u32(pos + 8);
                                const quint16 dep1 = u16(pos + 12), dep2 = u16(pos + 14);
                                qToBigEndian<quint32>(av, bytes.data() + base + pos);
                                qToBigEndian<quint32>(size, bytes.data() + base + pos + 4);
                                qToBigEndian<quint32>(offset, bytes.data() + base + pos + 8);
                                qToBigEndian<quint16>(dep1, bytes.data() + base + pos + 12);
                                qToBigEndian<quint16>(dep2, bytes.data() + base + pos + 14);
                            }
                        qToBigEndian<quint16>(tag, bytes.data() + base + at);
                        qToBigEndian<quint16>(type, bytes.data() + base + at + 2);
                        qToBigEndian<quint32>(number, bytes.data() + base + at + 4);
                        if (type == 4 || number > 4)
                            qToBigEndian<quint32>(value, bytes.data() + base + at + 8);
                    }
                }
                if (convert) {
                    qToBigEndian<quint16>(count, bytes.data() + base + dir);
                    qToBigEndian<quint32>(next, bytes.data() + base + dir + 2 + 12 * count);
                }
                dir = next;
            }
        }
        return fields.size() == 4;
    }
private Q_SLOTS:
    void correct_data()
    {
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<bool>("bigEndian");
        QTest::addColumn<bool>("markerFill");
        QTest::newRow("jps") << QByteArray("jps") << QStringLiteral("jps") << false << false;
        QTest::newRow("jpeg-extension") << QByteArray("jps") << QStringLiteral("jpg") << false << false;
        QTest::newRow("mpo") << QByteArray("mpo") << QStringLiteral("mpo") << false << false;
        QTest::newRow("mpo-jpeg-extension") << QByteArray("mpo") << QStringLiteral("jpg") << false << false;
        QTest::newRow("mpo-big-endian") << QByteArray("mpo") << QStringLiteral("mpo") << true << false;
        QTest::newRow("jps-marker-fill") << QByteArray("jps") << QStringLiteral("jps") << false << true;
        QTest::newRow("jps-all-fields") << QByteArray("jps") << QStringLiteral("jps") << false << false;
    }
    void correct()
    {
        QFETCH(QByteArray, format);
        QFETCH(QString, suffix);
        QFETCH(bool, bigEndian);
        QFETCH(bool, markerFill);
        QTemporaryDir dir;
        QString path = dir.filePath(QStringLiteral("picture.") + suffix);
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        for (int y = 0; y < 24; ++y)
            for (int x = 32; x < 64; ++x)
                image.setPixelColor(x, y, Qt::blue);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        const bool fullFields = QByteArray(QTest::currentDataTag()) == "jps-all-fields";
        if (fullFields) {
            image.setText(QStringLiteral("Stereo3DJpsHalfHeight"), QStringLiteral("1"));
            image.setText(QStringLiteral("Stereo3DJpsHalfWidth"), QStringLiteral("1"));
            image.setText(QStringLiteral("Stereo3DJpsSeparation"), QStringLiteral("4"));
            image.setText(QStringLiteral("Stereo3DJpsComment"), QStringLiteral("Labelled views"));
        }
        QImageWriter writer(path, format);
        QVERIFY2(writer.write(image), qPrintable(writer.errorString()));
        if (bigEndian || markerFill || fullFields) {
            QFile source(path);
            QVERIFY(source.open(QIODevice::ReadOnly));
            auto bytes = source.readAll();
            source.close();
            QList<QPair<qsizetype, bool>> fields;
            if (bigEndian)
                QVERIFY(mpoFields(bytes, true, fields));
            if (fullFields) {
                const auto id = bytes.indexOf("_JPSJPS_");
                QVERIFY(id >= 4);
                const auto length = qFromBigEndian<quint16>(bytes.constData() + id - 2);
                bytes.insert(id + 14, QByteArray::fromHex("1234"));
                qToBigEndian<quint16>(6, bytes.data() + id + 8);
                qToBigEndian<quint16>(quint16(length + 2), bytes.data() + id - 2);
            }
            if (markerFill) {
                const auto id = bytes.indexOf("_JPSJPS_");
                QVERIFY(id >= 4);
                bytes.insert(id - 4, QByteArray::fromHex("ffff"));
            }
            QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(source.write(bytes), bytes.size());
            source.close();
        }
        const auto before = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(before.isStereo());
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();
        Picture::Options options;
        options.swapEyes = true;
        const auto result = Picture::saveCorrection(path, options);
        QVERIFY2(result.success, qPrintable(result.error));
        const auto after = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(after.isStereo());
        QCOMPARE(after.left.convertToFormat(QImage::Format_RGBA8888), before.right.convertToFormat(QImage::Format_RGBA8888));
        QCOMPARE(after.right.convertToFormat(QImage::Format_RGBA8888), before.left.convertToFormat(QImage::Format_RGBA8888));
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto changed = original.readAll();
        original.close();
        QVERIFY(changed != bytes);
        // Independent oracle: existing descriptor is fixed size; only the order flag or B101/B204 viewpoint/reference bytes may differ.
        QCOMPARE(changed.size(), bytes.size());
        QList<qsizetype> differences;
        for (qsizetype i = 0; i < bytes.size(); ++i)
            if (bytes[i] != changed[i])
                differences << i;
        if (format == "jps") {
            QCOMPARE(differences.size(), 1);
            QCOMPARE(differences[0], bytes.indexOf("_JPSJPS_") + 11);
        } else {
            QCOMPARE(differences.size(), 4);
            for (auto pos : differences)
                QVERIFY((uchar(bytes[pos]) == 1 && uchar(changed[pos]) == 2) || (uchar(bytes[pos]) == 2 && uchar(changed[pos]) == 1));
        }
        if (format == "mpo") {
            auto expected = bytes;
            QList<QPair<qsizetype, bool>> fields;
            QVERIFY(mpoFields(expected, false, fields));
            for (auto field : fields) {
                const auto old = field.second ? qFromLittleEndian<quint32>(bytes.constData() + field.first)
                                              : qFromBigEndian<quint32>(bytes.constData() + field.first);
                QVERIFY(old == 1 || old == 2);
                if (field.second)
                    qToLittleEndian<quint32>(3 - old, expected.data() + field.first);
                else
                    qToBigEndian<quint32>(3 - old, expected.data() + field.first);
            }
            QCOMPARE(changed, expected); // Every other byte, including every entropy-coded byte, is identical.
        }
        // An absolute correction is idempotent; clearing the caller's swap after success prevents applying it twice.
        options.inputLayout = after.layout;
        options.inputOrder = after.eyeOrder;
        options.swapEyes = false;
        QVERIFY(Picture::saveCorrection(path, options).success);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), changed);
        original.close();
        options = {};
        options.swapEyes = true;
        QVERIFY(Picture::saveCorrection(path, options).success);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes); // Relative swap is an involution, including B204 references.
    }
    void layouts_data()
    {
        QTest::addColumn<int>("layout");
        QTest::addColumn<int>("anaglyph");
        QTest::addColumn<int>("view");
        QTest::addColumn<QString>("mark");
        QTest::addColumn<bool>("left");
        for (bool left : { false, true }) {
            const QString order = left ? QStringLiteral("LeftFirst") : QStringLiteral("RightFirst");
            auto add = [&](const char *name, Picture::Layout layout, QString mark) {
                const QByteArray row = QByteArray(name) + (left ? "-left" : "-right");
                QTest::newRow(row.constData()) << int(layout) << 0 << 0 << mark + order << left;
            };
            add("sbs", Picture::Layout::SideBySide, QStringLiteral("sideBySide"));
            add("tb", Picture::Layout::TopAndBottom, QStringLiteral("topBottom"));
            add("rows", Picture::Layout::Rows, QStringLiteral("rowInterleaved"));
            QTest::newRow(left ? "anaglyph-left" : "anaglyph-right")
                << 0 << int(Picture::AnaglyphColorPair::RedCyan) << 0
                << (left ? QStringLiteral("anaglyphRedLeft") : QStringLiteral("anaglyphRedRight")) << left;
        }
        QTest::newRow("mono") << 0 << 0 << int(Picture::View::Mono) << QStringLiteral("mono") << true;
        QTest::newRow("mono-with-retained-input-options") << int(Picture::Layout::Columns) << int(Picture::AnaglyphColorPair::GreenMagenta)
                                                          << int(Picture::View::Mono) << QStringLiteral("mono") << false;
    }
    void layouts()
    {
        QFETCH(int, layout);
        QFETCH(int, anaglyph);
        QFETCH(int, view);
        QFETCH(QString, mark);
        QFETCH(bool, left);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("picture.jpg"));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::green);
        // Existing declared SBS must yield to an explicit choice of rows/TB/anaglyph/mono.
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, "jps");
        QVERIFY(writer.write(image));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        Picture::Options options;
        options.inputLayout = Picture::Layout(layout);
        options.anaglyph = Picture::AnaglyphColorPair(anaglyph);
        options.view = Picture::View(view);
        options.inputOrder = left ? Picture::EyeOrder::LeftFirst : Picture::EyeOrder::RightFirst;
        auto result = Picture::saveCorrection(path, options);
        QVERIFY2(result.success, qPrintable(result.error));
        QImageReader reader(path, "jps");
        QCOMPARE(reader.text(QStringLiteral("Stereo3DLayout")), mark);
        QVERIFY(!reader.read().isNull());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto after = file.readAll();
        const auto a = before.indexOf("_JPSJPS_"), b = after.indexOf("_JPSJPS_");
        QCOMPARE(a, b);
        QCOMPARE(before.left(a + 10), after.left(b + 10));
        QCOMPARE(before.mid(a + 14), after.mid(b + 14));
    }
    void addDescriptor_data()
    {
        QTest::addColumn<bool>("progressive");
        QTest::newRow("sequential") << false;
        QTest::newRow("progressive") << true;
    }
    void addDescriptor()
    {
        QFETCH(bool, progressive);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("plain.jpg"));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::yellow);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QImageWriter writer(&buffer, "jpeg");
        writer.setProgressiveScanWrite(progressive);
        QVERIFY(writer.write(image));
        // Opaque APP1 data with fake marker/descriptor signatures must survive and must not be parsed as headers.
        QByteArray opaque = QByteArray::fromHex("ffe10020") + QByteArray("opaque _JPSJPS_ MPF\0", 19) + QByteArray::fromHex("ffdaffd9ff00");
        opaque = QByteArray::fromHex("ffe10000") + opaque.mid(4);
        qToBigEndian<quint16>(quint16(opaque.size() - 2), opaque.data() + 2);
        bytes.insert(2, opaque);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        Picture::Options options;
        options.inputLayout = Picture::Layout::SideBySide;
        auto result = Picture::saveCorrection(path, options);
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto after = file.readAll();
        file.close();
        // Independent header walk: remove precisely the new JPS APP3, compare every remaining byte including all scans.
        qsizetype pos = 2;
        bool found = false;
        while (pos + 4 < after.size()) {
            const unsigned marker = uchar(after[pos + 1]);
            const auto length = qFromBigEndian<quint16>(after.constData() + pos + 2);
            if (marker == 0xda)
                break;
            if (marker == 0xe3 && after.mid(pos + 4, 8) == "_JPSJPS_") {
                after.remove(pos, length + 2);
                found = true;
                break;
            }
            pos += length + 2;
        }
        QVERIFY(found);
        QCOMPARE(after, bytes);
        QVERIFY(Picture::load(path, {}, Stereo::OutputMode::Stereo).isStereo());
    }
    void storedChoice_data()
    {
        QTest::addColumn<bool>("storedSwap");
        QTest::addColumn<bool>("callerSwap");
        QTest::addColumn<bool>("fileSwapped");
        // the file is saved as the caller shows it, which is its own option with the stored choice (ORDER 551)
        QTest::newRow("another program saves with no option of its own") << true << false << true;
        QTest::newRow("another program swaps back what the store swaps") << true << true << false;
        QTest::newRow("no stored choice, the caller's own swap") << false << true << true;
    }
    void storedChoice()
    {
        // A choice kept for the file in the shared store, and a program that calls the shared save itself: the save
        // takes the stored choice with the caller's and clears it, so no program applies it a second time.
        QFETCH(bool, storedSwap);
        QFETCH(bool, callerSwap);
        QFETCH(bool, fileSwapped);
        QTemporaryDir dir;
        qputenv("XDG_STATE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("state"))));
        qputenv("XDG_CACHE_HOME", QFile::encodeName(dir.filePath(QStringLiteral("cache"))));
        const auto restore = qScopeGuard([] {
            qunsetenv("XDG_STATE_HOME");
            qunsetenv("XDG_CACHE_HOME");
        });
        const QString path = dir.filePath(QStringLiteral("picture.jps"));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        for (int y = 0; y < 24; ++y)
            for (int x = 32; x < 64; ++x)
                image.setPixelColor(x, y, Qt::blue);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, "jps");
        QVERIFY2(writer.write(image), qPrintable(writer.errorString()));
        const auto asStored = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(asStored.isStereo());
        Picture::Options kept;
        kept.swapEyes = storedSwap;
        QVERIFY(Picture::setFileChoice(path, kept));
        QCOMPARE(Picture::hasFileChoice(path), storedSwap);
        Picture::Options own;
        own.swapEyes = callerSwap;
        const auto shown = Picture::load(path, own, Stereo::OutputMode::Stereo);
        QVERIFY(shown.isStereo());
        const auto result = Picture::saveCorrection(path, own);
        QVERIFY2(result.success, qPrintable(result.error));
        // the stored choice is unmarked again, for every program
        QVERIFY(!Picture::hasFileChoice(path));
        // and the file alone now shows what the caller showed
        const auto after = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(after.isStereo());
        QCOMPARE(after.left.convertToFormat(QImage::Format_RGBA8888), shown.left.convertToFormat(QImage::Format_RGBA8888));
        QCOMPARE(after.right.convertToFormat(QImage::Format_RGBA8888), shown.right.convertToFormat(QImage::Format_RGBA8888));
        const auto &first = fileSwapped ? asStored.right : asStored.left;
        QCOMPARE(after.left.convertToFormat(QImage::Format_RGBA8888), first.convertToFormat(QImage::Format_RGBA8888));
        // a refused save keeps the stored choice: a packing JPS cannot describe
        kept.swapEyes = false;
        kept.inputLayout = Picture::Layout::Columns;
        QVERIFY(Picture::setFileChoice(path, kept));
        const auto refused = Picture::saveCorrection(path, {});
        QVERIFY(!refused.success);
        QVERIFY(!refused.error.isEmpty());
        QCOMPARE(Picture::fileChoice(path).inputLayout, Picture::Layout::Columns);
        // and the menu's own save goes the same way
        kept.inputLayout = Picture::Layout::TopAndBottom;
        kept.inputOrder = Picture::EyeOrder::RightFirst;
        QVERIFY(Picture::setFileChoice(path, kept));
        const auto topBottom = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        const auto saved = Picture::saveFileChoice(path);
        QVERIFY2(saved.success, qPrintable(saved.error));
        QVERIFY(!Picture::hasFileChoice(path));
        const auto reread = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QCOMPARE(reread.layout, Picture::Layout::TopAndBottom);
        QCOMPARE(reread.eyeOrder, Picture::EyeOrder::RightFirst);
        QCOMPARE(reread.left.convertToFormat(QImage::Format_RGBA8888), topBottom.left.convertToFormat(QImage::Format_RGBA8888));
    }
    void malformed_data()
    {
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<int>("damage");
        for (int i = 0; i < 10; ++i)
            QTest::newRow(qPrintable(QStringLiteral("jps-%1").arg(i))) << QByteArray("jps") << i;
        for (int i = 0; i < 9; ++i)
            QTest::newRow(qPrintable(QStringLiteral("mpo-%1").arg(i))) << QByteArray("mpo") << i;
    }
    void malformed()
    {
        QFETCH(QByteArray, format);
        QFETCH(int, damage);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("picture.") + QString::fromLatin1(format));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, format);
        QVERIFY(writer.write(image));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray bytes = file.readAll();
        file.close();
        Picture::Options options;
        options.swapEyes = true;
        if (damage == 0)
            bytes.chop(1);
        else if (damage == 1)
            bytes[4] = char(0xff), bytes[5] = char(0xff);
        else if (format == "jps") {
            const auto id = bytes.indexOf("_JPSJPS_");
            QVERIFY(id >= 4);
            if (damage == 2)
                bytes[id + 8] = 0, bytes[id + 9] = 3; // descriptor too short
            if (damage == 3)
                bytes[id + 15] = char(0xff); // comment overrun
            if (damage == 4)
                bytes.insert(id - 4, bytes.mid(id - 4, 20)); // ambiguous duplicate
            if (damage == 5)
                bytes[id + 11] = char(0x84); // reserved flag
            if (damage == 6)
                bytes.append(bytes); // An additional JPEG needs an MPF index, not an opaque trailer.
            if (damage == 7)
                options.inputLayout = Picture::Layout::Columns;
            if (damage == 8)
                options.inputLayout = Picture::Layout::DepthSideBySide;
            if (damage == 9) {
                auto sof = bytes.indexOf(QByteArray::fromHex("ffc0"));
                QVERIFY(sof > 0);
                bytes[sof + 1] = char(0xc3);
            }
        } else {
            const auto base = bytes.indexOf(QByteArray("MPF\0", 4)) + 4;
            QVERIFY(base > 4);
            // The fixture's little-endian TIFF offsets are independently specified by its index directory.
            QDataStream fields(bytes);
            fields.setByteOrder(QDataStream::LittleEndian);
            QCOMPARE(fields.skipRawData(base + 4), base + 4);
            quint32 ifd = 0;
            fields >> ifd;
            QCOMPARE(fields.status(), QDataStream::Ok);
            auto put = [&](qsizetype offset, quint32 value) { qToLittleEndian<quint32>(value, bytes.data() + base + offset); };
            if (damage == 2)
                put(ifd + 2 + 12 * 3, ifd); // cyclic next-IFD
            if (damage == 3)
                put(ifd + 2 + 24 + 8, 0xfffffff0); // MPEntry array outside APP2
            QVERIFY(fields.device()->seek(base + ifd + 2 + 24 + 8));
            quint32 entry = 0;
            fields >> entry;
            QCOMPARE(fields.status(), QDataStream::Ok);
            if (damage == 4)
                put(entry + 24, 1); // overlap / wrong second SOI
            if (damage == 5)
                put(entry + 16, 0x050000); // gain map, not disparity
            if (damage == 6)
                put(entry + 4, 1); // primary size wrong
            if (damage == 7)
                options.inputLayout = Picture::Layout::SideBySide;
            if (damage == 8)
                put(64, 0xb204); // invalid ordinal or field structure
        }
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const auto result = Picture::saveCorrection(path, options);
        QVERIFY2(!result.success, qPrintable(QStringLiteral("Unexpected successful save, damage %1").arg(damage)));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 1);
    }
    void attributes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("picture.jps"));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, "jps");
        QVERIFY(writer.write(image));
        QVERIFY(Picture::setUserSwapEyes(path, true));
        const auto name = QFile::encodeName(path);
        QCOMPARE(setxattr(name.constData(), "user.test-preserved", "test", 4, 0), 0);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
        auto result = Picture::saveCorrection(path, {});
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(!Picture::userSwapEyes(path));
        char value[4];
        QCOMPARE(getxattr(name.constData(), "user.test-preserved", value, 4), 4);
        QCOMPARE(QByteArray(value, 4), QByteArray("test"));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        QCOMPARE(Picture::load(path, {}, Stereo::OutputMode::Stereo).eyeOrder, Picture::EyeOrder::RightFirst);
    }
    void codec()
    {
        const unsigned char comment[] = { 'o', 'k' }, extra[] = { 1, 2, 3 };
        stereo_jps_descriptor descriptor { 0xff070201, extra, comment, 3, 2 }, read {};
        unsigned char bytes[128];
        QCOMPARE(stereo_jps_encode(&descriptor, bytes, sizeof(bytes)), size_t(21));
        QVERIFY(stereo_jps_decode(bytes, 21, &read));
        QCOMPARE(read.value, descriptor.value);
        QCOMPARE(read.extension_size, 3);
        QCOMPARE(read.comment_size, 2);
        QVERIFY(!stereo_jps_decode(bytes, 20, &read));
        descriptor.value |= 0x800000;
        QVERIFY(!stereo_jps_encode(&descriptor, bytes, sizeof(bytes)));
        // Big-endian TIFF attribute directory, LONG ordinal=2 at offset18.
        const QByteArray tiff = QByteArray::fromHex("4d4d002a000000080001b1010004000000010000000200000000");
        QVERIFY(stereo_mpf_valid(tiff.constData(), size_t(tiff.size())));
        QCOMPARE(stereo_mpf_field(tiff.constData(), size_t(tiff.size()), 8, 0xb101, 4, 1), size_t(18));
        QCOMPARE(stereo_mpf_u32(tiff.constData(), size_t(tiff.size()), 18), uint32_t(2));
    }
    void temporaryFailure()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("picture.jps"));
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, "jps");
        QVERIFY(writer.write(image));
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const auto bytes = source.readAll();
        source.close();
        Picture::Options options;
        options.swapEyes = true;
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
        const auto failed = Picture::saveCorrection(path, options);
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(!failed.success);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), bytes);
        source.close();
        const auto alias = dir.filePath(QStringLiteral("alias.jps"));
        QCOMPARE(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(alias).constData()), 0);
        QVERIFY(!Picture::saveCorrection(alias, options).success);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), bytes);
    }
    void translations_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");
        const char *languages[] = { "en", "de", "es", "fr", "ja", "ko", "pl", "pt_BR", "zh_CN", "zh_TW" };
        const char *messages[] = { "The original picture could not be read completely.", "Das Originalbild konnte nicht vollständig gelesen werden.",
            "No se pudo leer por completo la imagen original.", "L’image originale n’a pas pu être lue entièrement.",
            "元の画像を完全に読み取れませんでした。", "원본 그림을 완전히 읽을 수 없습니다.", "Nie można odczytać całego oryginalnego obrazu.",
            "A imagem original não pôde ser lida por completo.", "无法完整读取原始图片。", "無法完整讀取原始圖片。" };
        for (int i = 0; i < 10; ++i)
            QTest::newRow(languages[i]) << QString::fromUtf8(languages[i]) << QString::fromUtf8(messages[i]);
    }
    void translations()
    {
        QFETCH(QString, language);
        QFETCH(QString, expected);
        QTranslator translator;
        if (language != QLatin1String("en")) {
            QVERIFY(translator.load(QStringLiteral(SAVE_TRANSLATIONS_DIR) + QLatin1Char('/') + language + QStringLiteral("/stereo-layer_qt.qm")));
            QVERIFY(QCoreApplication::installTranslator(&translator));
        }
        const auto result = Picture::saveCorrection(QStringLiteral("/missing-picture-for-save-test"), {});
        if (language != QLatin1String("en"))
            QCoreApplication::removeTranslator(&translator);
        QCOMPARE(result.error, expected);
        QVERIFY(!result.success);
    }
    void refuse()
    {
        QTemporaryDir dir;
        QString path = dir.filePath(QStringLiteral("bad.jps"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = QByteArray::fromHex("ffd8ffe300ff");
        file.write(bytes);
        file.close();
        Picture::Options options;
        options.inputLayout = Picture::Layout::Columns;
        auto result = Picture::saveCorrection(path, options);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
};
QTEST_MAIN(PictureSaveTest)
#include "picturesavetest.moc"
