// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "picture.h"
#include <QFile>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
using Stereo::Picture;
class PictureSaveTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void correct_data() {
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<QString>("suffix");
        QTest::newRow("jps") << QByteArray("jps") << QStringLiteral("jps");
        QTest::newRow("jpeg-extension") << QByteArray("jps") << QStringLiteral("jpg");
        QTest::newRow("mpo") << QByteArray("mpo") << QStringLiteral("mpo");
    }
    void correct() {
        QFETCH(QByteArray, format); QFETCH(QString, suffix);
        QTemporaryDir dir;
        QString path = dir.filePath(QStringLiteral("picture.") + suffix);
        QImage image(64, 24, QImage::Format_RGB888);
        image.fill(Qt::red);
        for (int y=0;y<24;++y) for(int x=32;x<64;++x) image.setPixelColor(x,y,Qt::blue);
        image.setText(QStringLiteral("Stereo3DLayout"), QStringLiteral("sideBySideLeftFirst"));
        QImageWriter writer(path, format); QVERIFY2(writer.write(image), qPrintable(writer.errorString()));
        const auto before = Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(before.isStereo());
        QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
        Picture::Options options; options.swapEyes=true;
        const auto result=Picture::saveCorrection(path, options);
        QVERIFY2(result.success, qPrintable(result.error));
        const auto after=Picture::load(path, {}, Stereo::OutputMode::Stereo);
        QVERIFY(after.isStereo());
        QCOMPARE(after.left.convertToFormat(QImage::Format_RGBA8888),before.right.convertToFormat(QImage::Format_RGBA8888));
        QCOMPARE(after.right.convertToFormat(QImage::Format_RGBA8888),before.left.convertToFormat(QImage::Format_RGBA8888));
        QVERIFY(original.open(QIODevice::ReadOnly)); const auto changed=original.readAll(); original.close();
        QVERIFY(changed!=bytes);
        // Independent oracle: existing descriptor is fixed size; only order flag or B101 ordinal bytes may differ.
        QCOMPARE(changed.size(), bytes.size());
        QList<qsizetype> differences;
        for(qsizetype i=0;i<bytes.size();++i) if(bytes[i]!=changed[i]) differences<<i;
        if(format=="jps") { QCOMPARE(differences.size(),1); QCOMPARE(differences[0],bytes.indexOf("_JPSJPS_")+11); }
        else { QCOMPARE(differences.size(),2); for(auto pos:differences) QVERIFY((uchar(bytes[pos])==1 && uchar(changed[pos])==2)||(uchar(bytes[pos])==2 && uchar(changed[pos])==1)); }
        // An absolute correction is idempotent; clearing the caller's swap after success prevents applying it twice.
        options.inputLayout=after.layout; options.inputOrder=after.eyeOrder;
        options.swapEyes=false;
        QVERIFY(Picture::saveCorrection(path,options).success);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),changed);
    }
    void refuse() {
        QTemporaryDir dir; QString path=dir.filePath(QStringLiteral("bad.jps"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); const QByteArray bytes=QByteArray::fromHex("ffd8ffe300ff"); file.write(bytes); file.close();
        Picture::Options options; options.inputLayout=Picture::Layout::Columns;
        auto result=Picture::saveCorrection(path,options); QVERIFY(!result.success); QVERIFY(!result.error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes);
    }
};
QTEST_MAIN(PictureSaveTest)
#include "picturesavetest.moc"
