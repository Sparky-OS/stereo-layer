// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

// AnimatedPicture with no image reader for the file's format: a reason, never an empty picture taken for a frame.
#include "animatedpicture.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using Stereo::AnimatedPicture;

class AnimatedPictureNoReaderTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_plugins;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // no image format plugins at all: only Qt's built-in readers remain, and JPEG XL is not one of them
        QVERIFY(m_plugins.isValid());
        QCoreApplication::setLibraryPaths({m_plugins.path()});
    }

    void missingReader()
    {
        AnimatedPicture animation(QFINDTESTDATA("readers/jxlstereo/animated-pair.jxl"));
        QVERIFY(animation.isNull());
        QVERIFY(!animation.isStereo());
        QVERIFY(!animation.reason().isEmpty());
        QVERIFY(animation.pairAt(0).original.isNull());
    }
};

QTEST_GUILESS_MAIN(AnimatedPictureNoReaderTest)
#include "animatedpicturenoreadertest.moc"
