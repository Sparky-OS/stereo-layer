// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoice.h"
#include "filechoicelist_p.h"
#include "filechoicemoves.h"

#include <QCoreApplication>
#include <QFileInfo>

using namespace Stereo;

using Group = FileChoiceList::Group;

namespace
{
// One follower of renamed and moved files for all the FileChoice objects of a program: it carries the choices in the
// store whether or not one of them names the file, as the widgets' programs do with a FileChoiceMoves of their own.
FileChoiceMoves *sharedMoves()
{
    static FileChoiceMoves *moves = nullptr;
    if (!moves && QCoreApplication::instance()) {
        moves = new FileChoiceMoves(QCoreApplication::instance());
    }
    return moves;
}
}

FileChoiceEntries::FileChoiceEntries(FileChoiceList *list, int group, QObject *parent)
    : QAbstractListModel(parent)
    , m_list(list)
    , m_group(group)
{
    const QList<FileChoiceList::Entry> &entries = list->entries();
    for (int index = 0; index < entries.size(); ++index) {
        if (int(entries.at(index).group) == group) {
            m_rows.append(index);
        }
    }
    m_offered = list->isGroupOffered(Group(group));
}

FileChoiceEntries::~FileChoiceEntries() = default;

int FileChoiceEntries::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant FileChoiceEntries::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const int entry = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return m_list->text(entry);
    case ChosenRole:
        return m_list->isChosen(entry);
    case OfferedRole:
        return m_list->isOffered(entry);
    case EntryIdRole:
        return m_list->entries().at(entry).id;
    default:
        return {};
    }
}

QHash<int, QByteArray> FileChoiceEntries::roleNames() const
{
    return {{LabelRole, "label"}, {ChosenRole, "chosen"}, {OfferedRole, "offered"}, {EntryIdRole, "entryId"}};
}

QString FileChoiceEntries::title() const { return FileChoiceList::groupTitle(Group(m_group)); }

bool FileChoiceEntries::offered() const { return m_offered; }

void FileChoiceEntries::choose(int row)
{
    if (row >= 0 && row < m_rows.size()) {
        m_list->choose(m_rows.at(row));
    }
}

void FileChoiceEntries::update()
{
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(m_rows.size() - 1));
    }
    const bool offered = m_list->isGroupOffered(Group(m_group));
    if (offered != m_offered) {
        m_offered = offered;
        Q_EMIT offeredChanged();
    }
}

struct FileChoice::Private {
    FileChoiceList *list = nullptr;
    QList<QUrl> sources;
    QList<FileChoiceEntries *> groups;
};

FileChoice::FileChoice(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->list = new FileChoiceList(this);
    for (const Group group : {Group::Top, Group::Whole, Group::Interleaved, Group::Sequence, Group::Anaglyph, Group::Depth}) {
        d->groups.append(new FileChoiceEntries(d->list, int(group), this));
    }
    connect(d->list, &FileChoiceList::stateChanged, this, [this] {
        for (FileChoiceEntries *group : std::as_const(d->groups)) {
            group->update();
        }
        Q_EMIT stateChanged();
    });
    connect(d->list, &FileChoiceList::changed, this, &FileChoice::changed);
    connect(d->list, &FileChoiceList::failed, this, &FileChoice::failed);
    if (FileChoiceMoves *moves = sharedMoves()) {
        // a choice arrived with a file this object names (it was renamed or moved to that path): show it again
        connect(moves, &FileChoiceMoves::carried, this, [this](const QStringList &paths) {
            // the follower names files by their real paths
            QStringList mine;
            const QStringList files = d->list->files();
            for (const QString &file : files) {
                if (paths.contains(file) || paths.contains(QFileInfo(file).canonicalFilePath())) {
                    mine.append(file);
                }
            }
            if (!mine.isEmpty()) {
                d->list->refresh();
                Q_EMIT changed(mine);
            }
        });
        connect(moves, &FileChoiceMoves::failed, this, &FileChoice::failed);
    }
}

FileChoice::~FileChoice() = default;

QList<QUrl> FileChoice::sources() const { return d->sources; }

void FileChoice::setSources(const QList<QUrl> &sources)
{
    if (sources == d->sources) {
        return;
    }
    d->sources = sources;
    // the store names local paths: a source that is not a local file has no place in it
    QStringList files;
    for (const QUrl &source : sources) {
        if (source.isLocalFile()) {
            files.append(source.toLocalFile());
        }
    }
    d->list->setFiles(files);
    Q_EMIT sourcesChanged();
}

QStringList FileChoice::files() const { return d->list->files(); }

FileChoice::Content FileChoice::content() const { return d->list->video() ? Video : Picture; }

void FileChoice::setContent(Content content)
{
    if (content == this->content()) {
        return;
    }
    d->list->setVideo(content == Video);
    Q_EMIT contentChanged();
}

bool FileChoice::shownAsPair() const { return d->list->shownAsPair(); }

void FileChoice::setShownAsPair(bool pair)
{
    if (pair == d->list->shownAsPair()) {
        return;
    }
    d->list->setShownAsPair(pair);
    Q_EMIT shownAsPairChanged();
}

QList<int> FileChoice::shownAsOneView() const
{
    QList<int> layouts;
    const QList<Stereo::Picture::Layout> kept = d->list->shownAsOneView();
    for (const Stereo::Picture::Layout layout : kept) {
        layouts.append(int(layout));
    }
    return layouts;
}

void FileChoice::setShownAsOneView(const QList<int> &layouts)
{
    if (layouts == shownAsOneView()) {
        return;
    }
    QList<Stereo::Picture::Layout> kept;
    for (const int layout : layouts) {
        if (layout > int(Stereo::Picture::Layout::None) && layout <= int(Stereo::Picture::Layout::DepthSeparate)) {
            kept.append(Stereo::Picture::Layout(layout));
        }
    }
    d->list->setShownAsOneView(kept);
    Q_EMIT shownAsOneViewChanged();
}

QString FileChoice::title() const { return FileChoiceList::title(); }

bool FileChoice::available() const { return d->list->isEnabled(); }

QString FileChoice::chosen() const
{
    const int chosen = d->list->chosen();
    return chosen < 0 ? QString() : d->list->entries().at(chosen).id;
}

FileChoiceEntries *FileChoice::top() const { return d->groups.at(0); }
FileChoiceEntries *FileChoice::whole() const { return d->groups.at(1); }
FileChoiceEntries *FileChoice::interleaved() const { return d->groups.at(2); }
FileChoiceEntries *FileChoice::sequence() const { return d->groups.at(3); }
FileChoiceEntries *FileChoice::anaglyph() const { return d->groups.at(4); }
FileChoiceEntries *FileChoice::depth() const { return d->groups.at(5); }

bool FileChoice::swapEyes() const { return d->list->swapEyes(); }

void FileChoice::setSwapEyes(bool swap) { d->list->setSwapEyes(swap); }

bool FileChoice::swapEnabled() const { return d->list->isSwapEnabled(); }

QString FileChoice::swapText() const { return FileChoiceList::swapText(); }

QString FileChoice::swapToolTip() const { return FileChoiceList::swapToolTip(); }

bool FileChoice::saveEnabled() const { return d->list->isSaveEnabled(); }

QString FileChoice::saveText() const { return FileChoiceList::saveText(); }

QString FileChoice::saveToolTip() const { return FileChoiceList::saveToolTip(); }

bool FileChoice::choose(const QString &entryId)
{
    const int index = d->list->indexOf(entryId);
    if (index < 0) {
        return false;
    }
    d->list->choose(index);
    return true;
}

void FileChoice::save() { d->list->save(); }

void FileChoice::refresh() { d->list->refresh(); }

Enable3D::Enable3D(QObject *parent)
    : QObject(parent)
{
}

Enable3D::~Enable3D() = default;

QString Enable3D::text() const { return FileChoiceList::enable3DText(); }

QString Enable3D::toolTip() const { return FileChoiceList::enable3DToolTip(); }
