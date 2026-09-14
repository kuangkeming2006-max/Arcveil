import QtQuick

Item {
    id: root
    property var modules: []
    property color textColor: "#7EE7FF"
    property int alignment: 2
    property bool verticalLine: true
    implicitHeight: list.implicitHeight
    Rectangle {
        width: 2; height: list.implicitHeight
        visible: root.verticalLine && root.modules.length > 0
        color: root.textColor; opacity: 0.8; radius: 1
    }
    Column {
        id: list
        x: root.verticalLine ? 13 : 0
        width: parent.width - x
        spacing: 6
        Repeater {
            model: root.modules
            delegate: Item {
                id: entry
                required property string modelData
                width: list.width; height: 24
                readonly property string modeSeparator: "  ·  "
                readonly property int modeOffset: modelData.indexOf(modeSeparator)
                readonly property string moduleName: modeOffset >= 0
                                                     ? modelData.substring(0, modeOffset)
                                                     : modelData
                readonly property string moduleMode: modeOffset >= 0
                                                     ? modelData.substring(modeOffset + modeSeparator.length)
                                                     : ""
                property var brightness: []
                function shuffle() {
                    const order = [], next = []
                    for (let i=0;i<moduleName.length;++i) { order.push(i); next.push(0.5) }
                    for (let i=order.length-1;i>0;--i) {
                        const j=Math.floor(Math.random()*(i+1)), old=order[i]
                        order[i]=order[j]; order[j]=old
                    }
                    for(let i=0;i<Math.ceil(order.length/2);++i) next[order[i]]=1
                    if (JSON.stringify(next)===JSON.stringify(brightness) && next.length>1) {
                        const bright=next.indexOf(1), dim=next.indexOf(0.5)
                        if(bright>=0 && dim>=0) { next[bright]=0.5; next[dim]=1 }
                    }
                    brightness=next
                }
                Component.onCompleted: shuffle()
                Timer { interval: 2400; repeat: true; running: root.visible; onTriggered: entry.shuffle() }
                Row {
                    x: root.alignment===1 ? (parent.width-width)*0.5
                       : root.alignment===2 ? parent.width-width : 0
                    Repeater {
                        model: entry.moduleName.length
                        delegate: Text {
                            required property int index
                            text: entry.moduleName[index]
                            color: root.textColor
                            font.family: "Segoe UI"; font.pixelSize: 19; font.weight: Font.DemiBold
                            opacity: entry.brightness[index] ?? 0.5
                            Behavior on opacity { NumberAnimation { duration: 2100; easing.type: Easing.InOutSine } }
                        }
                    }
                    Text {
                        visible: entry.moduleMode.length > 0
                        text: entry.moduleMode.length > 0 ? "  " + entry.moduleMode : ""
                        color: root.textColor
                        opacity: 0.48
                        font.family: "Segoe UI"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        y: 4
                    }
                }
            }
        }
    }
}
