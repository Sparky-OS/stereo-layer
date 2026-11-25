// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "animatedpicture.h"

#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

class AnimatedPictureTranslationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reasons_data()
    {
        QTest::addColumn<QByteArray>("locale");
        for (const char *locale : {"en", "pl", "pt_BR", "fr", "de", "es", "ja", "ko", "zh_CN", "zh_TW"}) {
            QTest::newRow(locale) << QByteArray(locale);
        }
    }

    void reasons()
    {
        QFETCH(QByteArray, locale);
        constexpr auto context = "Stereo::AnimatedPicture";
        constexpr auto openError = "This picture cannot be opened: %1.";
        constexpr const char *sources[] = {
            openError,
            "This drawing cannot be read as SVG.",
            "This drawing has no size to be drawn at.",
            "This animated picture keeps its two views as a stereo video track (ISO/IEC 14496-12, stereo video scheme), "
            "and its reader gives each frame only one view wide, so it cannot be shown as a pair.",
            "This JPEG XL picture cannot be shown: no JPEG XL image reader is installed (kimageformat6-plugins).",
            "This picture cannot be shown: no image reader for its format is installed (%1).",
            "Frame %1 of this animated picture cannot be decoded: %2.",
            "This animated picture has no frame %1.",
        };

        QTranslator translator;
        if (locale != "en") {
            const QString path = QStringLiteral(REASON_TRANSLATIONS_DIR "/%1/stereo-layer_qt.qm").arg(QString::fromLatin1(locale));
            QVERIFY2(translator.load(path), qPrintable(path));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            for (const char *source : sources) {
                const QString reason = translator.translate(context, source);
                QVERIFY2(!reason.isEmpty() && reason != QString::fromUtf8(source), source);
                QCOMPARE(reason.count(QStringLiteral("%1")), QString::fromUtf8(source).count(QStringLiteral("%1")));
                QCOMPARE(reason.count(QStringLiteral("%2")), QString::fromUtf8(source).count(QStringLiteral("%2")));
            }
        }

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const Stereo::AnimatedPicture missing(folder.filePath(QStringLiteral("missing.jxl")));
        QVERIFY(missing.isNull());
        const auto prefix = QCoreApplication::translate(context, openError).section(QStringLiteral("%1"), 0, 0);
        QVERIFY2(missing.reason().startsWith(prefix), qPrintable(missing.reason()));
    }
};

QTEST_GUILESS_MAIN(AnimatedPictureTranslationTest)
#include "animatedpicturetranslationtest.moc"
