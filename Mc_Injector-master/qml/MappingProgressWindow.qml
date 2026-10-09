import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import McOverlay 1.0

ApplicationWindow {
    id: window
    objectName: "mappingProgressWindow"
    required property var host
    title: "Arcveil · Mapping / 反混淆进度"
    width: 1180; height: 810
    minimumWidth: 940; minimumHeight: 680
    visible: false
    color: host.backgroundColor
    palette.window: host.backgroundColor
    palette.text: host.textColor
    palette.button: host.surfaceColor
    palette.buttonText: host.textColor
    onClosing: function(close) { close.accepted = true } // Closing hides only; controller lives in C++.
    Connections {
        target: MappingProgress
        function onOpenRequested() { window.show(); window.raise(); window.requestActivate() }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "Mapping / 反混淆进度"; color: host.textColor; font.pixelSize: 26; font.bold: true }
                Label { text: "运行时映射 · 每一个结果都有来源"; color: host.secondaryTextColor }
            }
            Item { Layout.fillWidth: true }
            Label { text: "ARCVEIL  /  v56.6"; color: host.secondaryTextColor; font.letterSpacing: 1.3 }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Repeater {
                model: ["查询本地 Cache", "获取 Snapshot", "自动反混淆", "反混淆成功"]
                delegate: Rectangle {
                    id: stage
                    required property int index
                    required property string modelData
                    readonly property string stateName: MappingProgress.steps[index] || "pending"
                    Layout.fillWidth: true; implicitHeight: 76; radius: 12
                    color: host.surfaceColor
                    border.color: stateName === "failed" ? "#d66b74" : stateName === "success" ? "#55b6a0" : host.outlineColor
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 5
                        RowLayout {
                            Label { text: (stage.index + 1).toString().padStart(2,"0"); color: host.secondaryTextColor; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: stage.stateName
                                color: stage.stateName === "degraded" ? "#c79745" : host.secondaryTextColor
                                font.pixelSize: 11
                                SequentialAnimation on opacity {
                                    running: window.visible && stage.stateName === "running"; loops: Animation.Infinite
                                    NumberAnimation { to: 0.35; duration: 650 }
                                    NumberAnimation { to: 1; duration: 650 }
                                }
                            }
                        }
                        Label { text: stage.modelData; color: host.textColor; font.bold: true; font.pixelSize: 14 }
                    }
                    Rectangle {
                        visible: stage.stateName === "running"
                        anchors.bottom: parent.bottom; height: 2; width: parent.width / 3; color: "#9b80dc"
                        SequentialAnimation on x {
                            running: window.visible && stage.stateName === "running"; loops: Animation.Infinite
                            NumberAnimation { from: 0; to: stage.width * 2/3; duration: 950 }
                            NumberAnimation { to: 0; duration: 950 }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 197; color: host.surfaceColor; radius: 12
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 8
                RowLayout {
                    Label { text: "REFERENCE"; color: host.secondaryTextColor; font.bold: true; font.letterSpacing: 1.2 }
                    Item { Layout.fillWidth: true }
                    Label {
                        objectName: "mappingReferenceStatus"
                        text: MappingProgress.reference.referenceAvailable ? "Verified reference selected" : "No reference selected"
                        color: MappingProgress.reference.referenceAvailable ? "#55b6a0" : "#c79745"
                    }
                }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 14; rowSpacing: 3
                    Repeater {
                        model: ["referenceAvailable", "sourceClientFamily", "sourceFingerprint", "sourcePack / sourceSnapshot", "targetClientFamily", "targetFingerprint", "selectionReason"]
                        delegate: RowLayout {
                            required property string modelData
                            Layout.fillWidth: true; Layout.columnSpan: 2; spacing: 14
                            Label { text: modelData; Layout.preferredWidth: 193; color: host.secondaryTextColor; font.pixelSize: 11 }
                            TextInput {
                                Layout.fillWidth: true; readOnly: true; selectByMouse: true; clip: true
                                text: modelData === "sourcePack / sourceSnapshot"
                                    ? ((MappingProgress.reference.sourcePack || "—") + " / " + (MappingProgress.reference.sourceSnapshot || "—"))
                                    : String(MappingProgress.reference[modelData] ?? "—")
                                color: host.textColor; font.family: "Consolas"; font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            Repeater {
                model: [ {title:"进行中", category:"active"}, {title:"已完成", category:"completed"}, {title:"待匹配", category:"pending"} ]
                delegate: Rectangle {
                    id: bucket
                    required property var modelData
                    Layout.fillWidth: true; Layout.fillHeight: true; color: host.surfaceColor; radius: 12
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 12
                        RowLayout {
                            Label { text: bucket.modelData.title; color: host.textColor; font.bold: true; font.pixelSize: 16 }
                            Item { Layout.fillWidth: true }
                            Label { text: MappingProgress[bucket.modelData.category + "Count"]; color: host.secondaryTextColor; font.pixelSize: 18 }
                        }
                        ListView {
                            id: list
                            objectName: "mapping_" + bucket.modelData.category
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 7
                            model: MappingProgress[bucket.modelData.category]
                            ScrollBar.vertical: ScrollBar {}
                            add: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 300 }
                                    NumberAnimation { property: "x"; from: -20; to: 0; duration: 300; easing.type: Easing.OutCubic }
                                }
                            }
                            remove: Transition {
                                ParallelAnimation {
                                    NumberAnimation { property: "opacity"; to: 0; duration: 230 }
                                    NumberAnimation { property: "x"; to: 24; duration: 230 }
                                }
                            }
                            displaced: Transition { NumberAnimation { properties: "y"; duration: 230; easing.type: Easing.OutCubic } }
                            delegate: Rectangle {
                                id: symbolRow
                                required property string symbol
                                required property string logicalName
                                required property string runtimeName
                                required property string symbolStatus
                                required property real confidence
                                required property string evidence
                                required property string reason
                                required property bool settling
                                required property bool isRequired
                                required property bool isVerified
                                width: list.width; height: content.implicitHeight + 20; radius: 8
                                color: host.backgroundColor
                                border.color: settling ? "#55b6a0" : host.outlineVariantColor
                                HoverHandler { id: rowHover }
                                ToolTip.visible: rowHover.hovered
                                ToolTip.delay: 500
                                ToolTip.text: symbolRow.symbol + "\n" + (symbolRow.reason || symbolRow.evidence)
                                Rectangle {
                                    visible: symbolRow.symbolStatus === "active" && !symbolRow.settling
                                    anchors.bottom: parent.bottom; height: 2; width: parent.width / 4; color: "#9b80dc"
                                    SequentialAnimation on x {
                                        running: window.visible && MappingProgress.matchingEnabled && symbolRow.symbolStatus === "active" && !symbolRow.settling; loops: Animation.Infinite
                                        NumberAnimation { from: 0; to: symbolRow.width * 3/4; duration: 1100 }
                                        NumberAnimation { to: 0; duration: 1100 }
                                    }
                                }
                                ColumnLayout {
                                    id: content
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                    anchors.margins: 10; spacing: 5
                                    Label { Layout.fillWidth: true; text: symbolRow.logicalName; textFormat: Text.PlainText; color: host.textColor; elide: Text.ElideRight; font.pixelSize: 12 }
                                    Label {
                                        Layout.fillWidth: true
                                        text: "→  " + (symbolRow.runtimeName || "等待运行时证据")
                                        textFormat: Text.PlainText; elide: Text.ElideMiddle
                                        color: symbolRow.settling || symbolRow.symbolStatus === "completed" ? "#55b6a0" : host.secondaryTextColor
                                        font.family: "Consolas"; font.pixelSize: 12
                                        SequentialAnimation on opacity {
                                            running: window.visible && MappingProgress.matchingEnabled && symbolRow.symbolStatus === "active" && !symbolRow.settling; loops: Animation.Infinite
                                            NumberAnimation { to: 0.3; duration: 500 }
                                            NumberAnimation { to: 1; duration: 500 }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10; color: host.secondaryTextColor
                                        text: (symbolRow.isRequired ? "required" : "optional") + " · confidence " + symbolRow.confidence.toFixed(2)
                                    }
                                    Label {
                                        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10
                                        color: symbolRow.isVerified ? "#55b6a0" : symbolRow.runtimeName ? "#c99948" : host.secondaryTextColor
                                        text: symbolRow.isVerified ? "verified" : symbolRow.runtimeName ? "provisional / awaiting-final-validation" : "unresolved"
                                    }
                                    Label {
                                        visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                                        text: symbolRow.reason || symbolRow.evidence; textFormat: Text.PlainText
                                        color: host.secondaryTextColor; font.pixelSize: 10
                                    }
                                }
                            }
                            Label { anchors.centerIn: parent; visible: list.count === 0; text: "暂无项目"; color: host.secondaryTextColor }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Label { Layout.fillWidth: true; text: MappingProgress.status; textFormat: Text.PlainText; color: host.secondaryTextColor; wrapMode: Text.Wrap; font.pixelSize: 12 }
            Button { objectName: "mappingStopMatching"; text: "Pause Automatic Matching"; enabled: MappingProgress.matchingEnabled; onClicked: MappingProgress.stopMatching() }
            Button { text: "Resume Automatic Matching"; visible: !MappingProgress.matchingEnabled && !MappingProgress.successful; enabled: MappingService.busy; onClicked: MappingProgress.resumeMatching() }
            Button { text: "关闭窗口"; onClicked: window.close() }
        }
        Label { text: "关闭窗口不会停止后台任务。停止匹配会暂停后续 snapshot watch / retry，也不会分离 Agent。"; color: host.secondaryTextColor; font.pixelSize: 11 }
    }
}
