// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoicemenu.h"
#include "filechoicelist_p.h"
#include "picture.h"

#include <QActionGroup>
#include <QHash>

using namespace Stereo;

// The widgets' face of the one list (filechoicelist_p.h): the entries, their words and the writes to the store are the
// list's, shared with the QML FileChoice; this class only shows them as a QMenu.
struct FileChoiceMenu::Private {
    FileChoiceList *list = nullptr;
    QActionGroup *inputs = nullptr;
    QList<QAction *> actions;
    QHash<int, QMenu *> groups;
    QAction *swap = nullptr;
    QAction *save = nullptr;
};

FileChoiceMenu::FileChoiceMenu(QWidget *parent)
    : QMenu(parent)
    , d(new Private)
{
    using Group = FileChoiceList::Group;
    d->list = new FileChoiceList(this);
    setTitle(FileChoiceList::title());
    d->inputs = new QActionGroup(this);
    const QList<FileChoiceList::Entry> &entries = d->list->entries();
    Group shown = Group::Top;
    for (int index = 0; index < entries.size(); ++index) {
        const Group group = entries.at(index).group;
        QMenu *menu = this;
        if (group == Group::Whole) {
            if (shown == Group::Top) {
                addSeparator();
            }
        } else if (group != Group::Top) {
            if (!d->groups.contains(int(group))) {
                d->groups.insert(int(group), addMenu(FileChoiceList::groupTitle(group)));
            }
            menu = d->groups.value(int(group));
        }
        shown = group;
        QAction *action = menu->addAction(entries.at(index).text);
        action->setCheckable(true);
        d->inputs->addAction(action);
        d->actions.append(action);
        connect(action, &QAction::triggered, this, [this, index] {
            d->list->choose(index);
        });
    }
    addSeparator();

    d->swap = addAction(FileChoiceList::swapText());
    d->swap->setCheckable(true);
    d->swap->setToolTip(FileChoiceList::swapToolTip());
    d->swap->setWhatsThis(FileChoiceList::swapWhatsThis());
    connect(d->swap, &QAction::triggered, this, [this](bool swap) {
        d->list->setSwapEyes(swap);
    });
    addSeparator();
    d->save = addAction(FileChoiceList::saveText());
    d->save->setToolTip(FileChoiceList::saveToolTip());
    connect(d->save, &QAction::triggered, this, [this] {
        d->list->save();
    });
    connect(d->list, &FileChoiceList::changed, this, &FileChoiceMenu::changed);
    connect(d->list, &FileChoiceList::failed, this, &FileChoiceMenu::failed);
    connect(d->list, &FileChoiceList::stateChanged, this, [this] {
        using Group = FileChoiceList::Group;
        for (auto group = d->groups.cbegin(); group != d->groups.cend(); ++group) {
            group.value()->menuAction()->setVisible(d->list->isGroupOffered(Group(group.key())));
        }
        for (int index = 0; index < d->actions.size(); ++index) {
            QAction *action = d->actions.at(index);
            action->setVisible(d->list->isOffered(index));
            action->setEnabled(d->list->isEnabled());
            action->setChecked(d->list->isChosen(index));
            action->setText(d->list->text(index));
        }
        d->swap->setEnabled(d->list->isSwapEnabled());
        d->swap->setChecked(d->list->swapEyes());
        d->save->setEnabled(d->list->isSaveEnabled());
    });
    setFiles({});
}

FileChoiceMenu::~FileChoiceMenu() { delete d; }

QStringList FileChoiceMenu::files() const { return d->list->files(); }

QAction *FileChoiceMenu::swapEyesAction() const { return d->swap; }

FileChoiceMenu::Content FileChoiceMenu::content() const { return d->list->video() ? Content::Video : Content::Picture; }

void FileChoiceMenu::setContent(Content content) { d->list->setVideo(content == Content::Video); }

void FileChoiceMenu::setShownAsPair(bool pair) { d->list->setShownAsPair(pair); }

void FileChoiceMenu::setShownAsOneView(const QList<Picture::Layout> &layouts) { d->list->setShownAsOneView(layouts); }

void FileChoiceMenu::setFiles(const QStringList &paths) { d->list->setFiles(paths); }

Enable3DAction::Enable3DAction(QObject *parent)
    : QAction(FileChoiceList::enable3DText(), parent)
{
    setObjectName(QStringLiteral("stereo_enable_3d"));
    setToolTip(FileChoiceList::enable3DToolTip());
    setCheckable(true);
    setChecked(true);
}

Enable3DAction::~Enable3DAction() = default;
