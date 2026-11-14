// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <stereo_layer_export.h>

namespace Stereo
{
class FileChoice;
class FileChoiceList;

// One group of the per-file choice list as a model a QML menu shows: the entries at the top, the packings one picture
// holds whole, or one of the sub-menus. Roles: label (the entry's words), chosen (it is the file's choice), offered (it
// is shown for this kind of file) and entryId (its name whatever the language: the Stereo3DLayout word).
class STEREO_LAYER_EXPORT FileChoiceEntries : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The groups of the list are a FileChoice's")
    // The sub-menu's title; empty for the two groups shown in the menu itself
    Q_PROPERTY(QString title READ title CONSTANT)
    // Whether the group is shown: the packings of a video are offered for a video, and for a file whose choice is one
    Q_PROPERTY(bool offered READ offered NOTIFY offeredChanged)
public:
    enum Role { LabelRole = Qt::UserRole + 1, ChosenRole, OfferedRole, EntryIdRole };
    ~FileChoiceEntries() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString title() const;
    bool offered() const;
    // The person chose the entry at row: kept for every file of the FileChoice
    Q_INVOKABLE void choose(int row);

Q_SIGNALS:
    void offeredChanged();

private:
    friend class FileChoice;
    FileChoiceEntries(FileChoiceList *list, int group, QObject *parent);
    void update();
    FileChoiceList *m_list;
    int m_group;
    QList<int> m_rows;
    bool m_offered = true;
};

// The per-file choice of the shared store for a QML program: the same list, words, state and writes as the widgets'
// Stereo::FileChoiceMenu (filechoicemenu.h ties each packing to its clause), with none of them repeated in QML. The
// program names the files it shows (sources), says whether it shows them as a pair (shownAsPair), and shows the menu
// with FileChoiceMenu { choice: ... } or builds its own from the groups; on changed() it shows the files again
// (StereoPicture.refresh()), and it says failed() where it shows its errors. The choice is kept in the programs' state
// and never in the file; save() writes the file only because the person chose it. A source that is not a local file has
// no place in the store: available is then false and nothing is offered. A program with a FileChoice also keeps every
// file's choice with it when the desktop's file jobs rename or move it (Stereo::FileChoiceMoves, one for the program),
// and changed() says so for a file this object names.
class STEREO_LAYER_EXPORT FileChoice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<QUrl> sources READ sources WRITE setSources NOTIFY sourcesChanged)
    // The local files among the sources: the ones the choice is kept for
    Q_PROPERTY(QStringList files READ files NOTIFY sourcesChanged)
    Q_PROPERTY(Content content READ content WRITE setContent NOTIFY contentChanged)
    // Whether the program shows the files as pairs now; false for one shown flat, which is then offered no swap
    Q_PROPERTY(bool shownAsPair READ shownAsPair WRITE setShownAsPair NOTIFY shownAsPairChanged)
    // The packings (Stereo::Picture::Layout values) the program cannot show in 3D where the menu is
    Q_PROPERTY(QList<int> shownAsOneView READ shownAsOneView WRITE setShownAsOneView NOTIFY shownAsOneViewChanged)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    // The name of the entry that is the first file's choice: its Stereo3DLayout word, as a file's mark has it
    // ("sideBySideRightFirst", "topBottomLeftFirst", "anaglyphCyanRed", "mono" for Not 3D), or "automatic" for none
    Q_PROPERTY(QString chosen READ chosen NOTIFY stateChanged)
    Q_PROPERTY(Stereo::FileChoiceEntries *top READ top CONSTANT)
    Q_PROPERTY(Stereo::FileChoiceEntries *whole READ whole CONSTANT)
    Q_PROPERTY(Stereo::FileChoiceEntries *interleaved READ interleaved CONSTANT)
    Q_PROPERTY(Stereo::FileChoiceEntries *sequence READ sequence CONSTANT)
    Q_PROPERTY(Stereo::FileChoiceEntries *anaglyph READ anaglyph CONSTANT)
    Q_PROPERTY(Stereo::FileChoiceEntries *depth READ depth CONSTANT)
    Q_PROPERTY(bool swapEyes READ swapEyes WRITE setSwapEyes NOTIFY stateChanged)
    Q_PROPERTY(bool swapEnabled READ swapEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString swapText READ swapText CONSTANT)
    Q_PROPERTY(QString swapToolTip READ swapToolTip CONSTANT)
    Q_PROPERTY(bool saveEnabled READ saveEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString saveText READ saveText CONSTANT)
    Q_PROPERTY(QString saveToolTip READ saveToolTip CONSTANT)
public:
    enum Content { Picture, Video };
    Q_ENUM(Content)

    explicit FileChoice(QObject *parent = nullptr);
    ~FileChoice() override;

    QList<QUrl> sources() const;
    void setSources(const QList<QUrl> &sources);
    QStringList files() const;
    Content content() const;
    void setContent(Content content);
    bool shownAsPair() const;
    void setShownAsPair(bool pair);
    QList<int> shownAsOneView() const;
    void setShownAsOneView(const QList<int> &layouts);
    QString title() const;
    bool available() const;
    QString chosen() const;
    FileChoiceEntries *top() const;
    FileChoiceEntries *whole() const;
    FileChoiceEntries *interleaved() const;
    FileChoiceEntries *sequence() const;
    FileChoiceEntries *anaglyph() const;
    FileChoiceEntries *depth() const;
    bool swapEyes() const;
    void setSwapEyes(bool swap);
    bool swapEnabled() const;
    QString swapText() const;
    QString swapToolTip() const;
    bool saveEnabled() const;
    QString saveText() const;
    QString saveToolTip() const;

    // For a program's shortcut: chooses the entry of that name (as chosen gives it) for every file; false when there
    // is no such entry
    Q_INVOKABLE bool choose(const QString &entryId);
    // Only after the person chose it: writes the correction into the files (the shared save) and unmarks the choice
    Q_INVOKABLE void save();
    // Reads the store again, after another program may have changed it
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void sourcesChanged();
    void contentChanged();
    void shownAsPairChanged();
    void shownAsOneViewChanged();
    void stateChanged();
    // The choice of these files changed (or was saved into them): show them again
    void changed(const QStringList &paths);
    // A choice could not be kept, or a save was refused: message says which file, why and what was kept
    void failed(const QString &message);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
