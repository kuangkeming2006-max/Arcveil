import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Rectangle {
    required property var host
    id: windowChrome
    z: 500
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: host.chromeHeight
    visible: false
    color: host.darkTheme ? "#12151A" : "#F8F6FA"
    border.width: 1
    border.color: host.outlineVariantColor

    RowLayout {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 9
        Rectangle {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            radius: 7
            color: host.primaryColor
            Text {
                anchors.centerIn: parent
                text: "A"
                color: host.primaryForegroundColor
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }
        Text {
            text: "Arcveil"
            color: host.textColor
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.right: chromeButtons.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        acceptedButtons: Qt.LeftButton
        onPressed: host.startSystemMove()
        onDoubleClicked: {
            if (host.visibility === Window.Maximized) host.showNormal()
            else host.showMaximized()
        }
    }

    Row {
        id: chromeButtons
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Repeater {
            model: [
                { "glyph": "−", "action": "minimize", "label": "Minimize" },
                { "glyph": host.visibility === Window.Maximized ? "❐" : "□",
                  "action": "maximize", "label": "Maximize or restore" },
                { "glyph": "×", "action": "close", "label": "Close" }
            ]
            delegate: Rectangle {
                required property var modelData
                width: 48
                height: windowChrome.height
                color: chromeMouse.containsMouse
                       ? (modelData.action === "close" ? "#D83B3B"
                                                      : host.hoverColor)
                       : "transparent"
                Behavior on color { ColorAnimation { duration: 130 } }
                Text {
                    anchors.centerIn: parent
                    text: modelData.glyph
                    color: chromeMouse.containsMouse && modelData.action === "close"
                           ? "white" : host.textColor
                    font.pixelSize: modelData.action === "minimize" ? 19 : 17
                }
                MouseArea {
                    id: chromeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (modelData.action === "minimize") host.showMinimized()
                        else if (modelData.action === "maximize") {
                            if (host.visibility === Window.Maximized) host.showNormal()
                            else host.showMaximized()
                        } else host.close()
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData.label
            }
        }
    }
    // Preserve native-feeling resize hit targets after replacing the Windows
    // caption with the unified Material chrome.
    MouseArea {
        parent: host.contentItem
        z: 1000; width: 6; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        enabled: false; cursorShape: Qt.SizeHorCursor
        onPressed: host.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1000; width: 6; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        enabled: false; cursorShape: Qt.SizeHorCursor
        onPressed: host.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1000; height: 6; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        enabled: false; cursorShape: Qt.SizeVerCursor
        onPressed: host.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1000; height: 6; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        enabled: false; cursorShape: Qt.SizeVerCursor
        onPressed: host.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1001; width: 10; height: 10; anchors.left: parent.left; anchors.top: parent.top
        enabled: false; cursorShape: Qt.SizeFDiagCursor
        onPressed: host.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1001; width: 10; height: 10; anchors.right: parent.right; anchors.top: parent.top
        enabled: false; cursorShape: Qt.SizeBDiagCursor
        onPressed: host.startSystemResize(Qt.RightEdge | Qt.TopEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1001; width: 10; height: 10; anchors.left: parent.left; anchors.bottom: parent.bottom
        enabled: false; cursorShape: Qt.SizeBDiagCursor
        onPressed: host.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }
    MouseArea {
        parent: host.contentItem
        z: 1001; width: 10; height: 10; anchors.right: parent.right; anchors.bottom: parent.bottom
        enabled: false; cursorShape: Qt.SizeFDiagCursor
        onPressed: host.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }
}
