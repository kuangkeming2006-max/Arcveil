import QtQuick
import QtQuick3D
import "../qml"
Item {
    width: 480; height: 640
    property url skinUrl
    property real modelYaw: 0
    View3D {
        anchors.fill: parent
        environment: SceneEnvironment { backgroundMode: SceneEnvironment.Color; clearColor: "#141820" }
        PerspectiveCamera { position: Qt.vector3d(0,16,75); fieldOfView: 32 }
        DirectionalLight { brightness: 1 }
        SkinAvatar { textureSource: skinUrl; eulerRotation.y: modelYaw }
    }
}
