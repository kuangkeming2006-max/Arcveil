import QtQuick

WheelHandler {
    id: wheelHandler
    required property var scroller
    property real destination: 0
    target: null
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    onWheel: event => {
        const maximum = Math.max(0, scroller.contentHeight - scroller.height)
        if (maximum <= 0) { event.accepted = false; return }
        // 100 logical pixels per notch, independent of the display DPI.
        // Do not synthesize mouse events or interfere with buttons/drags.
        const displacement = event.pixelDelta.y !== 0
                    ? -event.pixelDelta.y * 2.15 : -event.angleDelta.y / 120 * 128
        scroller.cancelFlick()
        if (!wheelMotion.running)
            destination = scroller.contentY
        destination = Math.max(0, Math.min(maximum, destination + displacement))
        // Keep one continuous critically damped motion alive for an entire
        // wheel gesture.  Additional packets move the target without
        // restarting the animation, and a larger accumulated distance creates
        // proportionally more speed instead of a longer fixed-velocity queue.
        if (!wheelMotion.running)
            wheelMotion.start()
        event.accepted = true
    }

    // Pointer handlers do not expose a default object-list property in
    // Qt 6.10.  Keep the animation in an explicit property so WheelPage stays
    // a valid reusable QML type on every page.
    property var wheelMotion: FrameAnimation {
        onTriggered: {
            const maximum = Math.max(0, wheelHandler.scroller.contentHeight
                                        - wheelHandler.scroller.height)
            wheelHandler.destination = Math.max(0, Math.min(maximum,
                                                            wheelHandler.destination))
            const dt = Math.max(1.0 / 240.0,
                                Math.min(1.0 / 30.0, frameTime || 1.0 / 60.0))
            // Exponential target following is frame-rate independent and its
            // speed grows with accumulated wheel distance. Rapid wheel input
            // therefore moves faster instead of lengthening a low-velocity
            // critically-damped tail.
            const delta = wheelHandler.destination - wheelHandler.scroller.contentY
            const response = 1.0 - Math.exp(-21.0 * dt)
            wheelHandler.scroller.contentY = Math.max(0, Math.min(maximum,
                    wheelHandler.scroller.contentY + delta * response))

            if (Math.abs(delta) < 0.2) {
                wheelHandler.scroller.contentY = wheelHandler.destination
                stop()
            }
        }
        onRunningChanged: {
            if (!running) {
                wheelHandler.destination = wheelHandler.scroller.contentY
            }
        }
    }
}
