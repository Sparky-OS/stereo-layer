// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include "picture.h"
#include <QList>
#include <QObject>
#include <QStringList>

namespace Stereo
{
// The one list behind the per-file choice menu, whatever toolkit shows it: the entries with their words, what each
// chooses, which of them is the file's choice now, and the writes to the shared store. The widgets' FileChoiceMenu and
// the QML FileChoice are two faces of this list and hold none of their own, so a QWidget program and a QML program
// offer the same packings in the same words (filechoicemenu.h ties each to its clause).
class FileChoiceList : public QObject
{
    Q_OBJECT
public:
    // Where an entry is shown: at the top (Automatic, Not 3D), with the packings one picture holds whole, or in one of
    // the four sub-menus.
    enum class Group { Top, Whole, Interleaved, Sequence, Anaglyph, Depth };
    struct Entry {
        // a name that does not change with the language, for a program's shortcut ("not-3d", "side-by-side-right")
        QString id;
        QString text;
        Group group = Group::Top;
        Picture::Layout layout = Picture::Layout::None;
        Picture::EyeOrder order = Picture::EyeOrder::LeftFirst;
        Picture::AnaglyphColorPair anaglyph = Picture::AnaglyphColorPair::None;
        Picture::View view = Picture::View::Stereo;
        bool videoOnly = false;
    };

    explicit FileChoiceList(QObject *parent = nullptr);

    const QList<Entry> &entries() const;
    int indexOf(const QString &id) const;

    static QString title();
    static QString groupTitle(Group group);
    static QString swapText();
    static QString swapToolTip();
    static QString swapWhatsThis();
    static QString saveText();
    static QString saveToolTip();

    void setFiles(const QStringList &paths);
    QStringList files() const;
    void setVideo(bool video);
    bool video() const;
    void setShownAsPair(bool pair);
    bool shownAsPair() const;
    void setShownAsOneView(const QList<Picture::Layout> &layouts);
    QList<Picture::Layout> shownAsOneView() const;

    // The state of entry index for the first file: its words (with the note for a packing shown as one view here),
    // whether it is that file's choice, whether it is offered for this kind of file, whether it can be chosen.
    QString text(int index) const;
    bool isChosen(int index) const;
    bool isOffered(int index) const;
    bool isEnabled() const;
    // The entry that is the first file's choice, -1 without a file
    int chosen() const;
    // The sub-menu of a video's packings is offered for a video, and for any file whose choice is one of them
    bool isGroupOffered(Group group) const;
    bool swapEyes() const;
    bool isSwapEnabled() const;
    bool isSaveEnabled() const;

    // The person's acts; each keeps the choice of every file, says failed() for one that could not be kept and
    // changed() for those that were.
    void choose(int index);
    void setSwapEyes(bool swap);
    void save();
    // Reads the store again (another program may have changed it)
    void refresh();

Q_SIGNALS:
    void stateChanged();
    void changed(const QStringList &paths);
    void failed(const QString &message);

private:
    QList<Entry> m_entries;
    QStringList m_files;
    QList<Picture::Layout> m_oneView;
    Picture::Options m_first;
    int m_chosen = -1;
    bool m_video = false;
    bool m_pair = true;
    bool m_swapEnabled = false;
    bool m_saveEnabled = false;
};
}
