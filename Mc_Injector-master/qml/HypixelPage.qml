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
        contentHeight: hypixelContent.implicitHeight + 68
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: hypixelContent
            x: 34
            y: 28
            width: parent.width - 68
            spacing: 18

            Text {
                text: "Hypixel"
                color: host.textColor
                font.pixelSize: 32
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "Bed Wars statistics queried asynchronously through the official Hypixel API"
                color: host.secondaryTextColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 78
                radius: 20
                color: OverlayManager.matchActive ? "#D9F8DF" : "#F1ECF3"

                Behavior on color { ColorAnimation { duration: 220 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        radius: 13
                        color: OverlayManager.matchActive ? "#20853B" : "#79747E"
                        Behavior on color { ColorAnimation { duration: 220 } }
                        Text {
                            anchors.centerIn: parent
                            text: OverlayManager.matchActive ? "✓" : "Ⅱ"
                            color: "white"
                            font.pixelSize: 17
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "Automatic match lookup"
                            color: host.textColor
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: OverlayManager.matchActive
                                  ? "Bed Wars team roster detected · queued players are queried once"
                                  : "Paused outside a Bed Wars match · no automatic API traffic"
                            color: host.secondaryTextColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        text: ApiKeys.configured ? "KEY READY" : "KEY REQUIRED"
                        color: ApiKeys.configured ? "#20853B" : "#BA1A1A"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }
                }
            }
            HypixelStatsCard {
                Layout.fillWidth: true
                primaryColor: host.primaryColor
                textColor: host.textColor
                secondaryTextColor: host.secondaryTextColor
                surfaceColor: host.surfaceColor
                outlineColor: host.outlineColor
            }
        }
    }
}
