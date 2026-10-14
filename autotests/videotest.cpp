// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "video.h"
#include <QTest>
using namespace Stereo;
class VideoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void shape_data();
    void shape();
    void marksAndChoices();
    void wholeMarkSet_data();
    void wholeMarkSet();
    void plainC();
    void swapOverMark();
    void fullViews();
};
void VideoTest::shape_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<bool>("sideBySide");
    QTest::newRow("full side by side 16:9, 3840x1080") << QSize(3840, 1080) << true;
    QTest::newRow("full side by side 4:3 of 720x480, 1440x480 (3:1)") << QSize(1440, 480) << true;
    QTest::newRow("scope film 2.40:1, 1920x800") << QSize(1920, 800) << false;
    QTest::newRow("Ultra Panavision 2.76:1, 1920x696") << QSize(1920, 696) << false;
    QTest::newRow("just under 3:1, 2999x1000") << QSize(2999, 1000) << false;
    QTest::newRow("16:9, 1920x1080") << QSize(1920, 1080) << false;
    QTest::newRow("portrait 9:16, 1080x1920: never top and bottom by shape") << QSize(1080, 1920) << false;
    QTest::newRow("empty") << QSize() << false;
}
void VideoTest::shape()
{
    QFETCH(QSize, size);
    QFETCH(bool, sideBySide);
    QCOMPARE(Video::sideBySideByShape(size), sideBySide);
    const auto result = Video::layout(size, {});
    QCOMPARE(result.layout, sideBySide ? Picture::Layout::SideBySide : Picture::Layout::None);
    QCOMPARE(result.source, sideBySide ? Picture::Source::Shape : Picture::Source::None);
    QCOMPARE(result.eyeOrder, Picture::EyeOrder::LeftFirst);
}
void VideoTest::marksAndChoices()
{
    // the stream's mark comes first, whatever the shape
    auto marked = Video::layout(QSize(1920, 1080), QStringLiteral("sideBySideRightFirst"));
    QCOMPARE(marked.layout, Picture::Layout::SideBySide);
    QCOMPARE(marked.eyeOrder, Picture::EyeOrder::RightFirst);
    QCOMPARE(marked.source, Picture::Source::FileMark);
    marked = Video::layout(QSize(1920, 1080), QStringLiteral("topBottomLeftFirst"));
    QCOMPARE(marked.layout, Picture::Layout::TopAndBottom);
    const auto mono = Video::layout(QSize(3840, 1080), QStringLiteral("mono"));
    QCOMPARE(mono.layout, Picture::Layout::None);
    QCOMPARE(mono.source, Picture::Source::FileMark);
    // then the user's choice, which covers what the shape no longer guesses
    Picture::Options options;
    options.inputLayout = Picture::Layout::SideBySide;
    options.inputOrder = Picture::EyeOrder::RightFirst;
    const auto chosen = Video::layout(QSize(1920, 800), {}, options);
    QCOMPARE(chosen.layout, Picture::Layout::SideBySide);
    QCOMPARE(chosen.eyeOrder, Picture::EyeOrder::RightFirst);
    QCOMPARE(chosen.source, Picture::Source::User);
    options.inputLayout = Picture::Layout::TopAndBottom;
    QCOMPARE(Video::layout(QSize(3840, 1080), QStringLiteral("sideBySideLeftFirst"), options).source, Picture::Source::FileMark);
}
void VideoTest::wholeMarkSet_data()
{
    // Every Stereo3DLayout value a stream can carry (RFC 9559 StereoMode, H.264/H.265 Table D-8): its packing and order
    QTest::addColumn<QString>("mark");
    QTest::addColumn<Picture::Layout>("layout");
    QTest::addColumn<Picture::EyeOrder>("order");
    const auto L = Picture::EyeOrder::LeftFirst;
    const auto R = Picture::EyeOrder::RightFirst;
    QTest::newRow("mono") << QStringLiteral("mono") << Picture::Layout::None << L;
    struct Row {
        const char *name;
        Picture::Layout layout;
    };
    const Row rows[] = {
        { "sideBySide", Picture::Layout::SideBySide },
        { "sideBySideQuincunx", Picture::Layout::SideBySideQuincunx },
        { "topBottom", Picture::Layout::TopAndBottom },
        { "checkerboard", Picture::Layout::Checkerboard },
        { "rowInterleaved", Picture::Layout::Rows },
        { "columnInterleaved", Picture::Layout::Columns },
        { "frameSequential", Picture::Layout::FrameSequential },
        { "blockLaced", Picture::Layout::BlockLaced },
        { "multiView", Picture::Layout::MultiView },
    };
    for (const Row &row : rows) {
        for (const auto order : { L, R }) {
            const QString mark = QLatin1String(row.name) + (order == L ? QLatin1String("LeftFirst") : QLatin1String("RightFirst"));
            QTest::addRow("%s", qPrintable(mark)) << mark << row.layout << order;
        }
    }
    // One picture holding both views in colours (StereoMode 10 and 12), and marks outside the set: no packing
    for (const char *mark : { "anaglyphCyanRed", "anaglyphGreenMagenta", "anaglyphRedLeft", "anaglyphRedRight", "sideBySide", "quincunx" })
        QTest::addRow("%s", mark) << QString::fromLatin1(mark) << Picture::Layout::None << L;
}
void VideoTest::wholeMarkSet()
{
    QFETCH(QString, mark);
    QFETCH(Picture::Layout, layout);
    QFETCH(Picture::EyeOrder, order);
    // a 3:1 frame, so that a mark that says nothing could not fall back on the shape
    const auto result = Video::layout(QSize(3840, 1080), mark);
    QCOMPARE(result.source, Picture::Source::FileMark);
    QCOMPARE(result.layout, layout);
    QCOMPARE(result.eyeOrder, order);
    int cLayout = -1, cOrder = -1, cSource = -1;
    QCOMPARE(stereo_layer_video_layout(3840, 1080, mark.toUtf8().constData(), &cLayout, &cOrder, &cSource), 0);
    QCOMPARE(cLayout, int(layout));
    QCOMPARE(cOrder, int(order));
    QCOMPARE(cSource, int(Picture::Source::FileMark));
}
void VideoTest::plainC()
{
    int layout = -1, order = -1, source = -1;
    QCOMPARE(stereo_layer_video_layout(3840, 1080, nullptr, &layout, &order, &source), 0);
    QCOMPARE(layout, int(Picture::Layout::SideBySide));
    QCOMPARE(source, int(Picture::Source::Shape));
    QCOMPARE(stereo_layer_video_layout(1920, 800, "", &layout, &order, &source), 0);
    QCOMPARE(layout, int(Picture::Layout::None));
    QCOMPARE(source, int(Picture::Source::None));
    QCOMPARE(stereo_layer_video_layout(1920, 800, "sideBySideRightFirst", &layout, &order, &source), 0);
    QCOMPARE(layout, int(Picture::Layout::SideBySide));
    QCOMPARE(order, int(Picture::EyeOrder::RightFirst));
    QCOMPARE(source, int(Picture::Source::FileMark));
    QCOMPARE(stereo_layer_video_layout(0, 800, nullptr, &layout, &order, &source), -1);
    QCOMPARE(stereo_layer_video_layout(1920, 800, nullptr, nullptr, &order, &source), -1);
}
void VideoTest::swapOverMark()
{
    // A file marked right first, corrected by the user, shows the left view in the left eye; without the correction,
    // the mark rules again
    const auto marked = Video::layout(QSize(1920, 1080), QStringLiteral("sideBySideRightFirst"));
    QCOMPARE(marked.eyeOrder, Picture::EyeOrder::RightFirst);
    Picture::Options options;
    options.swapEyes = true;
    const auto corrected = Video::layout(QSize(1920, 1080), QStringLiteral("sideBySideRightFirst"), options);
    QCOMPARE(corrected.layout, Picture::Layout::SideBySide);
    QCOMPARE(corrected.eyeOrder, Picture::EyeOrder::LeftFirst);
    QCOMPARE(corrected.source, Picture::Source::FileMark);
    // nothing to reverse in one picture
    const auto mono = Video::layout(QSize(3840, 1080), QStringLiteral("mono"), options);
    QCOMPARE(mono.layout, Picture::Layout::None);
    QCOMPARE(mono.eyeOrder, Picture::EyeOrder::LeftFirst);
}
void VideoTest::fullViews()
{
    QVERIFY(Video::fullViews(QSize(3840, 1080), Picture::Layout::SideBySide));
    QVERIFY(!Video::fullViews(QSize(1920, 1080), Picture::Layout::SideBySide));
    QVERIFY(Video::fullViews(QSize(1920, 2160), Picture::Layout::TopAndBottom));
    QVERIFY(!Video::fullViews(QSize(1920, 1080), Picture::Layout::TopAndBottom));
    QVERIFY(!Video::fullViews(QSize(1920, 1080), Picture::Layout::Rows));
    QCOMPARE(stereo_layer_video_full_views(3840, 1080, int(Picture::Layout::SideBySide)), 1);
    QCOMPARE(stereo_layer_video_full_views(1920, 1080, int(Picture::Layout::SideBySide)), 0);
    QCOMPARE(stereo_layer_video_full_views(1920, 1080, int(Picture::Layout::None)), -1);
}
QTEST_MAIN(VideoTest)
#include "videotest.moc"
