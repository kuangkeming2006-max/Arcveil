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

void OverlayManager::refreshBedCache()
{
    if (!m_authenticated) return;
    writeAgentCommand(QByteArrayLiteral("BED_RESCAN\n"));
    setStatusMessage(QStringLiteral("Immediate bed-cache refresh requested"));
}

void OverlayManager::processAgentLine(const QByteArray &line)
{
    const QList<QByteArray> fields = line.simplified().split(' ');
    if (fields.isEmpty())
        return;

    const QByteArray type = fields.first();
    if (type == QByteArrayLiteral("HELLO")) {
        bool pidValid = false;
        const quint32 reportedPid = fields.value(2).toUInt(&pidValid);
        const QByteArray expectedToken = m_pipeToken.toUtf8();
        if (fields.size() < 4 || fields.value(1) != QByteArrayLiteral("1")
            || !pidValid || reportedPid != m_targetPid
            || fields.value(3) != expectedToken) {
            fail(QStringLiteral("AGENT_AUTHENTICATION_FAILED"),
                 QStringLiteral("The native agent supplied an invalid protocol, PID, or token."));
            return;
        }

        m_authenticated = true;
        m_attachTimeout.stop();
        m_nativeFallbackGrace.stop();
        setState(State::WaitingForOpenGL);
        setStatusMessage(QStringLiteral("Native DLL loaded; waiting for Minecraft's first OpenGL frame..."));
        sendStateSnapshot();
        emit agentSessionReady();
        return;
    }

    if (!m_authenticated)
        return;

    if (type == QByteArrayLiteral("HOOK_READY")) {
        setRenderer(QString::fromUtf8(fields.value(1)));
        setStatusMessage(QStringLiteral("OpenGL presentation hook installed; waiting for a render context..."));
    } else if (type == QByteArrayLiteral("RENDERER_READY")) {
        setRenderer(QString::fromUtf8(fields.value(1)));
        setState(State::Active);
        setStatusMessage(QStringLiteral("Native %1 overlay is active inside Minecraft")
                             .arg(m_renderer.isEmpty() ? QStringLiteral("OpenGL") : m_renderer));
    } else if (type == QByteArrayLiteral("STATE_CHANGED")) {
        if (fields.size() != 3
            || (fields.at(1) != QByteArrayLiteral("0")
                && fields.at(1) != QByteArrayLiteral("1"))
            || (fields.at(2) != QByteArrayLiteral("0")
                && fields.at(2) != QByteArrayLiteral("1"))) {
            fail(QStringLiteral("AGENT_PROTOCOL_ERROR"),
                 QStringLiteral("The native agent sent an invalid STATE_CHANGED message."));
            return;
        }
        const bool enabled = fields.at(1) == QByteArrayLiteral("1");
        const bool interactive = fields.at(2) == QByteArrayLiteral("1");
        if (m_overlayEnabled != enabled) {
            m_overlayEnabled = enabled;
            emit overlayEnabledChanged();
        }
        if (m_interactive != interactive) {
            m_interactive = interactive;
            emit interactiveChanged();
        }
    } else if (type == QByteArrayLiteral("MEDIA_ACTION")) {
        if (fields.size() != 2) return;
        qInfo().noquote() << "Now Playing action received from Agent:" << fields[1];
        if (fields[1] == QByteArrayLiteral("PREVIOUS")) emit mediaPreviousRequested();
        else if (fields[1] == QByteArrayLiteral("NEXT")) emit mediaNextRequested();
        else if (fields[1] == QByteArrayLiteral("TOGGLE")) emit mediaToggleRequested();
    } else if (type == QByteArrayLiteral("MEDIA_SETTINGS_CHANGED")) {
        if (fields.size() != 9 && fields.size() != 10 && fields.size() != 11) return;
        bool enabledOk=false, opacityOk=false, previousOk=false, toggleOk=false;
        bool nextOk=false, colorOk=false, xOk=false, yOk=false;
        const int enabled=fields[1].toInt(&enabledOk);
        const int opacity=fields[2].toInt(&opacityOk);
        const int previous=fields[3].toInt(&previousOk);
        const int toggle=fields[4].toInt(&toggleOk);
        const int next=fields[5].toInt(&nextOk);
        const uint color=fields[6].toUInt(&colorOk);
        const int x=fields[7].toInt(&xOk);
        const int y=fields[8].toInt(&yOk);
        bool spectrumOk=true;
        const int spectrum=fields.size()>=10 ? fields[9].toInt(&spectrumOk) : 100;
        bool sizeOk=true;
        const int wireScale=fields.size()>=11 ? fields[10].toInt(&sizeOk) : 52;
        int scalePercent=wireScale;
        if(wireScale>=0 && wireScale<=3) {
            static constexpr std::array<int,4> legacyScales{42,52,68,84};
            scalePercent=legacyScales[static_cast<std::size_t>(wireScale)];
        }
        if(!spectrumOk || spectrum<0 || spectrum>100 || !sizeOk ||
           scalePercent<35 || scalePercent>100) return;
        if(!enabledOk || !opacityOk || !previousOk || !toggleOk || !nextOk ||
           !colorOk || !xOk || !yOk || enabled<0 || enabled>1 || opacity<20 ||
           opacity>100 || previous<0 || previous>254 || toggle<0 || toggle>254 ||
           next<0 || next>254 || color>0xFFFFFFU || x<-1 || x>1000 || y<-1 || y>1000) return;
        QSettings settings;
        settings.beginGroup(QStringLiteral("MediaOverlay"));
        settings.setValue(QStringLiteral("enabled"),enabled!=0);
        settings.setValue(QStringLiteral("opacity"),opacity);
        settings.setValue(QStringLiteral("previousHotkey"),previous);
        settings.setValue(QStringLiteral("toggleHotkey"),toggle);
        settings.setValue(QStringLiteral("nextHotkey"),next);
        settings.setValue(QStringLiteral("color"),color);
        settings.setValue(QStringLiteral("x"),x);
        settings.setValue(QStringLiteral("y"),y);
        settings.setValue(QStringLiteral("spectrumOpacity"),spectrum);
        settings.setValue(QStringLiteral("scalePercent"),scalePercent);
        settings.endGroup(); settings.sync();
        storeFeatureSettings();
        // MediaOverlay is persisted in its own settings group, but it is also
        // represented in the live TEXTGUI module list. Notify QML immediately
        // so its preview never waits for a reconnect or another feature edit.
        emit featureSettingsChanged();
    } else if (type == QByteArrayLiteral("AIM_OPTIONS_CHANGED")) {
        if(fields.size()!=2) return;
        bool ok=false; const unsigned value=fields[1].toUInt(&ok);
        if(!ok || value>0x7FFFFFFFU) return;
        m_silentFileDebug=(value&0x08000000U)!=0;
        m_silentChatDebug=(value&0x10000000U)!=0;
        m_aimSilentLock=(value&1U)!=0;
        m_aimScannerEnabled=(value&2U)!=0;
        m_bedBreakerEnabled=(value&4U)!=0;
        m_aimAttackViability=(value&8U)!=0;
        m_aimSequentialTargets=(value&0x20000000U)!=0;
        m_silentControlAdaptation=(value&0x40000000U)!=0;
        m_textGuiShowModes=(value&16U)!=0;
        m_localVelocityProbability=std::clamp(
            static_cast<int>((value>>5U)&0x7FU),0,100);
        m_localVelocityVerticalPercent=std::clamp(
            static_cast<int>((value>>12U)&0x7FU),0,100);
        m_velocityHotkey=std::clamp(
            static_cast<int>((value>>19U)&0xFFU),0,254);
        m_featureHotkeysPackedC=(m_featureHotkeysPackedC&0x1FF00U) |
            static_cast<quint32>(m_velocityHotkey);
        storeFeatureSettings(); emit featureSettingsChanged();
    } else if(type==QByteArrayLiteral("AIM_ATTACK_CPS_CHANGED")) {
        if(fields.size()!=2) return;
        bool ok=false; const int value=fields[1].toInt(&ok);
        if(!ok||value<1||value>20) return;
        m_aimAttackCps=value;
        storeFeatureSettings(); emit featureSettingsChanged();
    } else if(type==QByteArrayLiteral("SMART_HOTBAR_CHANGED")) {
        if(fields.size()!=2) return;
        bool ok=false;
        const quint32 value=fields[1].toUInt(&ok);
        if(!ok||!validSmartHotbarConfig(value)) return;
        if(m_smartHotbarConfig!=value) {
            m_smartHotbarConfig=value;
            storeFeatureSettings();
            emit featureSettingsChanged();
        }
    } else if (type == QByteArrayLiteral("FEATURE_STATE_CHANGED") ||
               type == QByteArrayLiteral("FEATURE_STATE_CHANGED_V2") ||
               type == QByteArrayLiteral("FEATURE_STATE_CHANGED_V3")) {
        const bool featureStateV3 =
            type == QByteArrayLiteral("FEATURE_STATE_CHANGED_V3");
        if (fields.size() != (featureStateV3 ? 86 : 85)) return;
        std::array<bool, 32U> values{};
        for (int index = 0; index < 32; ++index) {
            const QByteArray token = fields.at(index + 1);
            if (token != QByteArrayLiteral("0") && token != QByteArrayLiteral("1")) return;
            values[static_cast<std::size_t>(index)] = token == QByteArrayLiteral("1");
        }
        // Normalize the legacy slowdown slot before both change detection and
        // assignment; otherwise a mode reset could skip its UI/settings signal.
        if (type == QByteArrayLiteral("FEATURE_STATE_CHANGED")) values[29] = false;
        bool defenseRadiusOk = false;
        bool threatRadiusOk = false;
        bool bedHotkeyOk = false;
        bool panelOpacityOk = false;
        bool hypixelHotkeyOk = false;
        bool hypixelOpacityOk = false;
        bool hypixelScaleOk = false;
        bool hypixelXOk = false;
        bool hypixelYOk = false;
        bool clickGuiThemeOk = false;
        bool playerColorOk = false;
        bool bedColorOk = false;
        bool panelColorOk = false;
        bool hypixelColorOk = false;
        bool hypixelHeightOk = false;
        bool nametagOpacityOk = false;
        bool nametagColorOk = false;
        bool accentColorOk = false;
        bool hypixelFontIndexOk = false;
        bool nametagRangeOk = false;
        bool nametagSizeIndexOk = false;
        bool hypixelRailColorOk = false;
        bool hypixelRailOpacityOk = false;
        bool safewalkReleaseDelayOk = false;
        bool safewalkSensitivityOk = false, safewalkPitchOk = false;
        bool safewalkHotkeyOk = false, flySpeedOk = false;
        bool aimSlowdownOk = false, aimSpeedOk = false;
        bool textColorOk = false, textXOk = false, textYOk = false;
        bool bhopAirSpeedOk = false, hotkeysAOk = false, hotkeysBOk = false;
        bool hotkeysCOk = !featureStateV3;
        bool fireballEnabledOk = false, fireballFilledOk = false;
        bool longJumpEnabledOk = false, longJumpSpeedOk = false;
        bool fireballColorOk = false;
        bool aimMinimumDistanceOk = false, aimMaximumDistanceOk = false;
        bool aimFovOk = false, clickGuiWidthOk = false;
        bool clickGuiHeightOk = false, clickGuiOpacityOk = false;
        bool extraBitsOk = false, textGuiAlignmentOk = false;
        bool localMobReachOk = false, localAttackDelayOk = false;
        bool localVelocityPercentOk = false;
        const int defenseRadius = fields.at(33).toInt(&defenseRadiusOk);
        const int threatRadius = fields.at(34).toInt(&threatRadiusOk);
        const int bedHotkey = fields.at(35).toInt(&bedHotkeyOk);
        const int panelOpacity = fields.at(36).toInt(&panelOpacityOk);
        const int hypixelHotkey = fields.at(37).toInt(&hypixelHotkeyOk);
        const int hypixelOpacity = fields.at(38).toInt(&hypixelOpacityOk);
        const int hypixelScale = fields.at(39).toInt(&hypixelScaleOk);
        const int hypixelX = fields.at(40).toInt(&hypixelXOk);
        const int hypixelY = fields.at(41).toInt(&hypixelYOk);
        const int clickGuiTheme = fields.at(42).toInt(&clickGuiThemeOk);
        const quint32 playerColor = fields.at(43).toUInt(&playerColorOk);
        const quint32 bedColor = fields.at(44).toUInt(&bedColorOk);
        const quint32 panelColor = fields.at(45).toUInt(&panelColorOk);
        const quint32 hypixelColor = fields.at(46).toUInt(&hypixelColorOk);
        const int hypixelHeight = fields.at(47).toInt(&hypixelHeightOk);
        const int nametagOpacity = fields.at(48).toInt(&nametagOpacityOk);
        const quint32 nametagColor = fields.at(49).toUInt(&nametagColorOk);
        const quint32 accentColor = fields.at(50).toUInt(&accentColorOk);
        const int hypixelFontIndex = fields.at(51).toInt(&hypixelFontIndexOk);
        const int nametagRange = fields.at(52).toInt(&nametagRangeOk);
        const int nametagSizeIndex = fields.at(53).toInt(&nametagSizeIndexOk);
        const quint32 hypixelRailColor = fields.at(54).toUInt(&hypixelRailColorOk);
        const int hypixelRailOpacity = fields.at(55).toInt(&hypixelRailOpacityOk);
        const int safewalkReleaseDelayMs = fields.at(56).toInt(
            &safewalkReleaseDelayOk);
        const int safewalkSensitivity = fields.at(57).toInt(&safewalkSensitivityOk);
        const int safewalkPitch = fields.at(58).toInt(&safewalkPitchOk);
        const int safewalkHotkey = fields.at(59).toInt(&safewalkHotkeyOk);
        const int flySpeed = fields.at(60).toInt(&flySpeedOk);
        const int aimSlowdown = fields.at(61).toInt(&aimSlowdownOk);
        const int aimSpeed = fields.at(62).toInt(&aimSpeedOk);
        const quint32 textColor = fields.at(63).toUInt(&textColorOk);
        const int textX = fields.at(64).toInt(&textXOk);
        const int textY = fields.at(65).toInt(&textYOk);
        const int bhopAirSpeed = fields.at(66).toInt(&bhopAirSpeedOk);
        const quint64 hotkeysPackedA = fields.at(67).toULongLong(&hotkeysAOk);
        const quint64 hotkeysPackedB = fields.at(68).toULongLong(&hotkeysBOk);
        const quint32 hotkeysPackedC = featureStateV3
            ? fields.at(69).toUInt(&hotkeysCOk) : m_featureHotkeysPackedC;
        const int tail = featureStateV3 ? 70 : 69;
        const int fireballEnabled = fields.at(tail).toInt(&fireballEnabledOk);
        const int fireballFilled = fields.at(tail + 1).toInt(&fireballFilledOk);
        const int longJumpEnabled = fields.at(tail + 2).toInt(&longJumpEnabledOk);
        const int longJumpSpeed = fields.at(tail + 3).toInt(&longJumpSpeedOk);
        const quint32 fireballColor = fields.at(tail + 4).toUInt(&fireballColorOk);
        const int aimMinimumDistance = fields.at(tail + 5).toInt(&aimMinimumDistanceOk);
        const int aimMaximumDistance = fields.at(tail + 6).toInt(&aimMaximumDistanceOk);
        const int aimFovDegrees = fields.at(tail + 7).toInt(&aimFovOk);
        const int clickGuiWidthPercent = fields.at(tail + 8).toInt(&clickGuiWidthOk);
        const int clickGuiHeightPercent = fields.at(tail + 9).toInt(&clickGuiHeightOk);
        const int clickGuiOpacity = fields.at(tail + 10).toInt(&clickGuiOpacityOk);
        const quint32 extraBits = fields.at(tail + 11).toUInt(&extraBitsOk);
        const int textGuiAlignment = fields.at(tail + 12).toInt(&textGuiAlignmentOk);
        const int localMobReach = fields.at(tail + 13).toInt(&localMobReachOk);
        const int localAttackDelayMs = fields.at(tail + 14).toInt(&localAttackDelayOk);
        const int localVelocityPercent = fields.at(tail + 15).toInt(
            &localVelocityPercentOk);
        const auto validHotkeyPack = [](const quint64 packed,
                                        const int count) noexcept {
            for (int index = 0; index < count; ++index) {
                const int key = static_cast<int>((packed >> (index * 8)) & 0xFFU);
                if ((key > 0 && key < 8) || key > 254) return false;
            }
            return true;
        };
        if (!defenseRadiusOk || defenseRadius < 3 || defenseRadius > 10 ||
            !threatRadiusOk || threatRadius < 3 || threatRadius > 32 ||
            !bedHotkeyOk || (bedHotkey != 0 && bedHotkey < 8) || bedHotkey > 254 ||
            !panelOpacityOk || panelOpacity < 0 || panelOpacity > 100 ||
            !hypixelHotkeyOk || (hypixelHotkey != 0 && hypixelHotkey < 8) || hypixelHotkey > 254 ||
            !hypixelOpacityOk || hypixelOpacity < 0 || hypixelOpacity > 100 ||
            !hypixelScaleOk || hypixelScale < 70 || hypixelScale > 160 ||
            !hypixelHeightOk || hypixelHeight < 60 || hypixelHeight > 400 ||
            !hypixelXOk || hypixelX < -1 || hypixelX > 1000 ||
            !hypixelYOk || hypixelY < -1 || hypixelY > 1000 ||
            !clickGuiThemeOk || clickGuiTheme < 0 || clickGuiTheme > 1 ||
            !playerColorOk || playerColor > 0xFFFFFFU ||
            !bedColorOk || bedColor > 0xFFFFFFU ||
            !panelColorOk || panelColor > 0xFFFFFFU ||
            !hypixelColorOk || hypixelColor > 0xFFFFFFU ||
            !nametagOpacityOk || nametagOpacity < 10 || nametagOpacity > 100 ||
            !nametagColorOk || nametagColor > 0xFFFFFFU ||
            !accentColorOk || accentColor > 0xFFFFFFU ||
            !hypixelFontIndexOk || hypixelFontIndex < 0 || hypixelFontIndex > 3 ||
            !nametagRangeOk || nametagRange < 4 || nametagRange > 128 ||
            !nametagSizeIndexOk || nametagSizeIndex < 0 || nametagSizeIndex > 3 ||
            !hypixelRailColorOk || hypixelRailColor > 0xFFFFFFU ||
            !hypixelRailOpacityOk || hypixelRailOpacity < 0 ||
            hypixelRailOpacity > 100 || !safewalkReleaseDelayOk ||
            safewalkReleaseDelayMs < 0 || safewalkReleaseDelayMs > 750 ||
            !safewalkSensitivityOk || safewalkSensitivity < 0 || safewalkSensitivity > 95 ||
            !safewalkPitchOk || safewalkPitch < -90 || safewalkPitch > 90 ||
            !safewalkHotkeyOk || (safewalkHotkey != 0 && safewalkHotkey < 8) || safewalkHotkey > 254 ||
            !flySpeedOk || flySpeed < 10 || flySpeed > 500 ||
            !aimSlowdownOk || aimSlowdown < 5 || aimSlowdown > 95 ||
            !aimSpeedOk || aimSpeed < 1 || aimSpeed > 100 ||
            !textColorOk || textColor > 0xFFFFFFU ||
            !textXOk || textX < -1 || textX > 1000 ||
            !textYOk || textY < -1 || textY > 1000) return;
        if (!bhopAirSpeedOk || bhopAirSpeed < 10 || bhopAirSpeed > 300 ||
            !hotkeysAOk || !hotkeysBOk || !hotkeysCOk ||
            !validHotkeyPack(hotkeysPackedA, 8) ||
            !validHotkeyPack(hotkeysPackedB, 8) ||
            hotkeysPackedC > 0x7FFFFU ||
            !validHotkeyPack(hotkeysPackedC, 2) ||
            !fireballEnabledOk || fireballEnabled < 0 || fireballEnabled > 1 ||
            !fireballFilledOk || fireballFilled < 0 || fireballFilled > 1 ||
            !longJumpEnabledOk || longJumpEnabled < 0 || longJumpEnabled > 1 ||
            !longJumpSpeedOk || longJumpSpeed < 25 || longJumpSpeed > 250 ||
            !fireballColorOk || fireballColor > 0xFFFFFFU ||
            !aimMinimumDistanceOk || aimMinimumDistance < 0 ||
            aimMinimumDistance > 64 || !aimMaximumDistanceOk ||
            aimMaximumDistance < std::max(1, aimMinimumDistance) ||
            aimMaximumDistance > 128 || !aimFovOk || aimFovDegrees < 1 ||
            aimFovDegrees > 360 || !clickGuiWidthOk ||
            clickGuiWidthPercent < 80 || clickGuiWidthPercent > 150 ||
            !clickGuiHeightOk || clickGuiHeightPercent < 80 ||
            clickGuiHeightPercent > 150 || !clickGuiOpacityOk ||
            clickGuiOpacity < 35 || clickGuiOpacity > 100 ||
            !extraBitsOk || !textGuiAlignmentOk ||
            textGuiAlignment < 0 || textGuiAlignment > 2 ||
            !localMobReachOk || localMobReach < 3 || localMobReach > 10 ||
            !localAttackDelayOk || localAttackDelayMs < 100 ||
            localAttackDelayMs > 1500 || !localVelocityPercentOk ||
            localVelocityPercent < 0 || localVelocityPercent > 100) return;
        const QString playerColorName = QStringLiteral("#%1")
            .arg(playerColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString bedColorName = QStringLiteral("#%1")
            .arg(bedColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString panelColorName = QStringLiteral("#%1")
            .arg(panelColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString hypixelColorName = QStringLiteral("#%1")
            .arg(hypixelColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString nametagColorName = QStringLiteral("#%1")
            .arg(nametagColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString accentColorName = QStringLiteral("#%1")
            .arg(accentColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString hypixelRailColorName = QStringLiteral("#%1")
            .arg(hypixelRailColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString textColorName = QStringLiteral("#%1")
            .arg(textColor, 6, 16, QLatin1Char('0')).toUpper();
        const QString fireballColorName = QStringLiteral("#%1")
            .arg(fireballColor, 6, 16, QLatin1Char('0')).toUpper();
        const bool changed = m_espEnabled != values[0] ||
            m_entityEspEnabled != values[1] || m_bedEspEnabled != values[2] ||
            m_espLabelsEnabled != values[3] || m_hypixelPanelEnabled != values[4] ||
            m_bedThreatAlertsEnabled != values[5] ||
            m_bedDefensePanelEnabled != values[6] ||
            m_entityEspPlayersOnly != values[7] ||
            m_bedAutoRefreshEnabled != values[8] || m_bedEspFilled != values[9] ||
            m_debugChatEnabled != values[10] || m_showOwnBedDefenseInfo != values[11] ||
            m_showTeammateBoxes != values[12] ||
            m_bedDefenseHoldToShow != values[13] ||
            m_bedDefensePerspectiveScale != values[14] ||
            m_hypixelPanelHoldToShow != values[15] ||
            m_nametagEnabled != values[16] ||
            m_nametagSidePlacement != values[17] ||
            m_enemyItemIndicatorsEnabled != values[18] ||
            m_showTeammateNametags != values[19] ||
            m_nametagNearbyEnemiesOnly != values[20] ||
            m_nametagTeamPulse != values[21] ||
            m_showTeammateArrows != values[22] ||
            m_safewalkEnabled != values[23] ||
            m_scaffoldEnabled != values[24] || m_flyEnabled != values[25] ||
            m_bhopEnabled != values[26] || m_bhopAutoJump != values[27] ||
            m_aimAssistEnabled != values[28] || m_aimLockOnMode != values[29] ||
            m_textGuiEnabled != values[30] || m_allowHypixelMovement != values[31] ||
            m_bedDefenseRadius != defenseRadius || m_bedThreatRadius != threatRadius ||
            m_bedDefenseHotkey != bedHotkey ||
            m_bedDefensePanelOpacity != panelOpacity ||
            m_hypixelPanelHotkey != hypixelHotkey ||
            m_hypixelPanelOpacity != hypixelOpacity ||
            m_hypixelPanelScale != hypixelScale ||
            m_hypixelPanelHeight != hypixelHeight ||
            m_hypixelPanelX != hypixelX || m_hypixelPanelY != hypixelY ||
            m_clickGuiLightTheme != (clickGuiTheme != 0) ||
            m_playerEspColor != playerColorName || m_bedEspColor != bedColorName ||
            m_bedDefensePanelColor != panelColorName ||
            m_hypixelPanelColor != hypixelColorName ||
            m_nametagPanelOpacity != nametagOpacity ||
            m_nametagPanelColor != nametagColorName ||
            m_clickGuiAccentColor != accentColorName ||
            m_hypixelPanelFontIndex != hypixelFontIndex ||
            m_nametagRange != nametagRange ||
            m_nametagSizeIndex != nametagSizeIndex ||
            m_hypixelRailColor != hypixelRailColorName ||
            m_hypixelRailOpacity != hypixelRailOpacity ||
            m_safewalkReleaseDelayMs != safewalkReleaseDelayMs ||
            m_safewalkEdgeSensitivity != safewalkSensitivity ||
            m_safewalkMinimumPitch != safewalkPitch ||
            m_safewalkHotkey != safewalkHotkey ||
            m_flySpeedPercent != flySpeed ||
            m_aimSlowdownPercent != aimSlowdown || m_aimSpeedPercent != aimSpeed ||
            m_textGuiColor != textColorName || m_textGuiX != textX ||
            m_textGuiY != textY || m_bhopAirSpeedPercent != bhopAirSpeed ||
            m_featureHotkeysPackedA != hotkeysPackedA ||
            m_featureHotkeysPackedB != hotkeysPackedB ||
            m_featureHotkeysPackedC != hotkeysPackedC ||
            m_fireballEspEnabled != (fireballEnabled != 0) ||
            m_fireballEspFilled != (fireballFilled != 0) ||
            m_longJumpEnabled != (longJumpEnabled != 0) ||
            m_longJumpSpeedPercent != longJumpSpeed ||
            m_fireballEspColor != fireballColorName ||
            m_aimMinimumDistance != aimMinimumDistance ||
            m_aimMaximumDistance != aimMaximumDistance ||
            m_aimFovDegrees != aimFovDegrees ||
            m_clickGuiWidthPercent != clickGuiWidthPercent ||
            m_clickGuiHeightPercent != clickGuiHeightPercent ||
            m_clickGuiOpacity != clickGuiOpacity ||
            m_featureExtraBits != extraBits ||
            m_textGuiAlignment != textGuiAlignment ||
            m_localMobReach != localMobReach ||
            m_localAttackDelayMs != localAttackDelayMs ||
            m_localVelocityPercent != localVelocityPercent;
        m_espEnabled = values[0];
        m_entityEspEnabled = values[1];
        m_bedEspEnabled = values[2];
        m_espLabelsEnabled = values[3];
        m_hypixelPanelEnabled = values[4];
        m_bedThreatAlertsEnabled = values[5];
        m_bedDefensePanelEnabled = values[6];
        m_entityEspPlayersOnly = values[7];
        m_bedAutoRefreshEnabled = values[8];
        m_bedEspFilled = values[9];
        m_debugChatEnabled = values[10];
        m_showOwnBedDefenseInfo = values[11];
        m_showTeammateBoxes = values[12];
        m_bedDefenseHoldToShow = values[13];
        m_bedDefensePerspectiveScale = values[14];
        m_hypixelPanelHoldToShow = values[15];
        m_nametagEnabled = values[16];
        m_nametagSidePlacement = values[17];
        m_enemyItemIndicatorsEnabled = values[18];
        m_showTeammateNametags = values[19];
        m_nametagNearbyEnemiesOnly = values[20];
        m_nametagTeamPulse = values[21];
        m_showTeammateArrows = values[22];
        m_safewalkEnabled = values[23];
        m_scaffoldEnabled = values[24];
        m_flyEnabled = values[25];
        m_bhopEnabled = values[26];
        m_bhopAutoJump = values[27];
        m_aimAssistEnabled = values[28];
        m_aimLockOnMode = values[29];
        m_textGuiEnabled = values[30];
        m_allowHypixelMovement = values[31];
        m_bedDefenseRadius = defenseRadius;
        m_bedThreatRadius = threatRadius;
        m_bedDefenseHotkey = bedHotkey;
        m_bedDefensePanelOpacity = panelOpacity;
        m_hypixelPanelHotkey = hypixelHotkey;
        m_hypixelPanelOpacity = hypixelOpacity;
        m_hypixelPanelScale = hypixelScale;
        m_hypixelPanelHeight = hypixelHeight;
        m_hypixelPanelX = hypixelX;
        m_hypixelPanelY = hypixelY;
        m_clickGuiLightTheme = clickGuiTheme != 0;
        m_playerEspColor = playerColorName;
        m_bedEspColor = bedColorName;
        m_bedDefensePanelColor = panelColorName;
        m_hypixelPanelColor = hypixelColorName;
        m_nametagPanelOpacity = nametagOpacity;
        m_nametagPanelColor = nametagColorName;
        m_clickGuiAccentColor = accentColorName;
        m_hypixelPanelFontIndex = hypixelFontIndex;
        m_nametagRange = nametagRange;
        m_nametagSizeIndex = nametagSizeIndex;
        m_hypixelRailColor = hypixelRailColorName;
        m_hypixelRailOpacity = hypixelRailOpacity;
        m_safewalkReleaseDelayMs = safewalkReleaseDelayMs;
        m_safewalkEdgeSensitivity = safewalkSensitivity;
        m_safewalkMinimumPitch = safewalkPitch;
        m_safewalkHotkey = safewalkHotkey;
        m_flySpeedPercent = flySpeed;
        m_aimSlowdownPercent = aimSlowdown;
        m_aimSpeedPercent = aimSpeed;
        m_textGuiColor = textColorName;
        m_textGuiX = textX;
        m_textGuiY = textY;
        m_bhopAirSpeedPercent = bhopAirSpeed;
        m_featureHotkeysPackedA = hotkeysPackedA;
        m_featureHotkeysPackedB = hotkeysPackedB;
        m_featureHotkeysPackedC = hotkeysPackedC;
        m_velocityHotkey=static_cast<int>(hotkeysPackedC&0xFFU);
        m_fireballEspEnabled = fireballEnabled != 0;
        m_fireballEspFilled = fireballFilled != 0;
        m_longJumpEnabled = false;
        m_longJumpSpeedPercent = longJumpSpeed;
        m_fireballEspColor = fireballColorName;
        m_aimMinimumDistance = aimMinimumDistance;
        m_aimMaximumDistance = aimMaximumDistance;
        m_aimFovDegrees = aimFovDegrees;
        m_clickGuiWidthPercent = clickGuiWidthPercent;
        m_clickGuiHeightPercent = clickGuiHeightPercent;
        m_clickGuiOpacity = clickGuiOpacity;
        m_featureExtraBits = extraBits & ~0x10U;
        m_textGuiAlignment = textGuiAlignment;
        m_localMobReach = localMobReach;
        m_localAttackDelayMs = localAttackDelayMs;
        m_localVelocityPercent = localVelocityPercent;
        if (changed) {
            storeFeatureSettings();
            emit featureSettingsChanged();
        }
    } else if (type == QByteArrayLiteral("BIND_CHANGED")) {
        bool valid = false;
        const int virtualKey = fields.value(1).toInt(&valid);
        if (fields.size() == 2 && valid && (virtualKey == 0 || virtualKey >= 8) && virtualKey <= 254 &&
            m_menuHotkey != virtualKey) {
            m_menuHotkey = virtualKey;
            storeFeatureSettings();
            emit menuHotkeyChanged();
        }
    } else if (type == QByteArrayLiteral("GUI_SCALE_CHANGED")) {
        bool valid = false;
        const int index = fields.value(1).toInt(&valid);
        if (fields.size() == 2 && valid && index >= 0 && index <= 3
            && m_guiScaleIndex != index) {
            m_guiScaleIndex = index;
            storeFeatureSettings();
            emit guiScaleIndexChanged();
        }
    } else if (type == QByteArrayLiteral("PLAYER_FOUND")) {
        if (fields.size() != 3 && fields.size() != 4) return;
        const QString playerName = decodeProtocolToken(fields.at(1));
        const QString teamPrefix = decodeProtocolToken(fields.at(2)).toLower();
        QString uuid = fields.size() == 4 ? decodeProtocolToken(fields.at(3)).toLower()
                                          : QString{};
        uuid.remove(QLatin1Char('-'));
        static const QRegularExpression nameExpression(
            QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
        static const QRegularExpression uuidExpression(
            QStringLiteral("^[0-9a-f]{32}$"));
        const bool validTeam = teamPrefix.size() == 2 &&
            teamPrefix.at(0) == QChar(0x00A7) &&
            ((teamPrefix.at(1) >= QLatin1Char('0') && teamPrefix.at(1) <= QLatin1Char('9')) ||
             (teamPrefix.at(1) >= QLatin1Char('a') && teamPrefix.at(1) <= QLatin1Char('f')));
        if (!uuid.isEmpty() && !uuidExpression.match(uuid).hasMatch()) return;
        if (nameExpression.match(playerName).hasMatch() && validTeam) {
            emit playerFound(playerName, teamPrefix);
            emit playerIdentityFound(playerName, teamPrefix, uuid);
        }
    } else if (type == QByteArrayLiteral("BLACKLIST_ADD")) {
        if (fields.size() != 6) return;
        const QString name = decodeProtocolToken(fields.at(1));
        const QString uuid = decodeProtocolToken(fields.at(2));
        const QString reason = decodeProtocolToken(fields.at(3));
        if ((fields.at(4) != "0" && fields.at(4) != "1") ||
            (fields.at(5) != "0" && fields.at(5) != "1")) return;
        emit blacklistAddRequested(name, uuid, reason,
            fields.at(4) == "1", fields.at(5) == "1");
    } else if (type == QByteArrayLiteral("BLACKLIST_REMOVE")) {
        if (fields.size() == 2)
            emit blacklistRemoveRequested(decodeProtocolToken(fields.at(1)));
    } else if (type == QByteArrayLiteral("BLACKLIST_WARNING")) {
        if (fields.size() == 3 &&
            (fields.at(2) == "0" || fields.at(2) == "1")) {
            emit blacklistWarningRequested(decodeProtocolToken(fields.at(1)),
                                           fields.at(2) == "1");
        }
    } else if (type == QByteArrayLiteral("BLACKLIST_LAYOUT")) {
        if (fields.size() != 5) return;
        bool xOk = false, yOk = false, widthOk = false, heightOk = false;
        const int x = fields.at(1).toInt(&xOk);
        const int y = fields.at(2).toInt(&yOk);
        const int width = fields.at(3).toInt(&widthOk);
        const int height = fields.at(4).toInt(&heightOk);
        if (xOk && yOk && widthOk && heightOk && x >= -1 && x <= 1000 &&
            y >= -1 && y <= 1000 && width >= 60 && width <= 180 &&
            height >= 60 && height <= 300) {
            emit blacklistLayoutChanged(x, y, width, height);
        }
    } else if (type == QByteArrayLiteral("BLACKLIST_SETTINGS_CHANGED")) {
        if (fields.size() != 9 ||
            (fields.at(1) != "0" && fields.at(1) != "1") ||
            (fields.at(2) != "0" && fields.at(2) != "1") ||
            (fields.at(3) != "0" && fields.at(3) != "1") ||
            (fields.at(4) != "0" && fields.at(4) != "1") ||
            (fields.at(5) != "0" && fields.at(5) != "1")) return;
        bool opacityOk = false, scaleOk = false, colorOk = false;
        const int opacity = fields.at(6).toInt(&opacityOk);
        const int contentScale = fields.at(7).toInt(&scaleOk);
        const quint32 color = fields.at(8).toUInt(&colorOk);
        if (!opacityOk || opacity < 0 || opacity > 100 ||
            !scaleOk || contentScale < 80 || contentScale > 200 ||
            !colorOk || color > 0xFFFFFFU) return;
        emit blacklistSettingsChanged(
            fields.at(1) == "1", fields.at(2) == "1", fields.at(3) == "1",
            fields.at(4) == "1", fields.at(5) == "1",
            opacity, contentScale, QStringLiteral("#%1").arg(
                color, 6, 16, QLatin1Char('0')).toUpper());
    } else if (type == QByteArrayLiteral("MATCH_STATE")) {
        if (fields.size() != 2 ||
            (fields.at(1) != QByteArrayLiteral("0") && fields.at(1) != QByteArrayLiteral("1"))) {
            return;
        }
        const bool active = fields.at(1) == QByteArrayLiteral("1");
        if (m_matchActive != active) {
            m_matchActive = active;
            emit matchStateChanged(active);
        }
    } else if (type == QByteArrayLiteral("PLAYER_STATUS")) {
        if (fields.size() != 2) return;
        const QString name = decodeProtocolToken(fields.at(1));
        static const QRegularExpression nameExpression(
            QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
        if (nameExpression.match(name).hasMatch() && name != m_playerName) {
            m_playerName = name;
            emit playerStatusChanged();
        }
    } else if (type == QByteArrayLiteral("HYPIXEL_QUERY")) {
        if (fields.size() != 2) return;
        const QString playerId = decodeProtocolToken(fields.at(1));
        static const QRegularExpression nameExpression(
            QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
        if (nameExpression.match(playerId).hasMatch())
            emit hypixelQueryRequested(playerId);
    } else if (type == QByteArrayLiteral("DETACH_COMPLETE")) {
        if (fields.size() == 1 && m_state == State::Detaching)
            completeDetach(false);
    } else if (type == QByteArrayLiteral("GAME_STATE")) {
        // Protocol v1 (tokens separated by one or more ASCII spaces):
        // GAME_STATE 1 seq unixMs valid hp maxHp entityId x y z
        //            loadedEntities bedCount mappingPct statePct
        // String tokens are UTF-8 percent encoded; `-` means empty. Invalid
        // mapping snapshots retain zero numeric placeholders and valid=0.
        if (fields.size() != 15 || fields.at(1) != QByteArrayLiteral("1"))
            return;

        bool sequenceOk = false;
        bool timestampOk = false;
        bool healthOk = false;
        bool maxHealthOk = false;
        bool entityIdOk = false;
        bool xOk = false;
        bool yOk = false;
        bool zOk = false;
        bool entitiesOk = false;
        bool bedsOk = false;
        const quint64 sequence = fields.at(2).toULongLong(&sequenceOk);
        const qint64 timestamp = fields.at(3).toLongLong(&timestampOk);
        const bool validToken = fields.at(4) == QByteArrayLiteral("0")
                             || fields.at(4) == QByteArrayLiteral("1");
        const bool available = fields.at(4) == QByteArrayLiteral("1");
        const double health = fields.at(5).toDouble(&healthOk);
        const double maxHealth = fields.at(6).toDouble(&maxHealthOk);
        const int entityId = fields.at(7).toInt(&entityIdOk);
        const double x = fields.at(8).toDouble(&xOk);
        const double y = fields.at(9).toDouble(&yOk);
        const double z = fields.at(10).toDouble(&zOk);
        const int entities = fields.at(11).toInt(&entitiesOk);
        const int beds = fields.at(12).toInt(&bedsOk);

        const bool finiteNumbers = std::isfinite(health)
                                && std::isfinite(maxHealth)
                                && std::isfinite(x)
                                && std::isfinite(y)
                                && std::isfinite(z);
        const bool sensibleRanges = health >= -2048.0 && health <= 1000000.0
                                 && maxHealth >= 0.0 && maxHealth <= 1000000.0
                                 && std::abs(x) <= 100000000.0
                                 && std::abs(y) <= 100000000.0
                                 && std::abs(z) <= 100000000.0
                                 && entities >= 0 && entities <= 10000000
                                 && beds >= 0 && beds <= 10000000;
        if (!sequenceOk || sequence == 0 || !timestampOk || timestamp < 0 || !validToken
            || !healthOk || !maxHealthOk || !entityIdOk || !xOk || !yOk
            || !zOk || !entitiesOk || !bedsOk || !finiteNumbers
            || !sensibleRanges) {
            return;
        }

        // Protocol v1 deliberately keeps a fixed grammar for unavailable
        // mappings. Never surface stale or attacker-supplied gameplay values
        // from a valid=0 frame: every numeric placeholder must be zero.
        if (!available && (health != 0.0 || maxHealth != 0.0
                           || entityId != 0 || x != 0.0 || y != 0.0
                           || z != 0.0 || entities != 0 || beds != 0)) {
            return;
        }

        // Ignore delayed/reordered samples within one authenticated session.
        if (m_gameStateReceived && sequence <= m_gameStateSequence)
            return;

        m_gameStateReceived = true;
        m_gameStateAvailable = available;
        m_gameStateStale = false;
        m_playerHealth = health;
        m_playerMaxHealth = maxHealth;
        m_playerEntityId = entityId;
        m_playerX = x;
        m_playerY = y;
        m_playerZ = z;
        m_loadedEntities = entities;
        m_bedCount = beds;
        m_mappingProfile = decodeProtocolToken(fields.at(13));
        m_mappingState = decodeProtocolToken(fields.at(14));
        if (m_mappingState.isEmpty())
            m_mappingState = available ? QStringLiteral("ready")
                                           : QStringLiteral("unavailable");
        m_gameStateTimestamp = timestamp;
        m_lastGameStateReceiptElapsedMs = m_gameStateReceiptClock.elapsed();
        m_gameStateSequence = sequence;
        emit gameStateChanged();
    } else if (type == QByteArrayLiteral("STATUS")) {
        const int separator = line.indexOf(' ');
        if (separator >= 0)
            setStatusMessage(QString::fromUtf8(line.mid(separator + 1)));
    } else if (type == QByteArrayLiteral("ERROR")) {
        const QString code = QString::fromUtf8(fields.value(1));
        const int detailStart = line.indexOf(' ', line.indexOf(' ') + 1);
        const QString detail = detailStart >= 0
            ? QString::fromUtf8(line.mid(detailStart + 1))
            : QStringLiteral("The native agent reported an error.");
        // Statistics are an optional feature layered on top of the live
        // overlay session.  A malformed/failed result must never tear down the
        // control pipe: doing so leaves the already-loaded JVMTI agent alive
        // and makes a subsequent attach look like a duplicate injection.
        if (code == QStringLiteral("BAD_HYPIXEL_RESULT") ||
            code == QStringLiteral("BAD_STATS") ||
            code == QStringLiteral("BAD_STATS_ERROR")) {
            setStatusMessage(QStringLiteral("Statistics update rejected: %1").arg(detail));
            return;
        }
        fail(code.isEmpty() ? QStringLiteral("AGENT_ERROR") : code, detail);
    }
}

void OverlayManager::sendStateSnapshot()
{
    if (!m_authenticated)
        return;
    writeAgentCommand(QByteArrayLiteral("STATE ")
                      + (m_overlayEnabled ? QByteArrayLiteral("1 ")
                                          : QByteArrayLiteral("0 "))
                      + (m_interactive ? QByteArrayLiteral("1\n")
                                       : QByteArrayLiteral("0\n")));
    sendFeatureSnapshot();
    sendBindSnapshot();
    sendGuiScaleSnapshot();
    sendMediaSettings();
}

void OverlayManager::sendFeatureSnapshot()
{
    if (!m_authenticated) return;
    writeAgentCommand(QByteArrayLiteral("FEATURE_STATE_V3 ")
                      + (m_espEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_entityEspEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedEspEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_espLabelsEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_hypixelPanelEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedThreatAlertsEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedDefensePanelEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_entityEspPlayersOnly ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedAutoRefreshEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedEspFilled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_debugChatEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_showOwnBedDefenseInfo ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_showTeammateBoxes ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedDefenseHoldToShow ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bedDefensePerspectiveScale ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_hypixelPanelHoldToShow ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_nametagEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_nametagSidePlacement ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_enemyItemIndicatorsEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_showTeammateNametags ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_nametagNearbyEnemiesOnly ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_nametagTeamPulse ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_showTeammateArrows ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_safewalkEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_scaffoldEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_flyEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bhopEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_bhopAutoJump ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_aimAssistEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_aimLockOnMode ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_textGuiEnabled ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + (m_allowHypixelMovement ? QByteArrayLiteral("1 ") : QByteArrayLiteral("0 "))
                      + QByteArray::number(std::clamp(m_bedDefenseRadius, 3, 10)) + ' '
                      + QByteArray::number(std::clamp(m_bedThreatRadius, 3, 32)) + ' '
                      + QByteArray::number(std::clamp(m_bedDefenseHotkey, 0, 254)) + ' '
                      + QByteArray::number(std::clamp(m_bedDefensePanelOpacity, 0, 100)) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelHotkey, 0, 254)) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelOpacity, 0, 100)) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelScale, 70, 160)) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelX, -1, 1000)) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelY, -1, 1000)) + ' '
                      + QByteArray::number(m_clickGuiLightTheme ? 1 : 0) + ' '
                      + QByteArray::number(QColor(m_playerEspColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(QColor(m_bedEspColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(QColor(m_bedDefensePanelColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(QColor(m_hypixelPanelColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelHeight, 60, 400)) + ' '
                      + QByteArray::number(std::clamp(m_nametagPanelOpacity, 10, 100)) + ' '
                      + QByteArray::number(QColor(m_nametagPanelColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(QColor(m_clickGuiAccentColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(std::clamp(m_hypixelPanelFontIndex, 0, 3)) + ' '
                      + QByteArray::number(std::clamp(m_nametagRange, 4, 128)) + ' '
                      + QByteArray::number(std::clamp(m_nametagSizeIndex, 0, 3)) + ' '
                      + QByteArray::number(QColor(m_hypixelRailColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(std::clamp(m_hypixelRailOpacity, 0, 100)) + ' '
                      + QByteArray::number(std::clamp(m_safewalkReleaseDelayMs, 0, 750)) + ' '
                      + QByteArray::number(std::clamp(m_safewalkEdgeSensitivity, 0, 100)) + ' '
                      + QByteArray::number(std::clamp(m_safewalkMinimumPitch, -90, 90)) + ' '
                      + QByteArray::number(std::clamp(m_safewalkHotkey, 0, 254)) + ' '
                      + QByteArray::number(std::clamp(m_flySpeedPercent, 10, 500)) + ' '
                      + QByteArray::number(std::clamp(m_aimSlowdownPercent, 5, 95)) + ' '
                      + QByteArray::number(std::clamp(m_aimSpeedPercent, 1, 100)) + ' '
                      + QByteArray::number(QColor(m_textGuiColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(std::clamp(m_textGuiX, -1, 1000)) + ' '
                      + QByteArray::number(std::clamp(m_textGuiY, -1, 1000)) + ' '
                      + QByteArray::number(std::clamp(m_bhopAirSpeedPercent, 10, 300)) + ' '
                      + QByteArray::number(static_cast<qulonglong>(m_featureHotkeysPackedA)) + ' '
                      + QByteArray::number(static_cast<qulonglong>(m_featureHotkeysPackedB)) + ' '
                      + QByteArray::number(m_featureHotkeysPackedC) + ' '
                      + QByteArray::number(m_fireballEspEnabled ? 1 : 0) + ' '
                      + QByteArray::number(m_fireballEspFilled ? 1 : 0) + ' '
                      + QByteArray::number(m_longJumpEnabled ? 1 : 0) + ' '
                      + QByteArray::number(std::clamp(m_longJumpSpeedPercent, 25, 250)) + ' '
                      + QByteArray::number(QColor(m_fireballEspColor).rgb() & 0xFFFFFFU) + ' '
                      + QByteArray::number(std::clamp(m_aimMinimumDistance, 0, 64)) + ' '
                      + QByteArray::number(std::clamp(m_aimMaximumDistance,
                            std::max(1, m_aimMinimumDistance), 128)) + ' '
                      + QByteArray::number(std::clamp(m_aimFovDegrees, 1, 360)) + ' '
                      + QByteArray::number(std::clamp(m_clickGuiWidthPercent, 80, 150)) + ' '
                      + QByteArray::number(std::clamp(m_clickGuiHeightPercent, 80, 150)) + ' '
                      + QByteArray::number(std::clamp(m_clickGuiOpacity, 35, 100)) + ' '
                      + QByteArray::number(m_featureExtraBits) + ' '
                      + QByteArray::number(std::clamp(m_textGuiAlignment, 0, 2)) + ' '
                      + QByteArray::number(std::clamp(m_localMobReach, 3, 10)) + ' '
                      + QByteArray::number(std::clamp(m_localAttackDelayMs, 100, 1500)) + ' '
                      + QByteArray::number(std::clamp(m_localVelocityPercent, 0, 100)) + '\n');
    writeAgentCommand(QByteArrayLiteral("AIM_OPTIONS ")+QByteArray::number(
        (m_aimSilentLock ? 1U : 0U) | (m_aimScannerEnabled ? 2U : 0U) |
        (m_bedBreakerEnabled ? 4U : 0U) |
        (m_aimAttackViability ? 8U : 0U) |
        (m_textGuiShowModes ? 16U : 0U) |
        (m_silentControlAdaptation ? 0x40000000U : 0U) |
        (m_aimSequentialTargets ? 0x20000000U : 0U) |
        (m_silentFileDebug ? 0x08000000U : 0U) |
        (m_silentChatDebug ? 0x10000000U : 0U) |
        (static_cast<unsigned>(std::clamp(m_localVelocityProbability,0,100))<<5U) |
        (static_cast<unsigned>(std::clamp(m_localVelocityVerticalPercent,0,100))<<12U) |
        (static_cast<unsigned>(std::clamp(m_velocityHotkey,0,254))<<19U))+'\n');
    writeAgentCommand(QByteArrayLiteral("AIM_ATTACK_CPS ")+
        QByteArray::number(std::clamp(m_aimAttackCps,1,20))+'\n');
    writeAgentCommand(QByteArrayLiteral("SMART_HOTBAR ")+
        QByteArray::number(m_smartHotbarConfig)+'\n');
}

void OverlayManager::sendBindSnapshot()
{
    if (!m_authenticated) return;
    writeAgentCommand(QByteArrayLiteral("BIND ") +
                      QByteArray::number(std::clamp(m_menuHotkey, 0, 254)) + '\n');
}

void OverlayManager::sendGuiScaleSnapshot()
{
    if (!m_authenticated) return;
    writeAgentCommand(QByteArrayLiteral("GUI_SCALE ")
                      + QByteArray::number(std::clamp(m_guiScaleIndex, 0, 3)) + '\n');
}

