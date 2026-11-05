// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.sparky.stereo

// The controls of the depth options, the same set and words as the widgets' DepthControls: every player shows the same ones.
// The program shows them only for content that carries a depth map, and keeps the settings per file in its own store.
ColumnLayout {
    id: root

    required property StereoDepthSettings settings

    function word(key) {
        return settings.label(key)
    }

    // the library's values of stereo_dibr_view, _warp, _fill and _smooth
    readonly property var pictureModel: [{text: word("pictureLeft"), value: 0}, {text: word("pictureMiddle"), value: 1}]
    readonly property var fillModel: [{text: word("fillBackground"), value: 0}, {text: word("fillDepthAided"), value: 1},
                                      {text: word("fillPyramid"), value: 4}, {text: word("fillTelea"), value: 2},
                                      {text: word("fillNone"), value: 3}]
    readonly property var warpModel: [{text: word("warpForward"), value: 0}, {text: word("warpBackward"), value: 1}, {text: word("warpLayered"), value: 2}]

    Controls.Label {
        text: root.word("title")
        font.bold: true
    }

    GridLayout {
        columns: 2
        columnSpacing: 12

        Controls.Label { text: root.word("strength") }
        Controls.SpinBox {
            objectName: "strength"
            from: 0
            to: root.settings.strengthMax
            value: Math.round(root.settings.strength)
            editable: true
            onValueModified: root.settings.strength = value
            Controls.ToolTip.text: root.word("strengthTip")
            Controls.ToolTip.visible: hovered
        }

        Controls.Label { text: root.word("zero") }
        Controls.Slider {
            objectName: "zero"
            from: 0
            to: 1
            stepSize: 0.05
            value: root.settings.zero
            onMoved: root.settings.zero = value
            Controls.ToolTip.text: root.word("zeroTip")
            Controls.ToolTip.visible: hovered
        }

        Controls.Label { text: root.word("shift") }
        Controls.SpinBox {
            objectName: "shift"
            from: -root.settings.shiftMax
            to: root.settings.shiftMax
            value: Math.round(root.settings.shift)
            editable: true
            onValueModified: root.settings.shift = value
        }

        Controls.Label { text: root.word("picture") }
        Controls.ComboBox {
            objectName: "picture"
            model: root.pictureModel
            textRole: "text"
            valueRole: "value"
            currentIndex: count > 0 ? indexOfValue(root.settings.view) : -1
            onActivated: root.settings.view = currentValue
            Controls.ToolTip.text: root.word("pictureTip")
            Controls.ToolTip.visible: hovered
        }

        Item { width: 1; height: 1 }
        ColumnLayout {
            Controls.CheckBox {
                objectName: "automatic"
                text: root.word("automatic")
                checked: root.settings.automatic
                onToggled: root.settings.automatic = checked
                Controls.ToolTip.text: root.word("automaticTip")
                Controls.ToolTip.visible: hovered
            }
            Controls.Label {
                objectName: "chosen"
                visible: text.length > 0
                text: root.settings.chosenText
                wrapMode: Text.Wrap
                Layout.maximumWidth: 320
            }
        }

        Controls.Label { text: root.word("fill") }
        Controls.ComboBox {
            objectName: "fill"
            model: root.fillModel
            textRole: "text"
            valueRole: "value"
            currentIndex: count > 0 ? indexOfValue(root.settings.shownFill) : -1
            onActivated: root.settings.useWay(root.settings.shownWarp, currentValue)
            Controls.ToolTip.text: root.word("fillTip")
            Controls.ToolTip.visible: hovered
        }

        Controls.Label { text: root.word("warp") }
        Controls.ComboBox {
            objectName: "warp"
            model: root.warpModel
            textRole: "text"
            valueRole: "value"
            currentIndex: count > 0 ? indexOfValue(root.settings.shownWarp) : -1
            onActivated: root.settings.useWay(currentValue, root.settings.shownFill)
        }

        Item { width: 1; height: 1 }
        Controls.CheckBox {
            objectName: "smooth"
            text: root.word("smooth")
            checked: root.settings.smooth === 1
            onToggled: root.settings.smooth = checked ? 1 : 0
        }

        Controls.Label { text: root.word("reach") }
        Controls.SpinBox {
            objectName: "reach"
            from: 1
            to: root.settings.radiusMax
            value: root.settings.radius
            onValueModified: root.settings.radius = value
        }

        Controls.Label { text: root.word("nearest") }
        Controls.SpinBox {
            objectName: "nearest"
            from: 0
            to: 1000000
            value: Math.round(root.settings.nearDistance)
            editable: true
            onValueModified: root.settings.nearDistance = value
        }

        Controls.Label { text: root.word("farthest") }
        Controls.SpinBox {
            objectName: "farthest"
            from: 0
            to: 1000000
            value: Math.round(root.settings.farDistance)
            editable: true
            onValueModified: root.settings.farDistance = value
        }
    }

    Controls.Button {
        objectName: "reset"
        text: root.word("reset")
        enabled: !root.settings.isDefault
        onClicked: root.settings.reset()
    }
}
