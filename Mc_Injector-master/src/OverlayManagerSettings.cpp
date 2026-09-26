#include "OverlayManager.h"
#include "OverlayManagerCodec.internal.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <tlhelp32.h>
#endif


using namespace overlay_detail;

void OverlayManager::setOverlayEnabled(bool enabled)
{
    if (m_overlayEnabled == enabled && (enabled || !m_interactive))
        return;
    m_overlayEnabled = enabled;
    emit overlayEnabledChanged();
    if (!enabled && m_interactive) {
        m_interactive = false;
        emit interactiveChanged();
    }
    sendStateSnapshot();
}

void OverlayManager::setInteractive(bool interactive)
{
    if (m_interactive == interactive && (!interactive || m_overlayEnabled))
        return;
    if (interactive && !m_overlayEnabled) {
        m_overlayEnabled = true;
        emit overlayEnabledChanged();
    }
    m_interactive = interactive;
    emit interactiveChanged();
    sendStateSnapshot();
}

void OverlayManager::setEspEnabled(const bool enabled)
{
    if (m_espEnabled == enabled) return;
    m_espEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setEntityEspEnabled(const bool enabled)
{
    if (m_entityEspEnabled == enabled) return;
    m_entityEspEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setEntityEspPlayersOnly(const bool enabled)
{
    if (m_entityEspPlayersOnly == enabled) return;
    m_entityEspPlayersOnly = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedEspEnabled(const bool enabled)
{
    if (m_bedEspEnabled == enabled) return;
    m_bedEspEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedAutoRefreshEnabled(const bool enabled)
{
    if (m_bedAutoRefreshEnabled == enabled) return;
    m_bedAutoRefreshEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setEspLabelsEnabled(const bool enabled)
{
    if (m_espLabelsEnabled == enabled) return;
    m_espLabelsEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelEnabled(const bool enabled)
{
    if (m_hypixelPanelEnabled == enabled) return;
    m_hypixelPanelEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedThreatAlertsEnabled(const bool enabled)
{
    if (m_bedThreatAlertsEnabled == enabled) return;
    m_bedThreatAlertsEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefensePanelEnabled(const bool enabled)
{
    if (m_bedDefensePanelEnabled == enabled) return;
    m_bedDefensePanelEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelHoldToShow(const bool enabled)
{
    if (m_hypixelPanelHoldToShow == enabled) return;
    m_hypixelPanelHoldToShow = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelHotkey(const int virtualKey)
{
    if ((virtualKey != 0 && virtualKey < 8) || virtualKey > 254 || m_hypixelPanelHotkey == virtualKey) return;
    m_hypixelPanelHotkey = virtualKey;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelOpacity(const int opacity)
{
    const int bounded = std::clamp(opacity, 0, 100);
    if (m_hypixelPanelOpacity == bounded) return;
    m_hypixelPanelOpacity = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelColor(const QString &color)
{
    const QString requested = normalizedRgbColor(color);
    const QString normalized = requested == QStringLiteral("#FFFFFF")
        ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
    if (normalized.isEmpty() || normalized == m_hypixelPanelColor) return;
    m_hypixelPanelColor = normalized;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelRailColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_hypixelRailColor) return;
    m_hypixelRailColor = normalized;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelRailOpacity(const int opacity)
{
    const int bounded = std::clamp(opacity, 0, 100);
    if (bounded == m_hypixelRailOpacity) return;
    m_hypixelRailOpacity = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelScale(const int scale)
{
    const int bounded = std::clamp(scale, 70, 160);
    if (m_hypixelPanelScale == bounded) return;
    m_hypixelPanelScale = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelX(const int normalizedX)
{
    const int bounded = std::clamp(normalizedX, -1, 1000);
    if (m_hypixelPanelX == bounded) return;
    m_hypixelPanelX = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setHypixelPanelY(const int normalizedY)
{
    const int bounded = std::clamp(normalizedY, -1, 1000);
    if (m_hypixelPanelY == bounded) return;
    m_hypixelPanelY = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setClickGuiLightTheme(const bool light)
{
    if (m_clickGuiLightTheme == light) return;
    m_clickGuiLightTheme = light;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedEspFilled(const bool enabled)
{
    if (m_bedEspFilled == enabled) return;
    m_bedEspFilled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setDebugChatEnabled(const bool enabled)
{
    if (m_debugChatEnabled == enabled) return;
    m_debugChatEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setShowOwnBedDefenseInfo(const bool enabled)
{
    if (m_showOwnBedDefenseInfo == enabled) return;
    m_showOwnBedDefenseInfo = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setShowTeammateBoxes(const bool enabled)
{
    if (m_showTeammateBoxes == enabled) return;
    m_showTeammateBoxes = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setShowTeammateArrows(const bool enabled)
{
    if (m_showTeammateArrows == enabled) return;
    m_showTeammateArrows = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setSafewalkEnabled(const bool enabled)
{
    if (m_safewalkEnabled == enabled) return;
    m_safewalkEnabled = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setSafewalkReleaseDelayMs(const int delayMs)
{
    const int bounded = std::clamp(delayMs, 0, 750);
    if (m_safewalkReleaseDelayMs == bounded) return;
    m_safewalkReleaseDelayMs = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

#define MC_OVERLAY_BOOL_SETTER(Name, Member) \
void OverlayManager::Name(const bool enabled) \
{ \
    if (Member == enabled) return; \
    Member = enabled; \
    storeFeatureSettings(); \
    emit featureSettingsChanged(); \
    sendFeatureSnapshot(); \
}

MC_OVERLAY_BOOL_SETTER(setScaffoldEnabled, m_scaffoldEnabled)
MC_OVERLAY_BOOL_SETTER(setFlyEnabled, m_flyEnabled)
MC_OVERLAY_BOOL_SETTER(setBhopEnabled, m_bhopEnabled)
MC_OVERLAY_BOOL_SETTER(setBhopAutoJump, m_bhopAutoJump)
MC_OVERLAY_BOOL_SETTER(setFireballEspEnabled, m_fireballEspEnabled)
MC_OVERLAY_BOOL_SETTER(setFireballEspFilled, m_fireballEspFilled)
MC_OVERLAY_BOOL_SETTER(setLongJumpEnabled, m_longJumpEnabled)
MC_OVERLAY_BOOL_SETTER(setAimAssistEnabled, m_aimAssistEnabled)
MC_OVERLAY_BOOL_SETTER(setAimLockOnMode, m_aimLockOnMode)
MC_OVERLAY_BOOL_SETTER(setAimSilentLock, m_aimSilentLock)
MC_OVERLAY_BOOL_SETTER(setAimScannerEnabled, m_aimScannerEnabled)
MC_OVERLAY_BOOL_SETTER(setAimAttackViability, m_aimAttackViability)
MC_OVERLAY_BOOL_SETTER(setSilentControlAdaptation, m_silentControlAdaptation)
MC_OVERLAY_BOOL_SETTER(setTextGuiEnabled, m_textGuiEnabled)
MC_OVERLAY_BOOL_SETTER(setTextGuiShowModes, m_textGuiShowModes)
MC_OVERLAY_BOOL_SETTER(setAllowHypixelMovement, m_allowHypixelMovement)

#undef MC_OVERLAY_BOOL_SETTER

void OverlayManager::setSafewalkEdgeSensitivity(const int sensitivity)
{
    const int bounded = std::clamp(sensitivity, 0, 95);
    if (m_safewalkEdgeSensitivity == bounded) return;
    m_safewalkEdgeSensitivity = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setSafewalkMinimumPitch(const int pitch)
{
    const int bounded = std::clamp(pitch, -90, 90);
    if (m_safewalkMinimumPitch == bounded) return;
    m_safewalkMinimumPitch = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setSafewalkHotkey(const int virtualKey)
{
    if ((virtualKey != 0 && virtualKey < 8) || virtualKey > 254 || m_safewalkHotkey == virtualKey) return;
    m_safewalkHotkey = virtualKey;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setFlySpeedPercent(const int speed)
{
    const int bounded = std::clamp(speed, 10, 500);
    if (m_flySpeedPercent == bounded) return;
    m_flySpeedPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setBhopAirSpeedPercent(const int speed)
{
    const int bounded = std::clamp(speed, 10, 300);
    if (m_bhopAirSpeedPercent == bounded) return;
    m_bhopAirSpeedPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setLongJumpSpeedPercent(const int speed)
{
    const int bounded = std::clamp(speed, 25, 250);
    if (m_longJumpSpeedPercent == bounded) return;
    m_longJumpSpeedPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setFireballEspColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_fireballEspColor) return;
    m_fireballEspColor = normalized;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimSlowdownPercent(const int coefficient)
{
    const int bounded = std::clamp(coefficient, 5, 95);
    if (m_aimSlowdownPercent == bounded) return;
    m_aimSlowdownPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimSpeedPercent(const int speed)
{
    const int bounded = std::clamp(speed, 1, 100);
    if (m_aimSpeedPercent == bounded) return;
    m_aimSpeedPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimMinimumDistance(const int distance)
{
    const int bounded = std::clamp(distance, 0, 64);
    if (m_aimMinimumDistance == bounded) return;
    m_aimMinimumDistance = bounded;
    if (m_aimMaximumDistance < std::max(1, bounded))
        m_aimMaximumDistance = std::max(1, bounded);
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimMaximumDistance(const int distance)
{
    const int bounded = std::clamp(distance,
        std::max(1, m_aimMinimumDistance), 128);
    if (m_aimMaximumDistance == bounded) return;
    m_aimMaximumDistance = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimFovDegrees(const int degrees)
{
    const int bounded = std::clamp(degrees, 1, 360);
    if (m_aimFovDegrees == bounded) return;
    m_aimFovDegrees = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setAimAttackCps(const int cps)
{
    const int bounded=std::clamp(cps,1,20);
    if(m_aimAttackCps==bounded) return;
    m_aimAttackCps=bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setClickGuiWidthPercent(const int percent)
{
    const int bounded = std::clamp(percent, 80, 150);
    if (m_clickGuiWidthPercent == bounded) return;
    m_clickGuiWidthPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setClickGuiHeightPercent(const int percent)
{
    const int bounded = std::clamp(percent, 80, 150);
    if (m_clickGuiHeightPercent == bounded) return;
    m_clickGuiHeightPercent = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setClickGuiOpacity(const int opacity)
{
    const int bounded = std::clamp(opacity, 35, 100);
    if (m_clickGuiOpacity == bounded) return;
    m_clickGuiOpacity = bounded;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setTextGuiColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_textGuiColor) return;
    m_textGuiColor = normalized;
    storeFeatureSettings(); emit featureSettingsChanged(); sendFeatureSnapshot();
}

void OverlayManager::setBedDefenseHoldToShow(const bool enabled)
{
    if (m_bedDefenseHoldToShow == enabled) return;
    m_bedDefenseHoldToShow = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefensePerspectiveScale(const bool enabled)
{
    if (m_bedDefensePerspectiveScale == enabled) return;
    m_bedDefensePerspectiveScale = enabled;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefenseRadius(const int radius)
{
    const int bounded = std::clamp(radius, 3, 10);
    if (m_bedDefenseRadius == bounded) return;
    m_bedDefenseRadius = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedThreatRadius(const int radius)
{
    const int bounded = std::clamp(radius, 3, 32);
    if (m_bedThreatRadius == bounded) return;
    m_bedThreatRadius = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefenseHotkey(const int virtualKey)
{
    if ((virtualKey != 0 && virtualKey < 8) || virtualKey > 254 || m_bedDefenseHotkey == virtualKey) return;
    m_bedDefenseHotkey = virtualKey;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefensePanelOpacity(const int opacity)
{
    const int bounded = std::clamp(opacity, 0, 100);
    if (m_bedDefensePanelOpacity == bounded) return;
    m_bedDefensePanelOpacity = bounded;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setPlayerEspColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_playerEspColor) return;
    m_playerEspColor = normalized;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedEspColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_bedEspColor) return;
    m_bedEspColor = normalized;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setBedDefensePanelColor(const QString &color)
{
    const QString normalized = normalizedRgbColor(color);
    if (normalized.isEmpty() || normalized == m_bedDefensePanelColor) return;
    m_bedDefensePanelColor = normalized;
    storeFeatureSettings();
    emit featureSettingsChanged();
    sendFeatureSnapshot();
}

void OverlayManager::setMenuHotkey(const int virtualKey)
{
    if ((virtualKey != 0 && virtualKey < 8) || virtualKey > 254 || m_menuHotkey == virtualKey) return;
    m_menuHotkey = virtualKey;
    storeFeatureSettings();
    emit menuHotkeyChanged();
    sendBindSnapshot();
}

void OverlayManager::setGuiScaleIndex(const int index)
{
    const int bounded = std::clamp(index, 0, 3);
    if (m_guiScaleIndex == bounded) return;
    m_guiScaleIndex = bounded;
    storeFeatureSettings();
    emit guiScaleIndexChanged();
    sendGuiScaleSnapshot();
}

QStringList OverlayManager::textGuiModules() const
{
    QStringList result;
    const auto add=[&](bool enabled,const char* label) {
        if(enabled) result.append(QString::fromLatin1(label));
    };
    add(m_entityEspEnabled,"Player ESP"); add(m_bedEspEnabled,"Bed ESP");
    add(m_nametagEnabled,"Nametag"); add(m_bedThreatAlertsEnabled,"Bed Alert");
    add(m_safewalkEnabled,"Safewalk"); add(m_scaffoldEnabled,"Scaffold");
    add(m_flyEnabled,"Fly"); add(m_bhopEnabled,"BHop");
    if(m_aimAssistEnabled) {
        QString label=QStringLiteral("Aim Assist");
        if(m_textGuiShowModes) label+=m_aimSilentLock
            ? QStringLiteral("  ·  Silent Lock")
            : m_aimLockOnMode ? QStringLiteral("  ·  Lock On")
                              : QStringLiteral("  ·  Smooth");
        result.append(label);
    }
    add(m_hypixelPanelEnabled,"Player Stats");
    add(m_debugChatEnabled,"Debug"); add(m_fireballEspEnabled,"Fireball ESP");
    add(m_featureExtraBits & 0x04U,"Knockback Prediction");
    add(m_featureExtraBits & 0x08U,"Bow Prediction");
    add(m_featureExtraBits & 0x20U,"Local Velocity");
    add((m_featureHotkeysPackedC & 0x10000U) != 0U,"FreeLook");
    QSettings mediaSettings;
    mediaSettings.beginGroup(QStringLiteral("MediaOverlay"));
    const bool mediaEnabled=mediaSettings.value(QStringLiteral("enabled"),true).toBool();
    mediaSettings.endGroup();
    add(mediaEnabled,"Now Playing");
    return result;
}
