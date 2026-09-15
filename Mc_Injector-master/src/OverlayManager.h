#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

class QLocalSocket;

// QML keeps the historical OverlayManager name, but this class now controls
// an in-process JVM native agent. It never creates a transparent top-level
// window: the agent draws Dear ImGui into Minecraft's OpenGL frame.
class OverlayManager final : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Detached,
        Validating,
        StartingIpc,
        LaunchingAttachHelper,
        WaitingForAgent,
        WaitingForOpenGL,
        Active,
        Detaching,
        Error
    };
    Q_ENUM(State)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool attached READ attached NOTIFY attachedChanged)
    Q_PROPERTY(bool rendererActive READ rendererActive NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled
                   NOTIFY overlayEnabledChanged)
    Q_PROPERTY(bool interactive READ interactive WRITE setInteractive
                   NOTIFY interactiveChanged)
    Q_PROPERTY(bool espEnabled READ espEnabled WRITE setEspEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool entityEspEnabled READ entityEspEnabled WRITE setEntityEspEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool entityEspPlayersOnly READ entityEspPlayersOnly WRITE setEntityEspPlayersOnly NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedEspEnabled READ bedEspEnabled WRITE setBedEspEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedAutoRefreshEnabled READ bedAutoRefreshEnabled WRITE setBedAutoRefreshEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool espLabelsEnabled READ espLabelsEnabled WRITE setEspLabelsEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool hypixelPanelEnabled READ hypixelPanelEnabled WRITE setHypixelPanelEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool hypixelPanelHoldToShow READ hypixelPanelHoldToShow WRITE setHypixelPanelHoldToShow NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelPanelHotkey READ hypixelPanelHotkey WRITE setHypixelPanelHotkey NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelPanelOpacity READ hypixelPanelOpacity WRITE setHypixelPanelOpacity NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString hypixelPanelColor READ hypixelPanelColor WRITE setHypixelPanelColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString hypixelRailColor READ hypixelRailColor WRITE setHypixelRailColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelRailOpacity READ hypixelRailOpacity WRITE setHypixelRailOpacity NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelPanelScale READ hypixelPanelScale WRITE setHypixelPanelScale NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelPanelX READ hypixelPanelX WRITE setHypixelPanelX NOTIFY featureSettingsChanged)
    Q_PROPERTY(int hypixelPanelY READ hypixelPanelY WRITE setHypixelPanelY NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool clickGuiLightTheme READ clickGuiLightTheme WRITE setClickGuiLightTheme NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedThreatAlertsEnabled READ bedThreatAlertsEnabled WRITE setBedThreatAlertsEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedDefensePanelEnabled READ bedDefensePanelEnabled WRITE setBedDefensePanelEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedEspFilled READ bedEspFilled WRITE setBedEspFilled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool debugChatEnabled READ debugChatEnabled WRITE setDebugChatEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool showOwnBedDefenseInfo READ showOwnBedDefenseInfo WRITE setShowOwnBedDefenseInfo NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool showTeammateBoxes READ showTeammateBoxes WRITE setShowTeammateBoxes NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool showTeammateArrows READ showTeammateArrows WRITE setShowTeammateArrows NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool safewalkEnabled READ safewalkEnabled WRITE setSafewalkEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(int safewalkReleaseDelayMs READ safewalkReleaseDelayMs WRITE setSafewalkReleaseDelayMs NOTIFY featureSettingsChanged)
    Q_PROPERTY(int safewalkEdgeSensitivity READ safewalkEdgeSensitivity WRITE setSafewalkEdgeSensitivity NOTIFY featureSettingsChanged)
    Q_PROPERTY(int safewalkMinimumPitch READ safewalkMinimumPitch WRITE setSafewalkMinimumPitch NOTIFY featureSettingsChanged)
    Q_PROPERTY(int safewalkHotkey READ safewalkHotkey WRITE setSafewalkHotkey NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool scaffoldEnabled READ scaffoldEnabled WRITE setScaffoldEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool flyEnabled READ flyEnabled WRITE setFlyEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(int flySpeedPercent READ flySpeedPercent WRITE setFlySpeedPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bhopEnabled READ bhopEnabled WRITE setBhopEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bhopAutoJump READ bhopAutoJump WRITE setBhopAutoJump NOTIFY featureSettingsChanged)
    Q_PROPERTY(int bhopAirSpeedPercent READ bhopAirSpeedPercent WRITE setBhopAirSpeedPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool fireballEspEnabled READ fireballEspEnabled WRITE setFireballEspEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool fireballEspFilled READ fireballEspFilled WRITE setFireballEspFilled NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString fireballEspColor READ fireballEspColor WRITE setFireballEspColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool longJumpEnabled READ longJumpEnabled WRITE setLongJumpEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(int longJumpSpeedPercent READ longJumpSpeedPercent WRITE setLongJumpSpeedPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool aimAssistEnabled READ aimAssistEnabled WRITE setAimAssistEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool aimLockOnMode READ aimLockOnMode WRITE setAimLockOnMode NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool aimSilentLock READ aimSilentLock WRITE setAimSilentLock NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool aimScannerEnabled READ aimScannerEnabled WRITE setAimScannerEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool aimAttackViability READ aimAttackViability WRITE setAimAttackViability NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool silentControlAdaptation READ silentControlAdaptation WRITE setSilentControlAdaptation NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimSlowdownPercent READ aimSlowdownPercent WRITE setAimSlowdownPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimSpeedPercent READ aimSpeedPercent WRITE setAimSpeedPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimMinimumDistance READ aimMinimumDistance WRITE setAimMinimumDistance NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimMaximumDistance READ aimMaximumDistance WRITE setAimMaximumDistance NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimFovDegrees READ aimFovDegrees WRITE setAimFovDegrees NOTIFY featureSettingsChanged)
    Q_PROPERTY(int aimAttackCps READ aimAttackCps WRITE setAimAttackCps NOTIFY featureSettingsChanged)
    Q_PROPERTY(int clickGuiWidthPercent READ clickGuiWidthPercent WRITE setClickGuiWidthPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(int clickGuiHeightPercent READ clickGuiHeightPercent WRITE setClickGuiHeightPercent NOTIFY featureSettingsChanged)
    Q_PROPERTY(int clickGuiOpacity READ clickGuiOpacity WRITE setClickGuiOpacity NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool textGuiEnabled READ textGuiEnabled WRITE setTextGuiEnabled NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool textGuiShowModes READ textGuiShowModes WRITE setTextGuiShowModes NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString textGuiColor READ textGuiColor WRITE setTextGuiColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(QStringList textGuiModules READ textGuiModules NOTIFY featureSettingsChanged)
    Q_PROPERTY(int textGuiAlignment READ textGuiAlignment NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool textGuiVerticalLine READ textGuiVerticalLine NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool allowHypixelMovement READ allowHypixelMovement WRITE setAllowHypixelMovement NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedDefenseHoldToShow READ bedDefenseHoldToShow WRITE setBedDefenseHoldToShow NOTIFY featureSettingsChanged)
    Q_PROPERTY(bool bedDefensePerspectiveScale READ bedDefensePerspectiveScale WRITE setBedDefensePerspectiveScale NOTIFY featureSettingsChanged)
    Q_PROPERTY(int bedDefenseRadius READ bedDefenseRadius WRITE setBedDefenseRadius NOTIFY featureSettingsChanged)
    Q_PROPERTY(int bedThreatRadius READ bedThreatRadius WRITE setBedThreatRadius NOTIFY featureSettingsChanged)
    Q_PROPERTY(int bedDefenseHotkey READ bedDefenseHotkey WRITE setBedDefenseHotkey NOTIFY featureSettingsChanged)
    Q_PROPERTY(int bedDefensePanelOpacity READ bedDefensePanelOpacity WRITE setBedDefensePanelOpacity NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString playerEspColor READ playerEspColor WRITE setPlayerEspColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString bedEspColor READ bedEspColor WRITE setBedEspColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(QString bedDefensePanelColor READ bedDefensePanelColor WRITE setBedDefensePanelColor NOTIFY featureSettingsChanged)
    Q_PROPERTY(int menuHotkey READ menuHotkey WRITE setMenuHotkey NOTIFY menuHotkeyChanged)
    Q_PROPERTY(int guiScaleIndex READ guiScaleIndex WRITE setGuiScaleIndex
                   NOTIFY guiScaleIndexChanged)
    Q_PROPERTY(bool configAutoSave READ configAutoSave WRITE setConfigAutoSave
                   NOTIFY configStateChanged)
    Q_PROPERTY(QStringList configNames READ configNames NOTIFY configStateChanged)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY configStateChanged)
    Q_PROPERTY(quint32 targetPid READ targetPid NOTIFY targetChanged)
    Q_PROPERTY(QString targetTitle READ targetTitle NOTIFY targetChanged)
    Q_PROPERTY(QString renderer READ renderer NOTIFY rendererChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged)
    // Read-only game telemetry is populated by authenticated GAME_STATE IPC
    // messages. `gameStateAvailable` means the numeric payload is valid;
    // `gameStateStale` is driven by controller receipt time so an incorrect
    // target clock cannot make old values appear live.
    Q_PROPERTY(bool gameStateReceived READ gameStateReceived NOTIFY gameStateChanged)
    Q_PROPERTY(bool gameStateAvailable READ gameStateAvailable NOTIFY gameStateChanged)
    Q_PROPERTY(bool gameStateStale READ gameStateStale NOTIFY gameStateChanged)
    Q_PROPERTY(double playerHealth READ playerHealth NOTIFY gameStateChanged)
    Q_PROPERTY(double playerMaxHealth READ playerMaxHealth NOTIFY gameStateChanged)
    Q_PROPERTY(int playerEntityId READ playerEntityId NOTIFY gameStateChanged)
    Q_PROPERTY(double playerX READ playerX NOTIFY gameStateChanged)
    Q_PROPERTY(double playerY READ playerY NOTIFY gameStateChanged)
    Q_PROPERTY(double playerZ READ playerZ NOTIFY gameStateChanged)
    Q_PROPERTY(int loadedEntities READ loadedEntities NOTIFY gameStateChanged)
    Q_PROPERTY(int bedCount READ bedCount NOTIFY gameStateChanged)
    Q_PROPERTY(QString mappingProfile READ mappingProfile NOTIFY gameStateChanged)
    Q_PROPERTY(QString mappingState READ mappingState NOTIFY gameStateChanged)
    Q_PROPERTY(qint64 gameStateTimestamp READ gameStateTimestamp NOTIFY gameStateChanged)
    Q_PROPERTY(quint64 gameStateSequence READ gameStateSequence NOTIFY gameStateChanged)
    Q_PROPERTY(QString playerName READ playerName NOTIFY playerStatusChanged)
    Q_PROPERTY(bool matchActive READ matchActive NOTIFY matchStateChanged)

public:
    explicit OverlayManager(QObject *parent = nullptr);
    ~OverlayManager() override;

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] bool attached() const noexcept;
    [[nodiscard]] bool rendererActive() const noexcept { return m_state == State::Active; }
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool overlayEnabled() const noexcept { return m_overlayEnabled; }
    [[nodiscard]] bool interactive() const noexcept { return m_interactive; }
    [[nodiscard]] bool espEnabled() const noexcept { return m_espEnabled; }
    [[nodiscard]] bool entityEspEnabled() const noexcept { return m_entityEspEnabled; }
    [[nodiscard]] bool entityEspPlayersOnly() const noexcept { return m_entityEspPlayersOnly; }
    [[nodiscard]] bool bedEspEnabled() const noexcept { return m_bedEspEnabled; }
    [[nodiscard]] bool bedAutoRefreshEnabled() const noexcept { return m_bedAutoRefreshEnabled; }
    [[nodiscard]] bool espLabelsEnabled() const noexcept { return m_espLabelsEnabled; }
    [[nodiscard]] bool hypixelPanelEnabled() const noexcept { return m_hypixelPanelEnabled; }
    [[nodiscard]] bool hypixelPanelHoldToShow() const noexcept { return m_hypixelPanelHoldToShow; }
    [[nodiscard]] int hypixelPanelHotkey() const noexcept { return m_hypixelPanelHotkey; }
    [[nodiscard]] int hypixelPanelOpacity() const noexcept { return m_hypixelPanelOpacity; }
    [[nodiscard]] QString hypixelPanelColor() const { return m_hypixelPanelColor; }
    [[nodiscard]] QString hypixelRailColor() const { return m_hypixelRailColor; }
    [[nodiscard]] int hypixelRailOpacity() const noexcept { return m_hypixelRailOpacity; }
    [[nodiscard]] int hypixelPanelScale() const noexcept { return m_hypixelPanelScale; }
    [[nodiscard]] int hypixelPanelX() const noexcept { return m_hypixelPanelX; }
    [[nodiscard]] int hypixelPanelY() const noexcept { return m_hypixelPanelY; }
    [[nodiscard]] bool clickGuiLightTheme() const noexcept { return m_clickGuiLightTheme; }
    [[nodiscard]] bool bedThreatAlertsEnabled() const noexcept { return m_bedThreatAlertsEnabled; }
    [[nodiscard]] bool bedDefensePanelEnabled() const noexcept { return m_bedDefensePanelEnabled; }
    [[nodiscard]] bool bedEspFilled() const noexcept { return m_bedEspFilled; }
    [[nodiscard]] bool debugChatEnabled() const noexcept { return m_debugChatEnabled; }
    [[nodiscard]] bool showOwnBedDefenseInfo() const noexcept { return m_showOwnBedDefenseInfo; }
    [[nodiscard]] bool showTeammateBoxes() const noexcept { return m_showTeammateBoxes; }
    [[nodiscard]] bool showTeammateArrows() const noexcept { return m_showTeammateArrows; }
    [[nodiscard]] bool safewalkEnabled() const noexcept { return m_safewalkEnabled; }
    [[nodiscard]] int safewalkReleaseDelayMs() const noexcept { return m_safewalkReleaseDelayMs; }
    [[nodiscard]] int safewalkEdgeSensitivity() const noexcept { return m_safewalkEdgeSensitivity; }
    [[nodiscard]] int safewalkMinimumPitch() const noexcept { return m_safewalkMinimumPitch; }
    [[nodiscard]] int safewalkHotkey() const noexcept { return m_safewalkHotkey; }
    [[nodiscard]] bool scaffoldEnabled() const noexcept { return m_scaffoldEnabled; }
    [[nodiscard]] bool flyEnabled() const noexcept { return m_flyEnabled; }
    [[nodiscard]] int flySpeedPercent() const noexcept { return m_flySpeedPercent; }
    [[nodiscard]] bool bhopEnabled() const noexcept { return m_bhopEnabled; }
    [[nodiscard]] bool bhopAutoJump() const noexcept { return m_bhopAutoJump; }
    [[nodiscard]] int bhopAirSpeedPercent() const noexcept { return m_bhopAirSpeedPercent; }
    [[nodiscard]] bool fireballEspEnabled() const noexcept { return m_fireballEspEnabled; }
    [[nodiscard]] bool fireballEspFilled() const noexcept { return m_fireballEspFilled; }
    [[nodiscard]] QString fireballEspColor() const { return m_fireballEspColor; }
    [[nodiscard]] bool longJumpEnabled() const noexcept { return m_longJumpEnabled; }
    [[nodiscard]] int longJumpSpeedPercent() const noexcept { return m_longJumpSpeedPercent; }
    [[nodiscard]] bool aimAssistEnabled() const noexcept { return m_aimAssistEnabled; }
    [[nodiscard]] bool aimLockOnMode() const noexcept { return m_aimLockOnMode; }
    [[nodiscard]] bool aimSilentLock() const noexcept { return m_aimSilentLock; }
    [[nodiscard]] bool aimScannerEnabled() const noexcept { return m_aimScannerEnabled; }
    [[nodiscard]] bool aimAttackViability() const noexcept { return m_aimAttackViability; }
    [[nodiscard]] bool silentControlAdaptation() const noexcept { return m_silentControlAdaptation; }
    [[nodiscard]] bool bedBreakerEnabled() const noexcept { return m_bedBreakerEnabled; }
    [[nodiscard]] int aimSlowdownPercent() const noexcept { return m_aimSlowdownPercent; }
    [[nodiscard]] int aimSpeedPercent() const noexcept { return m_aimSpeedPercent; }
    [[nodiscard]] int aimMinimumDistance() const noexcept { return m_aimMinimumDistance; }
    [[nodiscard]] int aimMaximumDistance() const noexcept { return m_aimMaximumDistance; }
    [[nodiscard]] int aimFovDegrees() const noexcept { return m_aimFovDegrees; }
    [[nodiscard]] int aimAttackCps() const noexcept { return m_aimAttackCps; }
    [[nodiscard]] int clickGuiWidthPercent() const noexcept { return m_clickGuiWidthPercent; }
    [[nodiscard]] int clickGuiHeightPercent() const noexcept { return m_clickGuiHeightPercent; }
    [[nodiscard]] int clickGuiOpacity() const noexcept { return m_clickGuiOpacity; }
    [[nodiscard]] bool textGuiEnabled() const noexcept { return m_textGuiEnabled; }
    [[nodiscard]] bool textGuiShowModes() const noexcept { return m_textGuiShowModes; }
    [[nodiscard]] QString textGuiColor() const { return m_textGuiColor; }
    [[nodiscard]] QStringList textGuiModules() const;
    [[nodiscard]] int textGuiAlignment() const { return m_textGuiAlignment; }
    [[nodiscard]] bool textGuiVerticalLine() const { return (m_featureExtraBits & 0x02U)!=0; }
    [[nodiscard]] bool allowHypixelMovement() const noexcept { return m_allowHypixelMovement; }
    [[nodiscard]] bool bedDefenseHoldToShow() const noexcept { return m_bedDefenseHoldToShow; }
    [[nodiscard]] bool bedDefensePerspectiveScale() const noexcept { return m_bedDefensePerspectiveScale; }
    [[nodiscard]] int bedDefenseRadius() const noexcept { return m_bedDefenseRadius; }
    [[nodiscard]] int bedThreatRadius() const noexcept { return m_bedThreatRadius; }
    [[nodiscard]] int bedDefenseHotkey() const noexcept { return m_bedDefenseHotkey; }
    [[nodiscard]] int bedDefensePanelOpacity() const noexcept { return m_bedDefensePanelOpacity; }
    [[nodiscard]] QString playerEspColor() const { return m_playerEspColor; }
    [[nodiscard]] QString bedEspColor() const { return m_bedEspColor; }
    [[nodiscard]] QString bedDefensePanelColor() const { return m_bedDefensePanelColor; }
    [[nodiscard]] int menuHotkey() const noexcept { return m_menuHotkey; }
    [[nodiscard]] int guiScaleIndex() const noexcept { return m_guiScaleIndex; }
    [[nodiscard]] bool configAutoSave() const noexcept { return m_configAutoSave; }
    [[nodiscard]] QStringList configNames() const { return m_configNames; }
    [[nodiscard]] QString activeConfig() const { return m_activeConfig; }
    [[nodiscard]] quint32 targetPid() const noexcept { return m_targetPid; }
    [[nodiscard]] QString targetTitle() const { return m_targetTitle; }
    [[nodiscard]] QString renderer() const { return m_renderer; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] QString errorCode() const { return m_errorCode; }
    [[nodiscard]] QString errorDetail() const { return m_errorDetail; }
    [[nodiscard]] bool gameStateReceived() const noexcept { return m_gameStateReceived; }
    [[nodiscard]] bool gameStateAvailable() const noexcept { return m_gameStateAvailable; }
    [[nodiscard]] bool gameStateStale() const noexcept { return m_gameStateStale; }
    [[nodiscard]] double playerHealth() const noexcept { return m_playerHealth; }
    [[nodiscard]] double playerMaxHealth() const noexcept { return m_playerMaxHealth; }
    [[nodiscard]] int playerEntityId() const noexcept { return m_playerEntityId; }
    [[nodiscard]] double playerX() const noexcept { return m_playerX; }
    [[nodiscard]] double playerY() const noexcept { return m_playerY; }
    [[nodiscard]] double playerZ() const noexcept { return m_playerZ; }
    [[nodiscard]] int loadedEntities() const noexcept { return m_loadedEntities; }
    [[nodiscard]] int bedCount() const noexcept { return m_bedCount; }
    [[nodiscard]] QString mappingProfile() const { return m_mappingProfile; }
    [[nodiscard]] QString mappingState() const { return m_mappingState; }
    [[nodiscard]] qint64 gameStateTimestamp() const noexcept { return m_gameStateTimestamp; }
    [[nodiscard]] quint64 gameStateSequence() const noexcept { return m_gameStateSequence; }
    [[nodiscard]] QString playerName() const { return m_playerName; }
    [[nodiscard]] bool matchActive() const noexcept { return m_matchActive; }

    // Returns true when the asynchronous attach was started or queued behind
    // a graceful detach. The attached property becomes true only after an
    // authenticated handshake arrives from the DLL inside the target JVM.
    Q_INVOKABLE bool attachToProcess(quint32 pid);
    Q_INVOKABLE void detach();
    Q_INVOKABLE void refreshBedCache();
    Q_INVOKABLE bool saveConfig(const QString &name);
    Q_INVOKABLE bool applyConfig(const QString &name);
    Q_INVOKABLE bool removeConfig(const QString &name);

public slots:
    void setOverlayEnabled(bool enabled);
    void setInteractive(bool interactive);
    void setEspEnabled(bool enabled);
    void setEntityEspEnabled(bool enabled);
    void setEntityEspPlayersOnly(bool enabled);
    void setBedEspEnabled(bool enabled);
    void setBedAutoRefreshEnabled(bool enabled);
    void setEspLabelsEnabled(bool enabled);
    void setHypixelPanelEnabled(bool enabled);
    void setHypixelPanelHoldToShow(bool enabled);
    void setHypixelPanelHotkey(int virtualKey);
    void setHypixelPanelOpacity(int opacity);
    void setHypixelPanelColor(const QString &color);
    void setHypixelRailColor(const QString &color);
    void setHypixelRailOpacity(int opacity);
    void setHypixelPanelScale(int scale);
    void setHypixelPanelX(int normalizedX);
    void setHypixelPanelY(int normalizedY);
    void setClickGuiLightTheme(bool light);
    void setBedThreatAlertsEnabled(bool enabled);
    void setBedDefensePanelEnabled(bool enabled);
    void setBedEspFilled(bool enabled);
    void setDebugChatEnabled(bool enabled);
    void setShowOwnBedDefenseInfo(bool enabled);
    void setShowTeammateBoxes(bool enabled);
    void setShowTeammateArrows(bool enabled);
    void setSafewalkEnabled(bool enabled);
    void setSafewalkReleaseDelayMs(int delayMs);
    void setSafewalkEdgeSensitivity(int sensitivity);
    void setSafewalkMinimumPitch(int pitch);
    void setSafewalkHotkey(int virtualKey);
    void setScaffoldEnabled(bool enabled);
    void setFlyEnabled(bool enabled);
    void setFlySpeedPercent(int speed);
    void setBhopEnabled(bool enabled);
    void setBhopAutoJump(bool enabled);
    void setBhopAirSpeedPercent(int speed);
    void setFireballEspEnabled(bool enabled);
    void setFireballEspFilled(bool enabled);
    void setFireballEspColor(const QString &color);
    void setLongJumpEnabled(bool enabled);
    void setLongJumpSpeedPercent(int speed);
    void setAimAssistEnabled(bool enabled);
    void setAimLockOnMode(bool enabled);
    void setAimSilentLock(bool enabled);
    void setAimScannerEnabled(bool enabled);
    void setAimAttackViability(bool enabled);
    void setSilentControlAdaptation(bool enabled);
    void setAimSlowdownPercent(int coefficient);
    void setAimSpeedPercent(int speed);
    void setAimMinimumDistance(int distance);
    void setAimMaximumDistance(int distance);
    void setAimFovDegrees(int degrees);
    void setAimAttackCps(int cps);
    void setClickGuiWidthPercent(int percent);
    void setClickGuiHeightPercent(int percent);
    void setClickGuiOpacity(int opacity);
    void setTextGuiEnabled(bool enabled);
    void setTextGuiShowModes(bool enabled);
    void setTextGuiColor(const QString &color);
    void setAllowHypixelMovement(bool enabled);
    void setBedDefenseHoldToShow(bool enabled);
    void setBedDefensePerspectiveScale(bool enabled);
    void setBedDefenseRadius(int radius);
    void setBedThreatRadius(int radius);
    void setBedDefenseHotkey(int virtualKey);
    void setBedDefensePanelOpacity(int opacity);
    void setPlayerEspColor(const QString &color);
    void setBedEspColor(const QString &color);
    void setBedDefensePanelColor(const QString &color);
    void setMenuHotkey(int virtualKey);
    void setGuiScaleIndex(int index);
    void setConfigAutoSave(bool enabled);
    void sendBlacklistCommand(const QByteArray &command);
    void publishHypixelResult(int state, const QString &uuid, const QString &displayName,
                              qint64 wins, qint64 losses, qint64 finalKills,
                              qint64 finalDeaths, qint64 bedsBroken, qint64 bedsLost,
                              double winRate, double fkdr, const QString &status);
    void publishPlayerStats(const QString &playerName, const QString &teamPrefix,
                            int stars, double fkdr, double wlr, double bblr,
                            qint64 wins, qint64 finalKills, qint64 bedsBroken,
                            int winStreak, int level);
    void publishPlayerStatsError(const QString &playerName, const QString &reason);
    void publishMediaState(bool available, bool playing, const QString &title,
                           const QString &artist, const QString &source,
                           const QString &coverPath, qint64 positionMs,
                           qint64 durationMs);
    void publishMediaSpectrum(const QByteArray &bands);

signals:
    void stateChanged();
    void attachedChanged();
    void busyChanged();
    void overlayEnabledChanged();
    void interactiveChanged();
    void featureSettingsChanged();
    void hypixelQueryRequested(const QString &playerId);
    void playerFound(const QString &playerName, const QString &teamPrefix);
    void playerIdentityFound(const QString &playerName, const QString &teamPrefix,
                             const QString &uuid);
    void blacklistAddRequested(const QString &playerName, const QString &uuid,
                               const QString &reason, bool idOnlyNick,
                               bool warnOnEncounter);
    void blacklistRemoveRequested(const QString &key);
    void blacklistWarningRequested(const QString &key, bool enabled);
    void blacklistLayoutChanged(int x, int y, int width, int height);
    void blacklistSettingsChanged(bool panelEnabled, bool matchAlertsEnabled,
                                  bool allowIdOnlyNicks, bool showWithClickGui,
                                  bool collapsed, int opacity, int contentScale,
                                  const QString &color);
    void matchStateChanged(bool active);
    void playerStatusChanged();
    void menuHotkeyChanged();
    void guiScaleIndexChanged();
    void configStateChanged();
    void mediaPreviousRequested();
    void mediaNextRequested();
    void mediaToggleRequested();
    void agentSessionReady();
    void targetChanged();
    void rendererChanged();
    void statusMessageChanged();
    void errorChanged();
    void gameStateChanged();
    void targetExited(quint32 pid);

private:
    friend struct ControllerResponsivenessTests;
    enum class LoaderKind {
        None,
        JvmAttach,
        NativeLoadLibrary
    };

    struct JavaRuntime {
        QString executable;
        QString toolsJar;
        bool modular = true;
    };

    void acceptAgentConnection();
    void readAgentMessages();
    void handleAgentDisconnected();
    void handleAttachFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleAttachError(QProcess::ProcessError error);
    void monitorTarget();
    void beginDetach();
    void completeDetach(bool timedOut);
    void finalizeDetachedState();
    void startPendingAttach();

    [[nodiscard]] QString locateAgentDll() const;
    [[nodiscard]] QString locateAttachHelper() const;
    [[nodiscard]] QString locateNativeLoader() const;
    [[nodiscard]] JavaRuntime locateJavaRuntime(const QString &targetExecutable) const;
    [[nodiscard]] QString targetExecutablePath(quint32 pid) const;
    [[nodiscard]] QString targetWindowTitle(quint32 pid) const;
    [[nodiscard]] bool targetArchitectureSupported(quint32 pid) const;
    [[nodiscard]] bool targetProcessIsRunning(quint32 pid) const;
    [[nodiscard]] bool targetHasLoadedJvm(quint32 pid) const;
    [[nodiscard]] bool targetHasLoadedOverlayAgent(quint32 pid) const;
    [[nodiscard]] static bool isRecoverableJvmAttachFailure(
        int exitCode, const QByteArray &standardError) noexcept;
    [[nodiscard]] bool startNativeLoaderFallback();

    // Authenticated Agent -> Controller telemetry protocol (v1):
    //
    // GAME_STATE 1 <sequence> <unix-ms> <valid-0-or-1>
    //            <hp> <max-hp> <entity-id> <x> <y> <z>
    //            <loaded-entities> <bed-count> <mapping-pct> <state-pct>
    //
    // The two text fields are percent-encoded UTF-8 tokens (`-` is empty).
    // Numeric placeholders remain mandatory when valid=0, allowing states
    // such as resolving/unsupported/no_player without a second grammar.
    // Sequence numbers must increase within an authenticated pipe session.
    void processAgentLine(const QByteArray &line);
    void sendStateSnapshot();
    void sendFeatureSnapshot();
    void sendBindSnapshot();
    void sendGuiScaleSnapshot();
    void sendMediaSettings();
    void loadFeatureSettings();
    void storeFeatureSettings();
    void flushFeatureSettings();
    void writeAgentCommand(const QByteArray &command);
    // Closes IPC and asks a still-running helper to terminate. This function
    // is deliberately non-blocking; QProcess::finished completes any queued
    // re-attach after the old helper has actually exited.
    [[nodiscard]] bool closeSessionTransport();
    void setState(State state);
    void setStatusMessage(const QString &message);
    void setRenderer(const QString &renderer);
    void resetGameState();
    void refreshGameStateFreshness();
    void clearError();
    void setErrorState(const QString &code, const QString &detail);
    void fail(const QString &code, const QString &detail);

    QLocalServer m_server;
    QPointer<QLocalSocket> m_agentSocket;
    QProcess m_attachProcess;
    QTimer m_attachTimeout;
    // Forge 1.8.9 can report a recoverable Attach error while the asynchronous
    // Agent_OnAttach bootstrap is already running. This grace period lets that
    // first session authenticate before starting the visible DLL fallback.
    QTimer m_nativeFallbackGrace;
    QTimer m_detachTimeout;
    QTimer m_targetMonitor;
    QTimer m_gameStateFreshnessTimer;
    QTimer m_featureSettingsStoreTimer;
    QElapsedTimer m_gameStateReceiptClock;
    QByteArray m_agentReadBuffer;
    bool m_agentReadScheduled = false;
    QByteArray m_helperStandardOutput;
    QByteArray m_helperStandardError;
    QString m_jvmAttachFallbackReason;
    QString m_pipeToken;
    quint32 m_targetPid = 0;
    QString m_targetTitle;
    QString m_renderer;
    QString m_statusMessage = QStringLiteral("Native overlay is detached");
    QString m_errorCode;
    QString m_errorDetail;
    QString m_agentDllPath;
    QString m_agentOptions;
    State m_state = State::Detached;
    LoaderKind m_loaderKind = LoaderKind::None;
    bool m_overlayEnabled = true;
    bool m_interactive = false;
    bool m_espEnabled = true;
    bool m_entityEspEnabled = true;
    bool m_entityEspPlayersOnly = false;
    bool m_bedEspEnabled = true;
    bool m_bedAutoRefreshEnabled = false;
    bool m_espLabelsEnabled = true;
    bool m_hypixelPanelEnabled = true;
    bool m_hypixelPanelHoldToShow = true;
    int m_hypixelPanelHotkey = 0x09; // VK_TAB
    int m_hypixelPanelOpacity = 76;
    QString m_hypixelPanelColor = QStringLiteral("#000000");
    QString m_hypixelRailColor = QStringLiteral("#825DE8");
    int m_hypixelRailOpacity = 100;
    int m_hypixelPanelScale = 100;
    int m_hypixelPanelHeight = 100;
    int m_hypixelPanelX = -1;
    int m_hypixelPanelY = -1;
    int m_hypixelPanelFontIndex = 1;
    bool m_clickGuiLightTheme = false;
    bool m_nametagEnabled = true;
    bool m_nametagSidePlacement = false;
    bool m_enemyItemIndicatorsEnabled = true;
    bool m_showTeammateNametags = true;
    bool m_nametagNearbyEnemiesOnly = false;
    bool m_nametagTeamPulse = true;
    int m_nametagRange = 32;
    int m_nametagSizeIndex = 1;
    int m_nametagPanelOpacity = 82;
    QString m_nametagPanelColor = QStringLiteral("#101218");
    QString m_clickGuiAccentColor = QStringLiteral("#825DE8");
    bool m_bedThreatAlertsEnabled = true;
    bool m_bedDefensePanelEnabled = true;
    bool m_bedEspFilled = false;
    bool m_debugChatEnabled = true;
    bool m_showOwnBedDefenseInfo = true;
    bool m_showTeammateBoxes = true;
    bool m_showTeammateArrows = true;
    bool m_safewalkEnabled = false;
    int m_safewalkReleaseDelayMs = 120;
    int m_safewalkEdgeSensitivity = 55;
    int m_safewalkMinimumPitch = -5;
    int m_safewalkHotkey = 0x77; // VK_F8
    bool m_scaffoldEnabled = false;
    bool m_flyEnabled = false;
    int m_flySpeedPercent = 100;
    bool m_bhopEnabled = false;
    bool m_bhopAutoJump = true;
    int m_bhopAirSpeedPercent = 100;
    quint64 m_featureHotkeysPackedA = 0U;
    quint64 m_featureHotkeysPackedB = 0U;
    // v39 extension: byte 0 mirrors Velocity, byte 1 is the FreeLook hold
    // binding, and bit 16 stores the FreeLook enabled state.
    quint32 m_featureHotkeysPackedC = 0xA400U;
    bool m_fireballEspEnabled = false;
    bool m_fireballEspFilled = true;
    QString m_fireballEspColor = QStringLiteral("#FF9D3D");
    bool m_longJumpEnabled = false;
    int m_longJumpSpeedPercent = 100;
    bool m_aimAssistEnabled = false;
    bool m_aimLockOnMode = false;
    bool m_aimSilentLock = false;
    bool m_silentFileDebug = false;
    bool m_silentChatDebug = false;
    bool m_aimScannerEnabled = true;
    bool m_aimAttackViability = true;
    bool m_silentControlAdaptation = false;
    bool m_aimSequentialTargets = false;
    bool m_bedBreakerEnabled = false;
    int m_aimSlowdownPercent = 45;
    int m_aimSpeedPercent = 35;
    int m_aimMinimumDistance = 0;
    int m_aimMaximumDistance = 16;
    int m_aimFovDegrees = 90;
    int m_aimAttackCps = 10;
    int m_clickGuiWidthPercent = 100;
    int m_clickGuiHeightPercent = 100;
    int m_clickGuiOpacity = 96;
    quint32 m_featureExtraBits = 0x43U;
    quint32 m_smartHotbarConfig = 0U;
    int m_textGuiAlignment = 2;
    int m_localMobReach = 4;
    int m_localAttackDelayMs = 500;
    int m_localVelocityPercent = 100;
    int m_localVelocityProbability = 100;
    int m_localVelocityVerticalPercent = 100;
    int m_velocityHotkey = 0;
    bool m_textGuiEnabled = false;
    bool m_textGuiShowModes = false;
    QString m_textGuiColor = QStringLiteral("#7EE7FF");
    int m_textGuiX = -1;
    int m_textGuiY = -1;
    bool m_allowHypixelMovement = false;
    bool m_bedDefenseHoldToShow = true;
    bool m_bedDefensePerspectiveScale = false;
    int m_bedDefenseRadius = 6;
    int m_bedThreatRadius = 8;
    int m_bedDefenseHotkey = 0xA4; // VK_LMENU
    int m_bedDefensePanelOpacity = 78;
    QString m_playerEspColor = QStringLiteral("#FF3B30");
    QString m_bedEspColor = QStringLiteral("#FF5C68");
    QString m_bedDefensePanelColor = QStringLiteral("#191621");
    int m_menuHotkey = 0xDE; // VK_OEM_7 / apostrophe
    int m_guiScaleIndex = 1; // S/M/L/XL -> 0..3
    bool m_configAutoSave = false;
    QStringList m_configNames;
    QString m_activeConfig;
    bool m_authenticated = false;
    bool m_closingTransport = false;
    bool m_nativeFallbackAttempted = false;
    bool m_detachTransportComplete = false;
    bool m_detachTimedOut = false;
    bool m_destroying = false;
    quint32 m_pendingAttachPid = 0;

    bool m_gameStateReceived = false;
    bool m_gameStateAvailable = false;
    bool m_gameStateStale = false;
    double m_playerHealth = 0.0;
    double m_playerMaxHealth = 0.0;
    int m_playerEntityId = 0;
    double m_playerX = 0.0;
    double m_playerY = 0.0;
    double m_playerZ = 0.0;
    int m_loadedEntities = 0;
    int m_bedCount = 0;
    QString m_mappingProfile;
    QString m_mappingState = QStringLiteral("waiting");
    qint64 m_gameStateTimestamp = 0;
    qint64 m_lastGameStateReceiptElapsedMs = 0;
    quint64 m_gameStateSequence = 0;
    QString m_playerName;
    bool m_matchActive = false;
};
