import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import McOverlay 1.0

Item {
    required property var host
    ConfigPage {
        anchors.fill: parent
        z: 10
        backgroundColor: host.backgroundColor
        surfaceColor: host.surfaceColor
        surfaceVariant: host.surfaceVariant
        textColor: host.textColor
        secondaryTextColor: host.secondaryTextColor
        primaryColor: host.primaryColor
        primaryForegroundColor: host.primaryForegroundColor
        outlineVariantColor: host.outlineVariantColor
    }
    LegacyFeatureControls { host: parent.host }
}
