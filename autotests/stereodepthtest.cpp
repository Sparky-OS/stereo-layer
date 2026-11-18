// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// StereoDepth: the parallax of a depth against the boot splash's own numbers, and a window on a desktop that tells no depth
// (here the offscreen platform) drawn exactly as without it: no _q_stereo_depth on the window, no _q_stereo_parallax on an item.
#include "stereodepth.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QSignalSpy>
#include <cmath>
#include <limits>

class StereoDepthTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parallax_data()
    {
        QTest::addColumn<qreal>("depth");
        QTest::addColumn<qreal>("popped");
        QTest::addColumn<qreal>("sunk");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("expected");
        // the rows of the boot splash's guest rig (stereo-window-3d-area, boot-splash-depth.md): logo at the popped limit, base at half of it
        QTest::newRow("logo, nothing saved") << 1.0 << 3.86 << 9.66 << 1920.0 << 4.0;
        QTest::newRow("base, nothing saved") << 0.5 << 3.86 << 9.66 << 1920.0 << 2.0;
        QTest::newRow("logo, popped 2") << 1.0 << 1.93 << 4.83 << 1920.0 << 2.0;
        QTest::newRow("base, popped 2") << 0.5 << 1.93 << 4.83 << 1920.0 << 1.0;
        QTest::newRow("logo, popped 8") << 1.0 << 7.73 << 19.3 << 1920.0 << 8.0;
        QTest::newRow("base, popped 8") << 0.5 << 7.73 << 19.3 << 1920.0 << 4.0;
        QTest::newRow("limits zero") << 1.0 << 0.0 << 0.0 << 1920.0 << 0.0;
        // KWin's own default limits at 1920 and at 1280 wide: 4 and 10 times 1280 / 1920
        QTest::newRow("popped at 1280") << 1.0 << 4.0 << 10.0 << 1280.0 << 3.0;
        QTest::newRow("half popped at 1280") << 0.5 << 4.0 << 10.0 << 1280.0 << 1.0;
        QTest::newRow("sunk at 1280") << -1.0 << 4.0 << 10.0 << 1280.0 << -7.0;
        QTest::newRow("0.6 sunk") << -0.6 << 4.0 << 10.0 << 1920.0 << -6.0;
        QTest::newRow("half a pixel away from zero") << -0.25 << 4.0 << 10.0 << 1920.0 << -3.0;
        QTest::newRow("beyond the limit is the limit") << 2.0 << 4.0 << 10.0 << 1920.0 << 4.0;
        QTest::newRow("screen") << 0.0 << 4.0 << 10.0 << 1920.0 << 0.0;
    }
    void parallax()
    {
        QFETCH(qreal, depth);
        QFETCH(qreal, popped);
        QFETCH(qreal, sunk);
        QFETCH(qreal, width);
        QFETCH(qreal, expected);
        QCOMPARE(Stereo::Depth::parallaxFor(depth, popped, sunk, width), expected);
    }
    void artworkParallax_data()
    {
        QTest::addColumn<qreal>("popped");
        QTest::addColumn<qreal>("sunk");
        QTest::addColumn<qreal>("width");
        QTest::addColumn<qreal>("multiple");
        QTest::addColumn<qreal>("expected");
        QTest::newRow("default logo") << 4.0 << 10.0 << 1920.0 << 2.0 << 8.0;
        QTest::newRow("changed logo") << 8.0 << 20.0 << 1920.0 << 2.0 << 16.0;
        QTest::newRow("zero limits") << 0.0 << 0.0 << 1920.0 << 2.0 << 0.0;
        QTest::newRow("125 percent") << 4.0 << 10.0 << (1536.0 * 1.25) << 2.0 << 8.0;
        QTest::newRow("multiply before rounding") << 4.0 << 10.0 << 1600.0 << 2.0 << 7.0;
        QTest::newRow("ordinary multiple") << 4.0 << 10.0 << 1920.0 << 1.0 << 4.0;
        QTest::newRow("invalid multiple") << 4.0 << 10.0 << 1920.0 << 0.0 << 4.0;
    }
    void artworkParallax()
    {
        QFETCH(qreal, popped);
        QFETCH(qreal, sunk);
        QFETCH(qreal, width);
        QFETCH(qreal, multiple);
        QFETCH(qreal, expected);
        QCOMPARE(Stereo::Depth::parallaxFor(1, popped, sunk, width, multiple), expected);
        QCOMPARE(Stereo::Depth::parallaxFor(2, popped, sunk, width), std::round(popped * width / 1920));
    }
    void artworkCppApi()
    {
        QQuickItem item;
        Stereo::DepthAttached depth(&item);
        QSignalSpy changed(&depth, &Stereo::DepthAttached::artworkDepthMultipleChanged);
        QCOMPARE(depth.artworkDepthMultiple(), 1.0);
        depth.setDepth(2);
        QCOMPARE(depth.depth(), 1.0);
        depth.setArtworkDepthMultiple(2);
        QCOMPARE(depth.artworkDepthMultiple(), 2.0);
        QCOMPARE(changed.count(), 1);
        depth.setArtworkDepthMultiple(2);
        depth.setArtworkDepthMultiple(0);
        depth.setArtworkDepthMultiple(std::numeric_limits<qreal>::infinity());
        depth.setArtworkDepthMultiple(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(depth.artworkDepthMultiple(), 2.0);
        QCOMPARE(changed.count(), 1);
        depth.setArtworkDepthMultiple(1);
        QCOMPARE(depth.artworkDepthMultiple(), 1.0);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(depth.parallax(), 0.0);
    }
    void artworkQmlApi()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import org.sparky.stereo
Item {
    StereoDepth.depth: StereoDepth.Popped
    StereoDepth.artworkDepthMultiple: 2
})", QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto attached = qobject_cast<Stereo::DepthAttached *>(qmlAttachedPropertiesObject<Stereo::Depth>(object.get(), false));
        QVERIFY(attached);
        QCOMPARE(attached->depth(), 1.0);
        QCOMPARE(attached->artworkDepthMultiple(), 2.0);
        QCOMPARE(attached->shown(), false);
        QCOMPARE(attached->parallax(), 0.0);
    }
    void flatWhereNoDepthIsTold()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(import QtQuick
import org.sparky.stereo
Window {
    width: 320; height: 200; visible: true
    property alias logo: logo
    Rectangle { id: logo; objectName: "logo"; width: 40; height: 40; color: "white"; StereoDepth.depth: StereoDepth.Popped; StereoDepth.artworkDepthMultiple: 2 }
    Rectangle { objectName: "base"; x: 100; width: 40; height: 40; color: "grey"; StereoDepth.depth: 0.5 }
    Rectangle { objectName: "deep"; x: 200; width: 40; height: 40; color: "black"; StereoDepth.depth: StereoDepth.Sunk * 0.6 }
})",
                          QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto window = qobject_cast<QQuickWindow *>(object.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(300);
        QVERIFY(!window->property("_q_stereo_depth").isValid() || !window->property("_q_stereo_depth").toBool());
        for (const char *name : {"logo", "base", "deep"}) {
            auto item = window->findChild<QQuickItem *>(QString::fromLatin1(name));
            QVERIFY(item);
            QVERIFY2(!item->property("_q_stereo_parallax").isValid(), name);
            auto attached = qobject_cast<Stereo::DepthAttached *>(qmlAttachedPropertiesObject<Stereo::Depth>(item, false));
            QVERIFY(attached);
            QCOMPARE(attached->shown(), false);
            QCOMPARE(attached->parallax(), 0.0);
        }
        auto deep = qobject_cast<Stereo::DepthAttached *>(qmlAttachedPropertiesObject<Stereo::Depth>(window->findChild<QQuickItem *>(QStringLiteral("deep")), false));
        QCOMPARE(deep->depth(), -0.6);
    }
};

QTEST_MAIN(StereoDepthTest)
#include "stereodepthtest.moc"
