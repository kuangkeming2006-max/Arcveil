import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import McOverlay 1.0

Popup {
    id: mappingPopup
    objectName: "mappingConsole"
    required property var host
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(host.width - 48, 1120)
    height: Math.min(host.height - 64, 780)
    palette.base: host.backgroundColor
    palette.text: host.textColor
    palette.button: host.surfaceColor
    palette.buttonText: host.textColor
    palette.placeholderText: host.secondaryTextColor
    padding: 22
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: host.surfaceColor
        border.color: host.outlineColor
        radius: 20
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Mapping Console"; color: host.textColor; font.pixelSize: 24; font.bold: true }
            Item { Layout.fillWidth: true }
            Label { text: "Developer tools · v55.9"; color: host.secondaryTextColor }
            Button { objectName: "mappingConsoleClose"; text: "Close"; onClicked: mappingPopup.close() }
        }
        Label {
            Layout.fillWidth: true
            text: MappingService.status || "No mapping check has run in this session."
            textFormat: Text.PlainText
            color: host.textColor
            elide: Text.ElideRight
        }
        ProgressBar {
            Layout.fillWidth: true
            indeterminate: MappingService.busy && MappingService.progress < 0
            value: Math.max(0, MappingService.progress)
            visible: MappingService.busy
        }
        TextField {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            text: MappingService.fingerprint
            placeholderText: "Runtime fingerprint appears here after capture"
            font.family: "Consolas"
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: filter
                objectName: "mappingConsoleFilter"
                Layout.fillWidth: true
                placeholderText: "Filter symbols, events or failure reasons"
                selectByMouse: true
            }
            Button { text: "Clear view"; onClicked: MappingService.clearEvents() }
            Button { text: "Rollback previous"; enabled: !MappingService.busy; onClicked: MappingService.rollback() }
            Button { text: "Cancel Attach"; enabled: MappingService.busy; onClicked: OverlayManager.cancelAttach() }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: host.backgroundColor
            border.color: host.outlineVariantColor
            ListView {
                id: events
                objectName: "mappingConsoleEvents"
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                spacing: 4
                model: MappingService.events
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property string kind
                    required property string timeText
                    required property string symbol
                    required property string mappingText
                    required property real confidence
                    required property string evidence
                    required property string reason
                    required property string detail
                    required property bool accepted
                    readonly property bool matches: filter.text.length === 0 ||
                        (kind + " " + symbol + " " + mappingText + " " + detail + " " + evidence).toLowerCase().includes(filter.text.toLowerCase())
                    width: events.width - 12
                    height: matches ? rowContent.implicitHeight + 14 : 0
                    visible: matches
                    radius: 7
                    color: host.surfaceColor
                    ColumnLayout {
                        id: rowContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 7
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: row.timeText; color: host.secondaryTextColor; font.pixelSize: 11; font.family: "Consolas" }
                            Label { text: row.kind; color: host.primaryColor; font.pixelSize: 12; font.bold: true; textFormat: Text.PlainText }
                            Label { Layout.fillWidth: true; text: row.symbol; color: host.textColor; font.family: "Consolas"; elide: Text.ElideRight; textFormat: Text.PlainText }
                            Label {
                                visible: row.confidence >= 0
                                text: "Confidence " + Math.round(row.confidence * 100) + "%"
                                color: row.accepted ? (host.darkTheme ? "#97D9AB" : "#24653B") : (host.darkTheme ? "#FFB4AB" : "#A12828")
                                font.pixelSize: 11
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.kind === "symbol" ? (row.mappingText || row.reason) : row.detail
                            color: host.textColor
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: row.evidence
                            color: host.secondaryTextColor
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: events.count === 0
                    text: "Checks run automatically before injection.\nThis console stays hidden unless you open it."
                    horizontalAlignment: Text.AlignHCenter
                    color: host.secondaryTextColor
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Rollback applies to the next injection. The current Agent registry stays frozen."
            color: host.secondaryTextColor
            font.pixelSize: 11
        }
        TextField {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            text: MappingService.logPath
            placeholderText: "JSONL log path"
            font.pixelSize: 11
        }
    }
}
