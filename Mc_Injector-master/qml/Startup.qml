import QtQuick
import QtQuick.Window

Window {
    id: splash
    width: 420
    height: 300
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"
    title: "Java Overlay Studio — Loading"
    property bool darkTheme: false
    property bool completed: false
    property bool failed: false
    property string phaseText: "Starting Java Overlay Studio"
    onClosing: Lifecycle.requestExit("startup window close")
    readonly property color ink: darkTheme ? "#F1EEF4" : "#1D1B20"
    readonly property color accent: darkTheme ? "#CFBCFF" : "#6750A4"
    opacity: completed ? 0 : 1
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Timer { interval: 190; running: splash.completed; onTriggered: splash.visible = false }

    Rectangle {
        anchors.fill: parent
        radius: 28
        color: splash.darkTheme ? "#181B21" : "#F7F2FA"
        border.width: 1
        border.color: splash.darkTheme ? "#34323B" : "#E7E0EC"
        Rectangle {
            x: 376; y: 12; width: 30; height: 30; radius: 15
            color: closeMouse.containsMouse ? (splash.darkTheme ? "#35313D" : "#E8E1EE") : "transparent"
            Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 23; color: splash.ink }
            MouseArea {
                id: closeMouse
                anchors.fill: parent
                enabled: !splash.completed
                hoverEnabled: true
                onClicked: Lifecycle.requestExit("startup close button")
            }
        }
        Rectangle {
            id: logo
            anchors.horizontalCenter: parent.horizontalCenter
            y: 42; width: 76; height: 76; radius: 25
            color: splash.darkTheme ? "#4D3D6C" : "#EADDFF"
            Text { anchors.centerIn: parent; text: "J"; color: splash.accent; font.family: "Segoe UI"; font.pixelSize: 46; font.weight: Font.DemiBold }
            scale: 1
            SequentialAnimation on scale {
                running: !splash.completed && !splash.failed
                loops: Animation.Infinite
                NumberAnimation { to: 1.045; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 800; easing.type: Easing.InOutSine }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 141; text: "Java Overlay Studio"
            color: splash.ink; font.family: "Segoe UI"; font.pixelSize: 23; font.weight: Font.DemiBold
        }
        Text {
            x: 26; y: 182; width: parent.width - 52
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: splash.phaseText; font.family: "Segoe UI"; font.pixelSize: 13
            color: splash.darkTheme ? "#CAC4D0" : "#625B71"
        }
        Rectangle {
            id: track
            anchors.horizontalCenter: parent.horizontalCenter
            y: 244; width: 220; height: 4; radius: 2; clip: true
            visible: !splash.failed
            color: splash.darkTheme ? "#494252" : "#E3DAEE"
            Rectangle {
                width: 76; height: 4; radius: 2; color: splash.accent
                SequentialAnimation on x {
                    running: !splash.completed && !splash.failed
                    loops: Animation.Infinite
                    NumberAnimation {
                        from: -76; to: 220; duration: 1200
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1, 1]
                    }
                }
            }
        }
    }
}
