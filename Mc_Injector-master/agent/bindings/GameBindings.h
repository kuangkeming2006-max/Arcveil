#pragma once

#include <jni.h>
#include <jvmti.h>
#include <windows.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "BedWarsState.h"
#include "MappingProvider.h"
#include "AimControl.h"
#include "SilentLockCoordinator.h"
#include "LiveInteractionTransform.h"
#include "LiveAttackTransform.h"
#include "LiveHotbarTransform.h"
#include "LiveImpulseTransform.h"
#include "LiveInteractionObserver.h"
#include "LiveMovementTransform.h"
#include "LiveJumpTransform.h"
#include "LivePacketTransform.h"
#include "LiveFreeLookTransform.h"
#include "FreeLookDiagnostics.h"
#include "KnockbackEvidence.h"

namespace mcoverlay {

struct AxisAlignedBox final {
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double maxZ = 0.0;
};

struct WorldPoint final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct EntityMarker final {
    AxisAlignedBox bounds{};
    double previousX = 0.0;
    double previousY = 0.0;
    double previousZ = 0.0;
    double currentX = 0.0;
    double currentY = 0.0;
    double currentZ = 0.0;
    double motionX = 0.0;
    double motionY = 0.0;
    double motionZ = 0.0;
    jint entityId = -1;
    float health = 0.0F;
    float maxHealth = 0.0F;
    int hurtTime = -1;
    bool groundKnown = false;
    double distance = 0.0;
    bool player = false;
    bool hostile = false;
    bool onGround = false;
    bool invisible = false;
    bool hasArmor = false;
    std::uint8_t protectionLevel = 0U;
    std::int16_t heldItemId = -1;
    std::uint8_t heldItemCount = 0U;
    std::uint16_t heldItemDamage = 0U;
    char armorTeam = 'u';
    char teamColor = 'u';
    std::array<char, 17U> playerName{};
    std::array<char, 65U> displayName{}; // UTF-8 offline/custom-server nickname.
    std::array<char, 37U> uuid{};
    // True only after this spawned entity was joined to the persistent TAB
    // roster (UUID first, exact name as a compatibility fallback).  Renderers
    // use this to exclude shop NPCs and other player-shaped entities.
    bool confirmedPlayer = false;
    bool fireball = false;
    // OpenGL texture name owned by Minecraft's TextureManager in the same
    // context used by the SwapBuffers hook.  The agent never deletes it.
    std::uint32_t skinTextureId = 0U;
};

struct KnockbackTrajectory final {
    static constexpr std::size_t MaxPoints = 48U;
    jint entityId = -1;
    AxisAlignedBox startBounds{};
    std::array<WorldPoint, MaxPoints> points{};
    std::uint8_t pointCount = 0U;
    bool landed = false;
};

struct BowTrajectory final {
    static constexpr std::size_t MaxPoints = 96U;
    std::array<WorldPoint, MaxPoints> points{};
    std::uint8_t pointCount = 0U;
    WorldPoint impact{};
    jint impactEntityId = -1;
    bool hasImpact = false;
    bool impactPlayer = false;
    bool impactLiving = false;
    bool budgetLimited = false;
    bool active = false;
};

struct BedDefenseBlock final {
    static constexpr std::size_t MaxRadius = 10U;
    std::uint16_t blockId = 0U;
    std::uint8_t metadata = 0U;
    // Index is the exact Chebyshev ring (1..10). The renderer sums rings up
    // to the user-selected radius, so changing 3..10 never triggers JNI work.
    std::array<std::uint16_t, MaxRadius + 1U> ringCounts{};
};

struct BedMarker final {
    static constexpr std::size_t MaxDefenseBlocks = 12U;
    int x = 0;
    int y = 0;
    int z = 0;
    int footX = 0;
    int footZ = 0;
    std::array<BedDefenseBlock, MaxDefenseBlocks> defense{};
    std::uint8_t defenseCount = 0U;
    // Derived only from nearby, bulk-copied team-coloured wool evidence. An
    // ambiguous/absent result remains unknown; the threat detector must never
    // guess an own bed from player proximity.
    char teamColor = 'u';
};

struct PlayerIdentity final {
    std::array<char, 17U> name{};
    std::array<char, 37U> uuid{};
    // Minecraft formatting color code without the section-sign prefix.
    // For example 'c' represents the protocol token "\xC2\xA7c".
    char teamColor = 'u';
};

struct WorldCameraSnapshot final {
    std::array<float, 16U> modelView{};
    std::array<float, 16U> projection{};
    std::array<int, 4U> viewport{};
    double renderX = 0.0;
    double renderY = 0.0;
    double renderZ = 0.0;
    float partialTicks = 0.0F;
    bool valid = false;
};

struct GameSnapshot final {
    static constexpr std::size_t MaxEntityMarkers = 128U;
    static constexpr std::size_t MaxBedMarkers = 128U;
    static constexpr std::size_t MaxDiscoveredPlayers = 64U;
    static constexpr std::size_t MaxKnockbackTrajectories = 12U;
    enum class State : std::uint8_t {
        Resolving,
        Unsupported,
        WaitingForGameThread,
        NoPlayer,
        Ready,
        JniError
    };

    State state = State::Resolving;
    float health = 0.0F;
    float maxHealth = 0.0F;
    jint entityId = -1;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    AxisAlignedBox bounds{};
    jint loadedEntities = 0;
    bool singlePlayer = false;
    bool integratedSinglePlayer = false;
    bool hypixelServer = false;
    bool gameScreenOpen = true;
    std::array<EntityMarker, MaxEntityMarkers> entityMarkers{};
    std::uint32_t entityMarkerCount = 0U;
    // Incremented only when the 20 Hz JNI entity snapshot is refreshed. The
    // renderer combines this with renderPartialTicks to detect a game-tick
    // boundary that happened between two snapshots and briefly extrapolate it.
    std::uint64_t entitySampleGeneration = 0U;
    std::uint64_t worldGeneration = 0U;
    double entityRenderTick = 0.0;
    std::array<KnockbackTrajectory, MaxKnockbackTrajectories>
        knockbackTrajectories{};
    std::uint8_t knockbackTrajectoryCount = 0U;
    bool knockbackHurtAvailable = false;
    std::uint32_t knockbackDamageEvents = 0, knockbackImpulseEvents = 0,
                  knockbackConfirmedEvents = 0;
    BowTrajectory bowTrajectory{};
    std::array<BedMarker, MaxBedMarkers> bedMarkers{};
    std::uint32_t bedMarkerCount = 0U;
    std::uint32_t bedCount = 0U;
    float bedScanProgress = 0.0F;
    std::array<PlayerIdentity, MaxDiscoveredPlayers> players{};
    std::uint32_t playerCount = 0U;
    std::uint64_t playerRosterGeneration = 0U;
    std::array<char, 17U> localPlayerName{};
    // True only after two consecutive snapshots agree on a local team. An
    // explicit [R]/[B]/... roster tag is the primary signal; a complete
    // Sidebar snapshot is accepted as corroborating evidence.
    bool matchActive = false;
    char ownTeam = 'u';
    enum class OwnBedSource : std::uint8_t { Unknown, TeamWool, MatchSpawn };
    bool ownBedKnown = false;
    int ownBedX = 0;
    int ownBedY = 0;
    int ownBedZ = 0;
    OwnBedSource ownBedSource = OwnBedSource::Unknown;
    WorldCameraSnapshot camera{};
    jint aimTargetEntityId = -1;
    jint aimAttackTargetEntityId = -1;
    bool silentAimAvailable = false;
    std::uint32_t mappingAttempt = 0U;
    std::uint32_t mappingRetryInMs = 0U;
    const char* mapping = "unresolved";
};

struct GameplaySettings final {
    bool safewalk = false;
    bool scaffold = false;
    bool scaffoldSameLayerOnly = true;
    bool fly = false;
    bool bhop = false;
    bool bhopAutoJump = true;
    bool aimAssist = false;
    bool longJump = false;
    bool aimLockOnMode = false;
    bool aimSilentLock = false;
    bool silentFileDebug = false;
    bool silentChatDebug = false;
    bool aimAttackViability = true;
    bool silentControlAdaptation = false;
    bool aimSequentialTargets = false;
    bool aimNearestPriority = true;
    bool bedBreaker = false;
    bool localMobAura = false;
    bool localVelocity = false;
    // Keep the user's UI request separate from runtime gating so FreeLook's
    // always-on diagnostic trace can explain why an enabled toggle did not
    // become an active camera request.
    bool freeLookConfigured = false;
    bool freeLookGuiOpen = false;
    bool freeLookForeground = false;
    bool freeLook = false;
    bool smartHotbar = false;
    bool smartHotbarRefill = false;
    bool forceSprint = false;
    int shieldAttackerId = -1;
    int freeLookHotkey = VK_LMENU;
    std::array<int, 9U> smartHotbarActions{};
    int safewalkReleaseDelayMs = 120;
    int safewalkEdgeSensitivity = 55;
    int safewalkMinimumPitch = -5;
    int flySpeedPercent = 100;
    int bhopAirSpeedPercent = 100;
    int aimSlowdownPercent = 45; // Legacy transport compatibility only.
    int aimSpeedPercent = 35;
    int aimAttackCps = 10;
    int aimMinimumDistance = 0;
    int aimMaximumDistance = 16;
    int aimFovDegrees = 90;
    int longJumpSpeedPercent = 100;
    int localMobReach = 4;
    int localAttackDelayMs = 500;
    int localVelocityPercent = 100;
    int localVelocityProbability = 100;
    int localVelocityVerticalPercent = 100;
};

// Minecraft 1.8.9-only JNI binding cache. Only jclass global references and
// method/field IDs survive a frame. Minecraft/player/AABB object references
// are always local to sample() and are discarded before SwapBuffers returns.
class GameBindings final {
public:
    GameBindings(JavaVM* vm, jvmtiEnv* jvmti) noexcept;
    ~GameBindings();

    GameBindings(const GameBindings&) = delete;
    GameBindings& operator=(const GameBindings&) = delete;

    // Runtime mapping packs are converted to this owned, validated form by an
    // external parser and must be registered before runResolver starts. This
    // keeps file/JSON parsing out of the injected render path and provides the
    // transformed-client mapping integration point without guessing names.
    [[nodiscard]] bindings::MappingRegistrationResult registerMappingDictionary(
        bindings::MappingDictionary dictionary,
        std::string* error = nullptr) noexcept;

    // The resolver is called exactly once from AgentRuntime's dedicated native
    // daemon thread. It owns every JVMTI class-table walk and all ID lookups;
    // neither resolve() nor GetLoadedClasses can therefore reach SwapBuffers.
    // One snapshot is used because repeated GetLoadedClasses calls can force
    // global JVM safepoints even from a background thread.
    void runResolver(JNIEnv* env, HANDLE stopEvent) noexcept;
    // Dedicated JNI daemon. Every 500 ms it snapshots the loaded chunk list,
    // bulk-copies only newly seen section char[] arrays, and publishes a
    // fixed-size bed cache. It never executes from SwapBuffers.
    void runBedScanner(JNIEnv* env, HANDLE stopEvent) noexcept;
    // Invalidates the processed-chunk set. The scanner then bulk-copies every
    // currently loaded chunk on its next cycle, discovering beds placed after
    // initial chunk load without moving any block reads onto the render thread.
    void requestBedRescan() noexcept;
    void markResolverUnavailable() noexcept;

    // Rendering owns m_snapshot. Resolver progress is kept in atomics and is
    // folded into a by-value copy, so the resolver never writes render-owned
    // memory. The immutable cache is published with release/acquire ordering.
    [[nodiscard]] GameSnapshot snapshot(std::uint64_t tickMilliseconds) const noexcept;
    // Render-thread-only cheap state read; avoid copying the entire entity /
    // trajectory snapshot just to decide whether title-menu input is allowed.
    [[nodiscard]] bool hasPlayerSnapshot() const noexcept {
        return m_snapshot.state == GameSnapshot::State::Ready;
    }
    [[nodiscard]] bool gameScreenOpen(JNIEnv* env) noexcept;
    [[nodiscard]] const GameSnapshot& sample(JNIEnv* env, std::uint64_t tickMilliseconds) noexcept;
    // ActiveRenderInfo is refreshed by Minecraft during every 3D world pass.
    // This lightweight copy intentionally runs once per SwapBuffers frame so
    // yaw/pitch, FOV and view-bobbing never inherit the 10 Hz telemetry limit.
    void sampleCamera(JNIEnv* env) noexcept;
    void sampleBow(JNIEnv* env, GameSnapshot& snapshot, bool enabled) noexcept;
    jobject serializeLogicalPacket(JNIEnv* env, jobject packet) noexcept;
    void deactivateSilentOutput() noexcept;
    jint aimTargetId() const noexcept { return m_logicalController.targetId(); }
    jint aimAttackTargetId() const noexcept {
        return m_logicalController.attackTargetId();
    }
    bool silentAvailable() const noexcept;
    bool silentAttackAvailable() const noexcept;
    // Releases/reacquires LWJGL's mouse grab through Minecraft's own focus
    // methods. Must be called from the Java-owned render thread.
    [[nodiscard]] bool setInputCaptured(JNIEnv* env, bool guiOpen) noexcept;
    // Transformed clients can re-grab LWJGL Mouse after an external WndProc
    // handled the hotkey. While Click GUI is open, enforce only the stable
    // LWJGL release path each frame without repeatedly invoking Minecraft's
    // mapped focus methods.
    [[nodiscard]] bool maintainInputReleased(JNIEnv* env) noexcept;
    // Main-thread edge assistant. It queries current/predicted foot collision
    // geometry and controls Minecraft's own sneak KeyBinding. The
    // method restores the physical key state whenever the feature disables,
    // the world disappears, or the agent shuts down.
    [[nodiscard]] bool updateGameplay(JNIEnv* env,
                                      const GameplaySettings& settings,
                                      const GameSnapshot& snapshot,
                                      std::uint64_t tickMilliseconds) noexcept;
    // Adds a client-side component directly to EntityPlayerSP's chat log. It
    // does not invoke the network handler and therefore cannot send a message
    // to the server. Calls are de-duplicated by the roster generation.
    void publishDebugChat(JNIEnv* env, bool enabled) noexcept;
    // IPC/query threads may enqueue bounded diagnostics here. The Java chat
    // call itself is always performed later by the attached render thread.
    void enqueueDebugChatLine(std::string_view line) noexcept;
    // Queues a local-only, clickable blacklist warning. Clicking it asks the
    // Minecraft chat screen to prefill `/wdr <name>` via SUGGEST_COMMAND; the
    // command is never sent automatically.
    void enqueueWarningChatLine(std::string_view playerName,
                                std::string_view reason) noexcept;
    void release(JNIEnv* env) noexcept;
    void abandon() noexcept;

private:
    using MappingProfile = bindings::MappingDictionary;
    struct BindingCache;

    [[nodiscard]] bool resolve(JNIEnv* env) noexcept;
    [[nodiscard]] bool resolveProfile(JNIEnv* env,
                                      const MappingProfile& profile,
                                      jclass minecraft,
                                      BindingCache& candidate);
    [[nodiscard]] jclass loadWithClassLoader(JNIEnv* env,
                                             jobject loader,
                                             jmethodID loadClass,
                                             const char* binaryName) noexcept;
    // A resolve attempt takes exactly one JVMTI class-table snapshot. Reusing
    // that pass for all registered profiles is critical: GetLoadedClasses plus
    // thousands of GetClassSignature calls is far too expensive for every
    // SwapBuffers frame (or even every 20 Hz data sample).
    [[nodiscard]] jclass findMinecraftClass(
        JNIEnv* env,
        bindings::MappingCandidates& candidates,
        bindings::ClientEnvironment& environment) noexcept;
    void probeEnvironmentHints(JNIEnv* env,
                               bindings::ClientEnvironment& environment) noexcept;
    void clearException(JNIEnv* env) const noexcept;
    [[nodiscard]] bool ensureLwjglMouseBindings(JNIEnv* env) noexcept;
    [[nodiscard]] bool queryLwjglMouseGrabbed(JNIEnv* env, bool& grabbed) noexcept;
    [[nodiscard]] bool setLwjglMouseGrabbed(JNIEnv* env, bool grabbed) noexcept;
    [[nodiscard]] bool ensureLwjglKeyboardBindings(JNIEnv* env) noexcept;
    [[nodiscard]] bool queryLwjglKeyDown(JNIEnv* env, int lwjglKey,
                                         bool& down) noexcept;
    [[nodiscard]] bool queryMinecraftBindingDown(JNIEnv* env,int keyCode,
                                                 bool& down) noexcept;
    [[nodiscard]] jfloat beginLogicalMovement(JNIEnv* env, jobject entity,
                                              jfloat strafe,
                                              jfloat forward) noexcept;
    [[nodiscard]] jfloat logicalMovementForward(JNIEnv* env, jobject entity,
                                                 jfloat fallback) noexcept;
    void endLogicalMovement(JNIEnv* env, jobject entity) noexcept;
    [[nodiscard]] bool arbitrateLogicalSprint(JNIEnv* env,jobject entity,
                                               bool requested) noexcept;
    void beginLogicalJump(JNIEnv* env, jobject entity) noexcept;
    void endLogicalJump(JNIEnv* env, jobject entity) noexcept;
    void rotateFreeLookCamera(JNIEnv* env,jobject entity,jfloat yawDelta,
                              jfloat pitchDelta) noexcept;
    [[nodiscard]] jfloat freeLookCameraAngle(
        JNIEnv* env,jobject entity,LiveFreeLookTransform::Angle angle) noexcept;
    void endFreeLook(JNIEnv* env,const char* reason="disabled",
                     bool forced=true) noexcept;
    [[nodiscard]] bool setFreeLookPerspective(JNIEnv* env,int perspective,
                                               int* previous=nullptr) noexcept;
    [[nodiscard]] bool consumeLogicalInteraction(
        JNIEnv* env, jobject minecraft, LiveInteractionTransform::Entry entry,
        bool heldDown) noexcept;
    [[nodiscard]] jobject arbitrateLogicalAttack(
        JNIEnv* env,jobject originalTarget) noexcept;
    [[nodiscard]] bool executeLogicalInteraction(
        JNIEnv* env, jobject minecraft,
        const silent::InteractionCommand& command) noexcept;
    [[nodiscard]] silent::BlockRayHit traceLogicalBlock(
        JNIEnv* env, jobject world, const silent::LogicalFramePlan& plan) noexcept;
    [[nodiscard]] bool readCombatEye(JNIEnv* env,jobject player,
                                    silent::Vec3& eye) noexcept;
    [[nodiscard]] bool readCombatBounds(JNIEnv* env,jobject entity,
                                       silent::Bounds& bounds) noexcept;
    static void deleteGlobalRefs(JNIEnv* env, BindingCache& cache) noexcept;

    JavaVM* m_vm = nullptr;
    jvmtiEnv* m_jvmti = nullptr;
    bindings::MappingRegistry m_mappingRegistry;
    std::unique_ptr<BindingCache> m_cache;

    enum class ResolutionPhase : std::uint8_t {
        Resolving,
        Resolved,
        Unsupported,
        Unavailable,
        Stopped
    };
    std::atomic<ResolutionPhase> m_resolutionPhase{ResolutionPhase::Resolving};
    std::atomic<std::uint32_t> m_mappingAttempt{0U};
    std::atomic<std::uint64_t> m_retryAtMilliseconds{0U};
    std::uint64_t m_lastSample = 0U;
    std::uint64_t m_entitySampleGeneration = 0U;
    std::uint64_t m_worldGeneration = 0U;
    std::uint64_t m_lastPlayerScan = 0U;
    std::uint64_t m_playerRosterGeneration = 0U;
    std::uint64_t m_debugRosterGeneration = 0U;
    std::uint64_t m_bedOwnershipGeneration = 0U;
    std::uint64_t m_debugBedOwnershipGeneration = 0U;
    struct MatchProbeState final {
        bool sidebarAvailable = false;
        bool tabAvailable = false;
        bool sidebarEvidence = false;
        bool rosterEvidence = false;
        bool armorEvidence = false;
        std::uint8_t sidebarLines = 0U;
        std::uint8_t sidebarTeams = 0U;
        std::uint8_t sidebarYouRows = 0U;
        std::uint8_t rosterTaggedPlayers = 0U;
        std::uint8_t rosterPlayers = 0U;
        std::uint8_t rosterTeams = 0U;
        char rosterOwnTeam = 'u';
        char localArmorTeam = 'u';
        std::uint8_t armorTeams = 0U;
        std::uint8_t stableCount = 0U;

        [[nodiscard]] bool operator==(const MatchProbeState&) const noexcept = default;
    };
    MatchProbeState m_matchProbe{};
    std::uint64_t m_matchProbeGeneration = 0U;
    std::uint64_t m_debugMatchProbeGeneration = 0U;
    jweak m_lastWorld = nullptr;
    bedwars::Team m_sidebarCandidateTeam = bedwars::Team::Unknown;
    std::uint8_t m_sidebarStableCount = 0U;
    std::uint8_t m_sidebarMissingCount = 0U;
    bool m_matchAnchorValid = false;
    double m_matchAnchorX = 0.0;
    double m_matchAnchorY = 0.0;
    double m_matchAnchorZ = 0.0;
    bool m_lockedOwnBedKnown = false;
    int m_lockedOwnBedX = 0;
    int m_lockedOwnBedY = 0;
    int m_lockedOwnBedZ = 0;
    GameSnapshot::OwnBedSource m_lockedOwnBedSource =
        GameSnapshot::OwnBedSource::Unknown;
    GameSnapshot m_snapshot{};
    struct KnockbackTrack {
        int entityId = -1;
        std::array<char, 37U> uuid{};
        std::array<char, 17U> name{};
        std::uint64_t lastSeen = 0;
        WorldPoint position{};
        prediction::KnockbackEvidence evidence;
        prediction::Velocity pendingImpulse{};
        std::uint64_t pendingAt=0U;
        bool pendingPrediction=false;
    };
    std::array<KnockbackTrack, GameSnapshot::MaxEntityMarkers> m_knockbackTracks{};

    static constexpr std::size_t DebugQueueCapacity = 32U;
    static constexpr std::size_t DebugLineCapacity = 160U;
    mutable SRWLOCK m_debugQueueLock = SRWLOCK_INIT;
    std::array<std::array<char, DebugLineCapacity>, DebugQueueCapacity> m_debugQueue{};
    std::uint32_t m_debugQueueHead = 0U;
    std::uint32_t m_debugQueueCount = 0U;

    struct WarningChatLine final {
        std::array<char, 17U> playerName{};
        std::array<char, 81U> reason{};
    };
    static constexpr std::size_t WarningQueueCapacity = 8U;
    mutable SRWLOCK m_warningQueueLock = SRWLOCK_INIT;
    std::array<WarningChatLine, WarningQueueCapacity> m_warningQueue{};
    std::uint32_t m_warningQueueHead = 0U;
    std::uint32_t m_warningQueueCount = 0U;

    struct PublishedBedCache final {
        std::array<BedMarker, GameSnapshot::MaxBedMarkers> markers{};
        std::uint32_t markerCount = 0U;
        std::uint32_t loadedChunkCount = 0U;
        std::uint64_t generation = 0U;
    };
    mutable SRWLOCK m_bedCacheLock = SRWLOCK_INIT;
    PublishedBedCache m_publishedBedCache{};
    std::atomic<bool> m_bedRescanRequested{false};
    HANDLE m_bedRescanEvent = nullptr;

    jclass m_lwjglMouseClass = nullptr;
    jmethodID m_lwjglSetGrabbed = nullptr;
    jmethodID m_lwjglIsGrabbed = nullptr;
    jmethodID m_lwjglIsButtonDown = nullptr;
    jclass m_lwjglKeyboardClass = nullptr;
    jmethodID m_lwjglIsKeyDown = nullptr;
    bool m_overlayInputSessionActive = false;
    bool m_inputGrabStateKnown = false;
    bool m_inputWasGrabbed = true;
    bool m_safewalkSneakForced = false;
    std::atomic<std::uint32_t> m_smartHotbarConfig{0U};
    std::atomic<int> m_smartHotbarRequest{0};
    std::atomic<int> m_smartHotbarRefillRequest{0};
    std::atomic<bool> m_forceSprint{false};
    LiveHotbarTransform m_smartHotbarHook;
    LiveItemUseTransform m_itemUseHook;
    LiveImpulseTransform m_impulseHook;
    LiveVelocityTransform m_velocityHook;
    std::atomic<int> m_shieldAttacker{-1};
    std::atomic<int> m_shieldLocalPlayer{-1};
    std::uint64_t m_nextImpulseHookAttempt=0U;
    [[nodiscard]] bool suppressKnownImpulse(JNIEnv*,jobject,jobject) noexcept;
    std::atomic<bool> m_refillEnabled{false};
    int m_refillSlot=-1;
    [[nodiscard]] bool onItemUse(JNIEnv* env,jobject minecraft,bool entering) noexcept;
    std::uint64_t m_nextSmartHotbarHookAttemptTick=0U;
    [[nodiscard]] bool consumeSmartHotbarPress(JNIEnv* env,jobject binding) noexcept;
    [[nodiscard]] bool processSmartHotbarRequests(JNIEnv* env,jobject minecraft) noexcept;
    void refreshAttackAtPublication(JNIEnv* env) noexcept;
    int m_safewalkSneakKeyCode = 0;
    std::uint8_t m_safewalkSupportMask = 0U;
    std::uint64_t m_safewalkReleaseAt = 0U;
    LivePacketTransform m_silentRotationHook;
    LiveMovementTransform m_logicalMovementHook;
    LiveJumpTransform m_logicalJumpHook;
    LiveInteractionTransform m_logicalInteractionHook;
    LiveAttackTransform m_attackOwnershipHook;
    FreeLookDiagnostics m_freeLookDiagnostics;
    LiveFreeLookTransform m_freeLookHook;
    // A transformed client can finish loading the queue/click owner after the
    // first usable render frame.  Failed installs are therefore retried with
    // a cooldown instead of being permanently poisoned by a one-shot flag.
    std::uint64_t m_nextSilentRotationHookAttemptTick = 0U;
    std::uint64_t m_nextLogicalMovementHookAttemptTick = 0U;
    std::uint64_t m_nextLogicalInteractionHookAttemptTick = 0U;
    std::uint64_t m_nextAttackOwnershipHookAttemptTick = 0U;
    std::uint64_t m_nextFreeLookHookAttemptTick = 0U;
    std::uint8_t m_freeLookHookAttemptCount = 0U;
    bool m_freeLookHookRetryLatched = false;
    std::uint64_t m_nextAimCandidateDebugTick = 0U;
    std::uint64_t m_nextTargetDiagTick = 0U;
    int m_lastTargetDiagMissingId=-1;
    bool m_targetDiagMissingLatched=false;
    silent::LogicalStateController m_logicalController;
    LiveInteractionObserver m_interactionObserver;
    std::uint64_t m_nextInteractionObserverAttempt=0;
    std::atomic<int> m_lastAttackEntryEntity{-1};
    std::atomic<std::uint64_t> m_lastAttackEntryTick{0U};
    bool observeLogicalCamera(JNIEnv* env,jobject minecraft,bool leftDown) noexcept;
    bool m_waitingVanillaResume=false;
    std::atomic<bool> m_freeLookRequested{false};
    std::atomic<int> m_freeLookHotkey{VK_LMENU};
    jobject m_freeLookEntity=nullptr;
    float m_freeLookYaw=0.0F;
    float m_freeLookPitch=0.0F;
    float m_freeLookPreviousYaw=0.0F;
    float m_freeLookPreviousPitch=0.0F;
    int m_freeLookPreviousPerspective=0;
    bool m_freeLookPerspectiveSaved=false;
    bool m_freeLookActive=false;
    bool m_freeLookObservationInitialized=false;
    bool m_freeLookUiRequestedObserved=false;
    bool m_freeLookRuntimeRequestedObserved=false;
    bool m_freeLookReadyObserved=false;
    bool m_freeLookCapabilityObserved=false;
    bool m_freeLookHeldObserved=false;
    bool m_freeLookGuiObserved=false;
    bool m_freeLookForegroundObserved=false;
    bool m_freeLookNotReadyObserved=false;
    bool m_freeLookActiveEventLogged=false;
    std::uint8_t m_freeLookBridgeMask=0U;
    std::uint64_t m_nextFreeLookVerboseTick=0U;
    void observeActualInteraction(JNIEnv* env,LiveInteractionObserver::Event event,jobject argument) noexcept;
    void observeDigPacket(JNIEnv* env,jobject packet) noexcept;
    std::uint64_t m_lastScaffoldPlacementTick = 0U;
    // Scaffold keeps the last supported block layer while the player is in
    // the air. Recomputing this from minY during a jump raises the target one
    // block and is the source of the old diagonal/jump gaps.
    int m_scaffoldPlatformY = 0;
    bool m_scaffoldPlatformYValid = false;
    std::uint64_t m_lastGameplayTick = 0U;
    std::uint64_t m_lastLongJumpTick = 0U;
    bool m_aimSensitivityModified = false;
    float m_originalMouseSensitivity = 0.5F;
    std::uint64_t m_lastLocalAttackTick = 0U;
    std::uint64_t m_lastBedBreakerTick = 0U;
    int m_bedBreakerTargetX = 0;
    int m_bedBreakerTargetY = 0;
    int m_bedBreakerTargetZ = 0;
    bool m_bedBreakerTargetValid = false;
    float m_lastLocalHealth = -1.0F;
    jint m_lastLocalEntityId = -1;
};

} // namespace mcoverlay
