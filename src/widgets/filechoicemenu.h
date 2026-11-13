// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QMenu>
#include <QStringList>
#include <stereo_layer_export.h>

namespace Stereo
{
// The person's corrections of the input of one media file, as a menu any program shows for a file it shows in 3D,
// declared or not (a declaration can be wrong): the input packing chosen by hand, every packing a still can hold in
// either order, "Not 3D", and "Swap left and right eyes". The choice is kept per file in the shared store
// (Picture::setFileChoice()), never in the file, and applies at once to every program built on the layer. "Save the
// correction into the file" writes the file only because the person chose it, through the shared save
// (Picture::saveFileChoice()); the per-file choice is then unmarked again. It holds no output format: the screen's
// format is the compositor's. The program gives the files (setFiles()) and shows the menu only for files it shows
// as pairs or that have a choice (Picture::hasFileChoice()); it shows them again on changed(), and says failed().
class STEREO_LAYER_EXPORT FileChoiceMenu : public QMenu
{
    Q_OBJECT
public:
    explicit FileChoiceMenu(QWidget *parent = nullptr);
    ~FileChoiceMenu() override;

    // The local files the menu acts on; the marks show the choice of the first.
    void setFiles(const QStringList &paths);
    QStringList files() const;

    // The swap as an action of its own, for a program's shortcut or toolbar: the same words, state and effect.
    QAction *swapEyesAction() const;

Q_SIGNALS:
    // The choice of these files changed (or was saved into them): show them again.
    void changed(const QStringList &paths);
    // A choice could not be kept, or a save was refused: message says which file, why and what was kept.
    void failed(const QString &message);

private:
    struct Private;
    Private *d;
};
}
