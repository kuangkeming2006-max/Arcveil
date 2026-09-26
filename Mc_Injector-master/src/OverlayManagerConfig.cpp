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

namespace {
bool settingsGroupHasValues(QSettings &settings, const QString &group)
{
    settings.beginGroup(group);
    const bool result = !settings.childKeys().isEmpty();
    settings.endGroup();
    return result;
}

void copySettingsGroup(QSettings &settings, const QString &source,
                       const QString &destination)
{
    settings.beginGroup(source);
    const QStringList keys = settings.childKeys();
    QVariantMap values;
    for (const QString &key : keys)
        values.insert(key, settings.value(key));
    settings.endGroup();

    settings.beginGroup(destination);
    settings.remove(QString{});
    for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator)
        settings.setValue(iterator.key(), iterator.value());
    settings.endGroup();
}

}

namespace overlay_detail {
bool validConfigName(const QString &name)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9 _.-]{1,32}$"));
    return expression.match(name).hasMatch();
}

}

void OverlayManager::setConfigAutoSave(const bool enabled)
{
    if (m_configAutoSave == enabled) return;
    m_configAutoSave = enabled;
    if (enabled && m_activeConfig.isEmpty()) {
        (void)saveConfig(QStringLiteral("Auto Save"));
    } else {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Config"));
        settings.setValue(QStringLiteral("autoSave"), enabled);
        settings.endGroup();
        settings.sync();
        emit configStateChanged();
    }
}

bool OverlayManager::saveConfig(const QString &requestedName)
{
    const QString name = requestedName.simplified();
    if (!validConfigName(name)) {
        setStatusMessage(QStringLiteral(
            "Config names may contain letters, numbers, spaces, dots, dashes and underscores."));
        return false;
    }
    if (m_featureSettingsStoreTimer.isActive()) {
        m_featureSettingsStoreTimer.stop();
        flushFeatureSettings();
    }
    QSettings settings;
    copySettingsGroup(settings, QStringLiteral("features"),
                      QStringLiteral("ConfigProfiles/%1/features").arg(name));
    copySettingsGroup(settings, QStringLiteral("MediaOverlay"),
                      QStringLiteral("ConfigProfiles/%1/media").arg(name));
    if (!m_configNames.contains(name)) {
        m_configNames.append(name);
        m_configNames.sort(Qt::CaseInsensitive);
    }
    m_activeConfig = name;
    settings.beginGroup(QStringLiteral("Config"));
    settings.setValue(QStringLiteral("names"), m_configNames);
    settings.setValue(QStringLiteral("active"), m_activeConfig);
    settings.setValue(QStringLiteral("autoSave"), m_configAutoSave);
    settings.endGroup();
    settings.sync();
    setStatusMessage(QStringLiteral("Saved config “%1”").arg(name));
    emit configStateChanged();
    return true;
}

bool OverlayManager::applyConfig(const QString &requestedName)
{
    const QString name = requestedName.simplified();
    if (!validConfigName(name) || !m_configNames.contains(name)) return false;
    if (m_featureSettingsStoreTimer.isActive()) {
        m_featureSettingsStoreTimer.stop();
        flushFeatureSettings();
    }
    QSettings settings;
    const QString profileRoot = QStringLiteral("ConfigProfiles/%1/").arg(name);
    if (!settingsGroupHasValues(settings, profileRoot + QStringLiteral("features")))
        return false;
    copySettingsGroup(settings, profileRoot + QStringLiteral("features"),
                      QStringLiteral("features"));
    if (settingsGroupHasValues(settings, profileRoot + QStringLiteral("media"))) {
        copySettingsGroup(settings, profileRoot + QStringLiteral("media"),
                          QStringLiteral("MediaOverlay"));
    }
    settings.sync();
    loadFeatureSettings();
    m_activeConfig = name;
    settings.beginGroup(QStringLiteral("Config"));
    settings.setValue(QStringLiteral("active"), name);
    settings.endGroup();
    settings.sync();
    emit featureSettingsChanged();
    emit menuHotkeyChanged();
    emit guiScaleIndexChanged();
    emit configStateChanged();
    sendFeatureSnapshot();
    sendBindSnapshot();
    sendGuiScaleSnapshot();
    sendMediaSettings();
    setStatusMessage(QStringLiteral("Applied config “%1”").arg(name));
    return true;
}

bool OverlayManager::removeConfig(const QString &requestedName)
{
    const QString name = requestedName.simplified();
    if (!m_configNames.removeOne(name)) return false;
    QSettings settings;
    settings.beginGroup(QStringLiteral("ConfigProfiles"));
    settings.remove(name);
    settings.endGroup();
    if (m_activeConfig == name) m_activeConfig.clear();
    settings.beginGroup(QStringLiteral("Config"));
    settings.setValue(QStringLiteral("names"), m_configNames);
    settings.setValue(QStringLiteral("active"), m_activeConfig);
    settings.endGroup();
    settings.sync();
    emit configStateChanged();
    return true;
}

void OverlayManager::loadFeatureSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("features"));
    m_menuHotkey = std::clamp(settings.value(
        QStringLiteral("menuHotkey"), 0xDE).toInt(), 0, 254);
    m_guiScaleIndex = std::clamp(settings.value(
        QStringLiteral("guiScaleIndex"), 1).toInt(), 0, 3);
    m_espEnabled = settings.value(QStringLiteral("espEnabled"), true).toBool();
    m_entityEspEnabled = settings.value(QStringLiteral("entityEspEnabled"), true).toBool();
    m_entityEspPlayersOnly = settings.value(QStringLiteral("entityEspPlayersOnly"), false).toBool();
    m_bedEspEnabled = settings.value(QStringLiteral("bedEspEnabled"), true).toBool();
    m_bedAutoRefreshEnabled = settings.value(QStringLiteral("bedAutoRefreshEnabled"), false).toBool();
    m_espLabelsEnabled = settings.value(QStringLiteral("labelsEnabled"), true).toBool();
    m_hypixelPanelEnabled = settings.value(QStringLiteral("hypixelPanelEnabled"), true).toBool();
    m_hypixelPanelHoldToShow = settings.value(
        QStringLiteral("hypixelPanelHoldToShow"), true).toBool();
    m_hypixelPanelHotkey = std::clamp(settings.value(
        QStringLiteral("hypixelPanelHotkey"), 0x09).toInt(), 0, 254);
    m_hypixelPanelOpacity = std::clamp(settings.value(
        QStringLiteral("hypixelPanelOpacity"), 76).toInt(), 0, 100);
    m_hypixelRailOpacity = std::clamp(settings.value(
        QStringLiteral("hypixelRailOpacity"), 100).toInt(), 0, 100);
    m_hypixelPanelScale = std::clamp(settings.value(
        QStringLiteral("hypixelPanelScale"), 100).toInt(), 70, 160);
    m_hypixelPanelHeight = std::clamp(settings.value(
        QStringLiteral("hypixelPanelHeight"), 100).toInt(), 60, 400);
    m_hypixelPanelX = std::clamp(settings.value(
        QStringLiteral("hypixelPanelX"), -1).toInt(), -1, 1000);
    m_hypixelPanelY = std::clamp(settings.value(
        QStringLiteral("hypixelPanelY"), -1).toInt(), -1, 1000);
    m_hypixelPanelFontIndex = std::clamp(settings.value(
        QStringLiteral("hypixelPanelFontIndex"), 1).toInt(), 0, 3);
    m_clickGuiLightTheme = settings.value(
        QStringLiteral("clickGuiLightTheme"), false).toBool();
    m_nametagEnabled = settings.value(QStringLiteral("nametagEnabled"), true).toBool();
    m_nametagSidePlacement = settings.value(
        QStringLiteral("nametagSidePlacement"), false).toBool();
    m_enemyItemIndicatorsEnabled = settings.value(
        QStringLiteral("enemyItemIndicatorsEnabled"), true).toBool();
    m_showTeammateNametags = settings.value(
        QStringLiteral("showTeammateNametags"), true).toBool();
    m_nametagNearbyEnemiesOnly = settings.value(
        QStringLiteral("nametagNearbyEnemiesOnly"), false).toBool();
    m_nametagTeamPulse = settings.value(
        QStringLiteral("nametagTeamPulse"), true).toBool();
    m_nametagRange = std::clamp(settings.value(
        QStringLiteral("nametagRange"), 32).toInt(), 4, 128);
    m_nametagSizeIndex = std::clamp(settings.value(
        QStringLiteral("nametagSizeIndex"), 1).toInt(), 0, 3);
    m_nametagPanelOpacity = std::clamp(settings.value(
        QStringLiteral("nametagPanelOpacity"), 82).toInt(), 10, 100);
    m_bedThreatAlertsEnabled = settings.value(QStringLiteral("bedThreatAlertsEnabled"), true).toBool();
    m_bedDefensePanelEnabled = settings.value(QStringLiteral("bedDefensePanelEnabled"), true).toBool();
    m_bedEspFilled = settings.value(QStringLiteral("bedEspFilled"), false).toBool();
    m_debugChatEnabled = settings.value(QStringLiteral("debugChatEnabled"), true).toBool();
    m_showOwnBedDefenseInfo = settings.value(QStringLiteral("showOwnBedDefenseInfo"), true).toBool();
    m_showTeammateBoxes = settings.value(QStringLiteral("showTeammateBoxes"), true).toBool();
    m_showTeammateArrows = settings.value(
        QStringLiteral("showTeammateArrows"), true).toBool();
    m_safewalkEnabled = settings.value(
        QStringLiteral("safewalkEnabled"), false).toBool();
    m_safewalkReleaseDelayMs = std::clamp(settings.value(
        QStringLiteral("safewalkReleaseDelayMs"), 120).toInt(), 0, 750);
    m_safewalkEdgeSensitivity = std::clamp(settings.value(
        QStringLiteral("safewalkEdgeSensitivity"), 55).toInt(), 0, 100);
    m_safewalkMinimumPitch = std::clamp(settings.value(
        QStringLiteral("safewalkMinimumPitch"), -5).toInt(), -90, 90);
    m_safewalkHotkey = std::clamp(settings.value(
        QStringLiteral("safewalkHotkey"), 0x77).toInt(), 0, 254);
    m_scaffoldEnabled = settings.value(
        QStringLiteral("scaffoldEnabled"), false).toBool();
    m_flyEnabled = settings.value(QStringLiteral("flyEnabled"), false).toBool();
    m_flySpeedPercent = std::clamp(settings.value(
        QStringLiteral("flySpeedPercent"), 100).toInt(), 10, 500);
    m_bhopEnabled = settings.value(QStringLiteral("bhopEnabled"), false).toBool();
    m_bhopAutoJump = settings.value(QStringLiteral("bhopAutoJump"), true).toBool();
    m_bhopAirSpeedPercent = std::clamp(settings.value(
        QStringLiteral("bhopAirSpeedPercent"), 100).toInt(), 10, 300);
    m_featureHotkeysPackedA = settings.value(
        QStringLiteral("featureHotkeysPackedA"), qulonglong(0)).toULongLong();
    m_featureHotkeysPackedB = settings.value(
        QStringLiteral("featureHotkeysPackedB"), qulonglong(0)).toULongLong();
    m_featureHotkeysPackedC = settings.value(
        QStringLiteral("featureHotkeysPackedC"), quint32(0xA400U)).toUInt();
    if(m_featureHotkeysPackedC>0x7FFFFU) m_featureHotkeysPackedC=0xA400U;
    m_fireballEspEnabled = settings.value(
        QStringLiteral("fireballEspEnabled"), false).toBool();
    m_fireballEspFilled = settings.value(
        QStringLiteral("fireballEspFilled"), true).toBool();
    m_longJumpEnabled = false;
    m_longJumpSpeedPercent = std::clamp(settings.value(
        QStringLiteral("longJumpSpeedPercent"), 100).toInt(), 25, 250);
    // Migrate the old standalone Safewalk binding into the page-hotkey pack.
    if (((m_featureHotkeysPackedA >> 32U) & 0xFFU) == 0U)
        m_featureHotkeysPackedA |= (static_cast<quint64>(m_safewalkHotkey) << 32U);
    m_aimAssistEnabled = settings.value(
        QStringLiteral("aimAssistEnabled"), false).toBool();
    m_aimLockOnMode = settings.value(
        QStringLiteral("aimLockOnMode"), false).toBool();
    m_aimSilentLock=settings.value(QStringLiteral("aimSilentLock"),false).toBool();
    m_silentFileDebug=settings.value(QStringLiteral("silentFileDebug"),false).toBool();
    m_silentChatDebug=settings.value(QStringLiteral("silentChatDebug"),false).toBool();
    m_aimScannerEnabled=settings.value(QStringLiteral("aimScannerEnabled"),true).toBool();
    m_aimAttackViability=settings.value(
        QStringLiteral("aimAttackViability"),true).toBool();
    m_silentControlAdaptation=settings.value(
        QStringLiteral("silentControlAdaptation"),false).toBool();
    m_aimSequentialTargets=settings.value(
        QStringLiteral("aimSequentialTargets"),false).toBool();
    m_bedBreakerEnabled=settings.value(QStringLiteral("bedBreakerEnabled"),false).toBool();
    m_textGuiShowModes=settings.value(QStringLiteral("textGuiShowModes"),false).toBool();
    m_localVelocityProbability=std::clamp(settings.value(
        QStringLiteral("localVelocityProbability"),100).toInt(),0,100);
    m_localVelocityVerticalPercent=std::clamp(settings.value(
        QStringLiteral("localVelocityVerticalPercent"),100).toInt(),0,100);
    m_velocityHotkey=std::clamp(settings.value(
        QStringLiteral("velocityHotkey"),
        static_cast<int>(m_featureHotkeysPackedC&0xFFU)).toInt(),0,254);
    m_featureHotkeysPackedC=(m_featureHotkeysPackedC&0x1FF00U) |
        static_cast<quint32>(m_velocityHotkey);
    m_aimSlowdownPercent = std::clamp(settings.value(
        QStringLiteral("aimSlowdownPercent"), 45).toInt(), 5, 95);
    m_aimSpeedPercent = std::clamp(settings.value(
        QStringLiteral("aimSpeedPercent"), 35).toInt(), 1, 100);
    m_aimMinimumDistance = std::clamp(settings.value(
        QStringLiteral("aimMinimumDistance"), 0).toInt(), 0, 64);
    m_aimMaximumDistance = std::clamp(settings.value(
        QStringLiteral("aimMaximumDistance"), 16).toInt(),
        std::max(1, m_aimMinimumDistance), 128);
    m_aimFovDegrees = std::clamp(settings.value(
        QStringLiteral("aimFovDegrees"), 90).toInt(), 1, 360);
    m_aimAttackCps=std::clamp(settings.value(
        QStringLiteral("aimAttackCps"),10).toInt(),1,20);
    m_clickGuiWidthPercent = std::clamp(settings.value(
        QStringLiteral("clickGuiWidthPercent"), 100).toInt(), 80, 150);
    m_clickGuiHeightPercent = std::clamp(settings.value(
        QStringLiteral("clickGuiHeightPercent"), 100).toInt(), 80, 150);
    m_clickGuiOpacity = std::clamp(settings.value(
        QStringLiteral("clickGuiOpacity"), 96).toInt(), 35, 100);
    m_featureExtraBits = settings.value(
        QStringLiteral("featureExtraBits"), 0x43U).toUInt() & ~0x10U;
    m_smartHotbarConfig=settings.value(
        QStringLiteral("smartHotbarConfig"),quint32(0U)).toUInt();
    if(!validSmartHotbarConfig(m_smartHotbarConfig)) m_smartHotbarConfig=0U;
    m_textGuiAlignment = std::clamp(settings.value(
        QStringLiteral("textGuiAlignment"), 2).toInt(), 0, 2);
    m_localMobReach = std::clamp(settings.value(
        QStringLiteral("localMobReach"), 4).toInt(), 3, 10);
    m_localAttackDelayMs = std::clamp(settings.value(
        QStringLiteral("localAttackDelayMs"), 500).toInt(), 100, 1500);
    m_localVelocityPercent = std::clamp(settings.value(
        QStringLiteral("localVelocityPercent"), 100).toInt(), 0, 100);
    m_textGuiEnabled = settings.value(
        QStringLiteral("textGuiEnabled"), false).toBool();
    m_textGuiX = std::clamp(settings.value(
        QStringLiteral("textGuiX"), -1).toInt(), -1, 1000);
    m_textGuiY = std::clamp(settings.value(
        QStringLiteral("textGuiY"), -1).toInt(), -1, 1000);
    m_allowHypixelMovement = settings.value(
        QStringLiteral("allowHypixelMovement"), false).toBool();
    m_bedDefenseHoldToShow = settings.value(QStringLiteral("bedDefenseHoldToShow"), true).toBool();
    m_bedDefensePerspectiveScale = settings.value(QStringLiteral("bedDefensePerspectiveScale"), false).toBool();
    m_bedDefenseRadius = std::clamp(
        settings.value(QStringLiteral("bedDefenseRadius"), 6).toInt(), 3, 10);
    m_bedThreatRadius = std::clamp(
        settings.value(QStringLiteral("bedThreatRadius"), 8).toInt(), 3, 32);
    m_bedDefenseHotkey = std::clamp(
        settings.value(QStringLiteral("bedDefenseHotkey"), 0xA4).toInt(), 0, 254);
    m_bedDefensePanelOpacity = std::clamp(
        settings.value(QStringLiteral("bedDefensePanelOpacity"), 78).toInt(), 0, 100);
    const QString savedPlayerColor = normalizedRgbColor(
        settings.value(QStringLiteral("playerEspColor"), QStringLiteral("#FF3B30")).toString());
    const QString savedBedColor = normalizedRgbColor(
        settings.value(QStringLiteral("bedEspColor"), QStringLiteral("#FF5C68")).toString());
    const QString savedPanelColor = normalizedRgbColor(
        settings.value(QStringLiteral("bedDefensePanelColor"), QStringLiteral("#191621")).toString());
    const QString savedHypixelPanelColor = normalizedRgbColor(
        settings.value(QStringLiteral("hypixelPanelColor"),
                       QStringLiteral("#000000")).toString());
    const QString savedHypixelRailColor = normalizedRgbColor(
        settings.value(QStringLiteral("hypixelRailColor"),
                       QStringLiteral("#825DE8")).toString());
    const QString savedNametagColor = normalizedRgbColor(settings.value(
        QStringLiteral("nametagPanelColor"), QStringLiteral("#101218")).toString());
    const QString savedAccentColor = normalizedRgbColor(settings.value(
        QStringLiteral("clickGuiAccentColor"), QStringLiteral("#825DE8")).toString());
    const QString savedTextGuiColor = normalizedRgbColor(settings.value(
        QStringLiteral("textGuiColor"), QStringLiteral("#7EE7FF")).toString());
    const QString savedFireballColor = normalizedRgbColor(settings.value(
        QStringLiteral("fireballEspColor"), QStringLiteral("#FF9D3D")).toString());
    m_playerEspColor = savedPlayerColor.isEmpty() ? QStringLiteral("#FF3B30") : savedPlayerColor;
    m_bedEspColor = savedBedColor.isEmpty() ? QStringLiteral("#FF5C68") : savedBedColor;
    m_bedDefensePanelColor = savedPanelColor.isEmpty()
        ? QStringLiteral("#191621") : savedPanelColor;
    // Version migration: the redesigned card intentionally exposes only
    // black/white surfaces. Existing custom colors map to the closest legible
    // tone instead of silently producing low-contrast column headers.
    const QColor migratedHypixelColor(savedHypixelPanelColor);
    m_hypixelPanelColor = migratedHypixelColor.isValid() &&
            migratedHypixelColor.lightness() >= 128
        ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
    m_hypixelRailColor = savedHypixelRailColor.isEmpty()
        ? QStringLiteral("#825DE8") : savedHypixelRailColor;
    m_nametagPanelColor = savedNametagColor.isEmpty()
        ? QStringLiteral("#101218") : savedNametagColor;
    m_clickGuiAccentColor = savedAccentColor.isEmpty()
        ? QStringLiteral("#825DE8") : savedAccentColor;
    m_textGuiColor = savedTextGuiColor.isEmpty()
        ? QStringLiteral("#7EE7FF") : savedTextGuiColor;
    m_fireballEspColor = savedFireballColor.isEmpty()
        ? QStringLiteral("#FF9D3D") : savedFireballColor;
    settings.endGroup();
}

void OverlayManager::storeFeatureSettings()
{
    m_featureSettingsStoreTimer.start();
}

void OverlayManager::flushFeatureSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("features"));
    settings.setValue(QStringLiteral("menuHotkey"), m_menuHotkey);
    settings.setValue(QStringLiteral("guiScaleIndex"), m_guiScaleIndex);
    settings.setValue(QStringLiteral("espEnabled"), m_espEnabled);
    settings.setValue(QStringLiteral("entityEspEnabled"), m_entityEspEnabled);
    settings.setValue(QStringLiteral("entityEspPlayersOnly"), m_entityEspPlayersOnly);
    settings.setValue(QStringLiteral("bedEspEnabled"), m_bedEspEnabled);
    settings.setValue(QStringLiteral("bedAutoRefreshEnabled"), m_bedAutoRefreshEnabled);
    settings.setValue(QStringLiteral("labelsEnabled"), m_espLabelsEnabled);
    settings.setValue(QStringLiteral("hypixelPanelEnabled"), m_hypixelPanelEnabled);
    settings.setValue(QStringLiteral("hypixelPanelHoldToShow"), m_hypixelPanelHoldToShow);
    settings.setValue(QStringLiteral("hypixelPanelHotkey"), m_hypixelPanelHotkey);
    settings.setValue(QStringLiteral("hypixelPanelOpacity"), m_hypixelPanelOpacity);
    settings.setValue(QStringLiteral("hypixelPanelColor"), m_hypixelPanelColor);
    settings.setValue(QStringLiteral("hypixelRailColor"), m_hypixelRailColor);
    settings.setValue(QStringLiteral("hypixelRailOpacity"), m_hypixelRailOpacity);
    settings.setValue(QStringLiteral("hypixelPanelScale"), m_hypixelPanelScale);
    settings.setValue(QStringLiteral("hypixelPanelHeight"), m_hypixelPanelHeight);
    settings.setValue(QStringLiteral("hypixelPanelX"), m_hypixelPanelX);
    settings.setValue(QStringLiteral("hypixelPanelY"), m_hypixelPanelY);
    settings.setValue(QStringLiteral("hypixelPanelFontIndex"), m_hypixelPanelFontIndex);
    settings.setValue(QStringLiteral("clickGuiLightTheme"), m_clickGuiLightTheme);
    settings.setValue(QStringLiteral("nametagEnabled"), m_nametagEnabled);
    settings.setValue(QStringLiteral("nametagSidePlacement"), m_nametagSidePlacement);
    settings.setValue(QStringLiteral("enemyItemIndicatorsEnabled"), m_enemyItemIndicatorsEnabled);
    settings.setValue(QStringLiteral("showTeammateNametags"), m_showTeammateNametags);
    settings.setValue(QStringLiteral("nametagNearbyEnemiesOnly"), m_nametagNearbyEnemiesOnly);
    settings.setValue(QStringLiteral("nametagTeamPulse"), m_nametagTeamPulse);
    settings.setValue(QStringLiteral("nametagRange"), m_nametagRange);
    settings.setValue(QStringLiteral("nametagSizeIndex"), m_nametagSizeIndex);
    settings.setValue(QStringLiteral("nametagPanelOpacity"), m_nametagPanelOpacity);
    settings.setValue(QStringLiteral("nametagPanelColor"), m_nametagPanelColor);
    settings.setValue(QStringLiteral("clickGuiAccentColor"), m_clickGuiAccentColor);
    settings.setValue(QStringLiteral("bedThreatAlertsEnabled"), m_bedThreatAlertsEnabled);
    settings.setValue(QStringLiteral("bedDefensePanelEnabled"), m_bedDefensePanelEnabled);
    settings.setValue(QStringLiteral("bedEspFilled"), m_bedEspFilled);
    settings.setValue(QStringLiteral("debugChatEnabled"), m_debugChatEnabled);
    settings.setValue(QStringLiteral("showOwnBedDefenseInfo"), m_showOwnBedDefenseInfo);
    settings.setValue(QStringLiteral("showTeammateBoxes"), m_showTeammateBoxes);
    settings.setValue(QStringLiteral("showTeammateArrows"), m_showTeammateArrows);
    settings.setValue(QStringLiteral("safewalkEnabled"), m_safewalkEnabled);
    settings.setValue(QStringLiteral("safewalkReleaseDelayMs"),
                      m_safewalkReleaseDelayMs);
    settings.setValue(QStringLiteral("safewalkEdgeSensitivity"),
                      m_safewalkEdgeSensitivity);
    settings.setValue(QStringLiteral("safewalkMinimumPitch"),
                      m_safewalkMinimumPitch);
    settings.setValue(QStringLiteral("safewalkHotkey"), m_safewalkHotkey);
    settings.setValue(QStringLiteral("scaffoldEnabled"), m_scaffoldEnabled);
    settings.setValue(QStringLiteral("flyEnabled"), m_flyEnabled);
    settings.setValue(QStringLiteral("flySpeedPercent"), m_flySpeedPercent);
    settings.setValue(QStringLiteral("bhopEnabled"), m_bhopEnabled);
    settings.setValue(QStringLiteral("bhopAutoJump"), m_bhopAutoJump);
    settings.setValue(QStringLiteral("bhopAirSpeedPercent"), m_bhopAirSpeedPercent);
    settings.setValue(QStringLiteral("featureHotkeysPackedA"),
                      QVariant::fromValue<qulonglong>(m_featureHotkeysPackedA));
    settings.setValue(QStringLiteral("featureHotkeysPackedB"),
                      QVariant::fromValue<qulonglong>(m_featureHotkeysPackedB));
    settings.setValue(QStringLiteral("featureHotkeysPackedC"),
                      m_featureHotkeysPackedC);
    settings.setValue(QStringLiteral("fireballEspEnabled"), m_fireballEspEnabled);
    settings.setValue(QStringLiteral("fireballEspFilled"), m_fireballEspFilled);
    settings.setValue(QStringLiteral("fireballEspColor"), m_fireballEspColor);
    settings.setValue(QStringLiteral("longJumpEnabled"), m_longJumpEnabled);
    settings.setValue(QStringLiteral("longJumpSpeedPercent"), m_longJumpSpeedPercent);
    settings.setValue(QStringLiteral("aimAssistEnabled"), m_aimAssistEnabled);
    settings.setValue(QStringLiteral("aimLockOnMode"), m_aimLockOnMode);
    settings.setValue(QStringLiteral("aimSilentLock"), m_aimSilentLock);
    settings.setValue(QStringLiteral("silentFileDebug"), m_silentFileDebug);
    settings.setValue(QStringLiteral("silentChatDebug"), m_silentChatDebug);
    settings.setValue(QStringLiteral("aimScannerEnabled"), m_aimScannerEnabled);
    settings.setValue(QStringLiteral("aimAttackViability"),m_aimAttackViability);
    settings.setValue(QStringLiteral("silentControlAdaptation"),m_silentControlAdaptation);
    settings.setValue(QStringLiteral("aimSequentialTargets"),m_aimSequentialTargets);
    settings.setValue(QStringLiteral("bedBreakerEnabled"), m_bedBreakerEnabled);
    settings.setValue(QStringLiteral("textGuiShowModes"),m_textGuiShowModes);
    settings.setValue(QStringLiteral("localVelocityProbability"),
                      m_localVelocityProbability);
    settings.setValue(QStringLiteral("localVelocityVerticalPercent"),
                      m_localVelocityVerticalPercent);
    settings.setValue(QStringLiteral("velocityHotkey"),m_velocityHotkey);
    settings.setValue(QStringLiteral("aimSlowdownPercent"), m_aimSlowdownPercent);
    settings.setValue(QStringLiteral("aimSpeedPercent"), m_aimSpeedPercent);
    settings.setValue(QStringLiteral("aimMinimumDistance"), m_aimMinimumDistance);
    settings.setValue(QStringLiteral("aimMaximumDistance"), m_aimMaximumDistance);
    settings.setValue(QStringLiteral("aimFovDegrees"), m_aimFovDegrees);
    settings.setValue(QStringLiteral("aimAttackCps"),m_aimAttackCps);
    settings.setValue(QStringLiteral("clickGuiWidthPercent"), m_clickGuiWidthPercent);
    settings.setValue(QStringLiteral("clickGuiHeightPercent"), m_clickGuiHeightPercent);
    settings.setValue(QStringLiteral("clickGuiOpacity"), m_clickGuiOpacity);
    settings.setValue(QStringLiteral("featureExtraBits"), m_featureExtraBits);
    settings.setValue(QStringLiteral("smartHotbarConfig"),m_smartHotbarConfig);
    settings.setValue(QStringLiteral("textGuiAlignment"), m_textGuiAlignment);
    settings.setValue(QStringLiteral("localMobReach"), m_localMobReach);
    settings.setValue(QStringLiteral("localAttackDelayMs"), m_localAttackDelayMs);
    settings.setValue(QStringLiteral("localVelocityPercent"), m_localVelocityPercent);
    settings.setValue(QStringLiteral("textGuiEnabled"), m_textGuiEnabled);
    settings.setValue(QStringLiteral("textGuiColor"), m_textGuiColor);
    settings.setValue(QStringLiteral("textGuiX"), m_textGuiX);
    settings.setValue(QStringLiteral("textGuiY"), m_textGuiY);
    settings.setValue(QStringLiteral("allowHypixelMovement"),
                      m_allowHypixelMovement);
    settings.setValue(QStringLiteral("bedDefenseHoldToShow"), m_bedDefenseHoldToShow);
    settings.setValue(QStringLiteral("bedDefensePerspectiveScale"), m_bedDefensePerspectiveScale);
    settings.setValue(QStringLiteral("bedDefenseRadius"), m_bedDefenseRadius);
    settings.setValue(QStringLiteral("bedThreatRadius"), m_bedThreatRadius);
    settings.setValue(QStringLiteral("bedDefenseHotkey"), m_bedDefenseHotkey);
    settings.setValue(QStringLiteral("bedDefensePanelOpacity"), m_bedDefensePanelOpacity);
    settings.setValue(QStringLiteral("playerEspColor"), m_playerEspColor);
    settings.setValue(QStringLiteral("bedEspColor"), m_bedEspColor);
    settings.setValue(QStringLiteral("bedDefensePanelColor"), m_bedDefensePanelColor);
    settings.endGroup();
    settings.sync();
    if (m_configAutoSave && !m_activeConfig.isEmpty()) {
        copySettingsGroup(settings, QStringLiteral("features"),
            QStringLiteral("ConfigProfiles/%1/features").arg(m_activeConfig));
        copySettingsGroup(settings, QStringLiteral("MediaOverlay"),
            QStringLiteral("ConfigProfiles/%1/media").arg(m_activeConfig));
        settings.sync();
    }
}

