// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <memory>
class QmlImportTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void unlinkedConsumer()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport org.sparky.stereo 1.0\nStereoPicture { width: 16; height: 8 }", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> item(component.create());
        QVERIFY(item);
        QCOMPARE(item->property("zoom").toReal(), qreal(1));
        QCOMPARE(item->property("width").toReal(), qreal(16));
        QVERIFY(!item->property("stereoActive").toBool());
        QVERIFY(item->property("colorDisclaimer").toString().contains(QStringLiteral("never changed")));
    }
};
QTEST_MAIN(QmlImportTest)
#include "qmlimporttest.moc"
