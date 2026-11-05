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
                                "fillTip", "fillBackground", "fillDepthAided", "fillTelea", "fillNone", "warp", "warpForward", "warpBackward", "warpLayered",
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
            if (box->findData(STEREO_DIBR_FILL_NONE) >= 0 && box->count() == 4)
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
        for (const char *name : {"strength", "zero", "shift", "picture", "fill", "warp", "smooth", "reach", "nearest", "farthest", "reset"})
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
