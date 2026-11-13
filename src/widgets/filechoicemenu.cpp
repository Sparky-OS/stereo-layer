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

struct FileChoiceMenu::Private {
    QStringList files;
    QActionGroup *inputs = nullptr;
    QList<QPair<QAction *, Input>> entries;
    QAction *swap = nullptr;
    QAction *save = nullptr;
};

FileChoiceMenu::FileChoiceMenu(QWidget *parent)
    : QMenu(parent)
    , d(new Private)
{
    setTitle(tr("3D Input Format"));
    d->inputs = new QActionGroup(this);
    const auto add = [this](QMenu *menu, const QString &text, const Input &input) {
        QAction *action = menu->addAction(text);
        action->setCheckable(true);
        d->inputs->addAction(action);
        d->entries.append({action, input});
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
    // The packings of RFC 9559 StereoMode and H.264/H.265 Table D-8 a still can hold (Picture::Layout: views that
    // alternate in time or in a stream's blocks are not in one picture, and MultiView and DepthSeparate need a second
    // image in the file), the anaglyph colour pairs, and the picture-with-depth layouts of docs/depth-marks.md.
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

void FileChoiceMenu::setFiles(const QStringList &paths)
{
    d->files = paths;
    const bool any = !paths.isEmpty();
    const Picture::Options first = any ? Picture::fileChoice(paths.first()) : Picture::Options();
    const Input input = inputOf(first);
    for (const auto &entry : std::as_const(d->entries)) {
        entry.first->setEnabled(any);
        entry.first->setChecked(any && entry.second == input);
    }
    // one flat picture has no eyes to swap
    d->swap->setEnabled(any && input.view != View::Mono);
    d->swap->setChecked(any && first.swapEyes);
    d->save->setEnabled(std::any_of(paths.cbegin(), paths.cend(), [](const QString &file) {
        return Picture::hasFileChoice(file);
    }));
}
