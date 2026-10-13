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
    void plainC();
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
QTEST_MAIN(VideoTest)
#include "videotest.moc"
