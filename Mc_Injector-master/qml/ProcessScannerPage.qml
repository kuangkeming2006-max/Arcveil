import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Item {
    required property var host
    required property real workspaceWidth
    signal attachRequested(int pid)
    id: scannerPage
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 34
        anchors.rightMargin: 24
        anchors.topMargin: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: "Process scanner"
                    color: host.textColor
                    font.pixelSize: 32
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "Choose a running Java or Minecraft window to host the overlay."
                    color: host.secondaryTextColor
                    font.pixelSize: 14
                }
            }

            MaterialButton {
                text: ProcessScanner.refreshing ? "Scanning…" : "Refresh"
                objectName: "scanRefreshButton"
                iconText: ProcessScanner.refreshing ? "" : "↻"
                filled: false
                foregroundColor: host.primaryColor
                outlineColor: host.outlineColor
                enabled: !ProcessScanner.refreshing
                onClicked: ProcessScanner.refresh()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: host.scanFeedbackVisible ? 40 : 0
            opacity: host.scanFeedbackVisible ? 1 : 0
            scale: host.scanFeedbackVisible ? 1 : 0.965
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: 360
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.34, 1.28, 0.64, 1.0, 1.0, 1.0]
                }
            }
            Behavior on opacity { NumberAnimation { duration: 210 } }
            Behavior on scale {
                NumberAnimation {
                    duration: 330
                    easing.type: Easing.OutBack
                    easing.overshoot: 0.8
                }
            }

            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 7
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ProcessScanner.statusMessage
                    color: host.secondaryTextColor
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                ScanIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(320, workspaceWidth * 0.42)
                    running: ProcessScanner.refreshing
                    color: host.primaryColor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 24
            color: host.darkTheme ? "#15191F" : "#ECE6EF"
            Behavior on color { ColorAnimation { duration: 280 } }
            clip: true

            WheelPage {
                id: processGrid
                anchors.fill: parent
                anchors.margins: 18
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                property int columnCount: width >= 1080 ? 3 : (width >= 680 ? 2 : 1)
                contentWidth: width
                contentHeight: processCardLayout.implicitHeight + 16

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                // GridView assumes one fixed cell height and would
                // overlap a card after its full title expands.
                // GridLayout keeps wide-screen columns while each
                // row naturally adopts its tallest card.
                GridLayout {
                    id: processCardLayout
                    x: 8
                    y: 8
                    width: processGrid.width - 16
                    columns: processGrid.columnCount
                    columnSpacing: 18
                    rowSpacing: 18

                    Repeater {
                        model: ProcessScanner

                        delegate: ProcessCard {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: Math.floor(
                                                       (processCardLayout.width
                                                        - processCardLayout.columnSpacing
                                                          * (processCardLayout.columns - 1))
                                                       / processCardLayout.columns)
                            Layout.minimumWidth: Layout.preferredWidth
                            Layout.maximumWidth: Layout.preferredWidth
                            Layout.preferredHeight: implicitHeight
                            height: implicitHeight
                            processPid: model.pid
                            executableName: model.executableName
                            executablePath: model.executablePath
                            windowTitle: model.windowTitle
                            memoryText: model.memoryText
                            hasWindow: model.hasWindow
                            selected: model.selected
                            entryDelay: Math.min(index, 8) * 55
                            primaryColor: host.primaryColor
                            surfaceColor: host.surfaceColor
                            textColor: host.textColor
                            secondaryTextColor: host.secondaryTextColor
                            sessionProcess: processPid === OverlayManager.targetPid
                                            && host.sessionAvailable
                            onSelectRequested: function(pid) { ProcessScanner.selectProcess(pid) }
                            onAttachRequested: function(pid) { scannerPage.attachRequested(pid) }
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: ProcessScanner.count === 0 && !ProcessScanner.refreshing

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 68
                    height: 68
                    radius: 24
                    color: host.primaryContainer
                    Text {
                        anchors.centerIn: parent
                        text: "J?"
                        color: host.primaryContainerText
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "No Java processes are running"
                    color: host.textColor
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Start Minecraft, then refresh the scanner."
                    color: host.secondaryTextColor
                    font.pixelSize: 13
                }
            }
        }
    }
}
