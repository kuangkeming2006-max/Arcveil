import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Item {
    required property var host
    signal browseRequested()
    signal attachRequested(int pid)
    signal detachRequested()
    id: sessionPage
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 34
        spacing: 20

        Text {
            text: "Main"
            color: host.textColor
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
        Text {
                text: "Review the active process, injection progress, errors, and overlay controls."
            color: host.secondaryTextColor
            font.pixelSize: 14
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                Layout.minimumWidth: 420
                radius: 24
                color: host.surfaceColor
                border.width: 1
                border.color: host.outlineVariantColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.preferredWidth: 60
                            Layout.preferredHeight: 60
                            radius: 20
                            color: OverlayManager.rendererActive ? "#D7F7DD"
                                  : (OverlayManager.attached || OverlayManager.busy
                                     ? "#FFF1C7" : host.surfaceVariant)
                            Text {
                                anchors.centerIn: parent
                                text: OverlayManager.rendererActive ? "✓"
                                      : (OverlayManager.attached || OverlayManager.busy ? "…" : "—")
                                color: OverlayManager.rendererActive ? "#155724"
                                      : (OverlayManager.attached || OverlayManager.busy
                                         ? "#7A4F00" : host.outlineColor)
                                font.pixelSize: 25
                                font.weight: Font.Bold
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: host.activeTargetPid !== 0
                                      ? host.activeTargetTitle : "No active target"
                                color: host.textColor
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: host.activeTargetPid !== 0
                                      ? "PID " + host.activeTargetPid
                                      : "Attach from the scanner to begin"
                                color: host.secondaryTextColor
                                font.pixelSize: 13
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: host.outlineVariantColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: OverlayManager.rendererActive
                                  ? "In-game renderer active"
                                  : (OverlayManager.attached ? "Waiting for an OpenGL frame"
                                     : (OverlayManager.busy ? "Loading native agent…" : "Agent not connected"))
                            color: OverlayManager.rendererActive ? "#155724" : host.secondaryTextColor
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: OverlayManager.statusMessage
                            color: host.secondaryTextColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 5
                            spacing: 7
                            visible: host.injectionInProgress

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: host.injectionPhaseLabel
                                    color: host.primaryColor
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: "PHASE " + host.injectionPhaseNumber
                                    color: host.outlineColor
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.8
                                }
                            }

                            Rectangle {
                                id: injectionProgressTrack
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                radius: 3
                                color: host.surfaceVariant
                                clip: true

                                Rectangle {
                                    width: injectionProgressTrack.width
                                           * host.injectionPhaseProgress
                                    height: parent.height
                                    radius: parent.radius
                                    color: host.primaryColor

                                    Behavior on width {
                                        NumberAnimation {
                                            duration: 420
                                            easing.type: Easing.BezierSpline
                                            easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                                        }
                                    }
                                }

                                // The last phase has no meaningful
                                // percentage: Minecraft controls
                                // when the next SwapBuffers arrives.
                                Rectangle {
                                    id: openGlProgressSweep
                                    visible: host.waitingForOpenGL
                                    width: Math.max(58, injectionProgressTrack.width * 0.22)
                                    height: parent.height
                                    radius: parent.radius
                                    color: host.primaryColor

                                    NumberAnimation on x {
                                        running: openGlProgressSweep.visible
                                        loops: Animation.Infinite
                                        from: -openGlProgressSweep.width
                                        to: injectionProgressTrack.width
                                        duration: 1250
                                        easing.type: Easing.BezierSpline
                                        easing.bezierCurve: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: OverlayManager.renderer.length > 0
                            text: "Renderer: " + OverlayManager.renderer
                            color: host.primaryColor
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: OverlayManager.errorDetail.length > 0
                            text: OverlayManager.errorCode + ": " + OverlayManager.errorDetail
                            color: "#BA1A1A"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Overlay visible"; color: host.textColor; font.pixelSize: 16; font.weight: Font.Medium }
                            Text { text: "Render Dear ImGui before Minecraft swaps its OpenGL frame"; color: host.secondaryTextColor; font.pixelSize: 12 }
                        }
                        Switch {
                            checked: OverlayManager.overlayEnabled
                            enabled: OverlayManager.attached
                            onToggled: OverlayManager.overlayEnabled = checked
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Interactive mode"; color: host.textColor; font.pixelSize: 16; font.weight: Font.Medium }
                            Text { text: "Capture input in ImGui; turn off to pass all input to the game"; color: host.secondaryTextColor; font.pixelSize: 12 }
                        }
                        Switch {
                            checked: OverlayManager.interactive
                            enabled: OverlayManager.attached
                            onToggled: OverlayManager.interactive = checked
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: OverlayManager.state === OverlayManager.Error
                        spacing: 10

                        MaterialButton {
                            text: "Retry attach"
                            iconText: "↻"
                            filled: true
                            containerColor: host.primaryColor
                            enabled: OverlayManager.targetPid !== 0 && !OverlayManager.busy
                            onClicked: sessionPage.attachRequested(OverlayManager.targetPid)
                        }

                        MaterialButton {
                            text: "Browse processes"
                            iconText: "⌕"
                            filled: false
                            foregroundColor: host.primaryColor
                            outlineColor: host.outlineColor
                            enabled: !OverlayManager.busy
                            onClicked: sessionPage.browseRequested()
                        }
                    }

                    MaterialButton {
                        Layout.alignment: Qt.AlignLeft
                        text: "Detach overlay"
                        iconText: "×"
                        filled: false
                        foregroundColor: "#BA1A1A"
                        outlineColor: "#BA1A1A"
                        enabled: OverlayManager.attached
                        visible: OverlayManager.attached
                        onClicked: sessionPage.detachRequested()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 220
                radius: 24
                color: "#211E24"
                clip: true

                Column {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 22
                    spacing: 22
                    Text {
                        text: "TEXTGUI PREVIEW"
                        color: "#D0C8D7"; font.pixelSize: 10
                        font.weight: Font.Bold; font.letterSpacing: 1.1
                    }
                    TextGuiPreview {
                        width: parent.width
                        modules: OverlayManager.textGuiModules.concat(Blacklist.panelEnabled ? ["Blacklist"] : [])
                        textColor: OverlayManager.textGuiColor
                        alignment: OverlayManager.textGuiAlignment
                        verticalLine: OverlayManager.textGuiVerticalLine
                    }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: OverlayManager.textGuiEnabled ? "Enabled modules · live settings" : "Preview only · TextGUI is disabled"
                        color: "#AAA5B2"; font.pixelSize: 12
                    }
                }
            }
        }
    }
}
