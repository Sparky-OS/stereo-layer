// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "depthcontrols.h"
#include "depthsettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QLabel>
#include <QSpinBox>
#include <QTest>
#include <stereo-dibr.h>

using namespace Stereo;

// The options of drawing the views from a picture and its depth as one set: the settings, the widgets' controls and the QML
// controls show and change the same set, with the same words, and the person's choice keeps as text.
class DepthControlsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startsAtTheLibrarysDefault()
    {
        DepthSettings s;
        QVERIFY(s.isDefault());
        QVERIFY(s.text().isEmpty());
        QCOMPARE(s.view(), int(STEREO_DIBR_VIEW_LEFT));
        QCOMPARE(s.warp(), int(STEREO_DIBR_WARP_LAYERED));
        QCOMPARE(s.fill(), int(STEREO_DIBR_FILL_DEPTH_AIDED));
        QCOMPARE(s.smooth(), int(STEREO_DIBR_SMOOTH_OFF));
        QCOMPARE(s.strength(), 58.0);
        QCOMPARE(s.zero(), 0.5);
        const Picture::DepthOptions o = s.toOptions();
        QVERIFY(o.warp == Picture::DepthOptions::Warp::Layered && o.fill == Picture::DepthOptions::Fill::DepthAided && !o.smooth);
        QCOMPARE(o.range, 58.0);
        // and the pictures' own defaults are the same set
        const Picture::DepthOptions plain;
        QVERIFY(o.view == plain.view && o.warp == plain.warp && o.fill == plain.fill && o.smooth == plain.smooth && o.radius == plain.radius && o.zero == plain.zero
                && o.shift == plain.shift && o.range == plain.range);
    }

    void aChoiceKeepsAsTextAndComesBack()
    {
        DepthSettings s;
        QSignalSpy changed(&s, &DepthSettings::changed);
        s.setFill(STEREO_DIBR_FILL_TELEA);
        s.setStrength(90);
        s.setView(STEREO_DIBR_VIEW_CENTER);
        QCOMPARE(changed.count(), 3);
        QVERIFY(!s.isDefault());
        const QString text = s.text();
        QVERIFY2(text.contains(QLatin1String("fill=telea")) && text.contains(QLatin1String("strength=90")) && text.contains(QLatin1String("view=middle")), qPrintable(text));
        DepthSettings t;
        QVERIFY(t.setText(text));
        QCOMPARE(t.fill(), int(STEREO_DIBR_FILL_TELEA));
        QCOMPARE(t.strength(), 90.0);
        QCOMPARE(t.text(), text);
        // the same value again is no change; the default is the empty text; a text that does not read is refused and changes nothing
        const int n = changed.count();
        s.setStrength(90);
        QCOMPARE(changed.count(), n);
        QVERIFY(!t.setText(QStringLiteral("fill=sparkles")));
        QCOMPARE(t.text(), text);
        t.reset();
        QVERIFY(t.isDefault() && t.text().isEmpty());
    }

    void valuesStayWithinTheLimits()
    {
        DepthSettings s;
        s.setStrength(1e9);
        QCOMPARE(s.strength(), DepthSettings::strengthMax());
        s.setZero(-3);
        QCOMPARE(s.zero(), 0.0);
        s.setRadius(0);
        QCOMPARE(s.radius(), 1);
    }

    void everyWordIsThere()
    {
        for (const char *key : {"title", "strength", "strengthTip", "zero", "zeroTip", "shift", "picture", "pictureTip", "pictureLeft", "pictureMiddle", "fill",
                                "fillTip", "fillBackground", "fillDepthAided", "fillTelea", "fillNone", "fillPyramid", "automatic", "automaticTip", "warp", "warpForward", "warpBackward", "warpLayered",
                                "smooth", "reach", "nearest", "farthest", "reset"})
            QVERIFY2(!DepthSettings::label(QLatin1String(key)).isEmpty(), key);
        QVERIFY(DepthSettings::label(QStringLiteral("nonsense")).isEmpty());
    }

    void widgetsShowAndChangeTheSettings()
    {
        DepthSettings s;
        DepthControls c(&s);
        auto *strength = c.findChildren<QDoubleSpinBox *>().at(0);
        auto combos = c.findChildren<QComboBox *>();
        auto *reset = c.findChild<QPushButton *>();
        QCOMPARE(strength->value(), 58.0);
        QVERIFY(!reset->isEnabled());
        // a control changes the settings
        strength->setValue(75);
        QCOMPARE(s.strength(), 75.0);
        QVERIFY(reset->isEnabled());
        QComboBox *fill = nullptr;
        for (QComboBox *box : combos)
            if (box->findData(STEREO_DIBR_FILL_NONE) >= 0 && box->count() == 5)
                fill = box;
        QVERIFY(fill);
        QCOMPARE(fill->currentData().toInt(), int(STEREO_DIBR_FILL_DEPTH_AIDED));
        fill->setCurrentIndex(fill->findData(STEREO_DIBR_FILL_TELEA));
        Q_EMIT fill->activated(fill->currentIndex());
        QCOMPARE(s.fill(), int(STEREO_DIBR_FILL_TELEA));
        c.findChild<QCheckBox *>()->setChecked(true);
        QCOMPARE(s.smooth(), int(STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN));
        // the settings change the controls (a file with another choice is opened)
        QVERIFY(s.setText(QStringLiteral("fill=depthAided strength=20 smooth=off")));
        QCOMPARE(strength->value(), 20.0);
        QCOMPARE(fill->currentData().toInt(), int(STEREO_DIBR_FILL_DEPTH_AIDED));
        QVERIFY(!c.findChild<QCheckBox *>()->isChecked());
        // back to the default
        reset->click();
        QVERIFY(s.isDefault());
        QCOMPARE(strength->value(), 58.0);
        QVERIFY(!reset->isEnabled());
    }

    void theWayIsAutomaticUntilThePersonPicksOne()
    {
        DepthSettings s;
        QVERIFY(s.automatic() && s.isDefault() && s.chosenText().isEmpty());
        QSignalSpy changed(&s, &DepthSettings::changed);
        // the player tells what the video's drawing chose, and why
        QVariantMap choice{{QStringLiteral("warp"), int(STEREO_DIBR_WARP_LAYERED)}, {QStringLiteral("fill"), int(STEREO_DIBR_FILL_BACKGROUND)},
                           {QStringLiteral("keepsUp"), true}, {QStringLiteral("fps"), 29.97}, {QStringLiteral("skippedWarp"), int(STEREO_DIBR_WARP_LAYERED)},
                           {QStringLiteral("skippedFill"), int(STEREO_DIBR_FILL_DEPTH_AIDED)}, {QStringLiteral("onGpu"), true}};
        s.setChoice(choice);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(s.shownWarp(), int(STEREO_DIBR_WARP_LAYERED));
        QCOMPARE(s.shownFill(), int(STEREO_DIBR_FILL_BACKGROUND));
        QCOMPARE(s.chosenText(), QStringLiteral("Layered, Background (Layered, Depth-aided does not keep 30 frames a second on this GPU)"));
        // the choice of the library is what the settings hold: the person has chosen nothing
        QVERIFY(s.isDefault() && s.text().isEmpty() && s.warp() == int(STEREO_DIBR_WARP_LAYERED) && s.fill() == int(STEREO_DIBR_FILL_DEPTH_AIDED));
        s.setChoice(choice);
        QCOMPARE(changed.count(), 1);
        // the best way keeping up: no reason to give; no way keeping up: said, on the processor when it is the C code
        choice[QStringLiteral("skippedWarp")] = -1;
        choice[QStringLiteral("skippedFill")] = -1;
        choice[QStringLiteral("fill")] = int(STEREO_DIBR_FILL_DEPTH_AIDED);
        choice[QStringLiteral("fps")] = 24;
        s.setChoice(choice);
        QCOMPARE(s.chosenText(), QStringLiteral("Layered, Depth-aided (keeps 24 frames a second on this GPU)"));
        choice[QStringLiteral("keepsUp")] = false;
        choice[QStringLiteral("onGpu")] = false;
        choice[QStringLiteral("warp")] = int(STEREO_DIBR_WARP_FORWARD);
        choice[QStringLiteral("fill")] = int(STEREO_DIBR_FILL_BACKGROUND);
        s.setChoice(choice);
        QCOMPARE(s.chosenText(), QStringLiteral("Forward, Background (the quickest: no way keeps 24 frames a second on this processor)"));
        s.setChoice({});
        QVERIFY(s.chosenText().isEmpty());
        QCOMPARE(s.shownFill(), int(STEREO_DIBR_FILL_DEPTH_AIDED));

        // turning automatic off keeps the way that is drawn as the person's
        choice[QStringLiteral("keepsUp")] = true;
        choice[QStringLiteral("onGpu")] = true;
        s.setChoice(choice);
        s.setAutomatic(false);
        QVERIFY(!s.automatic() && !s.isDefault() && s.chosenText().isEmpty());
        QCOMPARE(s.warp(), int(STEREO_DIBR_WARP_FORWARD));
        QCOMPARE(s.fill(), int(STEREO_DIBR_FILL_BACKGROUND));
        QVERIFY(s.text().contains(QLatin1String("way=own")));
        // and back: the way is the library's again
        s.setAutomatic(true);
        QVERIFY(s.isDefault() && s.warp() == int(STEREO_DIBR_WARP_LAYERED) && s.fill() == int(STEREO_DIBR_FILL_DEPTH_AIDED));

        // picking the way that is shown is a pick: it is the person's, the slowest included
        s.setChoice(choice);
        s.useWay(int(STEREO_DIBR_WARP_FORWARD), int(STEREO_DIBR_FILL_BACKGROUND));
        QVERIFY(!s.automatic() && s.shownFill() == int(STEREO_DIBR_FILL_BACKGROUND));
        s.reset();
        s.useWay(int(STEREO_DIBR_WARP_LAYERED), int(STEREO_DIBR_FILL_DEPTH_AIDED));
        QVERIFY(!s.automatic() && !s.isDefault() && s.text().contains(QLatin1String("way=own")));
        // a pick of one of the two is a pick
        s.reset();
        s.setFill(STEREO_DIBR_FILL_PYRAMID);
        QVERIFY(!s.automatic());
        QVERIFY(s.toOptions().fill == Picture::DepthOptions::Fill::Pyramid);
        // the person's way comes back from the text as theirs
        DepthSettings t;
        QVERIFY(t.setText(s.text()) && !t.automatic() && t.fill() == int(STEREO_DIBR_FILL_PYRAMID));

        // a still picture, and the saving into a file, are drawn the best way, whatever way a text of before held
        DepthSettings u;
        QVERIFY(u.setText(QStringLiteral("way=auto warp=forward fill=background")));
        QVERIFY(u.automatic());
        QVERIFY(u.toOptions().warp == Picture::DepthOptions::Warp::Layered && u.toOptions().fill == Picture::DepthOptions::Fill::DepthAided);
    }

    void widgetsShowWhatWasChosen()
    {
        DepthSettings s;
        DepthControls c(&s);
        QCheckBox *automatic = nullptr;
        for (QCheckBox *box : c.findChildren<QCheckBox *>())
            if (box->text() == DepthSettings::label(QStringLiteral("automatic")))
                automatic = box;
        QVERIFY(automatic && automatic->isChecked());
        auto labels = c.findChildren<QLabel *>();
        QLabel *chosen = nullptr;
        for (QLabel *l : labels)
            if (l->wordWrap())
                chosen = l;
        QVERIFY(chosen && !chosen->isVisibleTo(&c));
        s.setChoice({{QStringLiteral("warp"), int(STEREO_DIBR_WARP_FORWARD)}, {QStringLiteral("fill"), int(STEREO_DIBR_FILL_PYRAMID)}, {QStringLiteral("fps"), 60},
                     {QStringLiteral("keepsUp"), true}, {QStringLiteral("skippedWarp"), int(STEREO_DIBR_WARP_LAYERED)},
                     {QStringLiteral("skippedFill"), int(STEREO_DIBR_FILL_DEPTH_AIDED)}});
        QVERIFY(chosen->isVisibleTo(&c));
        QVERIFY2(chosen->text().startsWith(QLatin1String("Forward, Pyramid (Layered, Depth-aided does not keep 60")), qPrintable(chosen->text()));
        QComboBox *fill = nullptr;
        for (QComboBox *box : c.findChildren<QComboBox *>())
            if (box->findData(STEREO_DIBR_FILL_NONE) >= 0)
                fill = box;
        QVERIFY(fill);
        QCOMPARE(fill->currentData().toInt(), int(STEREO_DIBR_FILL_PYRAMID));
        // unchecking keeps what was shown as the person's
        automatic->setChecked(false);
        QVERIFY(!s.automatic());
        QCOMPARE(s.fill(), int(STEREO_DIBR_FILL_PYRAMID));
        QVERIFY(!chosen->isVisibleTo(&c));
    }

    void qmlControlsShowAndChangeTheSettings()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QML_IMPORT_DIR));
        DepthSettings s;
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral("import QtQuick\nimport org.sparky.stereo\nDepthControls { }\n"), QUrl());
        QObject *raw = component.beginCreate(engine.rootContext());
        QVERIFY2(raw, qPrintable(component.errorString()));
        raw->setProperty("settings", QVariant::fromValue(&s));
        component.completeCreate();
        std::unique_ptr<QObject> controls(raw);
        auto *item = qobject_cast<QQuickItem *>(controls.get());
        QVERIFY(item);
        auto find = [&](const char *name) { return controls->findChild<QObject *>(QLatin1String(name)); };
        for (const char *name : {"strength", "zero", "shift", "picture", "fill", "warp", "smooth", "reach", "nearest", "farthest", "reset", "automatic", "chosen"})
            QVERIFY2(find(name), name);
        QCOMPARE(find("strength")->property("value").toInt(), 58);
        QCOMPARE(find("fill")->property("currentValue").toInt(), int(STEREO_DIBR_FILL_DEPTH_AIDED));
        QVERIFY(!find("reset")->property("enabled").toBool());
        // the settings change the controls
        QVERIFY(s.setText(QStringLiteral("fill=telea strength=33 view=middle smooth=asymmetricGaussian")));
        QCOMPARE(find("strength")->property("value").toInt(), 33);
        QCOMPARE(find("fill")->property("currentValue").toInt(), int(STEREO_DIBR_FILL_TELEA));
        QCOMPARE(find("picture")->property("currentValue").toInt(), int(STEREO_DIBR_VIEW_CENTER));
        QVERIFY(find("smooth")->property("checked").toBool());
        QVERIFY(find("reset")->property("enabled").toBool());
        // a control changes the settings
        QMetaObject::invokeMethod(find("strength"), "valueModified");
        find("strength")->setProperty("value", 44);
        QMetaObject::invokeMethod(find("strength"), "valueModified");
        QCOMPARE(s.strength(), 44.0);
        QMetaObject::invokeMethod(find("reset"), "clicked");
        QVERIFY(s.isDefault());
        QCOMPARE(find("strength")->property("value").toInt(), 58);
    }
};

QTEST_MAIN(DepthControlsTest)
#include "depthcontrolstest.moc"
