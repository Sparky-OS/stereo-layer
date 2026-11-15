// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "filechoice.h"
#include "filechoicemenu.h"
#include "picture.h"
#include "stereo-policy.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace Stereo;

// The QML face of the per-file choice (FileChoice and FileChoiceMenu.qml) against the widgets' menu and the store: one
// list in both, the whole list and never a part of it, the same marks, the same writes, and nothing offered for a file
// the store cannot name.
class FileChoiceQmlTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir{QDir::currentPath() + QStringLiteral("/qmlchoice-XXXXXX")};
    QString m_picture;

    // every checkable entry of the widgets' menu in the order it shows them, shown or not, and the swap
    static QStringList widgetEntries(QMenu *menu)
    {
        QStringList texts;
        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions) {
            if (action->menu()) {
                texts += widgetEntries(action->menu());
            } else if (action->isCheckable()) {
                texts.append(action->text());
            }
        }
        return texts;
    }

    static QStringList modelLabels(QAbstractItemModel *model, bool offeredOnly = false)
    {
        QStringList labels;
        const QHash<int, QByteArray> roles = model->roleNames();
        for (int row = 0; row < model->rowCount(); ++row) {
            if (!offeredOnly || model->data(model->index(row, 0), roles.key("offered")).toBool()) {
                labels.append(model->data(model->index(row, 0), roles.key("label")).toString());
            }
        }
        return labels;
    }

    static QObject *create(QQmlEngine &engine, const QByteArray &qml)
    {
        QQmlComponent component(&engine);
        component.setData(qml, QUrl());
        if (!component.isReady()) {
            qWarning("%s", qPrintable(component.errorString()));
            return nullptr;
        }
        return component.create();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        qputenv("XDG_STATE_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("state"))));
        qputenv("XDG_CACHE_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("cache"))));
        m_picture = m_dir.filePath(QStringLiteral("pair.png"));
        QImage image(64, 16, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(m_picture));
    }

    void init()
    {
        qputenv("XDG_STATE_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("state"))));
        QVERIFY(Picture::setFileChoice(m_picture, {}));
    }

    void oneListInBothFaces()
    {
        // The widgets' menu and the QML type are faces of one list: every entry of the one is in the other, in the same
        // order and words, for a picture and for a video. A subset in QML would fail here.
        FileChoiceMenu menu;
        FileChoice choice;
        for (const bool video : {false, true}) {
            menu.setContent(video ? FileChoiceMenu::Content::Video : FileChoiceMenu::Content::Picture);
            choice.setContent(video ? FileChoice::Video : FileChoice::Picture);
            menu.setFiles({m_picture});
            choice.setSources({QUrl::fromLocalFile(m_picture)});
            QStringList qml;
            for (FileChoiceEntries *group : {choice.top(), choice.whole(), choice.interleaved(), choice.sequence(), choice.anaglyph(), choice.depth()}) {
                qml += modelLabels(group);
            }
            qml.append(choice.swapText());
            const QStringList widgets = widgetEntries(&menu);
            QCOMPARE(qml, widgets);
            QCOMPARE(qml.size(), 28);
            QCOMPARE(choice.title(), menu.title());
            QCOMPARE(choice.sequence()->offered(), video);
        }
        // a picture is offered no packing that needs more than one picture, a video every one
        choice.setContent(FileChoice::Picture);
        QVERIFY(modelLabels(choice.sequence(), true).isEmpty());
        QCOMPARE(modelLabels(choice.sequence()).size(), 4);
        choice.setContent(FileChoice::Video);
        QCOMPARE(modelLabels(choice.sequence(), true).size(), 4);
        // every packing the store can hold is in the list, or is one of the two that nothing chooses by hand
        QSet<int> offered;
        for (FileChoiceEntries *group : {choice.top(), choice.whole(), choice.interleaved(), choice.sequence(), choice.anaglyph(), choice.depth()}) {
            for (int row = 0; row < group->rowCount(); ++row) {
                group->choose(row);
                offered.insert(int(Picture::fileChoice(m_picture).inputLayout));
                QVERIFY(group->data(group->index(row, 0), FileChoiceEntries::ChosenRole).toBool());
            }
        }
        for (int layout = int(Picture::Layout::None); layout <= int(Picture::Layout::DepthSeparate); ++layout) {
            const bool byHand = layout != int(Picture::Layout::MultiView) && layout != int(Picture::Layout::DepthSeparate);
            QVERIFY2(offered.contains(layout) == byHand, qPrintable(QString::number(layout)));
        }
    }

    void theNamesAreTheMarksWords()
    {
        // An entry's name is the Stereo3DLayout word a file's mark uses for the same thing, so the idea has one name: the
        // layer's own mark reader gives, for each name, exactly what the entry keeps for the file. Two entries have no
        // word of a mark: "automatic" (no choice) and the amber and blue pair.
        FileChoice choice;
        choice.setContent(FileChoice::Video);
        choice.setSources({QUrl::fromLocalFile(m_picture)});
        int checked = 0;
        for (FileChoiceEntries *group : {choice.top(), choice.whole(), choice.interleaved(), choice.sequence(), choice.anaglyph(), choice.depth()}) {
            for (int row = 0; row < group->rowCount(); ++row) {
                const QString name = group->data(group->index(row, 0), FileChoiceEntries::EntryIdRole).toString();
                QVERIFY(choice.choose(name));
                QCOMPARE(choice.chosen(), name);
                const Picture::Options kept = Picture::fileChoice(m_picture);
                if (name == QLatin1String("automatic")) {
                    QVERIFY(!Picture::hasFileChoice(m_picture));
                    continue;
                }
                int layout = -1, order = -1, anaglyph = -1;
                const int read = stereo_policy_read_mark(name.toLatin1().constData(), &layout, &order, &anaglyph);
                if (name == QLatin1String("anaglyphAmberBlue")) {
                    QCOMPARE(read, 0);
                    QCOMPARE(kept.anaglyph, Picture::AnaglyphColorPair::AmberBlue);
                    continue;
                }
                QVERIFY2(read == 1, qPrintable(name));
                if (name == QLatin1String("mono")) {
                    QCOMPARE(kept.view, Picture::View::Mono);
                    continue;
                }
                QVERIFY2(int(kept.inputLayout) == layout, qPrintable(name));
                QVERIFY2(int(kept.anaglyph) == anaglyph, qPrintable(name));
                if (layout != STEREO_POLICY_LAYOUT_NONE) {
                    QVERIFY2(int(kept.inputOrder) == order, qPrintable(name));
                }
                ++checked;
            }
        }
        QCOMPARE(checked, 24);
    }

    void choiceIsTheStores()
    {
        FileChoice choice;
        QSignalSpy changed(&choice, &FileChoice::changed);
        QVERIFY(!choice.available());
        QVERIFY(!choice.choose(QStringLiteral("mono")) || changed.isEmpty());
        choice.setSources({QUrl::fromLocalFile(m_picture)});
        QVERIFY(choice.available());
        QCOMPARE(choice.chosen(), QStringLiteral("automatic"));
        QVERIFY(!choice.saveEnabled());
        // by name, as a program's shortcut does
        QVERIFY(choice.choose(QStringLiteral("sideBySideRightFirst")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toStringList(), QStringList{m_picture});
        QCOMPARE(choice.chosen(), QStringLiteral("sideBySideRightFirst"));
        Picture::Options kept = Picture::fileChoice(m_picture);
        QCOMPARE(kept.inputLayout, Picture::Layout::SideBySide);
        QCOMPARE(kept.inputOrder, Picture::EyeOrder::RightFirst);
        QVERIFY(choice.saveEnabled());
        QVERIFY(!choice.choose(QStringLiteral("no-such-entry")));
        // the widgets' menu shows the same mark at once: one store
        FileChoiceMenu menu;
        menu.setFiles({m_picture});
        QStringList checked;
        const QList<QAction *> all = menu.findChildren<QAction *>();
        for (QAction *action : all) {
            if (action->isCheckable() && action->isChecked()) {
                checked.append(action->text());
            }
        }
        QCOMPARE(checked, QStringList{QStringLiteral("Side by side, right eye first")});
        // the swap
        choice.setSwapEyes(true);
        QVERIFY(choice.swapEyes());
        QVERIFY(Picture::fileChoice(m_picture).swapEyes);
        QCOMPARE(Picture::fileChoice(m_picture).inputLayout, Picture::Layout::SideBySide);
        // a choice another program made is read on refresh()
        QVERIFY(Picture::setFileChoice(m_picture, {}));
        QVERIFY(choice.swapEyes());
        choice.refresh();
        QVERIFY(!choice.swapEyes());
        QCOMPARE(choice.chosen(), QStringLiteral("automatic"));
        // Not 3D: one flat picture has no eyes to swap
        QVERIFY(choice.choose(QStringLiteral("mono")));
        QCOMPARE(Picture::fileChoice(m_picture).view, Picture::View::Mono);
        QVERIFY(!choice.swapEnabled());
    }

    void noCorrectionOnAFlatPicture()
    {
        // A picture the program shows flat is offered no swap; a swap that is kept can always be taken back
        FileChoice choice;
        choice.setSources({QUrl::fromLocalFile(m_picture)});
        QVERIFY(choice.swapEnabled());
        choice.setShownAsPair(false);
        QVERIFY(!choice.swapEnabled());
        QVERIFY(!choice.saveEnabled());
        Picture::Options swapped;
        swapped.swapEyes = true;
        QVERIFY(Picture::setFileChoice(m_picture, swapped));
        choice.refresh();
        QVERIFY(choice.swapEnabled());
        QVERIFY(choice.swapEyes());
    }

    void aSourceTheStoreCannotName()
    {
        FileChoice choice;
        QSignalSpy changed(&choice, &FileChoice::changed);
        choice.setSources({QUrl(QStringLiteral("https://example.org/pair.jps"))});
        QVERIFY(choice.files().isEmpty());
        QVERIFY(!choice.available());
        QVERIFY(!choice.swapEnabled());
        QVERIFY(!choice.saveEnabled());
        choice.choose(QStringLiteral("mono"));
        choice.setSwapEyes(true);
        QVERIFY(changed.isEmpty());
    }

    void aStoreThatCannotBeWritten()
    {
        QFile blocker(m_dir.filePath(QStringLiteral("blocked")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        FileChoice choice;
        choice.setSources({QUrl::fromLocalFile(m_picture)});
        qputenv("XDG_STATE_HOME", QFile::encodeName(blocker.fileName()));
        choice.refresh();
        QSignalSpy refused(&choice, &FileChoice::failed);
        QSignalSpy changed(&choice, &FileChoice::changed);
        QVERIFY(choice.choose(QStringLiteral("mono")));
        QCOMPARE(refused.count(), 1);
        QVERIFY(refused.at(0).at(0).toString().contains(QStringLiteral("pair.png")));
        QVERIFY(changed.isEmpty());
        // not shown as kept
        QCOMPARE(choice.chosen(), QStringLiteral("automatic"));
    }

    void theMenuInQml()
    {
        // FileChoiceMenu.qml builds its items from the FileChoice: the top-level rows in the widgets' order, a click on
        // an item writes the store, and the marks follow the store. The menu is not open here, so an item's own
        // visible reads false whatever it is set to: the rows are told by the entries' offered.
        QQmlEngine engine;
        const std::unique_ptr<QObject> root(create(engine,
            "import QtQuick\nimport org.sparky.stereo\n"
            "Item {\n"
            "    property alias choice: choice\n"
            "    property alias menu: menu\n"
            "    property var failures: []\n"
            "    function rows() {\n"
            "        let texts = [];\n"
            "        for (let i = 0; i < menu.count; ++i) {\n"
            "            const item = menu.itemAt(i);\n"
            "            if (item.offered !== false) texts.push(item.subMenu ? '>' + item.subMenu.title : (item.text !== undefined ? item.text : '-'));\n"
            "        }\n"
            "        return texts.join('|');\n"
            "    }\n"
            "    function find(menu, name) {\n"
            "        for (let i = 0; i < menu.count; ++i) {\n"
            "            const item = menu.itemAt(i);\n"
            "            if (item.objectName === name) return item;\n"
            "            if (item.subMenu) { const found = find(item.subMenu, name); if (found) return found; }\n"
            "        }\n"
            "        return null;\n"
            "    }\n"
            "    function click(name) { const item = find(menu, name); if (!item) return false; item.triggered(); return true; }\n"
            "    function marked(name) { const item = find(menu, name); return item !== null && item.checked; }\n"
            "    function usable(name) { const item = find(menu, name); return item !== null && item.enabled; }\n"
            "    FileChoice { id: choice; onFailed: message => failures.push(message) }\n"
            "    FileChoiceMenu { id: menu; choice: choice }\n"
            "}\n"));
        QVERIFY(root);
        auto *choice = root->property("choice").value<FileChoice *>();
        QVERIFY(choice);
        choice->setSources({QUrl::fromLocalFile(m_picture)});
        QVariant rows;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "rows", Q_RETURN_ARG(QVariant, rows)));
        const QStringList picture{QStringLiteral("Automatic"), QStringLiteral("Not 3D"), QStringLiteral("-"), QStringLiteral("Side by side, left eye first"),
            QStringLiteral("Side by side, right eye first"), QStringLiteral("Top and bottom, left eye first"), QStringLiteral("Top and bottom, right eye first"),
            QStringLiteral(">Interleaved"), QStringLiteral(">Anaglyph"), QStringLiteral(">Picture and depth"), QStringLiteral("-"),
            QStringLiteral("Swap left and right eyes"), QStringLiteral("-"), QStringLiteral("Save the correction into the file")};
        QCOMPARE(rows.toString().split(QLatin1Char('|')), picture);
        // a video gains the sub-menu of the packings that are not in one picture, where the widgets' menu has it
        choice->setContent(FileChoice::Video);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "rows", Q_RETURN_ARG(QVariant, rows)));
        QStringList video = picture;
        video.insert(8, QStringLiteral(">One view after the other"));
        QCOMPARE(rows.toString().split(QLatin1Char('|')), video);
        choice->setContent(FileChoice::Picture);
        QVERIFY(QMetaObject::invokeMethod(root.get(), "rows", Q_RETURN_ARG(QVariant, rows)));
        QCOMPARE(rows.toString().split(QLatin1Char('|')), picture);

        const auto call = [&root](const char *function, const QString &name) {
            QVariant result;
            QMetaObject::invokeMethod(root.get(), function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, name));
            return result.toBool();
        };
        QVERIFY(call("marked", QStringLiteral("automatic")));
        QVERIFY(call("click", QStringLiteral("topBottomLeftFirst")));
        QCOMPARE(Picture::fileChoice(m_picture).inputLayout, Picture::Layout::TopAndBottom);
        QVERIFY(call("marked", QStringLiteral("topBottomLeftFirst")));
        QVERIFY(!call("marked", QStringLiteral("automatic")));
        // an entry in a sub-menu
        QVERIFY(call("click", QStringLiteral("anaglyphGreenMagenta")));
        QCOMPARE(Picture::fileChoice(m_picture).anaglyph, Picture::AnaglyphColorPair::GreenMagenta);
        QVERIFY(call("marked", QStringLiteral("anaglyphGreenMagenta")));
        QVERIFY(!call("marked", QStringLiteral("topBottomLeftFirst")));
        // the save is offered once a choice is kept, and no swap on a picture shown flat
        QVERIFY(call("usable", QStringLiteral("save")));
        QVERIFY(call("usable", QStringLiteral("swap-eyes")));
        choice->setShownAsPair(false);
        QVERIFY(!call("usable", QStringLiteral("swap-eyes")));
        // a choice of the other kind of file is still shown as the file's, never hidden
        Picture::Options frames;
        frames.inputLayout = Picture::Layout::FrameSequential;
        QVERIFY(Picture::setFileChoice(m_picture, frames));
        choice->refresh();
        QVERIFY(QMetaObject::invokeMethod(root.get(), "rows", Q_RETURN_ARG(QVariant, rows)));
        QCOMPARE(rows.toString().split(QLatin1Char('|')), video);
        QVERIFY(call("marked", QStringLiteral("frameSequentialLeftFirst")));
        QVERIFY(root->property("failures").toList().isEmpty());
    }

    void theChoiceFollowsARenamedFile()
    {
        // A program with a FileChoice keeps a file's choice with it when the desktop's file jobs rename it, and
        // changed() says so for the file it names, whichever comes first: the new source or the carry.
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            QSKIP("no session bus in this run: the follower is tested where the suite has one (dbus-run-session)");
        }
        const QString from = m_dir.filePath(QStringLiteral("before.png"));
        const QString to = m_dir.filePath(QStringLiteral("after.png"));
        QVERIFY(QFile::copy(m_picture, from));
        FileChoice choice;
        choice.setSources({QUrl::fromLocalFile(from)});
        QVERIFY(choice.choose(QStringLiteral("mono")));
        QSignalSpy changed(&choice, &FileChoice::changed);
        QSignalSpy refused(&choice, &FileChoice::failed);
        QVERIFY(QFile::rename(from, to));
        // the program's own model names the new file first, as a folder view does
        choice.setSources({QUrl::fromLocalFile(to)});
        QCOMPARE(choice.chosen(), QStringLiteral("automatic"));
        QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/"), QStringLiteral("org.kde.KDirNotify"), QStringLiteral("FileMoved"));
        message << QUrl::fromLocalFile(from).toString() << QUrl::fromLocalFile(to).toString();
        QVERIFY(bus.send(message));
        QVERIFY(changed.wait(5000));
        QCOMPARE(changed.at(0).at(0).toStringList(), QStringList{to});
        QCOMPARE(choice.chosen(), QStringLiteral("mono"));
        QVERIFY(!Picture::hasFileChoice(from));
        QVERIFY(refused.isEmpty());
        // a copy carries nothing: no announcement of a move, and the copy has no choice
        const QString copy = m_dir.filePath(QStringLiteral("copy.png"));
        QVERIFY(QFile::copy(to, copy));
        choice.setSources({QUrl::fromLocalFile(copy)});
        QCOMPARE(choice.chosen(), QStringLiteral("automatic"));
        QVERIFY(!Picture::hasFileChoice(copy));
        QCOMPARE(Picture::fileChoice(to).view, Picture::View::Mono);
    }

    void enable3DInBothFaces()
    {
        // The program's "Enable 3D" has one wording in both faces, is checked when made, and is not the file's "Not 3D"
        // (a correction of one file, kept in the store): two ideas, two names.
        Enable3DAction widgets;
        QQmlEngine engine;
        const std::unique_ptr<QObject> action(create(engine, "import QtQuick\nimport org.sparky.stereo\nEnable3DAction {}"));
        QVERIFY(action);
        QCOMPARE(action->property("text").toString(), widgets.text());
        QCOMPARE(action->property("toolTip").toString(), widgets.toolTip());
        QCOMPARE(action->objectName(), widgets.objectName());
        QCOMPARE(widgets.objectName(), QStringLiteral("stereo_enable_3d"));
        QVERIFY(widgets.isCheckable() && widgets.isChecked());
        QVERIFY(action->property("checkable").toBool() && action->property("checked").toBool());
        QVERIFY(!widgets.toolTip().isEmpty());
        FileChoiceMenu menu;
        const QStringList entries = widgetEntries(&menu);
        QVERIFY(!entries.contains(widgets.text()));
        QVERIFY(widgets.text() != entries.at(1));
        // toggled by the person, it says so, and nothing is written to the store
        const QDir store(m_dir.filePath(QStringLiteral("state/stereo-policy/files")));
        const QStringList before = store.entryList(QDir::Files, QDir::Name);
        QSignalSpy toggled(&widgets, &QAction::toggled);
        widgets.trigger();
        QCOMPARE(toggled.count(), 1);
        QVERIFY(!widgets.isChecked());
        QCOMPARE(store.entryList(QDir::Files, QDir::Name), before);
    }

    void thePictureShowsTheChoice()
    {
        // StereoPicture decodes through the layer with the file's path, so the stored choice applies; refresh() is what
        // a program calls on changed().
        QImage pair(64, 16, QImage::Format_RGB32);
        pair.fill(Qt::red);
        const QString path = m_dir.filePath(QStringLiteral("wide.png"));
        QVERIFY(pair.save(path));
        QQmlEngine engine;
        const std::unique_ptr<QObject> item(
            create(engine, "import QtQuick\nimport org.sparky.stereo\nStereoPicture { width: 64; height: 16 }"));
        QVERIFY(item);
        item->setProperty("source", QUrl::fromLocalFile(path));
        // 4:1 is no pair by its shape
        QVERIFY(!item->property("stereo").toBool());
        FileChoice choice;
        choice.setSources({QUrl::fromLocalFile(path)});
        QVERIFY(choice.choose(QStringLiteral("sideBySideLeftFirst")));
        QVERIFY(!item->property("stereo").toBool());
        QVERIFY(QMetaObject::invokeMethod(item.get(), "refresh"));
        QVERIFY(item->property("stereo").toBool());
        QCOMPARE(item->property("pictureSize").toSize(), QSize(32, 16));
        QVERIFY(choice.choose(QStringLiteral("mono")));
        QVERIFY(QMetaObject::invokeMethod(item.get(), "refresh"));
        QVERIFY(!item->property("stereo").toBool());
    }
};
QTEST_MAIN(FileChoiceQmlTest)
#include "filechoiceqmltest.moc"
