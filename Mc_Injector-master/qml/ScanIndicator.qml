import QtQuick

Item {
    id: root
    property bool running: false
    property color color: "#6750A4"
    implicitWidth: 240
    implicitHeight: 5
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.16)
    }

    Rectangle {
        id: segment
        y: 0
        width: Math.max(44, root.width * 0.34)
        height: root.height
        radius: height / 2
        color: root.color
        visible: root.running

        SequentialAnimation on x {
            running: root.running
            loops: Animation.Infinite
            NumberAnimation {
                from: -segment.width
                to: root.width
                duration: 1050
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.4, 0.0, 0.2, 1.0, 1.0, 1.0]
            }
            PauseAnimation { duration: 90 }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.color
        opacity: root.running ? 0 : 0.18
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }
}
