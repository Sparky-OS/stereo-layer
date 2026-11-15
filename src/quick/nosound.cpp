// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "nosound.h"

#include <QCoreApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

namespace Stereo
{
NoSound::NoSound(QObject *parent)
    : QObject(parent)
{
}

NoSound::~NoSound() = default;

namespace
{
// The library's catalogue is installed by a startup function of the library (ecm_create_qm_loader). A library that a program
// brings in after its application exists, as the QML plugin of a Qt Quick player, may be read before that has run; then the
// sentence is looked up once more after installing the catalogue of the program's language here.
void installCatalogue()
{
    if (!QCoreApplication::instance()) {
        return;
    }
    QStringList languages = QLocale().uiLanguages();
    for (QString &language : languages) {
        language.replace(QLatin1Char('-'), QLatin1Char('_'));
    }
    for (int i = 0; i < languages.size(); ++i) {
        const int cut = languages.at(i).indexOf(QLatin1Char('_'));
        if (cut > 0) {
            languages.insert(i + 1, languages.at(i).left(cut));
            ++i;
        }
    }
    for (const QString &language : std::as_const(languages)) {
        if (language == QLatin1String("en")) {
            return;
        }
        const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                    QStringLiteral("locale/") + language + QStringLiteral("/LC_MESSAGES/stereo-layer_qt.qm"));
        if (path.isEmpty()) {
            continue;
        }
        auto *translator = new QTranslator(QCoreApplication::instance());
        if (translator->load(path)) {
            QCoreApplication::installTranslator(translator);
            return;
        }
        delete translator;
    }
}
}

QString NoSound::sentence()
{
    static const char source[] = "This film has no sound.";
    static bool retried = false;
    QString text = QCoreApplication::translate("Stereo::NoSound", source);
    if (!retried && text == QLatin1String(source)) {
        retried = true;
        installCatalogue();
        text = QCoreApplication::translate("Stereo::NoSound", source);
    }
    return text;
}

bool NoSound::applies(bool loaded, bool hasVideo, int audioTracks) { return loaded && hasVideo && audioTracks <= 0; }

QString NoSound::text() const { return sentence(); }

bool NoSound::saysFor(bool loaded, bool hasVideo, int audioTracks) const { return applies(loaded, hasVideo, audioTracks); }
}
