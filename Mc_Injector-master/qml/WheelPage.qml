import QtQuick

Flickable {
    id: page
    acceptedButtons: Qt.NoButton
    property bool pageWheelEnabled: true
    PageWheelHandler { scroller: page; enabled: page.pageWheelEnabled }
}
