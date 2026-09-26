import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Rectangle {
    required property var host
    signal attachRequested(int pid)
    id: actionBar
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: 86
    color: host.darkTheme ? "#171A20" : "#FDF8FF"

    Behavior on color { ColorAnimation { duration: 320 } }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: host.outlineVariantColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 34
        anchors.rightMargin: 26
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: host.activeTargetPid === 0
                      ? "No process selected"
                      : "Selected PID " + host.activeTargetPid
                color: host.textColor
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                text: "Last scan: " + ProcessScanner.lastRefresh
                color: host.secondaryTextColor
                font.pixelSize: 11
            }
        }

        MaterialButton {
            readonly property bool selectedIsSession:
                ProcessScanner.selectedPid !== 0
                && ProcessScanner.selectedPid === OverlayManager.targetPid
                && host.sessionAvailable
            text: OverlayManager.busy ? "Attaching…" : "Attach native overlay"
            iconText: "↗"
            containerColor: host.primaryColor
            // The process card already owns Return. Keep the bottom bar
            // for the one non-duplicate primary action only.
            visible: host.activeRoute === "scanner"
                     && ProcessScanner.selectedPid !== 0 && !selectedIsSession
            enabled: !OverlayManager.busy
            onClicked: actionBar.attachRequested(ProcessScanner.selectedPid)
        }
    }
}
