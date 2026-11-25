// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// StereoPicture's place: a pair is a surface of its own, laid flat over the window, so it cannot follow a rotation, a scale, an
// opacity or a clip that cuts the item; a clip that holds the whole item (a Kirigami page and its ColumnView both clip) takes
// nothing from it (ORDER 63). And where the picture is drawn flat for one of those reasons, the log says why, once for each reason.
#include "pictureitem.h"
#include "screenmode.h"
#include <KScreen/Config>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <kscreen/backendmanager_p.h>
#include <memory>

namespace
{
QStringList told;
QtMessageHandler previous = nullptr;
void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith(QLatin1String("StereoPicture ")))
        told << message;
    previous(type, context, message);
}
}

class PicturePlaceTest : public QObject
{
    Q_OBJECT
    QQmlEngine engine;
    // a window of 600 by 400 holding `inner` (the item asked about) under `outer`, both described by QML
    std::unique_ptr<QObject> make(const QByteArray &body)
    {
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nItem { width: 600; height: 400\n" + body + "\n}", QUrl());
        std::unique_ptr<QObject> root(component.create());
        if (!root)
            qWarning() << component.errors();
        return root;
    }
    static QString reason(QObject *root) { return Stereo::PictureItem::flatReason(root->findChild<QQuickItem *>(QStringLiteral("inner"))); }

private Q_SLOTS:
    void reasons_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected");
        QTest::newRow("no clip") << QByteArray("Item { width: 300; height: 200; Item { objectName: \"inner\"; x: 250; width: 200; height: 100 } }") << QString();
        // the case of the Welcome page: the page and the view of pages clip, and the picture lies wholly inside both
        QTest::newRow("clips that hold the item") << QByteArray("Item { clip: true; anchors.fill: parent; Item { clip: true; x: 20; y: 20; width: 500; height: 300;"
                                                               " Item { objectName: \"inner\"; x: 10; y: 10; width: 480; height: 280 } } }")
                                                  << QString();
        QTest::newRow("a clip the item fills exactly") << QByteArray("Item { clip: true; width: 300; height: 200; Item { objectName: \"inner\"; width: 300; height: 200 } }") << QString();
        QTest::newRow("the item's own clip") << QByteArray("Item { objectName: \"inner\"; clip: true; width: 300; height: 200 }") << QString();
        QTest::newRow("a clip that cuts the item") << QByteArray("Item { objectName: \"page\"; clip: true; width: 300; height: 200; Item { objectName: \"inner\"; x: 250; width: 200; height: 100 } }")
                                                   << QStringLiteral("QQuickItem \"page\" clips it");
        QTest::newRow("a clip further up cuts it") << QByteArray("Item { objectName: \"view\"; clip: true; width: 300; height: 200; Item { x: 200; width: 400; height: 300;"
                                                                " Item { objectName: \"inner\"; width: 200; height: 100 } } }")
                                                   << QStringLiteral("QQuickItem \"view\" clips it");
        QTest::newRow("a quarter pixel over the edge") << QByteArray("Item { clip: true; width: 300; height: 200; Item { objectName: \"inner\"; x: 0.25; width: 300; height: 200 } }") << QString();
        QTest::newRow("a whole pixel over the edge") << QByteArray("Item { objectName: \"page\"; clip: true; width: 300; height: 200; Item { objectName: \"inner\"; x: 1; width: 300; height: 200 } }")
                                                     << QStringLiteral("QQuickItem \"page\" clips it");
        // a list scrolled so that the item is half out of view: the Flickable clips to its own rectangle, the content moves under it
        QTest::newRow("a flickable scrolled past the item") << QByteArray("Flickable { objectName: \"list\"; clip: true; width: 300; height: 200; contentWidth: 300; contentHeight: 1000; contentY: 150;"
                                                                         " Item { objectName: \"inner\"; y: 100; width: 300; height: 100 } }")
                                                            << QStringLiteral("QQuickFlickable \"list\" clips it");
        QTest::newRow("a flickable with the item in view") << QByteArray("Flickable { clip: true; width: 300; height: 200; contentWidth: 300; contentHeight: 1000; contentY: 100;"
                                                                        " Item { objectName: \"inner\"; y: 150; width: 300; height: 100 } }")
                                                           << QString();
        QTest::newRow("rotated") << QByteArray("Item { objectName: \"turn\"; rotation: 90; width: 300; height: 300; Item { objectName: \"inner\"; width: 100; height: 100 } }") << QStringLiteral("QQuickItem \"turn\" is rotated");
        QTest::newRow("scaled") << QByteArray("Item { objectName: \"zoom\"; scale: 2; width: 100; height: 100; Item { objectName: \"inner\"; width: 50; height: 50 } }") << QStringLiteral("QQuickItem \"zoom\" is scaled");
        QTest::newRow("fading") << QByteArray("Item { objectName: \"fade\"; opacity: 0.5; width: 100; height: 100; Item { objectName: \"inner\"; width: 50; height: 50 } }") << QStringLiteral("QQuickItem \"fade\" is not opaque");
    }
    void reasons()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, expected);
        auto root = make(body);
        QVERIFY(root);
        QCOMPARE(reason(root.get()), expected);
    }
    // A clip that moves away stops being a reason as soon as it holds the item again: the decision is made at each polish
    void followsTheClip()
    {
        auto root = make("Flickable { objectName: \"list\"; clip: true; width: 300; height: 200; contentWidth: 300; contentHeight: 1000;"
                         " Item { objectName: \"inner\"; y: 50; width: 300; height: 100 } }");
        QVERIFY(root);
        auto *list = root->findChild<QQuickItem *>(QStringLiteral("list"));
        QCOMPARE(reason(root.get()), QString());
        list->setProperty("contentY", 100);
        QCOMPARE(reason(root.get()), QStringLiteral("QQuickFlickable \"list\" clips it"));
        list->setProperty("contentY", 20);
        QCOMPARE(reason(root.get()), QString());
    }
    // A real StereoPicture showing a pair on a stereo screen: in a clip that holds it nothing is logged; in one that cuts it the log
    // names the clip once, however often the item is polished, and the picture is flat (stereoActive false)
    void logsOncePerReason()
    {
        qputenv("KSCREEN_BACKEND", "Fake");
        qputenv("KSCREEN_BACKEND_INPROCESS", "1");
        const QString config = QFINDTESTDATA("screenmode.json");
        QVERIFY(!config.isEmpty());
        KScreen::BackendManager::instance()->setBackendArgs({{QStringLiteral("TEST_DATA"), config}});
        auto *get = new KScreen::GetConfigOperation(KScreen::ConfigOperation::NoEDID);
        QVERIFY(get->exec());
        const auto state = get->config();
        QVERIFY(state);
        state->output(2)->currentMode()->setStereo3D(KScreen::Mode::Stereo3D::SideBySideFull);
        auto *set = new KScreen::SetConfigOperation(state);
        QVERIFY(set->exec());
        QScreen *stereo = nullptr;
        for (auto *screen : qGuiApp->screens())
            if (screen->name() == QLatin1String("stereo"))
                stereo = screen;
        QVERIFY(stereo);

        QTemporaryDir dir;
        QImage pair(400, 100, QImage::Format_RGB32);
        pair.fill(Qt::red);
        for (int y = 0; y < 100; ++y)
            for (int x = 200; x < 400; ++x)
                pair.setPixel(x, y, qRgb(0, 0, 255));
        const QString file = dir.filePath(QStringLiteral("pair.png"));
        QVERIFY(pair.save(file));

        // 0: a clip that holds the picture; 1: a clip that cuts it; 2: a window shown before the picture asked for an alpha channel
        for (const int kind : {0, 1, 2}) {
            told.clear();
            previous = qInstallMessageHandler(collect);
            QQuickWindow window;
            // the offscreen platform puts a window on the screen its place lies on
            window.setScreen(stereo);
            window.setGeometry(QRect(stereo->geometry().topLeft(), QSize(600, 400)));
            if (kind == 2) {
                window.show();
                QVERIFY(QTest::qWaitForWindowExposed(&window));
            }
            QQmlComponent component(&engine);
            component.setData(QByteArrayLiteral("import QtQuick\nimport org.sparky.stereo\nItem { width: 600; height: 400\n"
                                                "Item { objectName: \"page\"; clip: true; width: 300; height: 200\n"
                                                "StereoPicture { objectName: \"inner\"; inputLayout: 1; x: ")
                                  + (kind == 1 ? "250" : "50") + "; width: 200; height: 100; source: \"" + QUrl::fromLocalFile(file).toEncoded() + "\" } } }",
                              QUrl());
            std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
            QVERIFY2(root, qPrintable(component.errorString()));
            root->setParentItem(window.contentItem());
            if (kind != 2) {
                window.show();
                QVERIFY(QTest::qWaitForWindowExposed(&window));
            }
            auto *picture = root->findChild<Stereo::PictureItem *>(QStringLiteral("inner"));
            QVERIFY(picture);
            QTRY_VERIFY(picture->isStereo());
            // the fixture itself: the window is on the screen the fake backend shows in 3D
            QCOMPARE(window.screen()->name(), QStringLiteral("stereo"));
            Stereo::ScreenMode mode;
            mode.setScreen(window.screen());
            QTRY_COMPARE(mode.mode(), Stereo::OutputMode::Stereo);
            // the item polishes itself every 50 ms: ten rounds and more
            QTest::qWait(600);
            qInstallMessageHandler(previous);
            QVERIFY(!picture->stereoActive()); // no compositor here: a pair is never active, only the reason is told
            const QString start = QStringLiteral("StereoPicture ") + QUrl::fromLocalFile(file).toDisplayString() + QStringLiteral(": ");
            const QString end = QStringLiteral(", so the picture is shown flat, one eye, not in 3D, while it is");
            if (kind == 0) {
                QCOMPARE(told, QStringList());
            } else {
                QCOMPARE(told.size(), 1);
                const QString reason = kind == 1 ? QStringLiteral("QQuickItem \"page\" clips it")
                                                 : QStringLiteral("its window has no alpha channel (it was made before the picture asked for one)");
                QCOMPARE(told.first(), start + reason + end);
            }
        }
    }
};
QTEST_MAIN(PicturePlaceTest)
#include "pictureplacetest.moc"
