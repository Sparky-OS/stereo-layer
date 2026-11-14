// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoicemenu.h"
#include "picture.h"

#include <QActionGroup>
#include <QFileInfo>

using namespace Stereo;

namespace
{
using Layout = Picture::Layout;
using Order = Picture::EyeOrder;
using Anaglyph = Picture::AnaglyphColorPair;
using View = Picture::View;

// what one entry of the list chooses; the swap is kept apart
struct Input {
    Layout layout = Layout::None;
    Order order = Order::LeftFirst;
    Anaglyph anaglyph = Anaglyph::None;
    View view = View::Stereo;
    bool operator==(const Input &other) const
    {
        return layout == other.layout && anaglyph == other.anaglyph && view == other.view && (layout == Layout::None || order == other.order);
    }
};

Input inputOf(const Picture::Options &options)
{
    // one flat picture whatever else is kept; a view of one eye is another program's choice and reads as a pair here
    if (options.view == View::Mono) {
        return {Layout::None, Order::LeftFirst, Anaglyph::None, View::Mono};
    }
    return {options.inputLayout, options.inputOrder, options.anaglyph, View::Stereo};
}
}

struct Entry {
    QAction *action = nullptr;
    Input input;
    QString text;
    bool videoOnly = false;
};

struct FileChoiceMenu::Private {
    QStringList files;
    QActionGroup *inputs = nullptr;
    QList<Entry> entries;
    QAction *swap = nullptr;
    QAction *save = nullptr;
    QMenu *sequence = nullptr;
    Content content = Content::Picture;
    bool pair = true;
    QList<Layout> oneView;
};

FileChoiceMenu::FileChoiceMenu(QWidget *parent)
    : QMenu(parent)
    , d(new Private)
{
    setTitle(tr("3D Input Format"));
    d->inputs = new QActionGroup(this);
    const auto add = [this](QMenu *menu, const QString &text, const Input &input, bool videoOnly = false) {
        QAction *action = menu->addAction(text);
        action->setCheckable(true);
        d->inputs->addAction(action);
        d->entries.append({action, input, text, videoOnly});
        connect(action, &QAction::triggered, this, [this, input] {
            QStringList done;
            for (const QString &file : std::as_const(d->files)) {
                Picture::Options choice = Picture::fileChoice(file);
                choice.inputLayout = input.layout;
                choice.inputOrder = input.order;
                choice.anaglyph = input.anaglyph;
                choice.view = input.view;
                QString error;
                if (Picture::setFileChoice(file, choice, &error)) {
                    done.append(file);
                } else {
                    Q_EMIT failed(QFileInfo(file).fileName() + QLatin1String(": ") + error);
                }
            }
            setFiles(d->files);
            if (!done.isEmpty()) {
                Q_EMIT changed(done);
            }
        });
    };
    // The packings of RFC 9559 StereoMode and H.264/H.265 Table D-8 (filechoicemenu.h has the list by clause), the
    // anaglyph colour pairs, and the picture-with-depth layouts of docs/depth-marks.md. Views that alternate in time or
    // in a stream's blocks are not in one picture, so those entries are a video's; MultiView and DepthSeparate need a
    // second image, layer or track in the file and are nothing to choose by hand.
    add(this, tr("Automatic"), {});
    add(this, tr("Not 3D"), {Layout::None, Order::LeftFirst, Anaglyph::None, View::Mono});
    addSeparator();
    add(this, tr("Side by side, left eye first"), {Layout::SideBySide, Order::LeftFirst});
    add(this, tr("Side by side, right eye first"), {Layout::SideBySide, Order::RightFirst});
    add(this, tr("Top and bottom, left eye first"), {Layout::TopAndBottom, Order::LeftFirst});
    add(this, tr("Top and bottom, right eye first"), {Layout::TopAndBottom, Order::RightFirst});
    QMenu *interleaved = addMenu(tr("Interleaved"));
    add(interleaved, tr("Rows, left eye first"), {Layout::Rows, Order::LeftFirst});
    add(interleaved, tr("Rows, right eye first"), {Layout::Rows, Order::RightFirst});
    add(interleaved, tr("Columns, left eye first"), {Layout::Columns, Order::LeftFirst});
    add(interleaved, tr("Columns, right eye first"), {Layout::Columns, Order::RightFirst});
    add(interleaved, tr("Checkerboard, left eye first"), {Layout::Checkerboard, Order::LeftFirst});
    add(interleaved, tr("Checkerboard, right eye first"), {Layout::Checkerboard, Order::RightFirst});
    add(interleaved, tr("Side by side quincunx, left eye first"), {Layout::SideBySideQuincunx, Order::LeftFirst});
    add(interleaved, tr("Side by side quincunx, right eye first"), {Layout::SideBySideQuincunx, Order::RightFirst});
    d->sequence = addMenu(tr("One view after the other"));
    add(d->sequence, tr("Frames in turn, left eye first"), {Layout::FrameSequential, Order::LeftFirst}, true);
    add(d->sequence, tr("Frames in turn, right eye first"), {Layout::FrameSequential, Order::RightFirst}, true);
    add(d->sequence, tr("Both views in one block, left eye first"), {Layout::BlockLaced, Order::LeftFirst}, true);
    add(d->sequence, tr("Both views in one block, right eye first"), {Layout::BlockLaced, Order::RightFirst}, true);
    QMenu *anaglyph = addMenu(tr("Anaglyph"));
    add(anaglyph, tr("Red/cyan"), {Layout::None, Order::LeftFirst, Anaglyph::RedCyan});
    add(anaglyph, tr("Green/magenta"), {Layout::None, Order::LeftFirst, Anaglyph::GreenMagenta});
    add(anaglyph, tr("Amber/blue"), {Layout::None, Order::LeftFirst, Anaglyph::AmberBlue});
    QMenu *depth = addMenu(tr("Picture and depth"));
    add(depth, tr("Side by side, picture first"), {Layout::DepthSideBySide, Order::LeftFirst});
    add(depth, tr("Side by side, depth first"), {Layout::DepthSideBySide, Order::RightFirst});
    add(depth, tr("Top and bottom, picture first"), {Layout::DepthTopAndBottom, Order::LeftFirst});
    add(depth, tr("Top and bottom, depth first"), {Layout::DepthTopAndBottom, Order::RightFirst});
    add(depth, tr("Four rows: picture, depth, graphics, graphics depth"), {Layout::DepthGraphics, Order::LeftFirst});
    add(depth, tr("Four quarters: picture, depth, background, background depth"), {Layout::DepthBackground, Order::LeftFirst});
    addSeparator();

    d->swap = addAction(tr("Swap left and right eyes"));
    d->swap->setCheckable(true);
    d->swap->setToolTip(tr("Show each eye the other view of this file"));
    d->swap->setWhatsThis(tr("A stereo picture holds a view for each eye. When a file has them the wrong way round, this swaps them for this "
                             "file. The choice is kept with the program's own state: the file is not changed."));
    connect(d->swap, &QAction::triggered, this, [this](bool swap) {
        QStringList done;
        for (const QString &file : std::as_const(d->files)) {
            Picture::Options choice = Picture::fileChoice(file);
            if (choice.swapEyes == swap) {
                continue;
            }
            choice.swapEyes = swap;
            QString error;
            if (Picture::setFileChoice(file, choice, &error)) {
                done.append(file);
            } else {
                Q_EMIT failed(QFileInfo(file).fileName() + QLatin1String(": ") + error);
            }
        }
        setFiles(d->files);
        if (!done.isEmpty()) {
            Q_EMIT changed(done);
        }
    });
    addSeparator();
    d->save = addAction(tr("Save the correction into the file"));
    d->save->setToolTip(tr("Write this correction into the file itself, so every program and device reads it"));
    connect(d->save, &QAction::triggered, this, [this] {
        QStringList done;
        for (const QString &file : std::as_const(d->files)) {
            if (!Picture::hasFileChoice(file)) {
                continue;
            }
            const Picture::SaveResult result = Picture::saveFileChoice(file);
            if (result.success) {
                done.append(file);
            } else {
                Q_EMIT failed(tr("%1 was not changed and its correction is kept: %2").arg(QFileInfo(file).fileName(), result.error));
            }
        }
        setFiles(d->files);
        if (!done.isEmpty()) {
            Q_EMIT changed(done);
        }
    });
    setFiles({});
}

FileChoiceMenu::~FileChoiceMenu() { delete d; }

QStringList FileChoiceMenu::files() const { return d->files; }

QAction *FileChoiceMenu::swapEyesAction() const { return d->swap; }

FileChoiceMenu::Content FileChoiceMenu::content() const { return d->content; }

void FileChoiceMenu::setContent(Content content)
{
    d->content = content;
    setFiles(d->files);
}

void FileChoiceMenu::setShownAsPair(bool pair)
{
    d->pair = pair;
    setFiles(d->files);
}

void FileChoiceMenu::setShownAsOneView(const QList<Picture::Layout> &layouts)
{
    d->oneView = layouts;
    setFiles(d->files);
}

void FileChoiceMenu::setFiles(const QStringList &paths)
{
    d->files = paths;
    const bool any = !paths.isEmpty();
    const Picture::Options first = any ? Picture::fileChoice(paths.first()) : Picture::Options();
    const Input input = inputOf(first);
    const bool video = d->content == Content::Video;
    d->sequence->menuAction()->setVisible(video);
    for (const auto &entry : std::as_const(d->entries)) {
        const bool chosen = any && entry.input == input;
        // an entry of the other kind of file is not offered; one that is this file's choice is always shown, so the
        // menu never hides what is kept
        entry.action->setVisible(video || !entry.videoOnly || chosen);
        if (chosen && entry.videoOnly) {
            d->sequence->menuAction()->setVisible(true);
        }
        entry.action->setEnabled(any);
        entry.action->setChecked(chosen);
        entry.action->setText(entry.input.layout != Layout::None && d->oneView.contains(entry.input.layout) ? tr("%1 (one view shown here)").arg(entry.text)
                                                                                                             : entry.text);
    }
    // one flat picture has no eyes to swap; a swap that is kept can always be taken back
    d->swap->setEnabled(any && input.view != View::Mono && (d->pair || first.swapEyes));
    d->swap->setChecked(any && first.swapEyes);
    d->save->setEnabled(std::any_of(paths.cbegin(), paths.cend(), [](const QString &file) {
        return Picture::hasFileChoice(file);
    }));
}
