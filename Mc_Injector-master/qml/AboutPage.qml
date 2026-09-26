import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Item {
    required property var host
    WheelPage {
        anchors.fill: parent
        contentWidth: width
        contentHeight: aboutContent.implicitHeight + 68
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: aboutContent
            x: 34; y: 28; width: parent.width - 68; spacing: 18
            Text { text: "About"; color: host.textColor; font.pixelSize: 32; font.weight: Font.DemiBold }
            Text { objectName: "aboutBuildLabel"; text: "Arcveil · Internal build v53"; color: host.secondaryTextColor; font.pixelSize: 14 }

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 210
                radius: 26; color: host.primaryContainer
                RowLayout {
                    anchors.fill: parent; anchors.margins: 28; spacing: 24
                    Rectangle {
                        Layout.preferredWidth: 76; Layout.preferredHeight: 76; radius: 25; color: host.primaryColor
                        Text { objectName: "aboutBrandGlyph"; anchors.centerIn: parent; text: "A"; color: host.primaryForegroundColor; font.pixelSize: 20; font.weight: Font.Bold }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 8
                        Text { text: "Arcveil"; color: host.primaryContainerText; font.pixelSize: 23; font.weight: Font.DemiBold }
                        Text {
                            Layout.fillWidth: true
                            text: "C++20 · Qt 6/QML · JVMTI/JNI · Dear ImGui · OpenGL 2 · authenticated bidirectional IPC"
                            color: host.primaryContainerMutedText; font.pixelSize: 14; wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true; columns: 2; columnSpacing: 16; rowSpacing: 16
                Repeater {
                    model: [
                        { "title": "Controller", "body": "Windows process discovery, DPAPI-protected API configuration, asynchronous HTTPS and Material QML dashboard." },
                        { "title": "Agent", "body": "Native in-process OpenGL renderer with safe JVM thread attachment, cached JNI bindings and reversible hooks." },
                        { "title": "License & privacy", "body": "Apache-2.0 · Third-party licenses are included with Arcveil. The Hypixel key is encrypted for the current Windows account." },
                        { "title": "Features", "body": "Visual overlays, AimAssist, FreeLook, Smart Hotbar, player blacklist and Windows Now Playing. Some modules are restricted to local worlds." }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 150
                        radius: 22; color: host.surfaceColor; border.width: 1; border.color: host.outlineVariantColor
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 20; spacing: 8
                            Text { text: modelData.title; color: host.textColor; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: modelData.body; color: host.secondaryTextColor; font.pixelSize: 13; wrapMode: Text.WordWrap }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
    }
}
