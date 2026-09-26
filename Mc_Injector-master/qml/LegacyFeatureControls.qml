import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

WheelPage {
    required property var host
    function gameMetric(value, decimals, suffix) {
        if (!OverlayManager.gameStateAvailable || OverlayManager.gameStateStale)
            return "—"
        return Number(value).toFixed(decimals) + (suffix || "")
    }

    function mappingStateLabel() {
        const state = (OverlayManager.mappingState || "waiting").toLowerCase()
        if (state === "ready" || state === "resolved")
            return "Mapping ready"
        if (state === "unsupported")
            return "Unsupported mappings"
        if (state === "resolving")
            return "Resolving mappings"
        if (state === "waiting_for_game_thread")
            return "Waiting for game thread"
        if (state === "no_player")
            return "Waiting for local player"
        if (state === "jni_error")
            return "JNI data read unavailable"
        if (state === "unavailable")
            return "Game data unavailable"
        return "Waiting for game data"
    }

    function gameStateSummary() {
        if (!OverlayManager.attached)
            return "Attach the native agent to start the live data channel."
        if (!OverlayManager.gameStateReceived)
            return "Secure IPC connected · waiting for the first game snapshot."
        if (OverlayManager.gameStateStale)
            return "Telemetry has paused. Values are hidden until a fresh snapshot arrives."
        const state = (OverlayManager.mappingState || "").toLowerCase()
        if (state === "unsupported")
            return "Overlay rendering is active, but this client needs a compatible mapping profile."
        if (!OverlayManager.gameStateAvailable)
            return "The mapping resolver is still preparing Minecraft game data."
        return "Live JNI snapshot · sequence " + OverlayManager.gameStateSequence
    }

    function sampleTimeLabel() {
        if (!OverlayManager.gameStateReceived || OverlayManager.gameStateTimestamp <= 0)
            return "No samples yet"
        return "Agent time " + new Date(Number(OverlayManager.gameStateTimestamp)).toLocaleTimeString()
    }

    id: legacyControls
    visible: false
    enabled: false
    anchors.fill: parent
    contentWidth: width
    contentHeight: mainContent.implicitHeight + 58
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    ScrollBar.vertical: ScrollBar {
        policy: mainContent.implicitHeight + 58 > parent.height
                ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
    }

    ColumnLayout {
        id: mainContent
        x: 34
        y: 28
        width: parent.width - 68
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: "ESP"
                    color: host.textColor
                    font.pixelSize: 32
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "Entity hitboxes and bed markers for single-player world debugging"
                    color: host.secondaryTextColor
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.preferredWidth: liveStateLabel.implicitWidth + 34
                Layout.preferredHeight: 38
                radius: 19
                color: !OverlayManager.gameStateReceived ? host.surfaceVariant
                     : (OverlayManager.gameStateStale ? "#FFE2E0"
                        : (OverlayManager.gameStateAvailable ? "#D7F7DD" : "#FFF1C7"))

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: !OverlayManager.gameStateReceived ? "#79747E"
                             : (OverlayManager.gameStateStale ? "#BA1A1A"
                                : (OverlayManager.gameStateAvailable ? "#20853B" : "#B77900"))
                    }
                    Text {
                        id: liveStateLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: !OverlayManager.gameStateReceived ? "WAITING"
                            : (OverlayManager.gameStateStale ? "STALE"
                               : (OverlayManager.gameStateAvailable ? "LIVE" : "MAPPING"))
                        color: host.textColor
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.8
                    }
                }

                Behavior on color { ColorAnimation { duration: 220 } }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 94
            radius: 24
            color: !OverlayManager.gameStateReceived ? host.surfaceVariant
                 : (OverlayManager.gameStateStale ? "#FFF0EF"
                    : ((OverlayManager.mappingState || "").toLowerCase() === "unsupported"
                       ? "#FFF1C7" : host.primaryContainer))

            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 15

                Rectangle {
                    Layout.preferredWidth: 50
                    Layout.preferredHeight: 50
                    radius: 17
                    color: OverlayManager.gameStateAvailable
                           && !OverlayManager.gameStateStale
                           ? host.primaryColor : "#D6CFDA"
                    Text {
                        anchors.centerIn: parent
                        text: OverlayManager.gameStateAvailable
                              && !OverlayManager.gameStateStale ? "↯" : "…"
                        color: OverlayManager.gameStateAvailable
                               && !OverlayManager.gameStateStale ? "white" : host.secondaryTextColor
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: legacyControls.mappingStateLabel()
                        color: host.textColor
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: legacyControls.gameStateSummary()
                        color: host.secondaryTextColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }

                Text {
                    visible: OverlayManager.gameStateReceived
                    text: legacyControls.sampleTimeLabel()
                    color: host.secondaryTextColor
                    font.pixelSize: 10
                }
            }

            Behavior on color { ColorAnimation { duration: 240 } }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: featureControls.implicitHeight + 38
            radius: 24
            color: host.surfaceColor
            border.width: 1
            border.color: host.outlineVariantColor

            ColumnLayout {
                id: featureControls
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 19
                spacing: 10

                Text {
                    text: "IN-GAME FEATURE CONTROLS"
                    color: host.secondaryTextColor
                    font.pixelSize: 10
                    font.weight: Font.Bold
                    font.letterSpacing: 0.9
                }

                Repeater {
                    model: [
                        { "label": "ESP master", "detail": "World-space overlay rendering", "key": "master" },
                        { "label": "3D living hitboxes", "detail": "Occlusion-independent projected AABB wireframes", "key": "entities" },
                        { "label": "Players only", "detail": "Hide non-player living-entity boxes", "key": "playersOnly" },
                        { "label": "Show teammate boxes", "detail": "Keep 3D boxes around teammates in confirmed matches", "key": "teammateBoxes" },
                        { "label": "Bed ESP", "detail": "Chunk diffing, bulk section copy and lazy verification", "key": "beds" },
                        { "label": "Automatic bed refresh", "detail": "Periodically rebuild loaded-chunk bed data", "key": "bedAuto" },
                        { "label": "Solid translucent bed fill", "detail": "Fill projected bed boxes while retaining the outline", "key": "bedFill" },
                        { "label": "Bed proximity alert", "detail": "Persistent distance-tracking warning while an enemy is in range", "key": "bedThreat" },
                        { "label": "Bed defense panel", "detail": "Fixed-size material icons above each detected bed", "key": "bedDefense" },
                        { "label": "Show own bed materials", "detail": "Include the local team's bed defense information", "key": "ownBedInfo" },
                        { "label": "Hold key to show materials", "detail": "Show defense cards only while the configured key is held", "key": "bedHold" },
                        { "label": "Perspective-sized cards", "detail": "Near cards appear larger and distant cards smaller", "key": "bedPerspective" },
                        { "label": "Local Debug chat", "detail": "Show match, team and teammate decisions only in your chat log", "key": "debugChat" },
                        { "label": "World labels", "detail": "Coordinates and entity identifiers", "key": "labels" },
                        { "label": "Hypixel panel", "detail": "Compact automatic team roster and Bed Wars metrics", "key": "hypixel" },
                        { "label": "Hold key for player stats", "detail": "Keep the roster card hidden until its configured key is held", "key": "hypixelHold" }
                    ]

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 14
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: modelData.label; color: host.textColor; font.pixelSize: 14; font.weight: Font.Medium }
                            Text { text: modelData.detail; color: host.secondaryTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        Switch {
                            checked: modelData.key === "master" ? OverlayManager.espEnabled
                                   : modelData.key === "entities" ? OverlayManager.entityEspEnabled
                                   : modelData.key === "playersOnly" ? OverlayManager.entityEspPlayersOnly
                                   : modelData.key === "teammateBoxes" ? OverlayManager.showTeammateBoxes
                                   : modelData.key === "beds" ? OverlayManager.bedEspEnabled
                                   : modelData.key === "bedAuto" ? OverlayManager.bedAutoRefreshEnabled
                                   : modelData.key === "bedFill" ? OverlayManager.bedEspFilled
                                   : modelData.key === "bedThreat" ? OverlayManager.bedThreatAlertsEnabled
                                   : modelData.key === "bedDefense" ? OverlayManager.bedDefensePanelEnabled
                                   : modelData.key === "ownBedInfo" ? OverlayManager.showOwnBedDefenseInfo
                                   : modelData.key === "bedHold" ? OverlayManager.bedDefenseHoldToShow
                                   : modelData.key === "bedPerspective" ? OverlayManager.bedDefensePerspectiveScale
                                   : modelData.key === "debugChat" ? OverlayManager.debugChatEnabled
                                   : modelData.key === "labels" ? OverlayManager.espLabelsEnabled
                                   : modelData.key === "hypixel" ? OverlayManager.hypixelPanelEnabled
                                   : OverlayManager.hypixelPanelHoldToShow
                            onToggled: {
                                if (modelData.key === "master") OverlayManager.espEnabled = checked
                                else if (modelData.key === "entities") OverlayManager.entityEspEnabled = checked
                                else if (modelData.key === "playersOnly") OverlayManager.entityEspPlayersOnly = checked
                                else if (modelData.key === "teammateBoxes") OverlayManager.showTeammateBoxes = checked
                                else if (modelData.key === "beds") OverlayManager.bedEspEnabled = checked
                                else if (modelData.key === "bedAuto") OverlayManager.bedAutoRefreshEnabled = checked
                                else if (modelData.key === "bedFill") OverlayManager.bedEspFilled = checked
                                else if (modelData.key === "bedThreat") OverlayManager.bedThreatAlertsEnabled = checked
                                else if (modelData.key === "bedDefense") OverlayManager.bedDefensePanelEnabled = checked
                                else if (modelData.key === "ownBedInfo") OverlayManager.showOwnBedDefenseInfo = checked
                                else if (modelData.key === "bedHold") OverlayManager.bedDefenseHoldToShow = checked
                                else if (modelData.key === "bedPerspective") OverlayManager.bedDefensePerspectiveScale = checked
                                else if (modelData.key === "debugChat") OverlayManager.debugChatEnabled = checked
                                else if (modelData.key === "labels") OverlayManager.espLabelsEnabled = checked
                                else if (modelData.key === "hypixel") OverlayManager.hypixelPanelEnabled = checked
                                else OverlayManager.hypixelPanelHoldToShow = checked
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { Layout.preferredWidth: 156; text: "Player box color"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: ["#FF3B30", "#FF9500", "#FFD60A", "#30D158", "#64D2FF", "#0A84FF", "#BF5AF2", "#FFFFFF"]
                        Rectangle {
                            required property string modelData
                            width: 26; height: 26; radius: 13; color: modelData
                            border.width: OverlayManager.playerEspColor.toUpperCase() === modelData ? 3 : 1
                            border.color: OverlayManager.playerEspColor.toUpperCase() === modelData ? host.primaryColor : "#8B8490"
                            TapHandler { onTapped: OverlayManager.playerEspColor = parent.modelData }
                        }
                    }
                    MaterialTextField {
                        id: playerColorField
                        Layout.preferredWidth: 106; Layout.preferredHeight: 40
                        text: OverlayManager.playerEspColor
                        maximumLength: 7
                        onEditingFinished: {
                            if (/^#[0-9a-fA-F]{6}$/.test(text)) OverlayManager.playerEspColor = text
                            text = OverlayManager.playerEspColor
                        }
                        Connections {
                            target: OverlayManager
                            function onFeatureSettingsChanged() {
                                if (!playerColorField.activeFocus) playerColorField.text = OverlayManager.playerEspColor
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { Layout.preferredWidth: 156; text: "Material card color"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: ["#191621", "#202532", "#12252A", "#2A181F", "#241E34", "#111111"]
                        Rectangle {
                            required property string modelData
                            width: 26; height: 26; radius: 13; color: modelData
                            border.width: OverlayManager.bedDefensePanelColor.toUpperCase() === modelData ? 3 : 1
                            border.color: OverlayManager.bedDefensePanelColor.toUpperCase() === modelData ? host.primaryColor : "#8B8490"
                            TapHandler { onTapped: OverlayManager.bedDefensePanelColor = parent.modelData }
                        }
                    }
                    MaterialTextField {
                        id: materialPanelColorField
                        Layout.preferredWidth: 106; Layout.preferredHeight: 40
                        text: OverlayManager.bedDefensePanelColor
                        maximumLength: 7
                        onEditingFinished: {
                            if (/^#[0-9a-fA-F]{6}$/.test(text)) OverlayManager.bedDefensePanelColor = text
                            text = OverlayManager.bedDefensePanelColor
                        }
                        Connections {
                            target: OverlayManager
                            function onFeatureSettingsChanged() {
                                if (!materialPanelColorField.activeFocus)
                                    materialPanelColorField.text = OverlayManager.bedDefensePanelColor
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text { Layout.preferredWidth: 156; text: "Material card opacity"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: 100; stepSize: 1
                        value: OverlayManager.bedDefensePanelOpacity
                        onMoved: OverlayManager.bedDefensePanelOpacity = Math.round(value)
                    }
                    Rectangle {
                        Layout.preferredWidth: 74; Layout.preferredHeight: 34; radius: 17; color: host.primaryContainer
                        Text { anchors.centerIn: parent; text: OverlayManager.bedDefensePanelOpacity + "%"; color: host.primaryColor; font.pixelSize: 12; font.weight: Font.Bold }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { Layout.preferredWidth: 156; text: "Stats card color"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: ["#000000", "#FFFFFF"]
                        Rectangle {
                            required property string modelData
                            width: 34; height: 34; radius: 17; color: modelData
                            border.width: OverlayManager.hypixelPanelColor.toUpperCase() === modelData ? 3 : 1
                            border.color: OverlayManager.hypixelPanelColor.toUpperCase() === modelData ? host.primaryColor : "#8B8490"
                            TapHandler { onTapped: OverlayManager.hypixelPanelColor = parent.modelData }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: OverlayManager.hypixelPanelColor.toUpperCase() === "#FFFFFF" ? "LIGHT" : "DARK"
                        color: host.secondaryTextColor
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text { Layout.preferredWidth: 156; text: "Stats card opacity"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: 100; stepSize: 1
                        value: OverlayManager.hypixelPanelOpacity
                        onMoved: OverlayManager.hypixelPanelOpacity = Math.round(value)
                    }
                    Rectangle {
                        Layout.preferredWidth: 74; Layout.preferredHeight: 34; radius: 17; color: host.primaryContainer
                        Text { anchors.centerIn: parent; text: OverlayManager.hypixelPanelOpacity + "%"; color: host.primaryColor; font.pixelSize: 12; font.weight: Font.Bold }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { Layout.preferredWidth: 156; text: "STATS rail"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: ["#825DE8", "#0A84FF", "#30D158", "#FF9F0A", "#FF375F", "#FFFFFF", "#000000"]
                        Rectangle {
                            required property string modelData
                            width: 26; height: 26; radius: 13; color: modelData
                            border.width: OverlayManager.hypixelRailColor.toUpperCase() === modelData ? 3 : 1
                            border.color: OverlayManager.hypixelRailColor.toUpperCase() === modelData ? host.primaryColor : "#8B8490"
                            TapHandler { onTapped: OverlayManager.hypixelRailColor = parent.modelData }
                        }
                    }
                    MaterialTextField {
                        id: statsRailColorField
                        Layout.preferredWidth: 106; Layout.preferredHeight: 40
                        text: OverlayManager.hypixelRailColor
                        maximumLength: 7
                        onEditingFinished: {
                            if (/^#[0-9a-fA-F]{6}$/.test(text)) OverlayManager.hypixelRailColor = text
                            text = OverlayManager.hypixelRailColor
                        }
                        Connections {
                            target: OverlayManager
                            function onFeatureSettingsChanged() {
                                if (!statsRailColorField.activeFocus) statsRailColorField.text = OverlayManager.hypixelRailColor
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text { Layout.preferredWidth: 156; text: "Rail opacity"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: 100; stepSize: 1
                        value: OverlayManager.hypixelRailOpacity
                        onMoved: OverlayManager.hypixelRailOpacity = Math.round(value)
                    }
                    Rectangle {
                        Layout.preferredWidth: 74; Layout.preferredHeight: 34; radius: 17; color: host.primaryContainer
                        Text { anchors.centerIn: parent; text: OverlayManager.hypixelRailOpacity + "%"; color: host.primaryColor; font.pixelSize: 12; font.weight: Font.Bold }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: "Player-stats hold key"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: "TAB is the default; the confirmed roster remains cached while released"; color: host.secondaryTextColor; font.pixelSize: 11 }
                    }
                    KeyCaptureButton {
                        Layout.preferredWidth: 190
                        virtualKey: OverlayManager.hypixelPanelHotkey
                        primaryColor: host.primaryColor
                        surfaceColor: host.surfaceColor
                        textColor: host.textColor
                        onKeyCaptured: function(key) { OverlayManager.hypixelPanelHotkey = key }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: "Hold-to-view key"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                        Text { text: "Press and hold in game; release to hide the material cards"; color: host.secondaryTextColor; font.pixelSize: 11 }
                    }
                    KeyCaptureButton {
                        Layout.preferredWidth: 190
                        virtualKey: OverlayManager.bedDefenseHotkey
                        primaryColor: host.primaryColor
                        surfaceColor: host.surfaceColor
                        textColor: host.textColor
                        onKeyCaptured: function(key) { OverlayManager.bedDefenseHotkey = key }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text { Layout.preferredWidth: 156; text: "Bed box color"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Repeater {
                        model: ["#FF5C68", "#FF9500", "#FFD60A", "#30D158", "#64D2FF", "#0A84FF", "#BF5AF2", "#FFFFFF"]
                        Rectangle {
                            required property string modelData
                            width: 26; height: 26; radius: 13; color: modelData
                            border.width: OverlayManager.bedEspColor.toUpperCase() === modelData ? 3 : 1
                            border.color: OverlayManager.bedEspColor.toUpperCase() === modelData ? host.primaryColor : "#8B8490"
                            TapHandler { onTapped: OverlayManager.bedEspColor = parent.modelData }
                        }
                    }
                    MaterialTextField {
                        id: bedColorField
                        Layout.preferredWidth: 106; Layout.preferredHeight: 40
                        text: OverlayManager.bedEspColor
                        maximumLength: 7
                        onEditingFinished: {
                            if (/^#[0-9a-fA-F]{6}$/.test(text)) OverlayManager.bedEspColor = text
                            text = OverlayManager.bedEspColor
                        }
                        Connections {
                            target: OverlayManager
                            function onFeatureSettingsChanged() {
                                if (!bedColorField.activeFocus) bedColorField.text = OverlayManager.bedEspColor
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Text { Layout.preferredWidth: 156; text: "Bed warning range"; color: host.textColor; font.pixelSize: 13; font.weight: Font.Medium }
                    Slider {
                        Layout.fillWidth: true
                        from: 3; to: 32; stepSize: 1
                        value: OverlayManager.bedThreatRadius
                        onMoved: OverlayManager.bedThreatRadius = Math.round(value)
                    }
                    Rectangle {
                        Layout.preferredWidth: 74; Layout.preferredHeight: 34; radius: 17; color: host.primaryContainer
                        Text { anchors.centerIn: parent; text: OverlayManager.bedThreatRadius + " m"; color: host.primaryColor; font.pixelSize: 12; font.weight: Font.Bold }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: "Bed material radius"
                        color: host.textColor
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    Repeater {
                        model: 8
                        MaterialButton {
                            required property int index
                            readonly property int radiusValue: index + 3
                            compact: true
                            text: radiusValue.toString()
                            filled: OverlayManager.bedDefenseRadius === radiusValue
                            containerColor: host.primaryColor
                            foregroundColor: filled ? host.primaryForegroundColor : host.primaryColor
                            outlineColor: host.outlineVariantColor
                            onClicked: OverlayManager.bedDefenseRadius = radiusValue
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text {
                        Layout.fillWidth: true
                        text: "Placed a bed in an already loaded chunk? Rebuild the bed cache immediately."
                        color: host.secondaryTextColor
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    MaterialButton {
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 40
                        text: "Refresh beds now"
                        filled: false
                        enabled: OverlayManager.attached
                        foregroundColor: host.primaryColor
                        outlineColor: host.outlineVariantColor
                        onClicked: OverlayManager.refreshBedCache()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Press " + host.menuHotkeyLabel(OverlayManager.menuHotkey)
                          + " in Minecraft to open the animated Click GUI. Match-only team features remain inactive until Sidebar state is confirmed."
                    color: host.primaryColor
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 980 ? 4 : 2
            columnSpacing: 16
            rowSpacing: 16

            Repeater {
                model: [
                    {
                        "label": "HEALTH",
                        "value": legacyControls.gameMetric(OverlayManager.playerHealth, 1, " HP"),
                        "detail": OverlayManager.gameStateAvailable && !OverlayManager.gameStateStale
                                  ? "of " + Number(OverlayManager.playerMaxHealth).toFixed(1) + " HP" : "Awaiting player",
                        "icon": "♥",
                        "tint": "#FFE2E0"
                    },
                    {
                        "label": "ENTITY ID",
                        "value": OverlayManager.gameStateAvailable && !OverlayManager.gameStateStale
                                 ? "#" + OverlayManager.playerEntityId : "—",
                        "detail": "Local player",
                        "icon": "ID",
                        "tint": host.primaryContainer
                    },
                    {
                        "label": "ENTITIES",
                        "value": OverlayManager.gameStateAvailable && !OverlayManager.gameStateStale
                                 ? String(OverlayManager.loadedEntities) : "—",
                        "detail": "Loaded in world",
                        "icon": "◎",
                        "tint": "#D7F7DD"
                    },
                    {
                        "label": "BEDS",
                        "value": OverlayManager.gameStateAvailable && !OverlayManager.gameStateStale
                                 ? String(OverlayManager.bedCount) : "—",
                        "detail": "Known loaded blocks",
                        "icon": "▰",
                        "tint": "#FFF1C7"
                    }
                ]

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 154
                    radius: 22
                    color: metricHover.hovered ? host.surfaceElevatedColor : host.surfaceColor
                    border.width: 1
                    border.color: metricHover.hovered ? host.primaryColor : host.outlineVariantColor
                    scale: metricHover.hovered ? 1.012 : 1

                    HoverHandler { id: metricHover }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 7

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: host.secondaryTextColor
                                font.pixelSize: 10
                                font.weight: Font.Bold
                                font.letterSpacing: 0.9
                            }
                            Rectangle {
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 38
                                radius: 13
                                color: modelData.tint
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.icon
                                    color: host.textColor
                                    font.pixelSize: modelData.icon === "ID" ? 10 : 18
                                    font.weight: Font.Bold
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: host.textColor
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.detail
                            color: host.secondaryTextColor
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Behavior on scale {
                        NumberAnimation {
                            duration: 260
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                        }
                    }
                    Behavior on color { ColorAnimation { duration: 180 } }
                    Behavior on border.color { ColorAnimation { duration: 180 } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                Layout.preferredHeight: 132
                radius: 22
                color: host.surfaceColor
                border.width: 1
                border.color: host.outlineVariantColor

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8
                    Text {
                        text: "PLAYER POSITION"
                        color: host.secondaryTextColor
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.9
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "X  " + legacyControls.gameMetric(OverlayManager.playerX, 2, "")
                              + "     Y  " + legacyControls.gameMetric(OverlayManager.playerY, 2, "")
                              + "     Z  " + legacyControls.gameMetric(OverlayManager.playerZ, 2, "")
                        color: host.textColor
                        font.pixelSize: width >= 620 ? 21 : 16
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "World-space coordinates from the local player binding"
                        color: host.secondaryTextColor
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.preferredHeight: 132
                radius: 22
                color: host.surfaceVariant

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 7
                    Text {
                        text: "MAPPING PROFILE"
                        color: host.secondaryTextColor
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.9
                    }
                    Text {
                        Layout.fillWidth: true
                        text: OverlayManager.mappingProfile.length > 0
                              ? OverlayManager.mappingProfile : "Not detected"
                        color: host.textColor
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: legacyControls.mappingStateLabel()
                        color: host.primaryColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

    }
}
