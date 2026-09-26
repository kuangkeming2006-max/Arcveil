import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Item {
    required property var host
    signal autoRefreshRequested(bool enabled)
    id: settingsPage
    WheelPage {
        anchors.fill: parent
        contentWidth: width
        contentHeight: settingsContent.implicitHeight + 68
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: settingsContent
            x: Math.round((parent.width - width) / 2)
            y: 28
            width: Math.min(parent.width - 68, 1280)
            spacing: 14

        Text {
            text: "Arcveil settings"
            color: host.textColor
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
        Text {
            text: "Native JVM agent runtime and discovery preferences"
            color: host.secondaryTextColor
            font.pixelSize: 14
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 920 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            Behavior on color { ColorAnimation { duration: 300 } }
            Behavior on border.color { ColorAnimation { duration: 300 } }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Controller appearance"
                        color: host.textColor
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Choose a comfortable controller theme. Window size is restored automatically."
                        color: host.secondaryTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
                MaterialButton {
                    Layout.preferredWidth: 92
                    text: "☀  Light"
                    filled: !host.darkTheme
                    containerColor: host.primaryColor
                    foregroundColor: filled ? host.primaryForegroundColor : host.textColor
                    outlineColor: host.outlineVariantColor
                    onClicked: AppSettings.darkTheme = false
                }
                MaterialButton {
                    Layout.preferredWidth: 92
                    text: "☾  Dark"
                    filled: host.darkTheme
                    containerColor: host.primaryColor
                    foregroundColor: filled ? host.primaryForegroundColor : host.textColor
                    outlineColor: host.outlineVariantColor
                    onClicked: AppSettings.darkTheme = true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: "Automatic process refresh"; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: "Rescan Java processes every five seconds"; color: host.secondaryTextColor; font.pixelSize: 13 }
                }
                MaterialButton {
                    Layout.preferredWidth: 112
                    text: host.autoRefresh ? "Enabled" : "Disabled"
                    iconText: host.autoRefresh ? "✓" : ""
                    filled: host.autoRefresh
                    containerColor: host.primaryColor
                    foregroundColor: host.autoRefresh ? host.primaryForegroundColor : host.primaryColor
                    outlineColor: host.outlineVariantColor
                    onClicked: settingsPage.autoRefreshRequested(!host.autoRefresh)
                }
            }
        }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 154
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "Hypixel API key"; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Text { text: ApiKeys.statusMessage; color: ApiKeys.configured ? "#20853B" : host.secondaryTextColor; font.pixelSize: 12 }
                    }
                    Item { Layout.fillWidth: true }
                    MaterialButton {
                        text: "Remove"; filled: false; visible: ApiKeys.configured
                        Layout.preferredWidth: 104
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        foregroundColor: "#FFB4AB"; outlineColor: host.outlineVariantColor
                        onClicked: ApiKeys.clearKey()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    MaterialTextField {
                        id: apiKeyField
                        Layout.fillWidth: true; Layout.preferredHeight: 52
                        placeholderText: "Paste complete Hypixel Personal / Development API key"
                        echoMode: TextInput.Password
                        passwordCharacter: "●"
                        selectByMouse: true
                        maximumLength: 8192
                        Accessible.name: "Hypixel API key"
                        onAccepted: if (ApiKeys.saveKey(text)) text = ""
                    }
                    MaterialButton {
                        text: "Save securely"; filled: true; containerColor: host.primaryColor
                        enabled: apiKeyField.text.trim().length > 0
                        onClicked: if (ApiKeys.saveKey(apiKeyField.text)) apiKeyField.text = ""
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 920 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: "Click GUI hotkey"; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: "Only this configurable key opens the in-game menu"; color: host.secondaryTextColor; font.pixelSize: 13 }
                }
                KeyCaptureButton {
                    Layout.preferredWidth: 190
                    virtualKey: OverlayManager.menuHotkey
                    primaryColor: host.primaryColor
                    surfaceColor: host.backgroundColor
                    textColor: host.textColor
                    onKeyCaptured: function(key) { OverlayManager.menuHotkey = key }
                    Accessible.name: "Click GUI hotkey"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { text: "In-game interface size"; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { text: "Four rasterized font sizes; synchronized with Click GUI"; color: host.secondaryTextColor; font.pixelSize: 13 }
                }
                Repeater {
                    model: ["S", "M", "L", "XL"]
                    delegate: MaterialButton {
                        required property int index
                        required property string modelData
                        Layout.preferredWidth: 54
                        Layout.preferredHeight: 44
                        text: modelData
                        filled: OverlayManager.guiScaleIndex === index
                        containerColor: host.primaryColor
                        foregroundColor: filled ? host.primaryForegroundColor : host.primaryColor
                        outlineColor: host.outlineVariantColor
                        onClicked: OverlayManager.guiScaleIndex = index
                    }
                }
            }
        }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 144
            radius: 22
            color: host.primaryContainer

            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 20
                Rectangle {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    radius: 18
                    color: host.primaryColor
                    Text { anchors.centerIn: parent; text: "GPU"; color: host.primaryForegroundColor; font.pixelSize: 13; font.weight: Font.Bold }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Text { text: "Native overlay runtime"; color: host.primaryContainerText; font.pixelSize: 19; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: "JVM Attach with a native loader fallback · Dear ImGui · OpenGL 2 · authenticated local IPC"
                        color: host.primaryContainerMutedText
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            radius: 22
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 7
                Text { text: "Minecraft 1.8.9 bindings"; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: "Forge release runtimes use SRG symbols; pure Vanilla uses embedded obfuscated 1.8.9 symbols. JNI reads health, entity IDs, positions, collision boxes and bed blocks without installing a target-side Mod or JAR."
                    color: host.secondaryTextColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }

            Item { Layout.fillHeight: true }
        }
    }
}
