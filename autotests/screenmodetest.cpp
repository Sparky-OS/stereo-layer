// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "screenmode.h"
#include <KScreen/Config>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>
#include <QGuiApplication>
#include <QScreen>
#include <QTest>
#include <kscreen/backendmanager_p.h>
using namespace Stereo;
class ScreenModeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void perScreen()
    {
        qputenv("KSCREEN_BACKEND", "Fake");
        qputenv("KSCREEN_BACKEND_INPROCESS", "1");
        const QString config = QFINDTESTDATA("screenmode.json");
        QVERIFY(!config.isEmpty());
        KScreen::BackendManager::instance()->setBackendArgs({ { QStringLiteral("TEST_DATA"), config } });
        auto *get = new KScreen::GetConfigOperation(KScreen::ConfigOperation::NoEDID);
        QVERIFY(get->exec());
        const auto state = get->config();
        QVERIFY(state);
        state->output(1)->currentMode()->setStereo3D(KScreen::Mode::Stereo3D::AnaglyphModern);
        state->output(2)->currentMode()->setStereo3D(KScreen::Mode::Stereo3D::SideBySideFull);
        state->output(3)->modes().value(QStringLiteral("2"))->setStereo3D(KScreen::Mode::Stereo3D::AnaglyphModern);
        auto *set = new KScreen::SetConfigOperation(state);
        QVERIFY(set->exec());
        QCOMPARE(qGuiApp->screens().size(), 3);
        const auto screen = [](const QString &name) -> QScreen * {
            for (auto *screen : qGuiApp->screens())
                if (screen->name() == name)
                    return screen;
            return nullptr;
        };
        QVERIFY(screen(QStringLiteral("anaglyph")));
        QVERIFY(screen(QStringLiteral("stereo")));
        QVERIFY(screen(QStringLiteral("ordinary")));
        ScreenMode first, second, ordinary;
        first.setScreen(screen(QStringLiteral("anaglyph")));
        second.setScreen(screen(QStringLiteral("stereo")));
        ordinary.setScreen(screen(QStringLiteral("ordinary")));
        QTRY_COMPARE(first.mode(), OutputMode::Anaglyph);
        QTRY_COMPARE(second.mode(), OutputMode::Stereo);
        QTRY_COMPARE(ordinary.mode(), OutputMode::Mono);
        first.setScreen(screen(QStringLiteral("stereo")));
        QCOMPARE(first.mode(), OutputMode::Stereo);
        QCOMPARE(second.mode(), OutputMode::Stereo);
        state->output(3)->setCurrentModeId(QStringLiteral("2"));
        auto *selectAnaglyph = new KScreen::SetConfigOperation(state);
        QVERIFY(selectAnaglyph->exec());
        QTRY_COMPARE(ordinary.mode(), OutputMode::Anaglyph);
        QCOMPARE(second.mode(), OutputMode::Stereo);
        ordinary.setScreen(nullptr);
        QCOMPARE(ordinary.mode(), OutputMode::Unknown);
    }
};
QTEST_MAIN(ScreenModeTest)
#include "screenmodetest.moc"
