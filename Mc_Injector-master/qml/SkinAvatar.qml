import QtQuick
import QtQuick3D
import McOverlay 1.0

Node {
    id: root
    property url textureSource
    property bool slim: false
    Texture {
        id: atlas
        source: root.textureSource
        generateMipmaps: false
        minFilter: Texture.Nearest
        magFilter: Texture.Nearest
        tilingModeHorizontal: Texture.ClampToEdge
        tilingModeVertical: Texture.ClampToEdge
    }
    PrincipledMaterial {
        id: material
        baseColorMap: atlas
        baseColor: "white"
        roughness: 1
        metalness: 0
        alphaMode: PrincipledMaterial.Mask
        alphaCutoff: 0.1
        cullMode: Material.BackFaceCulling
    }
    Repeater3D {
        model: 12
        delegate: Model {
            required property int index
            readonly property int bodyPart: index % 6
            position: bodyPart === 0 ? Qt.vector3d(0,28,0)
                    : bodyPart === 1 ? Qt.vector3d(0,18,0)
                    : bodyPart === 2 ? Qt.vector3d(root.slim ? -5.5 : -6,18,0)
                    : bodyPart === 3 ? Qt.vector3d(root.slim ? 5.5 : 6,18,0)
                    : Qt.vector3d(bodyPart === 4 ? -2 : 2,6,0)
            geometry: SkinCuboidGeometry {
                part: [SkinCuboidGeometry.Head,SkinCuboidGeometry.Body,
                       SkinCuboidGeometry.RightArm,SkinCuboidGeometry.LeftArm,
                       SkinCuboidGeometry.RightLeg,SkinCuboidGeometry.LeftLeg][bodyPart]
                slim: root.slim
                outerLayer: index >= 6
            }
            materials: [material]
        }
    }
}
