import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import McOverlay 1.0

WheelPage {
    id: root
    required property color backgroundColor
    required property color surfaceColor
    required property color surfaceVariant
    required property color textColor
    required property color secondaryTextColor
    required property color primaryColor
    required property color onPrimaryColor
    required property color outlineVariantColor

    contentWidth: width
    contentHeight: content.implicitHeight + 64
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    ColumnLayout {
        id: content
        x: 34
        y: 28
        width: root.width - 68
        spacing: 18

        Text {
            text: "Config"
            color: root.textColor
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "Save the complete in-game feature surface and restore it as one profile."
            color: root.secondaryTextColor
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 108
            radius: 24
            color: root.surfaceColor
            border.width: 1
            border.color: root.outlineVariantColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Automatic save"
                        color: root.textColor
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: OverlayManager.activeConfig.length > 0
                              ? "Changes update “" + OverlayManager.activeConfig + "” after a short debounce."
                              : "Enabling creates an Auto Save profile for the current settings."
                        color: root.secondaryTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                Switch {
                    checked: OverlayManager.configAutoSave
                    onToggled: OverlayManager.configAutoSave = checked
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            radius: 24
            color: root.surfaceColor
            border.width: 1
            border.color: root.outlineVariantColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                Text {
                    text: "Save current settings"
                    color: root.textColor
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    MaterialTextField {
                        id: configName
                        Layout.fillWidth: true
                        Layout.preferredHeight: 50
                        placeholderText: "Profile name"
                        maximumLength: 32
                        selectByMouse: true
                        onAccepted: if (OverlayManager.saveConfig(text)) text = ""
                    }
                    MaterialButton {
                        Layout.preferredWidth: 128
                        text: "Save config"
                        filled: true
                        containerColor: root.primaryColor
                        foregroundColor: root.onPrimaryColor
                        enabled: configName.text.trim().length > 0
                        onClicked: if (OverlayManager.saveConfig(configName.text))
                                       configName.text = ""
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "SAVED PROFILES"
                color: root.primaryColor
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.0
            }
            Item { Layout.fillWidth: true }
            Text {
                text: OverlayManager.configNames.length + " saved"
                color: root.secondaryTextColor
                font.pixelSize: 12
            }
        }

        Repeater {
            model: OverlayManager.configNames
            delegate: Rectangle {
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                radius: 22
                color: profileMouse.hovered ? root.surfaceVariant : root.surfaceColor
                border.width: OverlayManager.activeConfig === modelData ? 2 : 1
                border.color: OverlayManager.activeConfig === modelData
                              ? root.primaryColor : root.outlineVariantColor
                Behavior on color { ColorAnimation { duration: 180 } }
                Behavior on border.color { ColorAnimation { duration: 180 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 46
                        radius: 15
                        color: root.surfaceVariant
                        Text {
                            anchors.centerIn: parent
                            text: "◆"
                            color: root.primaryColor
                            font.pixelSize: 18
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: modelData
                            color: root.textColor
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: OverlayManager.activeConfig === modelData
                                  ? (OverlayManager.configAutoSave ? "Active · auto saving" : "Active")
                                  : "Stored feature and media settings"
                            color: root.secondaryTextColor
                            font.pixelSize: 12
                        }
                    }
                    MaterialButton {
                        Layout.preferredWidth: 100
                        text: "Apply"
                        filled: OverlayManager.activeConfig !== modelData
                        containerColor: root.primaryColor
                        foregroundColor: filled ? root.onPrimaryColor : root.primaryColor
                        outlineColor: root.outlineVariantColor
                        onClicked: OverlayManager.applyConfig(modelData)
                    }
                    MaterialButton {
                        Layout.preferredWidth: 94
                        text: "Delete"
                        filled: false
                        foregroundColor: "#D64A45"
                        outlineColor: root.outlineVariantColor
                        onClicked: OverlayManager.removeConfig(modelData)
                    }
                }
                HoverHandler { id: profileMouse }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            visible: OverlayManager.configNames.length === 0
            radius: 22
            color: root.surfaceVariant
            Text {
                anchors.centerIn: parent
                text: "No profiles yet · save the current in-game GUI settings above"
                color: root.secondaryTextColor
                font.pixelSize: 13
            }
        }
    }
}
