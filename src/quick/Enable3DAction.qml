// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

import QtQuick.Controls as Controls
import org.sparky.stereo

// The program's own "Enable 3D" for a QML program, the face of the widgets' Stereo::Enable3DAction with the same words:
// checked, the program shows the pairs it shows in 3D; unchecked, every picture flat. It is the program's switch, kept as
// long as the program keeps it, and corrects no file: a file's "Not 3D" is in FileChoiceMenu. Use it as a menu item's
// or a button's action; the program binds checked to what it keeps and acts on toggled().
Controls.Action {
    // the words a program shows as the action's tooltip
    readonly property string toolTip: Enable3D.toolTip

    objectName: "stereo_enable_3d"
    text: Enable3D.text
    checkable: true
    checked: true
}
