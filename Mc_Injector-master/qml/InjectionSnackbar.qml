import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Popup {
    required property var host
    property alias closeTimer: snackbarCloseTimer
    Timer {
        id: snackbarCloseTimer
        interval: 5600
        repeat: false
        onTriggered: injectionSnackbar.close()
    }
    id: injectionSnackbar
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: parent.height - height - 104
    width: Math.min(540, host.width - 56)
    height: 82
    padding: 0
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: 18
        color: "#322F35"
        border.width: 1
        border.color: "#514D55"
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 10
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 38
            Layout.preferredHeight: 38
            radius: 13
            color: "#C9F8D1"
            Text {
                anchors.centerIn: parent
                text: "✓"
                color: "#155724"
                font.pixelSize: 19
                font.weight: Font.Bold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: "Injection complete"
                color: "#FFFBFE"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: "Native overlay is active · "
                      + host.menuHotkeyLabel(OverlayManager.menuHotkey)
                      + " opens Click GUI"
                color: "#D0C8D1"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        MaterialButton {
            Layout.preferredWidth: 86
            Layout.preferredHeight: 40
            text: "Dismiss"
            filled: false
            foregroundColor: "#D0BCFF"
            outlineColor: "transparent"
            onClicked: {
                snackbarCloseTimer.stop()
                injectionSnackbar.close()
            }
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 260
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
            }
            NumberAnimation {
                property: "scale"
                from: 0.92
                to: 1
                duration: 340
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
            }
        }
    }

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
                to: 0.96
                duration: 180
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1.0, 1.0]
            }
        }
    }
}
