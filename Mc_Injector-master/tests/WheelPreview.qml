import QtQuick
import QtQuick.Controls
import "../qml"
Item {
    width: 400; height: 300
    WheelPage {
        objectName: "wheelPage"
        anchors.fill: parent
        contentHeight: 1600
        pageWheelEnabled: !skin.containsMouse
        Rectangle { width: 380; height: 1600; color: "#444444" }
        MouseArea {
            id: skin
            objectName: "skinZone"
            x: 10; y: 120; width: 100; height: 100
            hoverEnabled: true; preventStealing: true
            property int wheelCount: 0
            onWheel: wheel => { ++wheelCount; wheel.accepted=true }
        }
    }
}
