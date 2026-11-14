// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQml.Models
import org.sparky.stereo

// The person's corrections of the input of one media file, as a menu any QML program shows for a file it shows in 3D:
// the QML face of the widgets' Stereo::FileChoiceMenu. It holds no entry of its own: the list, its words, which entry
// is the file's and the writes to the shared store are the FileChoice's, the same for every program on the layer. Use
// it as a popup (popup()) or as a sub-menu of the program's own 3D menu.
Controls.Menu {
    id: root

    // The files and how the program shows them
    required property FileChoice choice

    title: choice.title
    enabled: choice.available

    function itemPosition(item: Item): int {
        for (let index = 0; index < root.count; ++index) {
            if (root.itemAt(index) === item) {
                return index;
            }
        }
        return root.count;
    }

    function menuPosition(menu: Controls.Menu): int {
        for (let index = 0; index < root.count; ++index) {
            const item = root.itemAt(index);
            if (item && item.subMenu === menu) {
                return index;
            }
        }
        return root.count;
    }

    component Entry: Controls.MenuItem {
        id: entry

        required property int index
        required property string label
        required property bool chosen
        required property bool offered
        required property string entryId
        property FileChoiceEntries group

        objectName: entryId
        text: label
        checkable: true
        checked: chosen
        visible: offered
        height: visible ? implicitHeight : 0
        onTriggered: {
            group.choose(index);
            // a click toggles the mark itself; the mark is the store's
            checked = Qt.binding(() => entry.chosen);
        }
    }

    component Group: Controls.Menu {
        id: groupMenu

        required property FileChoiceEntries group
        required property FileChoice choice

        title: group.title
        enabled: choice.available

        Instantiator {
            model: groupMenu.group
            delegate: Entry {
                group: groupMenu.group
            }
            onObjectAdded: (index, object) => groupMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => groupMenu.removeItem(object as Item)
        }
    }

    // Automatic and Not 3D, above the separator
    Instantiator {
        model: root.choice.top
        delegate: Entry {
            group: root.choice.top
        }
        onObjectAdded: (index, object) => root.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => root.removeItem(object as Item)
    }

    Controls.MenuSeparator {
        id: wholeSeparator
    }

    // The packings one picture holds whole, below it
    Instantiator {
        model: root.choice.whole
        delegate: Entry {
            group: root.choice.whole
        }
        onObjectAdded: (index, object) => root.insertItem(root.itemPosition(wholeSeparator) + 1 + index, object as Item)
        onObjectRemoved: (index, object) => root.removeItem(object as Item)
    }

    Group {
        choice: root.choice
        group: root.choice.interleaved
    }

    // The packings of a video: offered for a video, and for a file whose choice is one of them
    Instantiator {
        active: root.choice.sequence.offered
        delegate: Group {
            choice: root.choice
            group: root.choice.sequence
        }
        onObjectAdded: (index, object) => root.insertMenu(root.menuPosition(anaglyphMenu), object as Controls.Menu)
        onObjectRemoved: (index, object) => root.removeMenu(object as Controls.Menu)
    }

    Group {
        id: anaglyphMenu
        choice: root.choice
        group: root.choice.anaglyph
    }

    Group {
        choice: root.choice
        group: root.choice.depth
    }

    Controls.MenuSeparator {}

    Controls.MenuItem {
        id: swapItem

        objectName: "swap-eyes"
        text: root.choice.swapText
        checkable: true
        checked: root.choice.swapEyes
        enabled: root.choice.swapEnabled
        Controls.ToolTip.text: root.choice.swapToolTip
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.delay: 1000
        onTriggered: {
            root.choice.swapEyes = checked;
            checked = Qt.binding(() => root.choice.swapEyes);
        }
    }

    Controls.MenuSeparator {}

    Controls.MenuItem {
        objectName: "save"
        text: root.choice.saveText
        enabled: root.choice.saveEnabled
        Controls.ToolTip.text: root.choice.saveToolTip
        Controls.ToolTip.visible: hovered
        Controls.ToolTip.delay: 1000
        onTriggered: root.choice.save()
    }
}
