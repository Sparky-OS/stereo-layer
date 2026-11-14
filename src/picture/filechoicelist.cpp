// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoicelist_p.h"

#include <QCoreApplication>
#include <QFileInfo>

using namespace Stereo;

namespace
{
using Layout = Picture::Layout;
using Order = Picture::EyeOrder;
using Anaglyph = Picture::AnaglyphColorPair;
using View = Picture::View;
using Group = FileChoiceList::Group;
using Entry = FileChoiceList::Entry;

// The words are the menu's (the context Stereo::FileChoiceMenu): one context for the widgets' and the QML face, so the
// catalogues hold each sentence once.

// one flat picture whatever else is kept; a view of one eye is another program's choice and reads as a pair here
bool sameInput(const Entry &entry, const Picture::Options &options)
{
    if (options.view == View::Mono) {
        return entry.view == View::Mono;
    }
    return entry.view == View::Stereo && entry.layout == options.inputLayout && entry.anaglyph == options.anaglyph
        && (entry.layout == Layout::None || entry.order == options.inputOrder);
}
}

FileChoiceList::FileChoiceList(QObject *parent)
    : QObject(parent)
{
    // The packings of RFC 9559 StereoMode and H.264/H.265 Table D-8 (filechoicemenu.h has the list by clause), the
    // anaglyph colour pairs, and the picture-with-depth layouts of docs/depth-marks.md. Views that alternate in time or
    // in a stream's blocks are not in one picture, so those entries are a video's; MultiView and DepthSeparate need a
    // second image, layer or track in the file and are nothing to choose by hand.
    const auto add = [this](const char *id, const QString &text, Group group, Layout layout = Layout::None, Order order = Order::LeftFirst,
                         Anaglyph anaglyph = Anaglyph::None, View view = View::Stereo, bool videoOnly = false) {
        m_entries.append({QString::fromLatin1(id), text, group, layout, order, anaglyph, view, videoOnly});
    };
    add("automatic", QCoreApplication::translate("Stereo::FileChoiceMenu", "Automatic"), Group::Top);
    add("not-3d", QCoreApplication::translate("Stereo::FileChoiceMenu", "Not 3D"), Group::Top, Layout::None, Order::LeftFirst, Anaglyph::None, View::Mono);
    add("side-by-side-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side, left eye first"), Group::Whole, Layout::SideBySide,
        Order::LeftFirst);
    add("side-by-side-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side, right eye first"), Group::Whole, Layout::SideBySide,
        Order::RightFirst);
    add("top-and-bottom-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Top and bottom, left eye first"), Group::Whole, Layout::TopAndBottom,
        Order::LeftFirst);
    add("top-and-bottom-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Top and bottom, right eye first"), Group::Whole,
        Layout::TopAndBottom, Order::RightFirst);
    add("rows-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Rows, left eye first"), Group::Interleaved, Layout::Rows, Order::LeftFirst);
    add("rows-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Rows, right eye first"), Group::Interleaved, Layout::Rows, Order::RightFirst);
    add("columns-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Columns, left eye first"), Group::Interleaved, Layout::Columns,
        Order::LeftFirst);
    add("columns-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Columns, right eye first"), Group::Interleaved, Layout::Columns,
        Order::RightFirst);
    add("checkerboard-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Checkerboard, left eye first"), Group::Interleaved,
        Layout::Checkerboard, Order::LeftFirst);
    add("checkerboard-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Checkerboard, right eye first"), Group::Interleaved,
        Layout::Checkerboard, Order::RightFirst);
    add("quincunx-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side quincunx, left eye first"), Group::Interleaved,
        Layout::SideBySideQuincunx, Order::LeftFirst);
    add("quincunx-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side quincunx, right eye first"), Group::Interleaved,
        Layout::SideBySideQuincunx, Order::RightFirst);
    add("frames-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Frames in turn, left eye first"), Group::Sequence, Layout::FrameSequential,
        Order::LeftFirst, Anaglyph::None, View::Stereo, true);
    add("frames-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Frames in turn, right eye first"), Group::Sequence, Layout::FrameSequential,
        Order::RightFirst, Anaglyph::None, View::Stereo, true);
    add("block-left", QCoreApplication::translate("Stereo::FileChoiceMenu", "Both views in one block, left eye first"), Group::Sequence, Layout::BlockLaced,
        Order::LeftFirst, Anaglyph::None, View::Stereo, true);
    add("block-right", QCoreApplication::translate("Stereo::FileChoiceMenu", "Both views in one block, right eye first"), Group::Sequence,
        Layout::BlockLaced, Order::RightFirst, Anaglyph::None, View::Stereo, true);
    add("anaglyph-red-cyan", QCoreApplication::translate("Stereo::FileChoiceMenu", "Red/cyan"), Group::Anaglyph, Layout::None, Order::LeftFirst,
        Anaglyph::RedCyan);
    add("anaglyph-green-magenta", QCoreApplication::translate("Stereo::FileChoiceMenu", "Green/magenta"), Group::Anaglyph, Layout::None, Order::LeftFirst,
        Anaglyph::GreenMagenta);
    add("anaglyph-amber-blue", QCoreApplication::translate("Stereo::FileChoiceMenu", "Amber/blue"), Group::Anaglyph, Layout::None, Order::LeftFirst,
        Anaglyph::AmberBlue);
    add("depth-side-by-side-picture", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side, picture first"), Group::Depth,
        Layout::DepthSideBySide, Order::LeftFirst);
    add("depth-side-by-side-depth", QCoreApplication::translate("Stereo::FileChoiceMenu", "Side by side, depth first"), Group::Depth,
        Layout::DepthSideBySide, Order::RightFirst);
    add("depth-top-and-bottom-picture", QCoreApplication::translate("Stereo::FileChoiceMenu", "Top and bottom, picture first"), Group::Depth,
        Layout::DepthTopAndBottom, Order::LeftFirst);
    add("depth-top-and-bottom-depth", QCoreApplication::translate("Stereo::FileChoiceMenu", "Top and bottom, depth first"), Group::Depth,
        Layout::DepthTopAndBottom, Order::RightFirst);
    add("depth-graphics", QCoreApplication::translate("Stereo::FileChoiceMenu", "Four rows: picture, depth, graphics, graphics depth"), Group::Depth,
        Layout::DepthGraphics, Order::LeftFirst);
    add("depth-background", QCoreApplication::translate("Stereo::FileChoiceMenu", "Four quarters: picture, depth, background, background depth"),
        Group::Depth, Layout::DepthBackground, Order::LeftFirst);
    refresh();
}

const QList<FileChoiceList::Entry> &FileChoiceList::entries() const { return m_entries; }

int FileChoiceList::indexOf(const QString &id) const
{
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

QString FileChoiceList::title() { return QCoreApplication::translate("Stereo::FileChoiceMenu", "3D Input Format"); }

QString FileChoiceList::groupTitle(Group group)
{
    switch (group) {
    case Group::Interleaved:
        return QCoreApplication::translate("Stereo::FileChoiceMenu", "Interleaved");
    case Group::Sequence:
        return QCoreApplication::translate("Stereo::FileChoiceMenu", "One view after the other");
    case Group::Anaglyph:
        return QCoreApplication::translate("Stereo::FileChoiceMenu", "Anaglyph");
    case Group::Depth:
        return QCoreApplication::translate("Stereo::FileChoiceMenu", "Picture and depth");
    default:
        return {};
    }
}

QString FileChoiceList::swapText() { return QCoreApplication::translate("Stereo::FileChoiceMenu", "Swap left and right eyes"); }

QString FileChoiceList::swapToolTip() { return QCoreApplication::translate("Stereo::FileChoiceMenu", "Show each eye the other view of this file"); }

QString FileChoiceList::swapWhatsThis()
{
    return QCoreApplication::translate("Stereo::FileChoiceMenu",
        "A stereo picture holds a view for each eye. When a file has them the wrong way round, this swaps them for this "
        "file. The choice is kept with the program's own state: the file is not changed.");
}

QString FileChoiceList::saveText() { return QCoreApplication::translate("Stereo::FileChoiceMenu", "Save the correction into the file"); }

QString FileChoiceList::saveToolTip()
{
    return QCoreApplication::translate("Stereo::FileChoiceMenu", "Write this correction into the file itself, so every program and device reads it");
}

QStringList FileChoiceList::files() const { return m_files; }

void FileChoiceList::setFiles(const QStringList &paths)
{
    m_files = paths;
    refresh();
}

bool FileChoiceList::video() const { return m_video; }

void FileChoiceList::setVideo(bool video)
{
    m_video = video;
    refresh();
}

bool FileChoiceList::shownAsPair() const { return m_pair; }

void FileChoiceList::setShownAsPair(bool pair)
{
    m_pair = pair;
    refresh();
}

QList<Picture::Layout> FileChoiceList::shownAsOneView() const { return m_oneView; }

void FileChoiceList::setShownAsOneView(const QList<Picture::Layout> &layouts)
{
    m_oneView = layouts;
    refresh();
}

void FileChoiceList::refresh()
{
    const bool any = !m_files.isEmpty();
    m_first = any ? Picture::fileChoice(m_files.first()) : Picture::Options();
    m_chosen = -1;
    for (int index = 0; any && index < m_entries.size(); ++index) {
        if (sameInput(m_entries.at(index), m_first)) {
            m_chosen = index;
            break;
        }
    }
    // one flat picture has no eyes to swap; a swap that is kept can always be taken back
    m_swapEnabled = any && m_first.view != View::Mono && (m_pair || m_first.swapEyes);
    m_saveEnabled = std::any_of(m_files.cbegin(), m_files.cend(), [](const QString &file) {
        return Picture::hasFileChoice(file);
    });
    Q_EMIT stateChanged();
}

QString FileChoiceList::text(int index) const
{
    const Entry &entry = m_entries.at(index);
    return entry.layout != Layout::None && m_oneView.contains(entry.layout)
        ? QCoreApplication::translate("Stereo::FileChoiceMenu", "%1 (one view shown here)").arg(entry.text)
        : entry.text;
}

int FileChoiceList::chosen() const { return m_chosen; }

bool FileChoiceList::isChosen(int index) const { return index == m_chosen; }

bool FileChoiceList::isOffered(int index) const
{
    // an entry of the other kind of file is not offered; one that is this file's choice is always shown, so the list
    // never hides what is kept
    return m_video || !m_entries.at(index).videoOnly || index == m_chosen;
}

bool FileChoiceList::isEnabled() const { return !m_files.isEmpty(); }

bool FileChoiceList::isGroupOffered(Group group) const
{
    return group != Group::Sequence || m_video || (m_chosen >= 0 && m_entries.at(m_chosen).group == Group::Sequence);
}

bool FileChoiceList::swapEyes() const { return !m_files.isEmpty() && m_first.swapEyes; }

bool FileChoiceList::isSwapEnabled() const { return m_swapEnabled; }

bool FileChoiceList::isSaveEnabled() const { return m_saveEnabled; }

void FileChoiceList::choose(int index)
{
    if (index < 0 || index >= m_entries.size()) {
        return;
    }
    const Entry entry = m_entries.at(index);
    QStringList done;
    for (const QString &file : std::as_const(m_files)) {
        Picture::Options choice = Picture::fileChoice(file);
        choice.inputLayout = entry.layout;
        choice.inputOrder = entry.order;
        choice.anaglyph = entry.anaglyph;
        choice.view = entry.view;
        QString error;
        if (Picture::setFileChoice(file, choice, &error)) {
            done.append(file);
        } else {
            Q_EMIT failed(QFileInfo(file).fileName() + QLatin1String(": ") + error);
        }
    }
    refresh();
    if (!done.isEmpty()) {
        Q_EMIT changed(done);
    }
}

void FileChoiceList::setSwapEyes(bool swap)
{
    QStringList done;
    for (const QString &file : std::as_const(m_files)) {
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
    refresh();
    if (!done.isEmpty()) {
        Q_EMIT changed(done);
    }
}

void FileChoiceList::save()
{
    QStringList done;
    for (const QString &file : std::as_const(m_files)) {
        if (!Picture::hasFileChoice(file)) {
            continue;
        }
        const Picture::SaveResult result = Picture::saveFileChoice(file);
        if (result.success) {
            done.append(file);
        } else {
            Q_EMIT failed(
                QCoreApplication::translate("Stereo::FileChoiceMenu", "%1 was not changed and its correction is kept: %2").arg(QFileInfo(file).fileName(), result.error));
        }
    }
    refresh();
    if (!done.isEmpty()) {
        Q_EMIT changed(done);
    }
}
