import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

ApplicationWindow {
    id: app

    width: AppSettings.windowWidth
    height: AppSettings.windowHeight
    minimumWidth: 1040
    minimumHeight: 680
    property bool startupReady: false
    visible: startupReady
    title: "Arcveil"
    color: backgroundColor
    // Keep the real Windows caption, snap targets and DPI-aware resize frame.
    flags: Qt.Window
    readonly property int chromeHeight: 0

    readonly property bool darkTheme: AppSettings.darkTheme
    readonly property color backgroundColor: darkTheme ? "#0F1217" : "#F5F3F8"
    readonly property color surfaceColor: darkTheme ? "#191D24" : "#FFFFFF"
    readonly property color surfaceElevatedColor: darkTheme ? "#222730" : "#FFFFFF"
    readonly property color surfaceVariant: darkTheme ? "#292E37" : "#ECE8F0"
    readonly property color primaryColor: darkTheme ? "#C9B7FF" : "#6750A4"
    readonly property color primaryContainer: darkTheme ? "#493B68" : "#EADDFF"
    readonly property color primaryContainerText: darkTheme ? "#F0E8FF" : "#21005D"
    readonly property color textColor: darkTheme ? "#F1EEF4" : "#1D1B20"
    readonly property color secondaryTextColor: darkTheme ? "#C9C3CF" : "#49454F"
    readonly property color outlineColor: darkTheme ? "#97919D" : "#79747E"
    readonly property color outlineVariantColor: darkTheme ? "#3C4049" : "#DED8E2"
    readonly property color hoverColor: darkTheme ? "#30343D" : "#E3DDE7"
    readonly property color selectedIconColor: darkTheme ? "#5B4B7D" : "#D7C7F5"
    readonly property color primaryForegroundColor: darkTheme ? "#24163E" : "#FFFFFF"
    readonly property color primaryContainerMutedText: darkTheme ? "#D4C4F4" : "#4F378B"
    property bool windowPersistenceReady: false
    property bool exitConfirmed: false
    readonly property var menuHotkeyOptions: [
        { "label": "Apostrophe (')", "value": 222 },
        { "label": "Insert", "value": 45 },
        { "label": "Home", "value": 36 },
        { "label": "End", "value": 35 },
        { "label": "F8", "value": 119 },
        { "label": "F9", "value": 120 },
        { "label": "F10", "value": 121 },
        { "label": "F12", "value": 123 }
    ]
    // Material desktop navigation panes commonly sit around 220–240 px. Keep
    // a useful, bounded resize range instead of tying the rail to a percentage
    // of an ultrawide display. The final clamp always leaves 720 px for content.
    readonly property real defaultNavigationPaneWidth: 232
    readonly property real minimumNavigationPaneWidth: 208
    readonly property real maximumNavigationPaneWidth: 320
    property real requestedNavigationPaneWidth: AppSettings.navigationPaneWidth
    readonly property real navigationPaneWidth: Math.round(
                                                    Math.max(minimumNavigationPaneWidth,
                                                             Math.min(requestedNavigationPaneWidth,
                                                                      Math.min(maximumNavigationPaneWidth,
                                                                               width - 720))))
    readonly property bool hasSelectedProcess: ProcessScanner.selectedPid !== 0
                                               || OverlayManager.targetPid !== 0
    // Error is a first-class session state: the target and its diagnostics stay
    // visible until the user explicitly retries, browses, or detaches. Browsing
    // never tears down a healthy resident Agent.
    readonly property bool sessionAvailable: OverlayManager.attached
                                             || OverlayManager.state === OverlayManager.Error
    property bool browsingProcesses: false
    readonly property bool workspaceUnlocked: sessionAvailable && !browsingProcesses
    readonly property int activeTargetPid: OverlayManager.targetPid !== 0
                                           ? OverlayManager.targetPid
                                           : ProcessScanner.selectedPid
    readonly property string activeTargetTitle: OverlayManager.targetPid !== 0
                                                ? OverlayManager.targetTitle
                                                : (ProcessScanner.processForPid(activeTargetPid).windowTitle
                                                   || (activeTargetPid === 0 ? "" : "PID " + activeTargetPid))
    readonly property var setupNavigationItems: [
        { "icon": "⌕", "label": "Scanner", "description": "Discover Java processes", "route": "scanner" },
        { "icon": "i", "label": "About", "description": "Arcveil · v53", "route": "about" },
        { "icon": "⚙", "label": "Settings", "description": "Discovery preferences", "route": "settings" }
    ]
    // Process-specific information architecture. Hypixel is intentionally a
    // separate network tool; local world diagnostics live under ESP.
    readonly property var featureNavigationItems: [
        { "icon": "⌂", "label": "Main", "description": "Session status and controls", "route": "main" },
        { "icon": "P", "label": "Player Status", "description": "Identity, health and skin", "route": "player" },
        { "icon": "◆", "label": "Config", "description": "Save and restore settings", "route": "config" },
        { "icon": "H", "label": "Hypixel", "description": "Official API statistics", "route": "hypixel" },
        { "icon": "⚙", "label": "Settings", "description": "Runtime preferences", "route": "settings" }
    ]
    readonly property var navigationItems: workspaceUnlocked
                                                    ? featureNavigationItems
                                                    : setupNavigationItems

    property string activeRoute: "scanner"
    onActiveRouteChanged: {
        Lifecycle.record("navigation: " + activeRoute)
        Qt.callLater(function() { pageEnterAnimation.restart() })
    }
    property alias autoRefresh: autoRefreshBinding.value
    QtObject {
        id: autoRefreshBinding
        property bool value: AppSettings.processAutoRefresh
        onValueChanged: if (AppSettings.processAutoRefresh !== value)
                            AppSettings.processAutoRefresh = value
    }
    property var pendingProcess: ({})
    // Scan feedback remains long enough to read, then collapses out of the
    // layout with a small upward rebound. It is deliberately independent from
    // statusMessage so the model can retain useful diagnostics without pinning
    // an empty strip above the process cards.
    property bool scanFeedbackVisible: false

    function menuHotkeyIndex(virtualKey) {
        for (let index = 0; index < menuHotkeyOptions.length; ++index) {
            if (menuHotkeyOptions[index].value === virtualKey)
                return index
        }
        return 0
    }

    function menuHotkeyLabel(virtualKey) {
        for (let index = 0; index < menuHotkeyOptions.length; ++index) {
            if (menuHotkeyOptions[index].value === virtualKey)
                return menuHotkeyOptions[index].label
        }
        if ((virtualKey >= 48 && virtualKey <= 57)
                || (virtualKey >= 65 && virtualKey <= 90))
            return String.fromCharCode(virtualKey)
        return "Key " + virtualKey
    }

    onRequestedNavigationPaneWidthChanged: navigationWidthSave.restart()
    onWidthChanged: if (windowPersistenceReady) windowSizeSave.restart()
    onHeightChanged: if (windowPersistenceReady) windowSizeSave.restart()
    onClosing: function(close) {
        Lifecycle.record("main window close requested")
        if ((OverlayManager.attached || OverlayManager.busy) && !exitConfirmed) {
            close.accepted = false
            confirmExitDialog.open()
            return
        }
        AppSettings.windowWidth = width
        AppSettings.windowHeight = height
        Lifecycle.requestExit("main window accepted close")
    }

    Dialog {
        id: confirmExitDialog
        anchors.centerIn: parent
        width: 430
        modal: true
        title: "Close Arcveil?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Closing will disconnect the current game overlay. Choose Cancel to keep the session running."
        }
        onAccepted: {
            app.exitConfirmed = true
            app.close()
        }
    }

    Component.onCompleted: windowPersistenceReady = true

    Behavior on color {
        ColorAnimation {
            duration: 320
            easing.type: Easing.BezierSpline
            easing.bezierCurve: [0.2, 0.0, 0.0, 1.0, 1.0, 1.0]
        }
    }

    Timer {
        id: navigationWidthSave
        interval: 180
        repeat: false
        onTriggered: AppSettings.navigationPaneWidth = app.requestedNavigationPaneWidth
    }

    Timer {
        id: windowSizeSave
        interval: 260
        repeat: false
        onTriggered: {
            AppSettings.windowWidth = app.width
            AppSettings.windowHeight = app.height
        }
    }

    // This is phase progress, not a fabricated byte/percent counter. Each
    // value represents a controller milestone; WaitingForOpenGL also gets a
    // moving segment because its duration depends entirely on the next frame.
    readonly property bool injectionInProgress:
        OverlayManager.state === OverlayManager.Validating
        || OverlayManager.state === OverlayManager.StartingIpc
        || OverlayManager.state === OverlayManager.LaunchingAttachHelper
        || OverlayManager.state === OverlayManager.WaitingForAgent
        || OverlayManager.state === OverlayManager.WaitingForOpenGL
    readonly property bool waitingForOpenGL:
        OverlayManager.state === OverlayManager.WaitingForOpenGL
    readonly property real injectionPhaseProgress:
        OverlayManager.state === OverlayManager.Validating ? 0.14
        : (OverlayManager.state === OverlayManager.StartingIpc ? 0.30
           : (OverlayManager.state === OverlayManager.LaunchingAttachHelper ? 0.48
              : (OverlayManager.state === OverlayManager.WaitingForAgent ? 0.66
                 : (OverlayManager.state === OverlayManager.WaitingForOpenGL ? 0.84
                    : (OverlayManager.state === OverlayManager.Active ? 1.0 : 0.0)))))
    readonly property int injectionPhaseNumber:
        OverlayManager.state === OverlayManager.Validating ? 1
        : (OverlayManager.state === OverlayManager.StartingIpc ? 2
           : (OverlayManager.state === OverlayManager.LaunchingAttachHelper ? 3
              : (OverlayManager.state === OverlayManager.WaitingForAgent ? 4
                 : (OverlayManager.state === OverlayManager.WaitingForOpenGL ? 5
                    : (OverlayManager.state === OverlayManager.Active ? 6 : 0)))))
    readonly property string injectionPhaseLabel:
        OverlayManager.state === OverlayManager.Validating ? "Validating process"
        : (OverlayManager.state === OverlayManager.StartingIpc ? "Opening secure channel"
           : (OverlayManager.state === OverlayManager.LaunchingAttachHelper ? "Loading native agent"
              : (OverlayManager.state === OverlayManager.WaitingForAgent ? "Starting in-game runtime"
                 : (OverlayManager.state === OverlayManager.WaitingForOpenGL ? "Waiting for a game frame"
                    : (OverlayManager.state === OverlayManager.Active ? "Overlay ready" : "")))))

    function openAttachDialog(pid) {
        if (pid === OverlayManager.targetPid && app.sessionAvailable) {
            app.browsingProcesses = false
            app.activeRoute = "main"
            return
        }
        ProcessScanner.selectProcess(pid)
        const details = ProcessScanner.processForPid(pid)
        pendingProcess = {
            "pid": pid,
            "windowTitle": details.windowTitle
                           || (OverlayManager.targetPid === pid
                               ? OverlayManager.targetTitle : "Selected Java process"),
            "hasWindow": details.hasWindow === true || OverlayManager.attached
        }
        attachDialog.open()
    }

    function pageIndexForRoute(route) {
        if (route === "config")
            return 1
        if (route === "main")
            return 2
        if (route === "hypixel")
            return 3
        if (route === "player")
            return 4
        if (route === "about")
            return 5
        if (route === "settings")
            return 6
        return 0
    }

    function browseProcesses() {
        browsingProcesses = true
        activeRoute = "scanner"
        ProcessScanner.refreshOnce()
    }

    function returnToSession() {
        browsingProcesses = false
        activeRoute = "main"
    }

    function clearScannerSelection() {
        if (app.injectionInProgress)
            OverlayManager.detach()
        pendingProcess = ({})
        ProcessScanner.selectProcess(0)
        ProcessScanner.refreshOnce()
    }

    Connections {
        target: ProcessScanner

        function onSelectedPidChanged() {
            if (!app.hasSelectedProcess)
                app.activeRoute = "scanner"
        }

        function onRefreshingChanged() {
            if (ProcessScanner.refreshing) {
                scanFeedbackDismiss.stop()
                app.scanFeedbackVisible = true
            } else if (app.scanFeedbackVisible) {
                scanFeedbackDismiss.restart()
            }
        }
    }

    Connections {
        target: OverlayManager

        function onTargetExited() {
            // A dead target has no session to return to. Keep the scanner route
            // explicit even if the subsequent retry transitions through Error.
            app.browsingProcesses = true
            app.activeRoute = "scanner"
            ProcessScanner.refreshOnce()
        }

        function onStateChanged() {
            if (OverlayManager.state === OverlayManager.WaitingForOpenGL) {
                // HELLO has been authenticated: reveal the process workspace
                // and lead with its live-data overview while OpenGL starts.
                app.browsingProcesses = false
                app.activeRoute = "main"
            } else if (OverlayManager.state === OverlayManager.Active) {
                app.browsingProcesses = false
                app.activeRoute = "main"
                injectionSnackbar.open()
                injectionSnackbar.closeTimer.restart()
            } else if (OverlayManager.state === OverlayManager.Error) {
                app.browsingProcesses = false
                app.activeRoute = "main"
                injectionSnackbar.closeTimer.stop()
                injectionSnackbar.close()
            } else if (OverlayManager.state === OverlayManager.Detached) {
                app.browsingProcesses = false
                app.activeRoute = "scanner"
                injectionSnackbar.closeTimer.stop()
                injectionSnackbar.close()
            }
        }
    }

    Timer {
        interval: 5000
        repeat: true
        // Wait five seconds *after* the previous five-second scan completes.
        // This prevents an always-busy loop and gives the result chip time to
        // finish its disappearance animation.
        running: app.autoRefresh && !ProcessScanner.refreshing
        onTriggered: ProcessScanner.refresh()
    }

    Timer {
        id: scanFeedbackDismiss
        interval: 1250
        repeat: false
        onTriggered: app.scanFeedbackVisible = false
    }

    Rectangle {
        anchors.fill: parent
        color: app.backgroundColor
    }

    WindowChrome { id: windowChrome; host: app }

    NavigationRail {
        id: navigationRail
        host: app
        anchors.top: windowChrome.bottom
        onRouteRequested: function(route) { app.activeRoute = route }
        onBrowseRequested: app.browseProcesses()
        onReturnRequested: app.returnToSession()
        onClearSelectionRequested: app.clearScannerSelection()
        onPaneWidthRequested: function(paneWidth) { app.requestedNavigationPaneWidth = paneWidth }
        onPaneWidthCommitted: AppSettings.navigationPaneWidth = app.requestedNavigationPaneWidth
    }

    Item {
        id: workspace
        anchors.left: navigationRail.right
        anchors.right: parent.right
        anchors.top: windowChrome.bottom
        anchors.bottom: actionBar.top
        transformOrigin: Item.Center

        SequentialAnimation {
            id: pageEnterAnimation
            PropertyAction { target: workspace; property: "opacity"; value: 0.72 }
            PropertyAction { target: workspace; property: "scale"; value: 0.982 }
            ParallelAnimation {
                NumberAnimation {
                    target: workspace
                    property: "opacity"
                    to: 1
                    duration: 330
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                }
                NumberAnimation {
                    target: workspace
                    property: "scale"
                    to: 1
                    duration: 420
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1.0, 1.0]
                }
            }
        }

        StackLayout {
            anchors.fill: parent
            currentIndex: app.pageIndexForRoute(app.activeRoute)

            // Process Scanner -------------------------------------------------
            ProcessScannerPage {
                host: app
                workspaceWidth: workspace.width
                onAttachRequested: function(pid) { app.openAttachDialog(pid) }
            }

            // Saved feature configurations ----------------------------------
            ConfigWorkspacePage { host: app }

            // Main / session dashboard ---------------------------------------
            SessionDashboardPage {
                host: app
                onBrowseRequested: app.browseProcesses()
                onAttachRequested: function(pid) { OverlayManager.attachToProcess(pid) }
                onDetachRequested: OverlayManager.detach()
            }

            // Hypixel official API -------------------------------------------
            HypixelPage { host: app }

            // Player identity / skin ----------------------------------------
            PlayerStatusPage {
                surfaceColor: app.surfaceColor
                textColor: app.textColor
                secondaryTextColor: app.secondaryTextColor
                primaryColor: app.primaryColor
            }

            // About ---------------------------------------------------------
            AboutPage { host: app }

            // Settings -------------------------------------------------------
            SettingsPage {
                host: app
                onAutoRefreshRequested: function(enabled) { app.autoRefresh = enabled }
            }
        }
    }

    SessionActionBar {
        id: actionBar
        host: app
        anchors.left: navigationRail.right
        onAttachRequested: function(pid) { app.openAttachDialog(pid) }
    }


    // Material 3 snackbar: completion is announced only after the native
    // renderer reports RENDERER_READY (OverlayManager.Active), not merely when
    // the DLL handshake succeeds.
    InjectionSnackbar { id: injectionSnackbar; host: app }

    AttachDialog {
        id: attachDialog
        host: app
        onConfirmAttach: function(pid) {
            OverlayManager.interactive = false
            if (OverlayManager.attachToProcess(pid)) {
                attachDialog.close()
                app.browsingProcesses = false
                app.activeRoute = "main"
            }
        }
    }
}
