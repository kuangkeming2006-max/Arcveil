#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Value-only settings/snapshots shared by the native agent and GUI.
// Hotkey integers retain the existing Windows virtual-key wire encoding.
namespace mcoverlay {

struct FeatureSettings final {
    static constexpr std::size_t FeatureHotkeyCount = 18U;
    bool espEnabled = true;
    bool entityEspEnabled = true;
    bool entityEspPlayersOnly = false;
    bool bedEspEnabled = true;
    bool bedAutoRefreshEnabled = false;
    bool labelsEnabled = true;
    bool hypixelPanelEnabled = true;
    bool bedThreatAlertsEnabled = true;
    bool bedDefensePanelEnabled = true;
    bool bedEspFilled = false;
    bool debugChatEnabled = true;
    bool showOwnBedDefenseInfo = true;
    bool showTeammateBoxes = true;
    bool bedDefenseHoldToShow = true;
    bool bedDefensePerspectiveScale = false;
    bool hypixelPanelHoldToShow = true;
    bool clickGuiLightTheme = false;
    bool nametagEnabled = true;
    bool nametagSidePlacement = false;
    bool enemyItemIndicatorsEnabled = true;
    bool showTeammateNametags = true;
    bool nametagNearbyEnemiesOnly = false;
    bool nametagTeamPulse = true;
    bool showTeammateArrows = true;
    bool safewalkEnabled = false;
    bool scaffoldEnabled = false;
    bool scaffoldSameLayerOnly = true;
    bool flyEnabled = false;
    bool bhopEnabled = false;
    bool bhopAutoJump = true;
    bool aimAssistEnabled = false;
    bool aimLockOnMode = false;
    bool aimSilentLock = false;
    bool silentFileDebug = false;
    bool silentChatDebug = false;
    bool aimScannerEnabled = true;
    bool aimAttackViability = true;
    // Independent opt-in for remapping vanilla movement/jump/sprint while
    // Silent Lock owns the logical rotation. Attack-ray policy must never
    // implicitly enable or disable this behaviour.
    bool silentControlAdaptation = false;
    // Rotates through attack-ready targets instead of waiting on one target's
    // hurt/click cooldown. One physical click still emits at most one attack.
    bool aimSequentialTargets = false;
    // Integrated-single-player only. The binding layer repeats this guard.
    bool bedBreakerEnabled = false;
    bool aimNearestPriority = true;
    bool textGuiEnabled = false;
    bool textGuiVerticalLine = true;
    bool textGuiShowModes = false;
    bool knockbackPredictionEnabled = false;
    bool bowPredictionEnabled = false;
    // These controls are enforced again inside GameBindings. They can never
    // execute unless Minecraft owns an integrated single-player server, and
    // the attack helper only accepts a living non-player hostile candidate.
    bool localMobAuraEnabled = false;
    bool localVelocityEnabled = false;
    bool fullscreenImeFixEnabled = false;
    bool fireballEspEnabled = false;
    bool fireballEspFilled = true;
    bool longJumpEnabled = false;
    bool freeLookEnabled = false;
    bool smartHotbarEnabled = false;
    bool smartHotbarRefill = false;
    bool sprintEnabled = false;
    bool nametagAlways = false;
    bool attackShieldEnabled = false;
    // The wildcard is safe to persist because it has no world/entity identity.
    // Specific UUID/entity selections remain tied to the current session.
    bool attackShieldWildcard = false;
    // High-risk movement helpers fail closed on Hypixel. This explicit,
    // persisted opt-in is intentionally separate from each feature switch so
    // an accidental hotkey press can never silently override the server guard.
    bool allowHypixelMovement = false;
    int bedDefenseRadius = 6;
    int bedThreatRadius = 8;
    int bedDefenseHotkey = 0xA4;
    int bedDefensePanelOpacity = 78;
    int hypixelPanelHotkey = 0x09;
    int hypixelPanelOpacity = 76;
    // The statistics card has an independent scale and normalized top-left
    // position so it remains usable when the Minecraft resolution changes.
    int hypixelPanelScale = 100;
    // Width and height are intentionally independent.  The legacy "scale"
    // value now controls width only; glyphs/row pitch remain fixed and crisp.
    int hypixelPanelHeight = 100;
    int hypixelPanelX = -1; // -1 = default right aligned; otherwise 0..1000
    int hypixelPanelY = -1; // -1 = default top aligned; otherwise 0..1000
    // Independent font preset (15/19/23/28 px). Panel resizing intentionally
    // does not scale glyphs, so rows remain readable and never overlap.
    int hypixelPanelFontIndex = 1;
    int nametagRange = 32;
    int nametagSizeIndex = 1;
    int safewalkReleaseDelayMs = 120;
    int safewalkEdgeSensitivity = 55;
    int safewalkMinimumPitch = -5;
    int safewalkHotkey = 0x77;
    int flySpeedPercent = 100;
    int bhopAirSpeedPercent = 100;
    int longJumpSpeedPercent = 100;
    int aimSlowdownPercent = 45; // Reserved legacy wire slot; no sensitivity modification.
    int aimSpeedPercent = 35;
    int aimMinimumDistance = 0;
    int aimMaximumDistance = 16;
    int aimFovDegrees = 90;
    int aimAttackCps = 10;
    int textGuiAlignment = 2; // 0=left, 1=center, 2=right
    int localMobReach = 4;
    int localAttackDelayMs = 500;
    int localVelocityPercent = 100;
    int localVelocityProbability = 100;
    int localVelocityVerticalPercent = 100;
    int clickGuiWidthPercent = 100;
    int clickGuiHeightPercent = 100;
    int clickGuiOpacity = 96;
    int clickGuiBlur = 65;
    int imePanelX = -1;
    int imePanelY = -1;
    int textGuiX = -1;
    int textGuiY = -1;
    // Stored as 0xRRGGBB so the value is renderer-independent and can travel
    // through the text IPC protocol without floating-point round trips.
    std::uint32_t playerEspColor = 0xFF3B30U;
    std::uint32_t bedEspColor = 0xFF5C68U;
    std::uint32_t bedDefensePanelColor = 0x191621U;
    std::uint32_t hypixelPanelColor = 0x000000U;
    std::uint32_t hypixelRailColor = 0x825DE8U;
    std::uint32_t nametagPanelColor = 0x101218U;
    std::uint32_t clickGuiAccentColor = 0x825DE8U;
    std::uint32_t textGuiColor = 0x7EE7FFU;
    std::uint32_t fireballEspColor = 0xFF9D3DU;
    int nametagPanelOpacity = 82;
    int hypixelRailOpacity = 100;
    // Page master hotkeys in navigation order, excluding Interface. Zero is
    // deliberately "Unbound"; configured keys are persisted by Controller.
    std::array<int, FeatureHotkeyCount> featureHotkeys{};
    // Each logical Minecraft hotbar binding can become a category shortcut:
    // 0=None, 1=Sword, 2=Blocks. The physical key remains owned by Minecraft.
    std::array<int, 9U> smartHotbarActions{};

    [[nodiscard]] bool operator==(const FeatureSettings&) const noexcept = default;
};

struct HypixelOverlaySnapshot final {
    enum class State : std::uint8_t { Idle, Loading, Ready, Error };
    State state = State::Idle;
    std::array<char, 40U> uuid{};
    std::array<char, 48U> displayName{};
    std::array<char, 160U> status{};
    std::int64_t wins = 0;
    std::int64_t losses = 0;
    std::int64_t finalKills = 0;
    std::int64_t finalDeaths = 0;
    std::int64_t bedsBroken = 0;
    std::int64_t bedsLost = 0;
    double winRate = 0.0;
    double fkdr = 0.0;
};

struct PlayerStatsEntry final {
    std::array<char, 17U> name{};
    std::array<char, 5U> teamPrefix{};
    std::array<char, 97U> status{};
    std::int32_t stars = 0;
    double fkdr = 0.0;
    double wlr = 0.0;
    double bblr = 0.0;
    std::int64_t wins = 0;
    std::int64_t finalKills = 0;
    std::int64_t bedsBroken = 0;
    std::int32_t winStreak = 0;
    std::int32_t level = 0;
    bool failed = false;
};

struct PlayerStatsOverlaySnapshot final {
    static constexpr std::size_t Capacity = 64U;
    std::array<PlayerStatsEntry, Capacity> entries{};
    std::uint32_t count = 0U;
};

struct BlacklistEntry final {
    std::array<char, 50U> key{};
    std::array<char, 37U> uuid{};
    std::array<char, 17U> name{};
    std::array<char, 161U> reason{};
    std::array<char, 260U> facePath{};
    std::int64_t addedAt = 0;
    bool nick = false;
    bool idOnly = false;
    bool warnOnEncounter = true;
};

struct BlacklistOverlaySnapshot final {
    static constexpr std::size_t Capacity = 128U;
    static constexpr std::size_t PresetCapacity = 8U;
    std::array<BlacklistEntry, Capacity> entries{};
    std::uint32_t count = 0U;
    std::array<std::array<char, 81U>, PresetCapacity> presets{};
    std::uint32_t presetCount = 0U;
    bool panelEnabled = true;
    bool matchAlertsEnabled = true;
    bool allowIdOnlyNicks = true;
    bool showWithClickGui = true;
    bool collapsed = false;
    int panelOpacity = 82;
    int contentScale = 100;
    std::uint32_t panelColor = 0x111218U;
    int panelX = -1;
    int panelY = -1;
    int panelWidth = 100;
    int panelHeight = 100;
};

struct BlacklistAction final {
    enum class Type : std::uint8_t { None, Add, Remove, Warning, Layout, Settings };
    Type type = Type::None;
    std::array<char, 50U> key{};
    std::array<char, 37U> uuid{};
    std::array<char, 17U> name{};
    std::array<char, 161U> reason{};
    bool idOnlyNick = false;
    bool warnOnEncounter = true;
    int x = -1;
    int y = -1;
    int width = 100;
    int height = 100;
    bool panelEnabled = true;
    bool matchAlertsEnabled = true;
    bool allowIdOnlyNicks = true;
    bool showWithClickGui = true;
    bool collapsed = false;
    int panelOpacity = 82;
    int contentScale = 100;
    std::uint32_t panelColor = 0x111218U;
};

struct MediaOverlaySettings final {
    bool enabled = true;
    int opacity = 58;
    int spectrumOpacity = 100;
    // Continuous whole-card scale. Every internal coordinate is derived from
    // the same design-space multiplier so typography and controls stay aligned.
    int scalePercent = 52;
    int previousHotkey = 0xB1;
    int toggleHotkey = 0xB3;
    int nextHotkey = 0xB0;
    std::uint32_t panelColor = 0x857F82U;
    int panelX = -1;
    int panelY = -1;
    [[nodiscard]] bool operator==(const MediaOverlaySettings&) const noexcept = default;
};

struct MediaPlaybackSnapshot final {
    bool available = false;
    bool playing = false;
    std::array<char, 192U> title{};
    std::array<char, 160U> artist{};
    std::array<char, 160U> source{};
    std::array<char, 512U> coverPath{};
    std::int64_t positionMs = 0;
    std::int64_t durationMs = 0;
    std::uint64_t receivedAtMs = 0U;
    std::array<float, 10U> spectrum{};
};

enum class MediaAction : std::uint8_t { None, Previous, Toggle, Next };


} // namespace mcoverlay
