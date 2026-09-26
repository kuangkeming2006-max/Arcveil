import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Rectangle {
    required property var host
    signal routeRequested(string route)
    signal browseRequested()
    signal returnRequested()
    signal clearSelectionRequested()
    signal paneWidthRequested(real paneWidth)
    signal paneWidthCommitted()
    id: navigationRail
    width: host.navigationPaneWidth
    z: 2
    anchors.left: parent.left
    anchors.bottom: parent.bottom
    color: host.darkTheme ? "#15181E" : "#F1ECF4"

    Behavior on color { ColorAnimation { duration: 320 } }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 22
        anchors.rightMargin: 22
        anchors.topMargin: 26
        anchors.bottomMargin: 24
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 18
                color: host.primaryColor

                Text {
                    anchors.centerIn: parent
                    text: "A"
                    color: host.primaryForegroundColor
                    font.pixelSize: 25
                    font.weight: Font.Bold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: "ARCVEIL"
                    color: host.textColor
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: host.workspaceUnlocked ? "Process workspace" : "Process setup"
                    color: host.secondaryTextColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: host.outlineVariantColor
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 14

            Text {
                anchors.fill: parent
                text: "SETUP"
                opacity: host.workspaceUnlocked ? 0 : 1
                color: host.secondaryTextColor
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.2
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
            Text {
                anchors.fill: parent
                text: "PROCESS TOOLS"
                opacity: host.workspaceUnlocked ? 1 : 0
                color: host.secondaryTextColor
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.2
                Behavior on opacity {
                    NumberAnimation {
                        duration: 300
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                    }
                }
            }
        }

        ListView {
            id: navigationList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: host.navigationItems
            spacing: 8
            interactive: contentHeight > height
            acceptedButtons: Qt.NoButton
            PageWheelHandler { scroller: navigationList }
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            add: Transition {
                ParallelAnimation {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 300
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                    }
                    NumberAnimation {
                        property: "x"
                        from: -16
                        to: 0
                        duration: 360
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                    }
                }
            }
            remove: Transition {
                ParallelAnimation {
                    NumberAnimation {
                        property: "opacity"
                        to: 0
                        duration: 150
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1.0, 1.0]
                    }
                    NumberAnimation {
                        property: "x"
                        to: -10
                        duration: 180
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1.0, 1.0]
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: navigationList.contentHeight > navigationList.height
                        ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            delegate: Item {
                required property int index
                required property var modelData
                width: navigationList.width
                height: 68
                opacity: 0
                transform: Translate { id: navigationEntryOffset; x: -12 }

                SequentialAnimation on opacity {
                    running: true
                    PauseAnimation { duration: index * 45 }
                    NumberAnimation {
                        from: 0
                        to: 1
                        duration: 280
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                    }
                }
                SequentialAnimation {
                    running: true
                    PauseAnimation { duration: index * 45 }
                    NumberAnimation {
                        target: navigationEntryOffset
                        property: "x"
                        from: -12
                        to: 0
                        duration: 340
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                    }
                }

                Rectangle {
                    id: navPill
                    anchors.fill: parent
                    radius: 20
                    color: "transparent"
                    scale: navMouse.pressed ? 0.985 : 1
                    transformOrigin: Item.Center
                    // A selected row becomes inactive while the pointer is
                    // still resting on it after navigation. Suppress that
                    // synthetic hover until the pointer actually exits;
                    // otherwise it appears as the old tab flashing dark.
                    property bool suppressHoverUntilExit: false

                    Behavior on scale {
                        NumberAnimation {
                            duration: 240
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: navPill.radius
                        color: host.primaryContainer
                        opacity: host.activeRoute === modelData.route ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation {
                                duration: 260
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: navPill.radius
                        color: host.hoverColor
                        opacity: host.activeRoute !== modelData.route
                                 && navMouse.containsMouse
                                 && !navPill.suppressHoverUntilExit ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation {
                                duration: 220
                                easing.type: Easing.BezierSpline
                                easing.bezierCurve: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
                            }
                        }
                    }

                    RippleEffect {
                        id: navRipple
                        anchors.fill: parent
                        rippleColor: host.primaryColor
                        peakOpacity: 0.0
                        cornerRadius: navPill.radius
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 13
                            color: host.activeRoute === modelData.route
                                   ? host.selectedIconColor : host.surfaceVariant

                            Behavior on color {
                                ColorAnimation {
                                    duration: 260
                                    easing.type: Easing.BezierSpline
                                    easing.bezierCurve: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: modelData.icon
                                color: host.activeRoute === modelData.route
                                       ? host.primaryContainerText : host.secondaryTextColor
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: host.activeRoute === modelData.route
                                       ? host.primaryContainerText : host.textColor
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.description
                                color: host.secondaryTextColor
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                MouseArea {
                    id: navMouse
                    objectName: "navigation_" + modelData.route
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: function(mouse) {
                        const point = mapToItem(navPill, mouse.x, mouse.y)
                        navRipple.burst(point.x, point.y)
                    }
                    onExited: navPill.suppressHoverUntilExit = false
                    onClicked: {
                        navPill.suppressHoverUntilExit = true
                        navigationRail.routeRequested(modelData.route)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: host.hasSelectedProcess ? 148 : 94
            radius: 24
            color: host.hasSelectedProcess ? host.surfaceColor : host.surfaceVariant
            border.width: host.hasSelectedProcess ? 1 : 0
            border.color: host.outlineVariantColor

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    duration: 360
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 7

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: OverlayManager.rendererActive ? "#20853B"
                              : (OverlayManager.attached || OverlayManager.busy
                                 ? "#B77900"
                                 : (host.hasSelectedProcess ? host.primaryColor : "#79747E"))
                    }
                    Text {
                        Layout.fillWidth: true
                        text: OverlayManager.rendererActive ? "OVERLAY LIVE"
                              : (OverlayManager.attached ? "AGENT READY"
                                 : (OverlayManager.busy ? "ATTACHING"
                                    : (host.hasSelectedProcess ? "PROCESS SELECTED" : "READY TO SCAN")))
                        color: OverlayManager.rendererActive ? "#155724" : host.secondaryTextColor
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: host.hasSelectedProcess
                          ? host.activeTargetTitle
                          : "Select a Java process to unlock its tools."
                    color: host.textColor
                    font.pixelSize: host.hasSelectedProcess ? 14 : 12
                    font.weight: host.hasSelectedProcess ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: host.hasSelectedProcess
                    Text {
                        Layout.fillWidth: true
                        text: "PID " + host.activeTargetPid
                        color: host.secondaryTextColor
                        font.pixelSize: 11
                    }
                    MaterialButton {
                        Layout.preferredWidth: 116
                        Layout.preferredHeight: 36
                        text: host.sessionAvailable
                              ? (host.browsingProcesses ? "Return" : "Browse processes")
                              : "Clear selection"
                        filled: false
                        foregroundColor: host.primaryColor
                        outlineColor: "transparent"
                        onClicked: {
                            if (host.sessionAvailable) {
                                if (host.browsingProcesses)
                                    navigationRail.returnRequested()
                                else
                                    navigationRail.browseRequested()
                            } else {
                                navigationRail.clearSelectionRequested()
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: host.outlineVariantColor
    }

    // A 12 px hit area straddles the visual divider, so the resize target
    // remains easy to acquire without making the divider look heavy.
    MouseArea {
        id: navigationResizeHandle
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: -6
        width: 12
        z: 20
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        preventStealing: true

        property real pressWindowX: 0
        property real pressPaneWidth: host.requestedNavigationPaneWidth

        onPressed: function(mouse) {
            pressWindowX = mapToItem(host.contentItem, mouse.x, mouse.y).x
            pressPaneWidth = host.navigationPaneWidth
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const windowX = mapToItem(host.contentItem, mouse.x, mouse.y).x
            navigationRail.paneWidthRequested(Math.max(
                        host.minimumNavigationPaneWidth,
                        Math.min(host.maximumNavigationPaneWidth,
                                 pressPaneWidth + windowX - pressWindowX)))
        }
        onReleased: navigationRail.paneWidthCommitted()
        onCanceled: navigationRail.paneWidthCommitted()
        onDoubleClicked: navigationRail.paneWidthRequested(host.defaultNavigationPaneWidth)

        ToolTip.visible: containsMouse
        ToolTip.delay: 500
        ToolTip.text: "Drag to resize · Double-click to reset"

        Rectangle {
            anchors.centerIn: parent
            width: navigationResizeHandle.pressed ? 4 : 3
            height: navigationResizeHandle.containsMouse ? 52 : 36
            radius: width / 2
            color: navigationResizeHandle.containsMouse
                   ? host.primaryColor : host.outlineColor
            opacity: navigationResizeHandle.containsMouse ? 0.9 : 0.45

            Behavior on height {
                NumberAnimation {
                    duration: 260
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                }
            }
            Behavior on color { ColorAnimation { duration: 160 } }
            Behavior on opacity { NumberAnimation { duration: 160 } }
        }
    }
}
