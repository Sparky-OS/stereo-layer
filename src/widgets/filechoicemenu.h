// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QList>
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
//
// The list follows what the files are (setContent()). For pictures: every packing one picture can hold. For videos:
// every packing the layer's video reader knows, each with both orders, which is every value a stream or a container
// can declare:
//   side by side                 H.264 D.2.26 and H.265 D.3.16 frame_packing_arrangement_type 3; RFC 9559 5.1.4.1.28.3
//                                StereoMode 1 and 11; st3d stereo_mode 2
//   top and bottom               type 4; StereoMode 3 and 2; st3d stereo_mode 1
//   rows, columns, checkerboard  types 2, 1 and 0 (H.264); StereoMode 7 and 6, 9 and 8, 5 and 4
//   side by side quincunx        type 3 with quincunx_sampling_flag
//   frames in turn               type 5, temporal interleaving
//   both views in one block      StereoMode 13 and 14
//   anaglyph                     StereoMode 10 (cyan and red) and 12 (green and magenta), and amber and blue
//   picture and depth            HDMI 1.4a 3D_Structure 0100 and 0101 and the layouts of docs/depth-marks.md
// A stream that carries its second view or its depth as a layer or a track of its own (MVC, MV-HEVC, an auxiliary
// depth stream) has nothing to choose by hand: the views are there or they are not. A packing the program cannot show
// in 3D where the menu is (a file manager's thumbnail is one frame, and one frame of a frame sequence is one view) is
// still listed, and the entry says so (setShownAsOneView()): the choice is kept for the programs that can.
class STEREO_LAYER_EXPORT FileChoiceMenu : public QMenu
{
    Q_OBJECT
public:
    explicit FileChoiceMenu(QWidget *parent = nullptr);
    ~FileChoiceMenu() override;

    // The local files the menu acts on; the marks show the choice of the first.
    void setFiles(const QStringList &paths);
    QStringList files() const;

    // What the files are: it decides the list. Pictures by default.
    enum class Content { Picture, Video };
    void setContent(Content content);
    Content content() const;

    // Whether the program shows the files as pairs now. A viewer that offers the list for any picture it shows (an
    // unmarked picture gets its packing by hand there) says false for one it shows flat: the swap and the save are
    // corrections of a pair and are then not offered, unless the file already has one to take back. True by default.
    void setShownAsPair(bool pair);

    // The packings the program cannot show in 3D where this menu is: their entries stay and say that one view is
    // shown here. None by default.
    void setShownAsOneView(const QList<Picture::Layout> &layouts);

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
