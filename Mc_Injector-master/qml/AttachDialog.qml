import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Popup {
    required property var host
    signal confirmAttach(int pid)
    id: attachDialog
    objectName: "attachDialog"
    onOpened: Lifecycle.record("attach dialog opened")
    onClosed: Lifecycle.record("attach dialog closed")
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, host.width - 80)
    height: 370
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    Overlay.modal: Rectangle {
        color: "#66000000"
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }

    background: Rectangle {
        radius: 26
        color: host.surfaceColor
        border.width: 1
        border.color: host.outlineVariantColor
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        Rectangle {
            Layout.preferredWidth: 52
            Layout.preferredHeight: 52
            radius: 17
            color: host.primaryContainer
            Text { anchors.centerIn: parent; text: "↗"; color: host.primaryContainerText; font.pixelSize: 23; font.weight: Font.Bold }
        }
        Text {
            Layout.fillWidth: true
            text: "Load native in-game overlay?"
            color: host.textColor
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: (host.pendingProcess.windowTitle || "Selected Java process")
                  + "\nPID " + (host.pendingProcess.pid || "—")
            color: host.secondaryTextColor
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            text: "The controller first tries the official JVM Attach API. If runtime Attach is unavailable, a selected game JVM can use a visible LoadLibraryW fallback and the DLL's explicit startup export. No Mod JAR is copied into the game."
            color: host.secondaryTextColor
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            MaterialButton {
                text: "Cancel"
                filled: false
                foregroundColor: host.primaryColor
                outlineColor: "transparent"
                onClicked: attachDialog.close()
            }
            MaterialButton {
                text: OverlayManager.busy ? "Attaching…" : "Load DLL"
                iconText: "↗"
                containerColor: host.primaryColor
                enabled: !OverlayManager.busy
                onClicked: attachDialog.confirmAttach(host.pendingProcess.pid || 0)
            }
        }
    }

    // Modal content uses the requested emphasized-decelerate expansion.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 300
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
            }
            NumberAnimation {
                property: "scale"
                from: 0.86
                to: 1
                duration: 380
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
            }
        }
    }

    // Material standard-accelerate: cubic-bezier(0.3, 0, 0.8, 0.15).
    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 180
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1.0, 1.0]
            }
            NumberAnimation {
                property: "scale"
                from: 1
                to: 0.92
                duration: 180
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1.0, 1.0]
            }
        }
    }
}
