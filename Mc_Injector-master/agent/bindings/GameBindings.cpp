#include "GameBindings.h"
#include "SmartHotbarPolicy.h"
#include "TrajectoryMath.h"
#include "SafeWalkPolicy.h"

#include "src/AgentLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcoverlay {

namespace {
struct LogicalMovementHookContext final {
    jint entityId = -1;
    jfloat originalYaw = 0.0F;
    jfloat mappedForward = 0.0F;
    bool sprintChanged = false;
    bool applied = false;
};
thread_local LogicalMovementHookContext g_logicalMovementHook{};
struct LogicalJumpHookContext final {
    jint entityId=-1;
    jfloat originalYaw=0.0F;
    bool sprintChanged=false;
    bool applied=false;
};
thread_local LogicalJumpHookContext g_logicalJumpHook{};
thread_local bool g_syntheticLogicalAttack=false;
} // namespace

// Resolver-owned until publication. Every field is immutable after
// m_resolutionPhase is release-stored as Resolved. The render thread performs
// an acquire load before dereferencing m_cache, so it can never observe a
// partially initialized JNI cache.
struct GameBindings::BindingCache final {
    jclass minecraftClass = nullptr;
    jclass playerClass = nullptr;
    jclass livingClass = nullptr;
    jclass hostileClass = nullptr;
    jclass entityClass = nullptr;
    jclass fireballClass = nullptr;
    jclass aabbClass = nullptr;
    jclass worldClass = nullptr;
    jclass worldClientClass = nullptr;
    jclass stateClass = nullptr;
    jclass blockClass = nullptr;
    jclass blockPosClass = nullptr;
    jclass bedClass = nullptr;
    jclass chunkProviderClass = nullptr;
    jclass chunkClass = nullptr;
    jclass storageClass = nullptr;
    jclass activeRenderInfoClass = nullptr;
    jclass renderManagerClass = nullptr;
    jclass timerClass = nullptr;
    jclass gameSettingsClass = nullptr;
    jclass entityRendererClass = nullptr;
    jclass renderGlobalClass = nullptr;
    jclass keyBindingClass = nullptr;
    jclass playerControllerClass = nullptr;
    jclass serverDataClass = nullptr;
    jclass itemBlockClass = nullptr;
    jclass enumFacingClass = nullptr;
    jclass vec3Class = nullptr;
    jclass rayVectorClass = nullptr;
    jclass rayHitClass = nullptr;
    jclass networkPacketClass = nullptr;
    jclass movementPacketClass = nullptr;
    jclass positionPacketClass = nullptr;
    jclass lookPacketClass = nullptr;
    jclass positionLookPacketClass = nullptr;
    jclass packetNetHandlerClass = nullptr;
    jmethodID rayVectorConstructor = nullptr;
    jmethodID clickMouse = nullptr;
    jmethodID rightClickMouse = nullptr;
    jmethodID knockBack = nullptr;
    jmethodID handleEntityVelocity=nullptr;
    jmethodID velocityEntityId=nullptr;
    jmethodID sendClickBlock = nullptr;
    jmethodID moveFlying = nullptr;
    jmethodID moveEntityWithHeading = nullptr;
    jmethodID getAIMoveSpeed = nullptr;
    jmethodID isSprinting = nullptr;
    jmethodID setSprinting = nullptr;
    jmethodID swingItem = nullptr;
    jmethodID rayTraceBlocks = nullptr;
    jmethodID getEntityById = nullptr;
    jmethodID getItemUseDuration = nullptr;
    jmethodID isUsingItem = nullptr;
    jmethodID getEyeHeight = nullptr;
    jfieldID hitVector = nullptr;
    jfieldID rayBlockPos = nullptr;
    jfieldID raySideHit = nullptr;
    jfieldID cameraMouseOver = nullptr; // read-only, never a redirected target
    jfieldID cameraHitEntity = nullptr;
    jfieldID cameraHitType = nullptr;
    jfieldID entityTicks = nullptr;
    jmethodID isSneaking = nullptr;
    jclass diggingPacketClass = nullptr;
    jmethodID diggingPosition = nullptr, diggingAction = nullptr, enumOrdinal = nullptr;
    std::array<jmethodID, 3U> blockPosCoordinates{};
    jmethodID facingIndex = nullptr;
    jmethodID addToSendQueue = nullptr;
    jmethodID movementPacketConstructor = nullptr;
    jmethodID positionPacketConstructor = nullptr;
    jmethodID lookPacketConstructor = nullptr;
    jmethodID positionLookPacketConstructor = nullptr;
    std::array<jfieldID, 3U> packetPosition{};
    jfieldID packetYaw = nullptr;
    jfieldID packetPitch = nullptr;
    jfieldID packetOnGround = nullptr;
    std::array<jfieldID, 3U> vectorFields{};
    jclass chatComponentClass = nullptr;
    jclass chatTextClass = nullptr;
    jclass chatSerializerClass = nullptr;
    jobject renderManagerObject = nullptr;
    jobject timerObject = nullptr;
    jobject modelViewBuffer = nullptr;
    jobject projectionBuffer = nullptr;
    jobject viewportBuffer = nullptr;

    jmethodID getMinecraft = nullptr;
    jfieldID minecraftInstanceField = nullptr;
    jfieldID playerField = nullptr;
    jmethodID getHealth = nullptr;
    jfieldID hurtTime = nullptr;
    jmethodID getMaxHealth = nullptr;
    jmethodID getEntityId = nullptr;
    jmethodID getBounds = nullptr;
    jmethodID isMainThread = nullptr;
    jmethodID isSingleplayer = nullptr;
    jfieldID worldField = nullptr;
    jfieldID positionX = nullptr;
    jfieldID positionY = nullptr;
    jfieldID positionZ = nullptr;
    std::array<jfieldID, 3U> previousPosition{};
    jmethodID getLoadedEntities = nullptr;
    jfieldID loadedEntitiesField = nullptr;
    jfieldID playerEntities = nullptr;
    jmethodID getName = nullptr;
    jmethodID isInvisible = nullptr;
    jmethodID getDisplayName = nullptr;
    jmethodID getFormattedText = nullptr;
    jmethodID chatTextConstructor = nullptr;
    jmethodID addChatMessage = nullptr;
    jmethodID parseChatJson = nullptr;
    jmethodID listSize = nullptr;
    jmethodID listGet = nullptr;
    jmethodID blockPosConstructor = nullptr;
    jmethodID getBlockState = nullptr;
    jmethodID getBlock = nullptr;
    jmethodID getBlockMetadata = nullptr;
    jmethodID setIngameFocus = nullptr;
    jmethodID setIngameNotInFocus = nullptr;
    jmethodID getChunkProvider = nullptr;
    jfieldID chunkListingField = nullptr;
    jfieldID chunkX = nullptr;
    jfieldID chunkZ = nullptr;
    jmethodID getStorageArrays = nullptr;
    jmethodID getStorageData = nullptr;
    jmethodID listToArray = nullptr;
    jmethodID collectionToArray = nullptr;
    jmethodID getRenderManager = nullptr;
    std::array<jfieldID, 3U> renderPosition{};
    jfieldID activeModelView = nullptr;
    jfieldID activeProjection = nullptr;
    jfieldID activeViewport = nullptr;
    jfieldID timerField = nullptr;
    jfieldID renderPartialTicks = nullptr;
    jfieldID gameSettingsField = nullptr;
    jfieldID thirdPersonView = nullptr;
    jmethodID updateCameraAndRender = nullptr;
    jmethodID orientCamera = nullptr;
    jmethodID setAngles = nullptr;
    jmethodID setupTerrain = nullptr;
    jfieldID currentScreen = nullptr;
    jmethodID aabbConstructor = nullptr;
    jmethodID getCollidingBoxes = nullptr;
    jfieldID keyBindSneakField = nullptr;
    jfieldID keyBindSprintField = nullptr;
    jfieldID keyBindsHotbar = nullptr;
    jmethodID keyBindingIsPressed = nullptr;
    jmethodID syncCurrentPlayItem = nullptr;
    std::array<jfieldID, 5U> movementKeyFields{};
    jmethodID getKeyCode = nullptr;
    jmethodID setKeyBindState = nullptr;
    jfieldID mouseSensitivity = nullptr;
    jfieldID rotationYaw = nullptr;
    jfieldID rotationPitch = nullptr;
    jfieldID previousRotationYaw = nullptr;
    jfieldID previousRotationPitch = nullptr;
    std::array<jfieldID, 2U> movementInputFields{};
    std::array<jfieldID, 3U> motionFields{};
    jfieldID onGround = nullptr;
    jmethodID jump = nullptr;
    jmethodID isAirBlock = nullptr;
    jmethodID getCurrentServerData = nullptr;
    jfieldID serverIp = nullptr;
    jfieldID playerControllerField = nullptr;
    jfieldID currentItem = nullptr;
    jfieldID mainInventory = nullptr;
    jmethodID getBlockFromItem = nullptr;
    jmethodID getIdFromBlock = nullptr;
    jmethodID getFacingByIndex = nullptr;
    jmethodID onPlayerRightClick = nullptr;
    jmethodID clickBlock = nullptr;
    jmethodID onPlayerDamageBlock = nullptr;
    jmethodID resetBlockRemoving = nullptr;
    jmethodID getBlockReachDistance = nullptr;
    jmethodID getStrVsBlock = nullptr;
    jmethodID attackEntity = nullptr;
    jmethodID windowClick = nullptr;
    jmethodID vec3Constructor = nullptr;
    jfieldID minX = nullptr;
    jfieldID minY = nullptr;
    jfieldID minZ = nullptr;
    jfieldID maxX = nullptr;
    jfieldID maxY = nullptr;
    jfieldID maxZ = nullptr;

    jclass scoreboardClass = nullptr;
    jclass scoreObjectiveClass = nullptr;
    jclass scoreClass = nullptr;
    jclass scorePlayerTeamClass = nullptr;
    jclass netHandlerClass = nullptr;
    jclass networkPlayerInfoClass = nullptr;
    jclass gameProfileClass = nullptr;
    jclass itemStackClass = nullptr;
    jclass itemClass = nullptr;
    jclass itemArmorClass = nullptr;
    jclass itemSwordClass = nullptr;
    jclass inventoryPlayerClass = nullptr;
    jclass enchantmentHelperClass = nullptr;
    jclass abstractClientPlayerClass = nullptr;
    jclass resourceLocationClass = nullptr;
    jclass textureManagerClass = nullptr;
    jclass textureObjectClass = nullptr;
    jclass uuidClass = nullptr;

    jmethodID getScoreboard = nullptr;
    jmethodID getObjectiveInDisplaySlot = nullptr;
    jmethodID getPlayersTeam = nullptr;
    jmethodID getSortedScores = nullptr;
    jmethodID getPlayerName = nullptr;
    jmethodID formatPlayerName = nullptr;
    jmethodID getNetHandler = nullptr;
    jmethodID getPlayerInfoMap = nullptr;
    jmethodID getGameProfile = nullptr;
    jmethodID gameProfileGetName = nullptr;
    jmethodID gameProfileGetId = nullptr;
    jmethodID uuidToString = nullptr;
    jfieldID inventoryField = nullptr;
    jfieldID armorInventoryField = nullptr;
    jmethodID getItem = nullptr;
    jmethodID hasColor = nullptr;
    jmethodID getColor = nullptr;
    jmethodID getEnchantmentLevel = nullptr;
    jmethodID getEquipmentInSlot = nullptr;
    jmethodID getIdFromItem = nullptr;
    jfieldID stackSize = nullptr;
    jmethodID getItemDamage = nullptr;
    jmethodID getUniqueId = nullptr;
    jmethodID getLocationSkin = nullptr;
    jmethodID getTextureManager = nullptr;
    jmethodID getTexture = nullptr;
    jmethodID getGlTextureId = nullptr;

    const MappingProfile* profile = nullptr;
};

namespace {

// GetLoadedClasses may require a HotSpot global safepoint even when it is
// invoked from a native helper thread. By the time the controller offers a
// visible Minecraft window the 1.8.9 client class is already loaded, so one
// snapshot is sufficient. Retrying unsupported/transformed clients would only
// introduce visible pauses at regular intervals without changing the result.

template<typename Identifier, typename Lookup>
[[nodiscard]] bool lookupRequired(JNIEnv* const env,
                                  Identifier& identifier,
                                  Lookup&& lookup) noexcept
{
    identifier = lookup();
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        identifier = nullptr;
        return false;
    }
    return identifier != nullptr;
}

class LocalReferenceSet final {
public:
    explicit LocalReferenceSet(JNIEnv* const env) noexcept : m_env(env) {}

    ~LocalReferenceSet()
    {
        if (m_env == nullptr) {
            return;
        }
        while (m_count != 0U) {
            --m_count;
            m_env->DeleteLocalRef(m_references[m_count]);
        }
    }

    void add(jobject const reference) noexcept
    {
        if (reference != nullptr && m_count < m_references.size()) {
            m_references[m_count] = reference;
            ++m_count;
        }
    }

private:
    JNIEnv* m_env = nullptr;
    std::array<jobject, 64U> m_references{};
    std::size_t m_count = 0U;
};

} // namespace

GameBindings::GameBindings(JavaVM* const vm, jvmtiEnv* const jvmti) noexcept
    : m_vm(vm), m_jvmti(jvmti)
{
    // Auto-reset wakeup: normal chunk diffing remains asleep for 500 ms, but a
    // user refresh interrupts that wait immediately without introducing a
    // high-frequency polling loop.
    m_bedRescanEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

GameBindings::~GameBindings()
{
    if (m_bedRescanEvent != nullptr) {
        ::CloseHandle(m_bedRescanEvent);
        m_bedRescanEvent = nullptr;
    }
}

bindings::MappingRegistrationResult GameBindings::registerMappingDictionary(
    bindings::MappingDictionary dictionary, std::string* const error) noexcept
{
    return m_mappingRegistry.registerDictionary(std::move(dictionary), error);
}

void GameBindings::clearException(JNIEnv* const env) const noexcept
{
    if (env != nullptr && env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
    }
}

void GameBindings::runResolver(JNIEnv* const env, HANDLE const stopEvent) noexcept
{
    if (env == nullptr || m_jvmti == nullptr || stopEvent == nullptr) {
        markResolverUnavailable();
        return;
    }

    const DWORD stopped = ::WaitForSingleObject(stopEvent, 0U);
    if (stopped == WAIT_OBJECT_0) {
        m_retryAtMilliseconds.store(0U, std::memory_order_release);
        m_resolutionPhase.store(ResolutionPhase::Stopped, std::memory_order_release);
        return;
    }
    if (stopped == WAIT_FAILED) {
        markResolverUnavailable();
        return;
    }

    m_mappingAttempt.store(1U, std::memory_order_release);
    m_retryAtMilliseconds.store(0U, std::memory_order_release);
    if (resolve(env)) {
        return;
    }

    // A failed lookup must never leak a pending exception into
    // DetachCurrentThread. Rendering remains independent of this result.
    clearException(env);
    m_resolutionPhase.store(ResolutionPhase::Unsupported,
                            std::memory_order_release);
    m_freeLookDiagnostics.event("MAPPING_UNSUPPORTED",
        "resolver stopped after one class snapshot");
    log::info("Minecraft mappings are unsupported; resolver stopped after one class snapshot.");
}

void GameBindings::markResolverUnavailable() noexcept
{
    m_retryAtMilliseconds.store(0U, std::memory_order_release);
    m_resolutionPhase.store(ResolutionPhase::Unavailable, std::memory_order_release);
}

GameSnapshot GameBindings::snapshot(const std::uint64_t tickMilliseconds) const noexcept
{
    // m_snapshot has one owner: the SwapBuffers render thread. Returning a copy
    // also keeps OverlayRenderer from retaining memory that release() resets.
    GameSnapshot result = m_snapshot;
    const ResolutionPhase phase = m_resolutionPhase.load(std::memory_order_acquire);
    result.mappingAttempt = m_mappingAttempt.load(std::memory_order_acquire);

    const std::uint64_t retryAt = m_retryAtMilliseconds.load(std::memory_order_acquire);
    const std::uint64_t retryRemaining = retryAt > tickMilliseconds
        ? retryAt - tickMilliseconds : 0U;
    result.mappingRetryInMs = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(retryRemaining, UINT32_MAX));

    switch (phase) {
    case ResolutionPhase::Resolved:
        // m_cache and every referenced ID were written before the resolver's
        // release-store of Resolved, so this acquire read makes them visible.
        if (m_cache != nullptr && m_cache->profile != nullptr) {
            result.mapping = m_cache->profile->label.c_str();
        }
        if (result.state == GameSnapshot::State::Resolving ||
            result.state == GameSnapshot::State::Unsupported) {
            result.state = GameSnapshot::State::WaitingForGameThread;
        }
        break;
    case ResolutionPhase::Unsupported:
        result.state = GameSnapshot::State::Unsupported;
        result.mapping = "unsupported client mappings";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Unavailable:
        result.state = GameSnapshot::State::JniError;
        result.mapping = "mapping resolver unavailable";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Stopped:
        result.state = GameSnapshot::State::Unsupported;
        result.mapping = "mapping resolver stopped";
        result.mappingRetryInMs = 0U;
        break;
    case ResolutionPhase::Resolving:
        result.state = GameSnapshot::State::Resolving;
        result.mapping = result.mappingAttempt == 0U
            ? "mapping resolver queued"
            : "probing mapping providers";
        break;
    }
    return result;
}

void GameBindings::probeEnvironmentHints(
    JNIEnv* const env, bindings::ClientEnvironment& environment) noexcept
{
    if (env == nullptr || env->PushLocalFrame(12) < 0) {
        clearException(env);
        return;
    }

    jclass systemClass = env->FindClass("java/lang/System");
    if (env->ExceptionCheck() == JNI_TRUE || systemClass == nullptr) {
        clearException(env);
        env->PopLocalFrame(nullptr);
        return;
    }
    jmethodID getProperty = env->GetStaticMethodID(
        systemClass, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
    if (env->ExceptionCheck() == JNI_TRUE || getProperty == nullptr) {
        clearException(env);
        env->PopLocalFrame(nullptr);
        return;
    }

    // Values are inspected in memory only and are never logged or sent over
    // IPC. java.home commonly identifies Lunar before an expensive class walk.
    constexpr std::array<const char*, 3U> kProperties{
        "java.home", "java.class.path", "sun.java.command"};
    for (const char* const property : kProperties) {
        jstring key = env->NewStringUTF(property);
        if (env->ExceptionCheck() == JNI_TRUE || key == nullptr) {
            clearException(env);
            continue;
        }
        jstring value = static_cast<jstring>(
            env->CallStaticObjectMethod(systemClass, getProperty, key));
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            continue;
        }
        if (value == nullptr) continue;

        const char* utf8 = env->GetStringUTFChars(value, nullptr);
        if (env->ExceptionCheck() == JNI_TRUE || utf8 == nullptr) {
            clearException(env);
            continue;
        }
        m_mappingRegistry.observeLaunchHint(utf8, environment);
        env->ReleaseStringUTFChars(value, utf8);
        if (env->ExceptionCheck() == JNI_TRUE) clearException(env);
    }
    env->PopLocalFrame(nullptr);
}

jclass GameBindings::findMinecraftClass(
    JNIEnv* const env,
    bindings::MappingCandidates& candidates,
    bindings::ClientEnvironment& environment) noexcept
{
    candidates = {};
    if (env == nullptr || m_jvmti == nullptr) {
        return nullptr;
    }

    const bindings::ClientFamily hintedFamily = environment.family();
    if (hintedFamily != bindings::ClientFamily::Unknown &&
        !m_mappingRegistry.hasMappingsForFamily(hintedFamily)) {
        log::info(std::string("Detected ") + bindings::clientFamilyName(hintedFamily) +
                  " client; no verified mapping dictionary is registered.");
        return nullptr;
    }

    jint count = 0;
    jclass* classes = nullptr;
    if (m_jvmti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE || classes == nullptr) {
        return nullptr;
    }

    // GetLoadedClasses returns JNI local references owned by this native frame.
    // Always delete every entry and deallocate the JVMTI array, including after
    // a match. This is one snapshot for every registered provider/profile.
    jclass result = nullptr;
    bool discoveryFailed = false;
    bool knownAnchorSeen = false;
    bool matchedAnchor = false;
    for (jint index = 0; index < count; ++index) {
        // After finding an anchor which has a dictionary for the detected
        // client family, only release remaining references. A transformed
        // client can load compatibility/wrapper Minecraft classes first; an
        // anchor with no family-compatible dictionary is therefore not enough
        // to stop the one-time scan.
        if (!matchedAnchor && !discoveryFailed) {
            char* candidateSignature = nullptr;
            char* genericSignature = nullptr;
            const jvmtiError signatureResult = m_jvmti->GetClassSignature(
                classes[index], &candidateSignature, &genericSignature);
            if (signatureResult == JVMTI_ERROR_NONE && candidateSignature != nullptr) {
                m_mappingRegistry.observeClassSignature(candidateSignature, environment);
                if (m_mappingRegistry.isMinecraftAnchor(candidateSignature)) {
                    knownAnchorSeen = true;

                    // A custom defining loader is useful client evidence and
                    // costs one lookup rather than another loaded-class pass.
                    jobject loader = nullptr;
                    if (m_jvmti->GetClassLoader(classes[index], &loader) ==
                            JVMTI_ERROR_NONE && loader != nullptr) {
                        jclass loaderClass = env->GetObjectClass(loader);
                        if (env->ExceptionCheck() == JNI_TRUE) {
                            clearException(env);
                            loaderClass = nullptr;
                        }
                        if (loaderClass != nullptr) {
                            char* loaderSignature = nullptr;
                            char* loaderGeneric = nullptr;
                            if (m_jvmti->GetClassSignature(loaderClass, &loaderSignature,
                                                           &loaderGeneric) ==
                                    JVMTI_ERROR_NONE && loaderSignature != nullptr) {
                                m_mappingRegistry.observeClassSignature(loaderSignature,
                                                                         environment);
                            }
                            if (loaderSignature != nullptr) {
                                m_jvmti->Deallocate(
                                    reinterpret_cast<unsigned char*>(loaderSignature));
                            }
                            if (loaderGeneric != nullptr) {
                                m_jvmti->Deallocate(
                                    reinterpret_cast<unsigned char*>(loaderGeneric));
                            }
                            env->DeleteLocalRef(loaderClass);
                        }
                        env->DeleteLocalRef(loader);
                    }

                    candidates = m_mappingRegistry.candidatesForAnchor(
                        candidateSignature, environment);
                    if (!candidates.empty()) {
                        matchedAnchor = true;
                        result = static_cast<jclass>(env->NewLocalRef(classes[index]));
                        if (env->ExceptionCheck() == JNI_TRUE) {
                            env->ExceptionClear();
                            result = nullptr;
                            discoveryFailed = true;
                        }
                    }
                }
            }
            // JVMTI normally allocates outputs only on success, but releasing
            // any non-null buffer also makes unusual error paths leak-free.
            if (candidateSignature != nullptr) {
                m_jvmti->Deallocate(reinterpret_cast<unsigned char*>(candidateSignature));
            }
            if (genericSignature != nullptr) {
                m_jvmti->Deallocate(reinterpret_cast<unsigned char*>(genericSignature));
            }
        }
        env->DeleteLocalRef(classes[index]);
    }
    m_jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
    if (knownAnchorSeen && candidates.empty()) {
        log::info(std::string("Minecraft anchor found for ") +
                  bindings::clientFamilyName(environment.family()) +
                  "; no verified provider matched the detected environment.");
    }
    return result;
}

jclass GameBindings::loadWithClassLoader(JNIEnv* const env,
                                         jobject const loader,
                                         const jmethodID loadClass,
                                         const char* const binaryName) noexcept
{
    if (env == nullptr || loader == nullptr || loadClass == nullptr || binaryName == nullptr) {
        return nullptr;
    }

    // Minecraft itself was found in the JVMTI snapshot first, so asking its
    // exact defining loader for dependencies cannot accidentally start the
    // game from Forge's splash context. ClassLoader.loadClass does not run a
    // class initializer and avoids another full JVMTI table walk per class.
    jclass result = nullptr;
    jstring name = env->NewStringUTF(binaryName);
    if (env->ExceptionCheck() == JNI_TRUE || name == nullptr) {
        clearException(env);
        return nullptr;
    }
    result = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name));
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        if (result != nullptr) {
            env->DeleteLocalRef(result);
        }
        result = nullptr;
    }
    env->DeleteLocalRef(name);
    return result;
}

bool GameBindings::resolveProfile(JNIEnv* const env,
                                  const MappingProfile& profile,
                                  jclass const minecraft,
                                  BindingCache& candidate)
{
    if (env == nullptr || minecraft == nullptr) {
        return false;
    }

    LocalReferenceSet localReferences(env);
    jobject minecraftLoader = nullptr;
    if (m_jvmti->GetClassLoader(minecraft, &minecraftLoader) != JVMTI_ERROR_NONE ||
        minecraftLoader == nullptr) {
        clearException(env);
        return false;
    }
    localReferences.add(minecraftLoader);

    jclass loaderClass = nullptr;
    if (!lookupRequired(env, loaderClass,
                        [&] { return env->FindClass("java/lang/ClassLoader"); })) {
        return false;
    }
    localReferences.add(loaderClass);
    jmethodID loadClassMethod = nullptr;
    if (!lookupRequired(env, loadClassMethod, [&] {
            return env->GetMethodID(loaderClass, "loadClass",
                                    "(Ljava/lang/String;)Ljava/lang/Class;");
        })) {
        return false;
    }

    auto loadClass = [&](jclass& destination, const char* const binaryName) noexcept {
        destination = loadWithClassLoader(env, minecraftLoader, loadClassMethod, binaryName);
        if (destination == nullptr) {
            return false;
        }
        localReferences.add(destination);
        return true;
    };

    jclass player = nullptr;
    jclass living = nullptr;
    jclass entity = nullptr;
    jclass fireball = nullptr;
    jclass aabb = nullptr;
    jclass world = nullptr;
    jclass worldClient = nullptr;
    jclass state = nullptr;
    jclass block = nullptr;
    jclass blockPos = nullptr;
    jclass bed = nullptr;
    jclass chunkProvider = nullptr;
    jclass chunk = nullptr;
    jclass storage = nullptr;
    jclass activeRenderInfo = nullptr;
    jclass renderManager = nullptr;
    jclass timer = nullptr;
    jclass chatComponent = nullptr;
    jclass chatText = nullptr;
    jclass chatSerializer = nullptr;
    jclass scoreboard = nullptr;
    jclass scoreObjective = nullptr;
    jclass score = nullptr;
    jclass scorePlayerTeam = nullptr;
    jclass netHandler = nullptr;
    jclass networkPlayerInfo = nullptr;
    jclass gameProfile = nullptr;
    jclass itemStack = nullptr;
    jclass item = nullptr;
    jclass itemArmor = nullptr;
    jclass itemSword = nullptr;
    jclass inventoryPlayer = nullptr;
    jclass enchantmentHelper = nullptr;
    jclass abstractClientPlayer = nullptr;
    jclass resourceLocation = nullptr;
    jclass textureManager = nullptr;
    jclass textureObject = nullptr;
    jclass uuid = nullptr;
    jclass gameSettings = nullptr;
    jclass entityRenderer = nullptr;
    jclass renderGlobal = nullptr;
    jclass keyBinding = nullptr;
    jclass playerController = nullptr;
    jclass hostile = nullptr;
    jclass serverData = nullptr;
    jclass itemBlock = nullptr;
    jclass enumFacing = nullptr;
    jclass vec3 = nullptr;
    jclass rayVector = nullptr;
    jclass rayHit = nullptr;
    jclass networkPacket = nullptr;
    jclass movementPacket = nullptr;
    jclass positionPacket = nullptr;
    jclass lookPacket = nullptr;
    jclass positionLookPacket = nullptr;
    jclass packetNetHandler = nullptr;
    // Only the long-standing game/render bindings are profile-critical.  The
    // BedWars sidebar and armor readers are optional capabilities: a missing or
    // stale auxiliary mapping must not invalidate an otherwise usable client.
    if (!loadClass(player, profile.playerName.c_str()) ||
        !loadClass(living, profile.livingName.c_str()) ||
        !loadClass(entity, profile.entityName.c_str()) ||
        !loadClass(aabb, profile.aabbName.c_str()) ||
        !loadClass(world, profile.worldName.c_str()) ||
        !loadClass(worldClient, profile.worldClientName.c_str()) ||
        !loadClass(state, profile.stateName.c_str()) ||
        !loadClass(block, profile.blockName.c_str()) ||
        !loadClass(blockPos, profile.blockPosName.c_str()) ||
        !loadClass(bed, profile.bedName.c_str()) ||
        !loadClass(chunkProvider, profile.chunkProviderName.c_str()) ||
        !loadClass(chunk, profile.chunkName.c_str()) ||
        !loadClass(storage, profile.storageName.c_str()) ||
        !loadClass(activeRenderInfo, profile.activeRenderInfoName.c_str()) ||
        !loadClass(renderManager, profile.renderManagerName.c_str()) ||
        !loadClass(timer, profile.timerName.c_str()) ||
        !loadClass(chatComponent, profile.chatComponentName.c_str())) {
        log::info(std::string("Core mapping class load failed for profile: ") +
                  profile.label);
        return false;
    }

    auto loadFeatureClass = [&](jclass& destination,
                                const std::string& binaryName,
                                const char* const capability,
                                const char* const logicalName) noexcept {
        if (binaryName.empty()) {
            log::info(std::string(capability) + " mappings unavailable for " +
                      profile.label + ": no " + logicalName + " class mapping.");
            return false;
        }
        destination = loadWithClassLoader(env, minecraftLoader, loadClassMethod,
                                          binaryName.c_str());
        if (destination == nullptr) {
            log::info(std::string(capability) + " mappings unavailable for " +
                      profile.label + ": could not load " + logicalName +
                      " (" + binaryName + ").");
            return false;
        }
        localReferences.add(destination);
        return true;
    };

    const bool fireballClassLoaded = loadFeatureClass(
        fireball, profile.fireballName, "Fireball ESP", "EntityFireball");
    const bool hostileClassLoaded = loadFeatureClass(
        hostile, profile.hostileName, "Hostile entity", "IMob");

    const bool sidebarClassesLoaded =
        loadFeatureClass(scoreboard, profile.scoreboardName, "Sidebar", "Scoreboard") &&
        loadFeatureClass(scoreObjective, profile.scoreObjectiveName, "Sidebar", "ScoreObjective") &&
        loadFeatureClass(score, profile.scoreName, "Sidebar", "Score") &&
        loadFeatureClass(scorePlayerTeam, profile.scorePlayerTeamName, "Sidebar", "ScorePlayerTeam");

    const bool tabClassesLoaded =
        loadFeatureClass(netHandler, profile.netHandlerName, "TAB roster", "NetHandlerPlayClient") &&
        loadFeatureClass(networkPlayerInfo, profile.networkPlayerInfoName,
                         "TAB roster", "NetworkPlayerInfo") &&
        loadFeatureClass(gameProfile, "com.mojang.authlib.GameProfile",
                         "TAB roster", "GameProfile");

    const bool itemClassesLoaded =
        loadFeatureClass(itemStack, profile.itemStackName, "Items", "ItemStack") &&
        loadFeatureClass(item, profile.itemName, "Items", "Item") &&
        loadFeatureClass(inventoryPlayer, profile.inventoryPlayerName,
                         "Items", "InventoryPlayer");

    const bool armorClassesLoaded = itemClassesLoaded &&
        loadFeatureClass(itemArmor, profile.itemArmorName, "Armor", "ItemArmor") &&
        loadFeatureClass(enchantmentHelper, profile.enchantmentHelperName,
                         "Armor", "EnchantmentHelper");
    const bool itemSwordClassLoaded = itemClassesLoaded &&
        loadFeatureClass(itemSword,profile.itemSwordName,
                         "Smart Hotbar","ItemSword");

    const bool skinClassesLoaded =
        loadFeatureClass(abstractClientPlayer, profile.abstractClientPlayerName,
                         "Player skin", "AbstractClientPlayer") &&
        loadFeatureClass(resourceLocation, profile.resourceLocationName,
                         "Player skin", "ResourceLocation") &&
        loadFeatureClass(textureManager, profile.textureManagerName,
                         "Player skin", "TextureManager") &&
        loadFeatureClass(textureObject, profile.textureObjectName,
                         "Player skin", "ITextureObject");

    const bool gameSettingsClassLoaded = loadFeatureClass(
        gameSettings, profile.gameSettingsName, "Aim/Movement", "GameSettings");
    const bool entityRendererClassLoaded = loadFeatureClass(
        entityRenderer, profile.entityRendererName, "FreeLook", "EntityRenderer");
    const bool renderGlobalClassLoaded = loadFeatureClass(
        renderGlobal, profile.renderGlobalName, "FreeLook terrain", "RenderGlobal");
    const bool keyBindingClassLoaded = loadFeatureClass(
        keyBinding, profile.keyBindingName, "Safewalk", "KeyBinding");
    const bool safewalkClassesLoaded =
        gameSettingsClassLoaded && keyBindingClassLoaded;

    const bool serverDataClassLoaded = loadFeatureClass(
        serverData, profile.serverDataName, "Server guard", "ServerData");
    // These classes serve independent capabilities.  Never short-circuit their
    // loading behind Scaffold: Silent Lock still needs PlayerControllerMP and
    // EnumFacing when an ItemBlock/inventory mapping is unavailable on Lunar.
    const bool playerControllerClassLoaded = loadFeatureClass(
        playerController, profile.playerControllerName,
        "Interaction", "PlayerControllerMP");
    const bool itemBlockClassLoaded = loadFeatureClass(
        itemBlock, profile.itemBlockName, "Scaffold", "ItemBlock");
    const bool enumFacingClassLoaded = loadFeatureClass(
        enumFacing, profile.enumFacingName, "Interaction", "EnumFacing");
    const bool vec3ClassLoaded = loadFeatureClass(
        vec3, profile.vec3Name, "Scaffold", "Vec3");
    const bool movementClassesLoaded = safewalkClassesLoaded && itemClassesLoaded &&
        playerControllerClassLoaded && itemBlockClassLoaded &&
        enumFacingClassLoaded && vec3ClassLoaded;

    if (lookupRequired(env, uuid, [&] { return env->FindClass("java/util/UUID"); })) {
        localReferences.add(uuid);
    }

    const bool debugChatClassLoaded =
        loadFeatureClass(chatText, profile.chatTextName, "Debug chat", "ChatComponentText");
    const bool richChatClassLoaded = debugChatClassLoaded &&
        loadFeatureClass(chatSerializer, profile.chatSerializerName,
                         "Rich local chat", "IChatComponent.Serializer");

    const std::string getMinecraftSignature = std::string("()") + profile.minecraftSignature;
    const std::string getBoundsSignature = std::string("()") + profile.aabbSignature;
    const std::string getBlockStateSignature =
        std::string("(") + profile.blockPosSignature + ")" + profile.stateSignature;
    const std::string getBlockSignature = std::string("()") + profile.blockSignature;
    const std::string getBlockMetadataSignature =
        std::string("(") + profile.stateSignature + ")I";
    const std::string getStorageArraysSignature =
        std::string("()[") + profile.storageSignature;
    const std::string getRenderManagerSignature =
        std::string("()") + profile.renderManagerSignature;

    const std::string getObjectiveInDisplaySlotSignature = std::string("(I)") + profile.scoreObjectiveSignature;
    const std::string getPlayersTeamSignature = std::string("(Ljava/lang/String;)") + profile.scorePlayerTeamSignature;
    const std::string getSortedScoresSignature = std::string("(") + profile.scoreObjectiveSignature + ")Ljava/util/Collection;";
    const std::string formatPlayerNameSignature = std::string("(") +
        profile.teamSignature + "Ljava/lang/String;)Ljava/lang/String;";
    const std::string getNetHandlerSignature = std::string("()") +
        profile.netHandlerSignature;
    const std::string getItemSignature = std::string("()") + profile.itemSignature;

    const bool singletonResolved = !profile.minecraftInstanceField.empty()
        ? lookupRequired(env, candidate.minecraftInstanceField, [&] {
              return env->GetStaticFieldID(minecraft,
                                           profile.minecraftInstanceField.c_str(),
                                           profile.minecraftSignature.c_str());
          })
        : lookupRequired(env, candidate.getMinecraft, [&] {
              return env->GetStaticMethodID(minecraft, profile.getMinecraft.c_str(),
                                            getMinecraftSignature.c_str());
          });
    if (!singletonResolved ||
        !lookupRequired(env, candidate.playerField, [&] {
            return env->GetFieldID(minecraft, profile.playerField.c_str(),
                                   profile.playerSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.getHealth, [&] {
            return env->GetMethodID(living, profile.getHealth.c_str(), "()F");
        }) ||
        !lookupRequired(env, candidate.getMaxHealth, [&] {
            return env->GetMethodID(living, profile.getMaxHealth.c_str(), "()F");
        }) ||
        !lookupRequired(env, candidate.getEntityId, [&] {
            return env->GetMethodID(entity, profile.getEntityId.c_str(), "()I");
        }) ||
        !lookupRequired(env, candidate.getBounds, [&] {
            return env->GetMethodID(entity, profile.getBounds.c_str(),
                                    getBoundsSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.isMainThread, [&] {
            return env->GetMethodID(minecraft, profile.isMainThread.c_str(), "()Z");
        }) ||
        !lookupRequired(env, candidate.isSingleplayer, [&] {
            return env->GetMethodID(minecraft, profile.isSingleplayer.c_str(), "()Z");
        }) ||
        !lookupRequired(env, candidate.worldField, [&] {
            return env->GetFieldID(minecraft, profile.worldField.c_str(),
                                   profile.worldClientSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.timerField, [&] {
            return env->GetFieldID(minecraft, profile.timerField.c_str(),
                                   profile.timerSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.setIngameFocus, [&] {
            return env->GetMethodID(minecraft, profile.setIngameFocus.c_str(), "()V");
        }) ||
        !lookupRequired(env, candidate.setIngameNotInFocus, [&] {
            return env->GetMethodID(minecraft, profile.setIngameNotInFocus.c_str(), "()V");
        }) ||
        !lookupRequired(env, candidate.getRenderManager, [&] {
            return env->GetMethodID(minecraft, profile.getRenderManager.c_str(),
                                    getRenderManagerSignature.c_str());
        })) {
        return false;
    }

    if (!profile.hurtTimeField.empty()) {
        (void)lookupRequired(env, candidate.hurtTime, [&] {
            return env->GetFieldID(living, profile.hurtTimeField.c_str(), "I");
        });
    }
    for (std::size_t index = 0U; index < candidate.renderPosition.size(); ++index) {
        if (!lookupRequired(env, candidate.renderPosition[index], [&] {
                return env->GetFieldID(renderManager,
                                       profile.renderPositionFields[index].c_str(), "D");
            })) {
            return false;
        }
    }

    for (std::size_t index = 0U; index < candidate.previousPosition.size(); ++index) {
        if (!lookupRequired(env, candidate.previousPosition[index], [&] {
                return env->GetFieldID(entity,
                                       profile.previousPositionFields[index].c_str(), "D");
            })) {
            return false;
        }
    }

    std::array<jfieldID, 3U> positionFields{};
    for (std::size_t index = 0U; index < positionFields.size(); ++index) {
        if (!lookupRequired(env, positionFields[index], [&] {
                return env->GetFieldID(entity, profile.positionFields[index].c_str(), "D");
            })) {
            return false;
        }
    }

    const bool loadedEntitiesResolved = !profile.loadedEntitiesField.empty()
        ? lookupRequired(env, candidate.loadedEntitiesField, [&] {
              return env->GetFieldID(world, profile.loadedEntitiesField.c_str(),
                                     "Ljava/util/List;");
          })
        : lookupRequired(env, candidate.getLoadedEntities, [&] {
              return env->GetMethodID(world, profile.getLoadedEntities.c_str(),
                                      "()Ljava/util/List;");
          });
    if (!loadedEntitiesResolved ||
        !lookupRequired(env, candidate.playerEntities, [&] {
            return env->GetFieldID(world, profile.playerEntitiesField.c_str(),
                                   "Ljava/util/List;");
        }) ||
        !lookupRequired(env, candidate.getName, [&] {
            return env->GetMethodID(entity, profile.getName.c_str(),
                                    "()Ljava/lang/String;");
        }) ||
        !lookupRequired(env, candidate.getDisplayName, [&] {
            return env->GetMethodID(entity, profile.getDisplayName.c_str(),
                                    (std::string("()") + profile.chatComponentSignature).c_str());
        }) ||
        !lookupRequired(env, candidate.getFormattedText, [&] {
            return env->GetMethodID(chatComponent, profile.getFormattedText.c_str(),
                                    "()Ljava/lang/String;");
        })) {
        return false;
    }
    // Invisibility is auxiliary: an older external mapping dictionary may not
    // provide it, but that must not disable the otherwise safe core binding.
    if (!profile.isInvisible.empty()) {
        candidate.isInvisible = env->GetMethodID(
            entity, profile.isInvisible.c_str(), "()Z");
        if (env->ExceptionCheck() == JNI_TRUE || candidate.isInvisible == nullptr) {
            env->ExceptionClear();
            candidate.isInvisible = nullptr;
            log::info(std::string("Invisibility capability disabled for profile: ") +
                      profile.label + " (auxiliary mapping did not resolve).");
        }
    }
    if (debugChatClassLoaded && !profile.addChatMessage.empty()) {
        candidate.chatTextConstructor = env->GetMethodID(
            chatText, "<init>", "(Ljava/lang/String;)V");
        candidate.addChatMessage = env->GetMethodID(
            player, profile.addChatMessage.c_str(),
            (std::string("(") + profile.chatComponentSignature + ")V").c_str());
        if (env->ExceptionCheck() == JNI_TRUE ||
            candidate.chatTextConstructor == nullptr || candidate.addChatMessage == nullptr) {
            env->ExceptionClear();
            candidate.chatTextConstructor = nullptr;
            candidate.addChatMessage = nullptr;
            log::info(std::string("Debug chat capability disabled for profile: ") +
                      profile.label + " (auxiliary mapping did not resolve).");
        }
    }
    if (richChatClassLoaded && !profile.parseChatJson.empty()) {
        candidate.parseChatJson = env->GetStaticMethodID(
            chatSerializer, profile.parseChatJson.c_str(),
            (std::string("(Ljava/lang/String;)") +
             profile.chatComponentSignature).c_str());
        if (env->ExceptionCheck() == JNI_TRUE || candidate.parseChatJson == nullptr) {
            env->ExceptionClear();
            candidate.parseChatJson = nullptr;
            log::info(std::string("Rich local chat capability disabled for profile: ") +
                      profile.label + " (auxiliary mapping did not resolve).");
        }
    }
    jclass listClass = nullptr;
    if (!lookupRequired(env, listClass,
                        [&] { return env->FindClass("java/util/List"); })) {
        return false;
    }
    localReferences.add(listClass);
    jclass collectionClass = nullptr;
    if (!lookupRequired(env, collectionClass,
                        [&] { return env->FindClass("java/util/Collection"); })) {
        return false;
    }
    localReferences.add(collectionClass);
    if (!lookupRequired(env, candidate.listSize,
                        [&] { return env->GetMethodID(listClass, "size", "()I"); }) ||
        !lookupRequired(env, candidate.listGet,
                        [&] { return env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;"); }) ||
        !lookupRequired(env, candidate.listToArray,
                        [&] { return env->GetMethodID(listClass, "toArray", "()[Ljava/lang/Object;"); }) ||
        !lookupRequired(env, candidate.collectionToArray,
                        [&] { return env->GetMethodID(collectionClass, "toArray", "()[Ljava/lang/Object;"); }) ||
        !lookupRequired(env, candidate.blockPosConstructor, [&] {
            return env->GetMethodID(blockPos, "<init>", "(III)V");
        }) ||
        !lookupRequired(env, candidate.getBlockState, [&] {
            return env->GetMethodID(world, profile.getBlockState.c_str(),
                                    getBlockStateSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.getBlock, [&] {
            return env->GetMethodID(state, profile.getBlock.c_str(),
                                    getBlockSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.getBlockMetadata, [&] {
            return env->GetMethodID(block, profile.getBlockMetadata.c_str(),
                                    getBlockMetadataSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.getChunkProvider, [&] {
            const std::string signature = std::string("()") +
                                          profile.chunkProviderInterfaceSignature;
            return env->GetMethodID(world, profile.getChunkProvider.c_str(),
                                    signature.c_str());
        }) ||
        !lookupRequired(env, candidate.chunkListingField, [&] {
            return env->GetFieldID(chunkProvider, profile.chunkListingField.c_str(),
                                   "Ljava/util/List;");
        }) ||
        !lookupRequired(env, candidate.chunkX, [&] {
            return env->GetFieldID(chunk, profile.chunkCoordinateFields[0].c_str(), "I");
        }) ||
        !lookupRequired(env, candidate.chunkZ, [&] {
            return env->GetFieldID(chunk, profile.chunkCoordinateFields[1].c_str(), "I");
        }) ||
        !lookupRequired(env, candidate.getStorageArrays, [&] {
            return env->GetMethodID(chunk, profile.getStorageArrays.c_str(),
                                    getStorageArraysSignature.c_str());
        }) ||
        !lookupRequired(env, candidate.getStorageData, [&] {
            return env->GetMethodID(storage, profile.getStorageData.c_str(), "()[C");
        }) ||
        !lookupRequired(env, candidate.activeModelView, [&] {
            return env->GetStaticFieldID(activeRenderInfo,
                                         profile.activeModelViewField.c_str(),
                                         "Ljava/nio/FloatBuffer;");
        }) ||
        !lookupRequired(env, candidate.activeProjection, [&] {
            return env->GetStaticFieldID(activeRenderInfo,
                                         profile.activeProjectionField.c_str(),
                                         "Ljava/nio/FloatBuffer;");
        }) ||
        !lookupRequired(env, candidate.activeViewport, [&] {
            return env->GetStaticFieldID(activeRenderInfo,
                                         profile.activeViewportField.c_str(),
                                         "Ljava/nio/IntBuffer;");
        }) ||
        !lookupRequired(env, candidate.renderPartialTicks, [&] {
            return env->GetFieldID(timer, profile.renderPartialTicksField.c_str(), "F");
        })) {
        return false;
    }

    bool sidebarCapability = sidebarClassesLoaded &&
        !profile.teamSignature.empty() &&
        !profile.getScoreboard.empty() &&
        !profile.getObjectiveInDisplaySlot.empty() &&
        !profile.getPlayersTeam.empty() &&
        !profile.getSortedScores.empty() &&
        !profile.getPlayerName.empty() &&
        !profile.formatPlayerName.empty();
    if (sidebarCapability) {
        sidebarCapability =
            lookupRequired(env, candidate.getScoreboard, [&] {
                return env->GetMethodID(world, profile.getScoreboard.c_str(),
                                        (std::string("()") + profile.scoreboardSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getObjectiveInDisplaySlot, [&] {
                return env->GetMethodID(scoreboard, profile.getObjectiveInDisplaySlot.c_str(),
                                        getObjectiveInDisplaySlotSignature.c_str());
            }) &&
            lookupRequired(env, candidate.getPlayersTeam, [&] {
                return env->GetMethodID(scoreboard, profile.getPlayersTeam.c_str(),
                                        getPlayersTeamSignature.c_str());
            }) &&
            lookupRequired(env, candidate.getSortedScores, [&] {
                return env->GetMethodID(scoreboard, profile.getSortedScores.c_str(),
                                        getSortedScoresSignature.c_str());
            }) &&
            lookupRequired(env, candidate.getPlayerName, [&] {
                return env->GetMethodID(score, profile.getPlayerName.c_str(),
                                        "()Ljava/lang/String;");
            }) &&
            lookupRequired(env, candidate.formatPlayerName, [&] {
                return env->GetStaticMethodID(scorePlayerTeam,
                                              profile.formatPlayerName.c_str(),
                                              formatPlayerNameSignature.c_str());
            });
    }
    if (!sidebarCapability) {
        candidate.getScoreboard = nullptr;
        candidate.getObjectiveInDisplaySlot = nullptr;
        candidate.getPlayersTeam = nullptr;
        candidate.getSortedScores = nullptr;
        candidate.getPlayerName = nullptr;
        candidate.formatPlayerName = nullptr;
        log::info(std::string("Sidebar capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    bool armorCapability = armorClassesLoaded &&
        !profile.getItem.empty() &&
        !profile.hasColor.empty() &&
        !profile.getColor.empty() && !profile.getEnchantmentLevel.empty() &&
        !profile.getEquipmentInSlot.empty() && !profile.getIdFromItem.empty() &&
        !profile.stackSizeField.empty() && !profile.getItemDamage.empty();
    if (armorCapability) {
        armorCapability =
            lookupRequired(env, candidate.getItem, [&] {
                return env->GetMethodID(itemStack, profile.getItem.c_str(),
                                        getItemSignature.c_str());
            }) &&
            lookupRequired(env, candidate.hasColor, [&] {
                return env->GetMethodID(itemArmor, profile.hasColor.c_str(),
                                        (std::string("(") + profile.itemStackSignature + ")Z").c_str());
            }) &&
            lookupRequired(env, candidate.getColor, [&] {
                return env->GetMethodID(itemArmor, profile.getColor.c_str(),
                                        (std::string("(") + profile.itemStackSignature + ")I").c_str());
            }) &&
            lookupRequired(env, candidate.getEnchantmentLevel, [&] {
                return env->GetStaticMethodID(
                    enchantmentHelper, profile.getEnchantmentLevel.c_str(),
                    (std::string("(I") + profile.itemStackSignature + ")I").c_str());
            }) &&
            lookupRequired(env, candidate.getEquipmentInSlot, [&] {
                return env->GetMethodID(
                    living, profile.getEquipmentInSlot.c_str(),
                    (std::string("(I)") + profile.itemStackSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getIdFromItem, [&] {
                return env->GetStaticMethodID(
                    item, profile.getIdFromItem.c_str(),
                    (std::string("(") + profile.itemSignature + ")I").c_str());
            }) &&
            lookupRequired(env, candidate.stackSize, [&] {
                return env->GetFieldID(itemStack, profile.stackSizeField.c_str(), "I");
            }) &&
            lookupRequired(env, candidate.getItemDamage, [&] {
                return env->GetMethodID(itemStack, profile.getItemDamage.c_str(), "()I");
            });
    }
    if (!armorCapability) {
        candidate.inventoryField = nullptr;
        candidate.armorInventoryField = nullptr;
        candidate.getItem = nullptr;
        candidate.hasColor = nullptr;
        candidate.getColor = nullptr;
        candidate.getEnchantmentLevel = nullptr;
        candidate.getEquipmentInSlot = nullptr;
        candidate.getIdFromItem = nullptr;
        candidate.stackSize = nullptr;
        candidate.getItemDamage = nullptr;
        log::info(std::string("Armor capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    bool identityCapability = uuid != nullptr && !profile.getUniqueId.empty();
    if (identityCapability) {
        identityCapability =
            lookupRequired(env, candidate.getUniqueId, [&] {
                return env->GetMethodID(entity, profile.getUniqueId.c_str(),
                                        "()Ljava/util/UUID;");
            }) &&
            lookupRequired(env, candidate.uuidToString, [&] {
                return env->GetMethodID(uuid, "toString", "()Ljava/lang/String;");
            });
    }
    if (!identityCapability) {
        candidate.getUniqueId = nullptr;
        candidate.uuidToString = nullptr;
        log::info(std::string("UUID identity capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    bool skinCapability = skinClassesLoaded &&
        !profile.getLocationSkin.empty() && !profile.getTextureManager.empty() &&
        !profile.getTexture.empty() && !profile.getGlTextureId.empty();
    if (skinCapability) {
        skinCapability =
            lookupRequired(env, candidate.getLocationSkin, [&] {
                return env->GetMethodID(
                    abstractClientPlayer, profile.getLocationSkin.c_str(),
                    (std::string("()") + profile.resourceLocationSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getTextureManager, [&] {
                return env->GetMethodID(
                    minecraft, profile.getTextureManager.c_str(),
                    (std::string("()") + profile.textureManagerSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getTexture, [&] {
                return env->GetMethodID(
                    textureManager, profile.getTexture.c_str(),
                    (std::string("(") + profile.resourceLocationSignature + ")" +
                     profile.textureObjectSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getGlTextureId, [&] {
                return env->GetMethodID(textureObject, profile.getGlTextureId.c_str(), "()I");
            });
    }
    if (!skinCapability) {
        candidate.getLocationSkin = nullptr;
        candidate.getTextureManager = nullptr;
        candidate.getTexture = nullptr;
        candidate.getGlTextureId = nullptr;
        log::info(std::string("Player skin capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    // Resolve the minimal Aim Assist surface independently. Previously these
    // four fields lived inside the all-or-nothing movement/scaffold chain, so a
    // missing ItemBlock/PlayerController mapping silently disabled aim on an
    // otherwise supported transformed client.
    bool aimCapability = gameSettingsClassLoaded &&
        !profile.gameSettingsField.empty() &&
        !profile.mouseSensitivityField.empty() &&
        !profile.rotationYawField.empty() &&
        !profile.rotationPitchField.empty();
    if (aimCapability) {
        aimCapability =
            lookupRequired(env, candidate.gameSettingsField, [&] {
                return env->GetFieldID(minecraft,
                    profile.gameSettingsField.c_str(),
                    profile.gameSettingsSignature.c_str());
            }) &&
            lookupRequired(env, candidate.mouseSensitivity, [&] {
                return env->GetFieldID(gameSettings,
                    profile.mouseSensitivityField.c_str(), "F");
            }) &&
            lookupRequired(env, candidate.rotationYaw, [&] {
                return env->GetFieldID(entity,
                    profile.rotationYawField.c_str(), "F");
            }) &&
            lookupRequired(env, candidate.rotationPitch, [&] {
                return env->GetFieldID(entity,
                    profile.rotationPitchField.c_str(), "F");
            });
    }
    // Optional to movement, required to the frame-rate aim output. A missing
    // historical-rotation mapping must never disable unrelated movement.
    if (!profile.previousRotationYawField.empty())
        (void)lookupRequired(env, candidate.previousRotationYaw, [&] {
            return env->GetFieldID(entity, profile.previousRotationYawField.c_str(), "F");
        });
    if (!profile.previousRotationPitchField.empty())
        (void)lookupRequired(env, candidate.previousRotationPitch, [&] {
            return env->GetFieldID(entity, profile.previousRotationPitchField.c_str(), "F");
        });
    bool freeLookCapability=aimCapability&&entityRendererClassLoaded&&
        candidate.previousRotationYaw&&candidate.previousRotationPitch&&
        !profile.thirdPersonViewField.empty()&&
        !profile.updateCameraAndRender.empty()&&!profile.orientCamera.empty()&&
        !profile.setAngles.empty();
    if(freeLookCapability) {
        freeLookCapability=
            lookupRequired(env,candidate.thirdPersonView,[&] {
                return env->GetFieldID(gameSettings,
                    profile.thirdPersonViewField.c_str(),"I");
            })&&
            lookupRequired(env,candidate.updateCameraAndRender,[&] {
                return env->GetMethodID(entityRenderer,
                    profile.updateCameraAndRender.c_str(),"(FJ)V");
            })&&
            lookupRequired(env,candidate.orientCamera,[&] {
                return env->GetMethodID(entityRenderer,
                    profile.orientCamera.c_str(),"(F)V");
            })&&
            lookupRequired(env,candidate.setAngles,[&] {
                return env->GetMethodID(entity,profile.setAngles.c_str(),"(FF)V");
            });
    }
    if(!freeLookCapability) {
        candidate.thirdPersonView=nullptr;
        candidate.updateCameraAndRender=nullptr;
        candidate.orientCamera=nullptr;
        candidate.setAngles=nullptr;
        candidate.setupTerrain=nullptr;
        log::info(std::string("FreeLook capability disabled for profile: ")+
                  profile.label+" (camera mapping did not resolve).");
    }
    if(freeLookCapability&&renderGlobalClassLoaded&&
       !profile.setupTerrain.empty()&&!profile.setupTerrainDescriptor.empty()) {
        (void)lookupRequired(env,candidate.setupTerrain,[&] {
            return env->GetMethodID(renderGlobal,profile.setupTerrain.c_str(),
                profile.setupTerrainDescriptor.c_str());
        });
        if(!candidate.setupTerrain)
            log::info(std::string("FreeLook terrain hook unavailable for profile: ")+
                      profile.label+" (setupTerrain mapping did not resolve).");
    }
    if (!aimCapability) {
        candidate.gameSettingsField = nullptr;
        candidate.mouseSensitivity = nullptr;
        candidate.rotationYaw = nullptr;
        candidate.rotationPitch = nullptr;
        log::info(std::string("Aim capability disabled for profile: ") +
                  profile.label + " (minimal mapping did not resolve).");
    }

    if (!profile.currentScreenField.empty() && !profile.guiScreenSignature.empty()) {
        (void)lookupRequired(env, candidate.currentScreen, [&] {
            return env->GetFieldID(minecraft, profile.currentScreenField.c_str(),
                                   profile.guiScreenSignature.c_str());
        });
    }
    (void)lookupRequired(env, candidate.aabbConstructor, [&] {
        return env->GetMethodID(aabb, "<init>", "(DDDDDD)V");
    });
    if (!profile.getCollidingBoundingBoxes.empty()) {
        (void)lookupRequired(env, candidate.getCollidingBoxes, [&] {
            return env->GetMethodID(world, profile.getCollidingBoundingBoxes.c_str(),
                (std::string("(") + profile.entitySignature + profile.aabbSignature +
                 ")Ljava/util/List;").c_str());
        });
    }
    bool safewalkCapability = safewalkClassesLoaded && aimCapability &&
        !profile.keyBindSneakField.empty() && !profile.getKeyCode.empty() &&
        !profile.setKeyBindState.empty() && !profile.isAirBlock.empty();
    if (safewalkCapability) {
        safewalkCapability =
            lookupRequired(env, candidate.keyBindSneakField, [&] {
                return env->GetFieldID(gameSettings,
                    profile.keyBindSneakField.c_str(),
                    profile.keyBindingSignature.c_str());
            }) &&
            lookupRequired(env, candidate.getKeyCode, [&] {
                return env->GetMethodID(keyBinding,
                    profile.getKeyCode.c_str(), "()I");
            }) &&
            lookupRequired(env, candidate.setKeyBindState, [&] {
                return env->GetStaticMethodID(keyBinding,
                    profile.setKeyBindState.c_str(), "(IZ)V");
            }) &&
            lookupRequired(env, candidate.isAirBlock, [&] {
                return env->GetMethodID(world, profile.isAirBlock.c_str(),
                    (std::string("(") + profile.blockPosSignature + ")Z").c_str());
            });
    }
    if (!safewalkCapability) {
        candidate.keyBindSneakField = nullptr;
        candidate.getKeyCode = nullptr;
        candidate.setKeyBindState = nullptr;
        candidate.isAirBlock = nullptr;
        log::info(std::string("Safewalk capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }
    if(safewalkCapability&&!profile.keyBindSprintField.empty()) {
        candidate.keyBindSprintField=env->GetFieldID(gameSettings,
            profile.keyBindSprintField.c_str(),profile.keyBindingSignature.c_str());
        clearException(env); // Sprint is optional; never disable locomotion.
    }

    bool serverGuardCapability = serverDataClassLoaded &&
        !profile.getCurrentServerData.empty() && !profile.serverIpField.empty();
    if (serverGuardCapability) {
        serverGuardCapability =
            lookupRequired(env, candidate.getCurrentServerData, [&] {
                return env->GetMethodID(minecraft,
                    profile.getCurrentServerData.c_str(),
                    (std::string("()") + profile.serverDataSignature).c_str());
            }) &&
            lookupRequired(env, candidate.serverIp, [&] {
                return env->GetFieldID(serverData, profile.serverIpField.c_str(),
                                       "Ljava/lang/String;");
            });
    }
    if (!serverGuardCapability) {
        candidate.getCurrentServerData = nullptr;
        candidate.serverIp = nullptr;
        log::info(std::string("Server-address guard disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    // Resolve locomotion independently: a missing placement/attack overload
    // must not erase motion fields or disable edge protection and flight.
    bool movementCapability = safewalkCapability;
    if (movementCapability) {
        for (std::size_t index = 0U;
             index < candidate.movementKeyFields.size(); ++index) {
            movementCapability = movementCapability && lookupRequired(
                env, candidate.movementKeyFields[index], [&] {
                    return env->GetFieldID(gameSettings,
                        profile.movementKeyFields[index].c_str(),
                        profile.keyBindingSignature.c_str());
                });
        }
        for (std::size_t index = 0U; index < candidate.motionFields.size(); ++index) {
            movementCapability = movementCapability && lookupRequired(
                env, candidate.motionFields[index], [&] {
                    return env->GetFieldID(entity,
                        profile.motionFields[index].c_str(), "D");
                });
        }
        movementCapability = movementCapability &&
            lookupRequired(env, candidate.onGround, [&] {
                return env->GetFieldID(entity,
                    profile.onGroundField.c_str(), "Z");
            }) &&
            lookupRequired(env, candidate.jump, [&] {
                return env->GetMethodID(living, profile.jump.c_str(), "()V");
            });
        for (std::size_t index = 0U;
             index < candidate.movementInputFields.size(); ++index) {
            movementCapability = movementCapability && lookupRequired(
                env, candidate.movementInputFields[index], [&] {
                    return env->GetFieldID(living,
                        profile.movementInputFields[index].c_str(), "F");
                });
        }
    }
    // PlayerController is a shared binding for attacks, block interaction and
    // placement.  Resolve it once, independently from all inventory methods.
    const bool controllerFieldAvailable = playerControllerClassLoaded &&
        !profile.playerControllerField.empty() &&
        lookupRequired(env, candidate.playerControllerField, [&] {
            return env->GetFieldID(minecraft,
                profile.playerControllerField.c_str(),
                profile.playerControllerSignature.c_str());
        });
    bool placementCapability = movementClassesLoaded && controllerFieldAvailable;
    if (placementCapability) {
        const std::string rightClickSignature = std::string("(") +
            profile.playerSignature + profile.worldClientSignature +
            profile.itemStackSignature + profile.blockPosSignature +
            profile.enumFacingSignature + profile.vec3Signature + ")Z";
        placementCapability = lookupRequired(env, candidate.inventoryField, [&] {
                return env->GetFieldID(player, profile.inventoryField.c_str(),
                                       profile.inventoryPlayerSignature.c_str());
            }) &&
            lookupRequired(env, candidate.currentItem, [&] {
                return env->GetFieldID(inventoryPlayer,
                    profile.currentItemField.c_str(), "I");
            }) &&
            lookupRequired(env, candidate.mainInventory, [&] {
                return env->GetFieldID(inventoryPlayer,
                    profile.mainInventoryField.c_str(),
                    (std::string("[") + profile.itemStackSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getItem, [&] {
                return env->GetMethodID(itemStack, profile.getItem.c_str(),
                    (std::string("()") + profile.itemSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getBlockFromItem, [&] {
                return env->GetMethodID(itemBlock,
                    profile.getBlockFromItem.c_str(),
                    (std::string("()") + profile.blockSignature).c_str());
            }) &&
            lookupRequired(env, candidate.getIdFromBlock, [&] {
                return env->GetStaticMethodID(block,
                    profile.getIdFromBlock.c_str(),
                    (std::string("(") + profile.blockSignature + ")I").c_str());
            }) &&
            lookupRequired(env, candidate.getFacingByIndex, [&] {
                return env->GetStaticMethodID(enumFacing,
                    profile.getFacingByIndex.c_str(),
                    (std::string("(I)") + profile.enumFacingSignature).c_str());
            }) &&
            lookupRequired(env, candidate.vec3Constructor, [&] {
                return env->GetMethodID(vec3, "<init>", "(DDD)V");
            }) &&
            lookupRequired(env, candidate.onPlayerRightClick, [&] {
                return env->GetMethodID(playerController,
                    profile.onPlayerRightClick.c_str(),
                    rightClickSignature.c_str());
            }) &&
            lookupRequired(env,candidate.syncCurrentPlayItem,[&] {
                return env->GetMethodID(playerController,
                    profile.syncCurrentPlayItem.c_str(),"()V");
            });
    }
    bool smartHotbarCapability=safewalkCapability&&placementCapability&&
        itemSwordClassLoaded&&!profile.keyBindsHotbarField.empty()&&
        !profile.windowClick.empty();
    if(smartHotbarCapability) {
        smartHotbarCapability=
            lookupRequired(env,candidate.keyBindingIsPressed,[&] {
                return env->GetMethodID(keyBinding,profile.keyBindingIsPressed.c_str(),"()Z");
            })&&
            lookupRequired(env,candidate.keyBindsHotbar,[&] {
                return env->GetFieldID(gameSettings,
                    profile.keyBindsHotbarField.c_str(),
                    (std::string("[")+profile.keyBindingSignature).c_str());
            })&&
            lookupRequired(env,candidate.windowClick,[&] {
                return env->GetMethodID(playerController,
                    profile.windowClick.c_str(),
                    (std::string("(IIII")+profile.entityPlayerSignature+")"+
                     profile.itemStackSignature).c_str());
            });
    }
    if(!smartHotbarCapability) {
        candidate.keyBindsHotbar=nullptr;
        candidate.windowClick=nullptr;
        log::info(std::string("Smart Hotbar capability disabled for profile: ")+
                  profile.label+" (auxiliary mapping did not resolve).");
    }
    bool bedBreakerCapability = playerControllerClassLoaded &&
        enumFacingClassLoaded && itemClassesLoaded && controllerFieldAvailable &&
        !profile.clickBlock.empty() && !profile.onPlayerDamageBlock.empty() &&
        !profile.resetBlockRemoving.empty() &&
        !profile.getBlockReachDistance.empty() && !profile.getStrVsBlock.empty();
    if (bedBreakerCapability) {
        const std::string blockInteractionSignature = std::string("(") +
            profile.blockPosSignature + profile.enumFacingSignature + ")Z";
        bedBreakerCapability =
            lookupRequired(env, candidate.clickBlock, [&] {
                return env->GetMethodID(playerController,
                    profile.clickBlock.c_str(), blockInteractionSignature.c_str());
            }) &&
            lookupRequired(env, candidate.onPlayerDamageBlock, [&] {
                return env->GetMethodID(playerController,
                    profile.onPlayerDamageBlock.c_str(),
                    blockInteractionSignature.c_str());
            }) &&
            lookupRequired(env, candidate.resetBlockRemoving, [&] {
                return env->GetMethodID(playerController,
                    profile.resetBlockRemoving.c_str(), "()V");
            }) &&
            lookupRequired(env, candidate.getBlockReachDistance, [&] {
                return env->GetMethodID(playerController,
                    profile.getBlockReachDistance.c_str(), "()F");
            }) &&
            lookupRequired(env, candidate.getStrVsBlock, [&] {
                return env->GetMethodID(itemStack, profile.getStrVsBlock.c_str(),
                    (std::string("(") + profile.blockSignature + ")F").c_str());
            });
        bedBreakerCapability = bedBreakerCapability &&
            candidate.playerControllerField != nullptr &&
            candidate.inventoryField != nullptr && candidate.currentItem != nullptr &&
            candidate.mainInventory != nullptr && candidate.getItem != nullptr &&
            candidate.getFacingByIndex != nullptr;
    }
    if (playerControllerClassLoaded && !profile.attackEntity.empty()) {
        (void)lookupRequired(env, candidate.attackEntity, [&] {
            return env->GetMethodID(playerController, profile.attackEntity.c_str(),
                (std::string("(") + profile.entityPlayerSignature +
                 profile.entitySignature + ")V").c_str());
        });
    }
    if (!movementCapability) {
        candidate.movementKeyFields.fill(nullptr);
        candidate.motionFields.fill(nullptr);
        candidate.onGround = nullptr;
        candidate.jump = nullptr;
        log::info(std::string("Locomotion mapping unavailable: ") + profile.label);
    }
    if (!placementCapability) {
        if (!bedBreakerCapability) {
            candidate.currentItem = nullptr;
            candidate.mainInventory = nullptr;
            candidate.getFacingByIndex = nullptr;
        }
        candidate.getBlockFromItem = nullptr;
        candidate.getIdFromBlock = nullptr;
        candidate.vec3Constructor = nullptr;
        candidate.onPlayerRightClick = nullptr;
        log::info(std::string("Scaffold capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }
    if (!bedBreakerCapability) {
        candidate.clickBlock = nullptr;
        candidate.onPlayerDamageBlock = nullptr;
        candidate.resetBlockRemoving = nullptr;
        candidate.getBlockReachDistance = nullptr;
        candidate.getStrVsBlock = nullptr;
        log::info(std::string("Local BedBreaker capability disabled for profile: ") +
                  profile.label + " (block interaction mapping did not resolve).");
    }

    // Read-only movement telemetry must not depend on jump/key/scaffold
    // methods resolving. Rendering/prediction also needs these on Lunar.
    for (std::size_t axis = 0; axis < candidate.motionFields.size(); ++axis) {
        if (!candidate.motionFields[axis] && !profile.motionFields[axis].empty())
            (void)lookupRequired(env, candidate.motionFields[axis], [&] {
                return env->GetFieldID(entity, profile.motionFields[axis].c_str(), "D");
            });
    }
    if (!candidate.onGround && !profile.onGroundField.empty())
        (void)lookupRequired(env, candidate.onGround, [&] {
            return env->GetFieldID(entity, profile.onGroundField.c_str(), "Z");
        });

    std::array<jfieldID, 6U> boundsFields{};
    for (std::size_t index = 0U; index < boundsFields.size(); ++index) {
        if (!lookupRequired(env, boundsFields[index], [&] {
                return env->GetFieldID(aabb, profile.aabbFields[index].c_str(), "D");
            })) {
            return false;
        }
    }

    candidate.positionX = positionFields[0U];
    candidate.positionY = positionFields[1U];
    candidate.positionZ = positionFields[2U];
    candidate.minX = boundsFields[0U];
    candidate.minY = boundsFields[1U];
    candidate.minZ = boundsFields[2U];
    candidate.maxX = boundsFields[3U];
    candidate.maxY = boundsFields[4U];
    candidate.maxZ = boundsFields[5U];

    auto makeGlobal = [&](jclass const local, jclass& global) noexcept {
        global = static_cast<jclass>(env->NewGlobalRef(local));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            if (global != nullptr) {
                env->DeleteGlobalRef(global);
                global = nullptr;
            }
            return false;
        }
        return global != nullptr;
    };
    // These optional IDs are independent of Scaffold/movement. Bow and silent
    // targeting must work even if an unrelated inventory mapping is missing.
    if (loadFeatureClass(rayVector, profile.vec3Name, "Ray trace", "Vec3") &&
        loadFeatureClass(rayHit, profile.rayHitName, "Ray trace", "MovingObjectPosition") &&
        makeGlobal(rayVector, candidate.rayVectorClass) &&
        makeGlobal(rayHit, candidate.rayHitClass)) {
        const auto method=[&](jmethodID& out,jclass type,const std::string& name,const std::string& signature) {
            if(!name.empty()) (void)lookupRequired(env,out,[&] { return env->GetMethodID(type,name.c_str(),signature.c_str()); });
        };
        const auto field=[&](jfieldID& out,jclass type,const std::string& name,const std::string& signature) {
            if(!name.empty()) (void)lookupRequired(env,out,[&] { return env->GetFieldID(type,name.c_str(),signature.c_str()); });
        };
        method(candidate.rayVectorConstructor,rayVector,"<init>","(DDD)V");
        method(candidate.rayTraceBlocks,world,profile.rayTraceBlocks,
            "("+profile.vec3Signature+profile.vec3Signature+"ZZZ)"+profile.rayHitSignature);
        method(candidate.getEntityById,world,profile.getEntityById,"(I)"+profile.entitySignature);
        method(candidate.isUsingItem,player,profile.isUsingItem,"()Z");
        method(candidate.getItemUseDuration,player,profile.getItemUseDuration,"()I");
        method(candidate.getEyeHeight,entity,profile.getEyeHeight,"()F");
        field(candidate.hitVector,rayHit,profile.hitVectorField,profile.vec3Signature);
        // Forge's 1.8.9 joined.srg: ave/s, auh/d and pk/W. Named/SRG clients
        // use the first two alternatives; descriptors disambiguate Notch IDs.
        for(const char* name:{"objectMouseOver","field_71476_x","s"}) {
            field(candidate.cameraMouseOver,minecraft,name,profile.rayHitSignature);
            if(candidate.cameraMouseOver) break;
        }
        for(const char* name:{"entityHit","field_72308_g","d"}) {
            field(candidate.cameraHitEntity,rayHit,name,profile.entitySignature);
            if(candidate.cameraHitEntity) break;
        }
        const bool notchHit=profile.rayHitName.find('.')==std::string::npos;
        const std::string hitTypeSignature=notchHit ? "Lauh$a;" :
            "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;";
        for(const char* name:{"typeOfHit","field_72313_a","a"}) {
            field(candidate.cameraHitType,rayHit,name,hitTypeSignature);
            if(candidate.cameraHitType) break;
        }
        jclass enumClass=env->FindClass("java/lang/Enum");
        if(enumClass) {method(candidate.enumOrdinal,enumClass,"ordinal","()I");env->DeleteLocalRef(enumClass);}
        for(const char* name:{"ticksExisted","field_70173_aa","W"}) {
            field(candidate.entityTicks,entity,name,"I"); if(candidate.entityTicks) break;
        }
        for(const char* name:{"isSneaking","func_70093_af","av"}) {
            method(candidate.isSneaking,entity,name,"()Z"); if(candidate.isSneaking) break;
        }
        jclass digging=nullptr;
        const bool notch=profile.movementPacketName.find('.')==std::string::npos;
        const std::string digName=notch ? "ir" : "net.minecraft.network.play.client.C07PacketPlayerDigging";
        if(loadFeatureClass(digging,digName,"Interaction diagnostics","C07PacketPlayerDigging") &&
           makeGlobal(digging,candidate.diggingPacketClass)) {
            const std::string actionSignature=notch ? "()Lir$a;" :
                "()Lnet/minecraft/network/play/client/C07PacketPlayerDigging$Action;";
            for(const char* name:{"getPosition","func_179715_a","a"}) {
                method(candidate.diggingPosition,digging,name,"()"+profile.blockPosSignature);
                if(candidate.diggingPosition) break;
            }
            for(const char* name:{"getStatus","func_180762_c","c"}) {
                method(candidate.diggingAction,digging,name,actionSignature);
                if(candidate.diggingAction) break;
            }
        }
        method(candidate.clickMouse,minecraft,profile.clickMouse,"()V");
        method(candidate.rightClickMouse,minecraft,profile.rightClickMouse,"()V");
        const std::string knockSignature="("+profile.entitySignature+"FDD)V";
        method(candidate.knockBack,living,profile.knockBack,knockSignature.c_str());
        jclass velocityPacket=nullptr;
        if(netHandler&&loadFeatureClass(velocityPacket,profile.velocityPacketName,
                "Attack Shield","S12PacketEntityVelocity")) {
            method(candidate.handleEntityVelocity,netHandler,profile.handleEntityVelocity,
                "("+profile.velocityPacketSignature+")V");
            method(candidate.velocityEntityId,velocityPacket,profile.velocityEntityId,"()I");
        }
        method(candidate.sendClickBlock,minecraft,profile.sendClickBlock,"(Z)V");
        method(candidate.moveFlying,entity,profile.moveFlying,"(FFF)V");
        method(candidate.moveEntityWithHeading,living,
               profile.moveEntityWithHeading,"(FF)V");
        method(candidate.getAIMoveSpeed,living,profile.getAIMoveSpeed,"()F");
        method(candidate.isSprinting,entity,profile.isSprinting,"()Z");
        method(candidate.setSprinting,entity,profile.setSprinting,"(Z)V");
        method(candidate.swingItem,player,profile.swingItem,"()V");
        field(candidate.rayBlockPos,rayHit,profile.rayBlockPosField,
              profile.blockPosSignature);
        field(candidate.raySideHit,rayHit,profile.raySideHitField,
              profile.enumFacingSignature);
        for(std::size_t i=0;i<candidate.blockPosCoordinates.size();++i)
            method(candidate.blockPosCoordinates[i],blockPos,
                   profile.blockPosCoordinateMethods[i],"()I");
        if(enumFacingClassLoaded)
            method(candidate.facingIndex,enumFacing,profile.facingIndexMethod,"()I");
        for(std::size_t i=0;i<3;++i) field(candidate.vectorFields[i],rayVector,profile.vectorFields[i],"D");
    }
    // Silent Lock replaces the argument of addToSendQueue(Packet), before the
    // concrete C05/C06 serializer is selected. C03/C04 become C05/C06 and an
    // already rotating packet is copied with the silent angles. The local
    // EntityPlayerSP yaw/pitch and render matrices are never written.
    const bool silentPacketClassesLoaded =
        loadFeatureClass(packetNetHandler, profile.netHandlerName,
                         "Silent rotation", "NetHandlerPlayClient") &&
        loadFeatureClass(networkPacket, profile.networkPacketName,
                         "Silent rotation", "Packet") &&
        loadFeatureClass(movementPacket, profile.movementPacketName,
                         "Silent rotation", "C03PacketPlayer") &&
        loadFeatureClass(positionPacket, profile.positionPacketName,
                         "Silent rotation", "C04PacketPlayerPosition") &&
        loadFeatureClass(lookPacket, profile.lookPacketName,
                         "Silent rotation", "C05PacketPlayerLook") &&
        loadFeatureClass(positionLookPacket, profile.positionLookPacketName,
                         "Silent rotation", "C06PacketPlayerPosLook");
    if (silentPacketClassesLoaded &&
        makeGlobal(packetNetHandler, candidate.packetNetHandlerClass) &&
        makeGlobal(networkPacket, candidate.networkPacketClass) &&
        makeGlobal(movementPacket, candidate.movementPacketClass) &&
        makeGlobal(positionPacket, candidate.positionPacketClass) &&
        makeGlobal(lookPacket, candidate.lookPacketClass) &&
        makeGlobal(positionLookPacket, candidate.positionLookPacketClass)) {
        candidate.addToSendQueue = env->GetMethodID(
            packetNetHandler, profile.addToSendQueue.c_str(),
            (std::string("(") + profile.networkPacketSignature + ")V").c_str());
        candidate.movementPacketConstructor = env->GetMethodID(
            movementPacket, "<init>", "(Z)V");
        candidate.positionPacketConstructor = env->GetMethodID(
            positionPacket, "<init>", "(DDDZ)V");
        candidate.lookPacketConstructor = env->GetMethodID(
            lookPacket, "<init>", "(FFZ)V");
        candidate.positionLookPacketConstructor = env->GetMethodID(
            positionLookPacket, "<init>", "(DDDFFZ)V");
        for (std::size_t i=0; i<candidate.packetPosition.size(); ++i)
            candidate.packetPosition[i]=env->GetFieldID(
                movementPacket,profile.packetPositionFields[i].c_str(),"D");
        candidate.packetYaw = env->GetFieldID(
            movementPacket, profile.packetYawField.c_str(), "F");
        candidate.packetPitch = env->GetFieldID(
            movementPacket, profile.packetPitchField.c_str(), "F");
        candidate.packetOnGround = env->GetFieldID(
            movementPacket, profile.packetOnGroundField.c_str(), "Z");
        if (env->ExceptionCheck() == JNI_TRUE || !candidate.addToSendQueue ||
            !candidate.movementPacketConstructor || !candidate.positionPacketConstructor ||
            !candidate.lookPacketConstructor ||
            !candidate.positionLookPacketConstructor || !candidate.packetYaw ||
            !candidate.packetPitch || !candidate.packetOnGround ||
            std::any_of(candidate.packetPosition.begin(),candidate.packetPosition.end(),
                        [](jfieldID value){ return value==nullptr; })) {
            clearException(env);
            log::info(std::string("Silent rotation capability disabled for profile: ") +
                      profile.label + " (send-queue packet members did not resolve).");
        }
    }
    if (!makeGlobal(minecraft, candidate.minecraftClass) ||
        !makeGlobal(player, candidate.playerClass) ||
        !makeGlobal(living, candidate.livingClass) ||
        !makeGlobal(entity, candidate.entityClass) ||
        !makeGlobal(aabb, candidate.aabbClass) ||
        !makeGlobal(world, candidate.worldClass) ||
        !makeGlobal(worldClient, candidate.worldClientClass) ||
        !makeGlobal(state, candidate.stateClass) ||
        !makeGlobal(block, candidate.blockClass) ||
        !makeGlobal(blockPos, candidate.blockPosClass) ||
        !makeGlobal(bed, candidate.bedClass) ||
        !makeGlobal(chunkProvider, candidate.chunkProviderClass) ||
        !makeGlobal(chunk, candidate.chunkClass) ||
        !makeGlobal(storage, candidate.storageClass) ||
        !makeGlobal(activeRenderInfo, candidate.activeRenderInfoClass) ||
        !makeGlobal(renderManager, candidate.renderManagerClass) ||
        !makeGlobal(timer, candidate.timerClass) ||
        !makeGlobal(chatComponent, candidate.chatComponentClass)) {
        return false;
    }
    if (fireballClassLoaded && !makeGlobal(fireball, candidate.fireballClass))
        candidate.fireballClass = nullptr;
    if (hostileClassLoaded && !makeGlobal(hostile, candidate.hostileClass))
        candidate.hostileClass = nullptr;

    bool tabCapability = tabClassesLoaded &&
        !profile.getNetHandler.empty() && !profile.getPlayerInfoMap.empty() &&
        !profile.getGameProfile.empty();
    if (tabCapability) {
        tabCapability =
            lookupRequired(env, candidate.getNetHandler, [&] {
                return env->GetMethodID(minecraft, profile.getNetHandler.c_str(),
                                        getNetHandlerSignature.c_str());
            }) &&
            lookupRequired(env, candidate.getPlayerInfoMap, [&] {
                return env->GetMethodID(netHandler, profile.getPlayerInfoMap.c_str(),
                                        "()Ljava/util/Collection;");
            }) &&
            lookupRequired(env, candidate.getGameProfile, [&] {
                return env->GetMethodID(networkPlayerInfo,
                                        profile.getGameProfile.c_str(),
                                        "()Lcom/mojang/authlib/GameProfile;");
            }) &&
            lookupRequired(env, candidate.gameProfileGetName, [&] {
                return env->GetMethodID(gameProfile, "getName",
                                        "()Ljava/lang/String;");
            });
        if (tabCapability && candidate.uuidToString != nullptr) {
            candidate.gameProfileGetId = env->GetMethodID(
                gameProfile, "getId", "()Ljava/util/UUID;");
            if (env->ExceptionCheck() == JNI_TRUE ||
                candidate.gameProfileGetId == nullptr) {
                clearException(env);
                candidate.gameProfileGetId = nullptr;
                log::info(std::string("TAB UUID bridge disabled for profile: ") +
                          profile.label + " (GameProfile.getId did not resolve).");
            }
        }
    }

    if (!tabCapability) {
        candidate.getNetHandler = nullptr;
        candidate.getPlayerInfoMap = nullptr;
        candidate.getGameProfile = nullptr;
        candidate.gameProfileGetName = nullptr;
        candidate.gameProfileGetId = nullptr;
        log::info(std::string("TAB roster capability disabled for profile: ") +
                  profile.label + " (auxiliary mapping did not resolve).");
    }

    auto clearGlobal = [&](jclass& reference) noexcept {
        if (reference != nullptr) {
            env->DeleteGlobalRef(reference);
            reference = nullptr;
        }
    };

    if (aimCapability &&
        !makeGlobal(gameSettings, candidate.gameSettingsClass)) {
        aimCapability = false;
        freeLookCapability = false;
        safewalkCapability = false;
        movementCapability = false;
        clearGlobal(candidate.gameSettingsClass);
        candidate.gameSettingsField = nullptr;
        candidate.mouseSensitivity = nullptr;
        candidate.rotationYaw = nullptr;
        candidate.rotationPitch = nullptr;
        log::info(std::string("Aim capability disabled for profile: ") +
                  profile.label + " (failed to publish GameSettings class).");
    }

    if(freeLookCapability&&
       !makeGlobal(entityRenderer,candidate.entityRendererClass)) {
        freeLookCapability=false;
        clearGlobal(candidate.entityRendererClass);
        candidate.thirdPersonView=nullptr;
        candidate.updateCameraAndRender=nullptr;
        candidate.orientCamera=nullptr;
        candidate.setAngles=nullptr;
        candidate.setupTerrain=nullptr;
        log::info(std::string("FreeLook capability disabled for profile: ")+
                  profile.label+" (failed to publish EntityRenderer class).");
    }
    if(freeLookCapability&&candidate.setupTerrain&&
       !makeGlobal(renderGlobal,candidate.renderGlobalClass)) {
        candidate.setupTerrain=nullptr;
        clearGlobal(candidate.renderGlobalClass);
        log::info(std::string("FreeLook terrain hook unavailable for profile: ")+
                  profile.label+" (failed to publish RenderGlobal class).");
    }

    if (safewalkCapability) {
        safewalkCapability = makeGlobal(keyBinding, candidate.keyBindingClass);
        if (!safewalkCapability) {
            movementCapability = false;
            clearGlobal(candidate.keyBindingClass);
            candidate.keyBindSneakField = nullptr;
            candidate.getKeyCode = nullptr;
            candidate.setKeyBindState = nullptr;
            candidate.isAirBlock = nullptr;
            log::info(std::string("Safewalk capability disabled for profile: ") +
                      profile.label + " (failed to publish class references).");
        }
    }

    if (serverGuardCapability &&
        !makeGlobal(serverData, candidate.serverDataClass)) {
        serverGuardCapability = false;
        candidate.getCurrentServerData = nullptr;
        candidate.serverIp = nullptr;
    }

    bool itemClassRefsPublished = itemClassesLoaded &&
        makeGlobal(itemStack, candidate.itemStackClass) &&
        makeGlobal(item, candidate.itemClass) &&
        makeGlobal(inventoryPlayer, candidate.inventoryPlayerClass);
    if (!itemClassRefsPublished) {
        clearGlobal(candidate.itemStackClass);
        clearGlobal(candidate.itemClass);
        clearGlobal(candidate.inventoryPlayerClass);
        placementCapability = false;
        bedBreakerCapability = false;
        armorCapability = false;
    }

    // Publish the interaction classes independently from Scaffold.  The old
    // all-or-nothing block cleared attackEntity/playerControllerField when an
    // unrelated placement class failed, which made Silent Lock disappear.
    const bool playerControllerRefPublished = playerControllerClassLoaded &&
        makeGlobal(playerController, candidate.playerControllerClass);
    if (!playerControllerRefPublished) {
        clearGlobal(candidate.playerControllerClass);
        candidate.playerControllerField = nullptr;
        candidate.attackEntity = nullptr;
        placementCapability = false;
        bedBreakerCapability = false;
    }
    const bool enumFacingRefPublished = enumFacingClassLoaded &&
        makeGlobal(enumFacing, candidate.enumFacingClass);
    if (!enumFacingRefPublished) {
        clearGlobal(candidate.enumFacingClass);
        candidate.facingIndex = nullptr;
        candidate.getFacingByIndex = nullptr;
        placementCapability = false;
        bedBreakerCapability = false;
    }

    if (placementCapability) {
        placementCapability =
            makeGlobal(itemBlock, candidate.itemBlockClass) &&
            makeGlobal(vec3, candidate.vec3Class);
        if (!placementCapability) {
            clearGlobal(candidate.itemBlockClass);
            clearGlobal(candidate.vec3Class);
            candidate.getBlockFromItem = nullptr;
            candidate.getIdFromBlock = nullptr;
            candidate.vec3Constructor = nullptr;
            candidate.onPlayerRightClick = nullptr;
        }
    }
    if(smartHotbarCapability&&
       !makeGlobal(itemSword,candidate.itemSwordClass)) {
        smartHotbarCapability=false;
        clearGlobal(candidate.itemSwordClass);
        candidate.keyBindsHotbar=nullptr;
        candidate.windowClick=nullptr;
    }

    if (candidate.chatTextConstructor != nullptr && candidate.addChatMessage != nullptr &&
        !makeGlobal(chatText, candidate.chatTextClass)) {
        candidate.chatTextConstructor = nullptr;
        candidate.addChatMessage = nullptr;
    }
    if (candidate.parseChatJson != nullptr &&
        !makeGlobal(chatSerializer, candidate.chatSerializerClass)) {
        candidate.parseChatJson = nullptr;
    }

    if (identityCapability && !makeGlobal(uuid, candidate.uuidClass)) {
        identityCapability = false;
        candidate.getUniqueId = nullptr;
        candidate.uuidToString = nullptr;
        candidate.gameProfileGetId = nullptr;
    }

    if (skinCapability) {
        skinCapability =
            makeGlobal(abstractClientPlayer, candidate.abstractClientPlayerClass) &&
            makeGlobal(resourceLocation, candidate.resourceLocationClass) &&
            makeGlobal(textureManager, candidate.textureManagerClass) &&
            makeGlobal(textureObject, candidate.textureObjectClass);
        if (!skinCapability) {
            clearGlobal(candidate.abstractClientPlayerClass);
            clearGlobal(candidate.resourceLocationClass);
            clearGlobal(candidate.textureManagerClass);
            clearGlobal(candidate.textureObjectClass);
            candidate.getLocationSkin = nullptr;
            candidate.getTextureManager = nullptr;
            candidate.getTexture = nullptr;
            candidate.getGlTextureId = nullptr;
        }
    }

    if (sidebarCapability) {
        sidebarCapability =
            makeGlobal(scoreboard, candidate.scoreboardClass) &&
            makeGlobal(scoreObjective, candidate.scoreObjectiveClass) &&
            makeGlobal(score, candidate.scoreClass) &&
            makeGlobal(scorePlayerTeam, candidate.scorePlayerTeamClass);
        if (!sidebarCapability) {
            clearGlobal(candidate.scoreboardClass);
            clearGlobal(candidate.scoreObjectiveClass);
            clearGlobal(candidate.scoreClass);
            clearGlobal(candidate.scorePlayerTeamClass);
            candidate.getScoreboard = nullptr;
            candidate.getObjectiveInDisplaySlot = nullptr;
            candidate.getPlayersTeam = nullptr;
            candidate.getSortedScores = nullptr;
            candidate.getPlayerName = nullptr;
            candidate.formatPlayerName = nullptr;
            log::info(std::string("Sidebar capability disabled for profile: ") +
                      profile.label + " (failed to publish class references).");
        }
    }

    if (tabCapability) {
        tabCapability =
            makeGlobal(netHandler, candidate.netHandlerClass) &&
            makeGlobal(networkPlayerInfo, candidate.networkPlayerInfoClass) &&
            makeGlobal(gameProfile, candidate.gameProfileClass);
        if (!tabCapability) {
            clearGlobal(candidate.netHandlerClass);
            clearGlobal(candidate.networkPlayerInfoClass);
            clearGlobal(candidate.gameProfileClass);
            candidate.getNetHandler = nullptr;
            candidate.getPlayerInfoMap = nullptr;
            candidate.getGameProfile = nullptr;
            candidate.gameProfileGetName = nullptr;
            candidate.gameProfileGetId = nullptr;
            log::info(std::string("TAB roster capability disabled for profile: ") +
                      profile.label + " (failed to publish class references).");
        }
    }

    if (armorCapability) {
        armorCapability = itemClassRefsPublished &&
            makeGlobal(itemArmor, candidate.itemArmorClass) &&
            makeGlobal(enchantmentHelper, candidate.enchantmentHelperClass);
        if (!armorCapability) {
            clearGlobal(candidate.itemArmorClass);
            clearGlobal(candidate.enchantmentHelperClass);
            candidate.armorInventoryField = nullptr;
            if (!movementCapability) {
                candidate.inventoryField = nullptr;
                candidate.getItem = nullptr;
            }
            candidate.hasColor = nullptr;
            candidate.getColor = nullptr;
            candidate.getEnchantmentLevel = nullptr;
            candidate.getEquipmentInSlot = nullptr;
            candidate.getIdFromItem = nullptr;
            candidate.stackSize = nullptr;
            candidate.getItemDamage = nullptr;
            log::info(std::string("Armor capability disabled for profile: ") +
                      profile.label + " (failed to publish class references).");
        }
    }

    jobject minecraftObject = candidate.minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(minecraft, candidate.minecraftInstanceField)
        : env->CallStaticObjectMethod(minecraft, candidate.getMinecraft);
    if (env->ExceptionCheck() == JNI_TRUE || minecraftObject == nullptr) {
        clearException(env);
        return false;
    }
    localReferences.add(minecraftObject);
    jobject renderManagerObject = env->CallObjectMethod(minecraftObject,
                                                         candidate.getRenderManager);
    jobject timerObject = env->GetObjectField(minecraftObject, candidate.timerField);
    jobject modelViewBuffer = env->GetStaticObjectField(activeRenderInfo,
                                                         candidate.activeModelView);
    jobject projectionBuffer = env->GetStaticObjectField(activeRenderInfo,
                                                          candidate.activeProjection);
    jobject viewportBuffer = env->GetStaticObjectField(activeRenderInfo,
                                                        candidate.activeViewport);
    if (env->ExceptionCheck() == JNI_TRUE || renderManagerObject == nullptr ||
        timerObject == nullptr ||
        modelViewBuffer == nullptr || projectionBuffer == nullptr || viewportBuffer == nullptr) {
        clearException(env);
        return false;
    }
    localReferences.add(renderManagerObject);
    localReferences.add(timerObject);
    localReferences.add(modelViewBuffer);
    localReferences.add(projectionBuffer);
    localReferences.add(viewportBuffer);
    if (env->GetDirectBufferCapacity(modelViewBuffer) < 16 ||
        env->GetDirectBufferCapacity(projectionBuffer) < 16 ||
        env->GetDirectBufferCapacity(viewportBuffer) < 4 ||
        env->GetDirectBufferAddress(modelViewBuffer) == nullptr ||
        env->GetDirectBufferAddress(projectionBuffer) == nullptr ||
        env->GetDirectBufferAddress(viewportBuffer) == nullptr) {
        clearException(env);
        return false;
    }
    const auto makeGlobalObject = [&](jobject const local, jobject& global) noexcept {
        global = env->NewGlobalRef(local);
        if (env->ExceptionCheck() == JNI_TRUE || global == nullptr) {
            clearException(env);
            if (global != nullptr) env->DeleteGlobalRef(global);
            global = nullptr;
            return false;
        }
        return true;
    };
    if (!makeGlobalObject(renderManagerObject, candidate.renderManagerObject) ||
        !makeGlobalObject(timerObject, candidate.timerObject) ||
        !makeGlobalObject(modelViewBuffer, candidate.modelViewBuffer) ||
        !makeGlobalObject(projectionBuffer, candidate.projectionBuffer) ||
        !makeGlobalObject(viewportBuffer, candidate.viewportBuffer)) {
        return false;
    }

    candidate.profile = &profile;
    return true;
}

bool GameBindings::resolve(JNIEnv* const env) noexcept
{
    if (env == nullptr || !m_mappingRegistry.freeze()) {
        return false;
    }

    bindings::ClientEnvironment environment;
    probeEnvironmentHints(env, environment);
    bindings::MappingCandidates profiles;
    jclass const minecraft = findMinecraftClass(env, profiles, environment);
    if (minecraft == nullptr || profiles.empty()) {
        return false;
    }

    if (profiles.truncated) {
        log::info("Mapping candidate capacity reached; trying the highest-priority profiles only.");
    }
    for (std::size_t index = 0U; index < profiles.count; ++index) {
        const MappingProfile* const profile = profiles.items[index];
        if (profile == nullptr) continue;

        std::unique_ptr<BindingCache> candidate(new (std::nothrow) BindingCache());
        if (candidate == nullptr) {
            env->DeleteLocalRef(minecraft);
            return false;
        }
        bool resolved = false;
        try {
            resolved = resolveProfile(env, *profile, minecraft, *candidate);
        } catch (...) {
            resolved = false;
        }
        if (!resolved) {
            deleteGlobalRefs(env, *candidate);
            clearException(env);
            continue;
        }

        // Publication point. No render-side code can read m_cache until this
        // release store; after it, the cache and owned profile are immutable
        // through callback drain.
        m_cache = std::move(candidate);
        env->DeleteLocalRef(minecraft);
        m_retryAtMilliseconds.store(0U, std::memory_order_relaxed);
        m_resolutionPhase.store(ResolutionPhase::Resolved, std::memory_order_release);
        try {
            log::info(std::string("Minecraft 1.8.9 bindings resolved: ") + profile->label);
            m_freeLookDiagnostics.event("MAPPING_RESOLVED",profile->label);
        } catch (...) {
            log::info("Minecraft 1.8.9 bindings resolved.");
            m_freeLookDiagnostics.event("MAPPING_RESOLVED","label unavailable");
        }
        return true;
    }

    env->DeleteLocalRef(minecraft);
    return false;
}

bool GameBindings::ensureLwjglMouseBindings(JNIEnv* const env) noexcept
{
    if (env == nullptr) return false;
    if (m_lwjglMouseClass == nullptr || m_lwjglSetGrabbed == nullptr ||
        m_lwjglIsGrabbed == nullptr || m_lwjglIsButtonDown == nullptr) {
        jclass localMouse = env->FindClass("org/lwjgl/input/Mouse");
        if (env->ExceptionCheck() == JNI_TRUE || localMouse == nullptr) {
            clearException(env);
            return false;
        }
        jmethodID setGrabbed = env->GetStaticMethodID(localMouse, "setGrabbed", "(Z)V");
        jmethodID isGrabbed = env->GetStaticMethodID(localMouse, "isGrabbed", "()Z");
        jmethodID isButtonDown=env->GetStaticMethodID(
            localMouse,"isButtonDown","(I)Z");
        if (env->ExceptionCheck() == JNI_TRUE || setGrabbed == nullptr ||
            isGrabbed == nullptr || isButtonDown == nullptr) {
            clearException(env);
            env->DeleteLocalRef(localMouse);
            return false;
        }
        jclass globalMouse = static_cast<jclass>(env->NewGlobalRef(localMouse));
        env->DeleteLocalRef(localMouse);
        if (env->ExceptionCheck() == JNI_TRUE || globalMouse == nullptr) {
            clearException(env);
            return false;
        }
        m_lwjglMouseClass = globalMouse;
        m_lwjglSetGrabbed = setGrabbed;
        m_lwjglIsGrabbed = isGrabbed;
        m_lwjglIsButtonDown=isButtonDown;
    }
    return true;
}

bool GameBindings::ensureLwjglKeyboardBindings(JNIEnv* const env) noexcept
{
    if (env == nullptr) return false;
    if (m_lwjglKeyboardClass == nullptr || m_lwjglIsKeyDown == nullptr) {
        jclass localKeyboard = env->FindClass("org/lwjgl/input/Keyboard");
        if (env->ExceptionCheck() == JNI_TRUE || localKeyboard == nullptr) {
            clearException(env);
            return false;
        }
        jmethodID isKeyDown = env->GetStaticMethodID(
            localKeyboard, "isKeyDown", "(I)Z");
        if (env->ExceptionCheck() == JNI_TRUE || isKeyDown == nullptr) {
            clearException(env);
            env->DeleteLocalRef(localKeyboard);
            return false;
        }
        jclass globalKeyboard = static_cast<jclass>(
            env->NewGlobalRef(localKeyboard));
        env->DeleteLocalRef(localKeyboard);
        if (env->ExceptionCheck() == JNI_TRUE || globalKeyboard == nullptr) {
            clearException(env);
            return false;
        }
        m_lwjglKeyboardClass = globalKeyboard;
        m_lwjglIsKeyDown = isKeyDown;
    }
    return true;
}

bool GameBindings::queryLwjglKeyDown(JNIEnv* const env, const int lwjglKey,
                                     bool& down) noexcept
{
    if (lwjglKey <= 0 || !ensureLwjglKeyboardBindings(env)) return false;
    const jboolean value = env->CallStaticBooleanMethod(
        m_lwjglKeyboardClass, m_lwjglIsKeyDown, static_cast<jint>(lwjglKey));
    const bool succeeded = env->ExceptionCheck() != JNI_TRUE;
    clearException(env);
    if (succeeded) down = value == JNI_TRUE;
    return succeeded;
}

bool GameBindings::queryMinecraftBindingDown(JNIEnv* const env,
                                             const int keyCode,
                                             bool& down) noexcept
{
    if(keyCode>0) return queryLwjglKeyDown(env,keyCode,down);
    // 1.8.9 KeyBinding encodes mouse button N as -100+N.
    const int button=keyCode+100;
    if(button<0||!ensureLwjglMouseBindings(env)) return false;
    const jboolean value=env->CallStaticBooleanMethod(
        m_lwjglMouseClass,m_lwjglIsButtonDown,static_cast<jint>(button));
    const bool succeeded=env->ExceptionCheck()!=JNI_TRUE;
    clearException(env);
    if(succeeded) down=value==JNI_TRUE;
    return succeeded;
}

bool GameBindings::queryLwjglMouseGrabbed(JNIEnv* const env, bool& grabbed) noexcept
{
    if (!ensureLwjglMouseBindings(env)) return false;
    const jboolean value = env->CallStaticBooleanMethod(m_lwjglMouseClass,
                                                        m_lwjglIsGrabbed);
    const bool succeeded = env->ExceptionCheck() != JNI_TRUE;
    clearException(env);
    if (succeeded) grabbed = value == JNI_TRUE;
    return succeeded;
}

bool GameBindings::setLwjglMouseGrabbed(JNIEnv* const env, const bool grabbed) noexcept
{
    if (!ensureLwjglMouseBindings(env)) return false;
    env->CallStaticVoidMethod(m_lwjglMouseClass, m_lwjglSetGrabbed,
                              grabbed ? JNI_TRUE : JNI_FALSE);
    const bool succeeded = env->ExceptionCheck() != JNI_TRUE;
    clearException(env);
    return succeeded;
}

bool GameBindings::setInputCaptured(JNIEnv* const env, const bool guiOpen) noexcept
{
    if (env == nullptr) return false;

    if (guiOpen && !m_overlayInputSessionActive) {
        bool wasGrabbed = true;
        m_inputGrabStateKnown = queryLwjglMouseGrabbed(env, wasGrabbed);
        m_inputWasGrabbed = wasGrabbed;
        m_overlayInputSessionActive = true;
    }
    const bool restoreGrabbed = m_inputGrabStateKnown ? m_inputWasGrabbed : true;
    // On a title/menu screen Mouse.isGrabbed() is false. Calling
    // Minecraft.setIngameFocus() while closing our GUI would incorrectly grab
    // and hide that already-free cursor, so only invoke Minecraft's focus pair
    // when the overlay actually interrupted a grabbed gameplay session.
    const bool changeMinecraftFocus = restoreGrabbed;

    bool minecraftFocusChanged = false;
    if (changeMinecraftFocus &&
        m_resolutionPhase.load(std::memory_order_acquire) == ResolutionPhase::Resolved &&
        m_cache != nullptr) {
        BindingCache* const cache = m_cache.get();
        jobject minecraft = cache->minecraftInstanceField != nullptr
            ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
            : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
        if (env->ExceptionCheck() != JNI_TRUE && minecraft != nullptr) {
            // func_71364_i updates Minecraft.inGameHasFocus as well as LWJGL;
            // this prevents a normal 1.8.9 client from immediately re-grabbing.
            env->CallVoidMethod(minecraft,
                                guiOpen ? cache->setIngameNotInFocus : cache->setIngameFocus);
            minecraftFocusChanged = env->ExceptionCheck() != JNI_TRUE;
            clearException(env);
            env->DeleteLocalRef(minecraft);
        } else {
            clearException(env);
        }
    }

    // Lunar may transform every Minecraft symbol and therefore never publish
    // the regular cache, but LWJGL2 Mouse is still a stable public class. This
    // mapping-independent call is the essential fallback which releases raw
    // relative input on the split presentation thread.
    const bool lwjglChanged = setLwjglMouseGrabbed(
        env, guiOpen ? false : restoreGrabbed);
    if (guiOpen) {
        ::ReleaseCapture();
        ::ClipCursor(nullptr);
    } else {
        m_overlayInputSessionActive = false;
        m_inputGrabStateKnown = false;
    }
    return minecraftFocusChanged || lwjglChanged;
}

bool GameBindings::maintainInputReleased(JNIEnv* const env) noexcept
{
    if (env == nullptr) return false;
    const bool released = setLwjglMouseGrabbed(env, false);
    // Do not release Win32 capture here. ImGui deliberately owns capture while
    // dragging/resizing; cancelling it every frame made MouseDelta alternate
    // between the game and the overlay and caused both drag failure and cursor
    // flicker. The one-shot transition in setInputCaptured() releases any old
    // Minecraft capture before ImGui starts interacting.
    ::ClipCursor(nullptr);
    return released;
}

bool GameBindings::gameScreenOpen(JNIEnv* const env) noexcept
{
    if (env == nullptr) return true;
    BindingCache* const cache =
        m_resolutionPhase.load(std::memory_order_acquire) == ResolutionPhase::Resolved
        ? m_cache.get() : nullptr;
    if (cache != nullptr && cache->currentScreen != nullptr) {
        jobject minecraft = cache->minecraftInstanceField != nullptr
            ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
            : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
        if (!env->ExceptionCheck() && minecraft != nullptr) {
            jobject screen = env->GetObjectField(minecraft, cache->currentScreen);
            const bool failed = env->ExceptionCheck() == JNI_TRUE;
            clearException(env);
            const bool open = screen != nullptr;
            if (screen != nullptr) env->DeleteLocalRef(screen);
            env->DeleteLocalRef(minecraft);
            if (!failed) return open;
        } else {
            clearException(env);
            if (minecraft != nullptr) env->DeleteLocalRef(minecraft);
        }
    }
    // Mapping-independent fallback; released cursor is not gameplay input.
    bool grabbed = false;
    return !queryLwjglMouseGrabbed(env, grabbed) || !grabbed;
}

bool GameBindings::suppressKnownImpulse(JNIEnv* env,jobject entity,jobject attacker) noexcept
{
    const int selected=m_shieldAttacker.load(std::memory_order_acquire);
    const int local=m_shieldLocalPlayer.load(std::memory_order_acquire);
    const auto* c=m_cache.get();
    if(selected==-1||local<0||!env||!c||!entity||!c->getEntityId||!c->playerClass) return false;
    if(!attacker&&selected!=-2) return false;
    if(env->IsInstanceOf(entity,c->playerClass)!=JNI_TRUE||
       (selected!=-2&&env->IsInstanceOf(attacker,c->playerClass)!=JNI_TRUE)) return false;
    const int victim=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()) {clearException(env);return false;}
    if(selected==-2) return victim==local;
    const int source=env->CallIntMethod(attacker,c->getEntityId);
    if(env->ExceptionCheck()) {clearException(env);return false;}
    // This method receives the actual damage source, not an inferred attacker.
    // Unknown network velocity packets and unrelated motion never reach it.
    return victim==local&&source==selected&&source!=victim;
}

bool GameBindings::onItemUse(JNIEnv* env,jobject minecraft,const bool entering) noexcept
{
    const int watched=entering?-1:m_refillSlot;
    m_refillSlot=-1;
    const auto* c=m_cache.get();
    if(!env||!minecraft||!c||!c->stackSize||!c->syncCurrentPlayItem||
       !c->isMainThread||!c->playerField||!c->currentScreen||!c->inventoryField||
       !c->currentItem||!c->mainInventory||!c->getItem||!c->itemBlockClass||
       !m_refillEnabled.load(std::memory_order_acquire)||env->PushLocalFrame(80)<0) {
        if(env) clearException(env);
        return false;
    }
    struct Frame {JNIEnv* e;~Frame(){if(e->ExceptionCheck())e->ExceptionClear();e->PopLocalFrame(nullptr);}} frame{env};
    if(env->CallBooleanMethod(minecraft,c->isMainThread)!=JNI_TRUE||env->ExceptionCheck()) return false;
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject screen=env->GetObjectField(minecraft,c->currentScreen);
    if(!player||screen||env->ExceptionCheck()) return false;
    jobject inventory=env->GetObjectField(player,c->inventoryField);
    if(!inventory||env->ExceptionCheck()) return false;
    const int selected=env->GetIntField(inventory,c->currentItem);
    auto stacks=static_cast<jobjectArray>(env->GetObjectField(inventory,c->mainInventory));
    if(!stacks||selected<0||selected>=9||env->ExceptionCheck()) return false;
    jobject held=env->GetObjectArrayElement(stacks,selected);
    const int count=held?env->GetIntField(held,c->stackSize):0;
    if(env->ExceptionCheck()) return false;
    if(entering) {
        jobject item=held?env->CallObjectMethod(held,c->getItem):nullptr;
        if(!env->ExceptionCheck()&&item&&count>0&&
           env->IsInstanceOf(item,c->itemBlockClass)==JNI_TRUE) m_refillSlot=selected;
        return false;
    }
    // Check after Minecraft has removed the consumed stack. Never redirect
    // its cleanup to a replacement slot, nor switch after a rejected placement.
    if(watched!=selected||count>0) return false;
    // The right-click hook is observational only. Inventory mutation here
    // produces rightClick -> CLICK_WINDOW -> HELD_ITEM_CHANGE (PacketOrderE).
    // Queue the destination and let the next stable input/PRE boundary decide
    // whether a hotbar-only switch or a neutral inventory move is safe.
    m_refillQueuedPacketSerial.store(
        m_movementPacketSerial.load(std::memory_order_acquire),
        std::memory_order_release);
    m_smartHotbarRefillRequest.store(selected+1,std::memory_order_release);
    return false;
}

bool GameBindings::consumeSmartHotbarPress(JNIEnv* env,jobject binding) noexcept
{
    const auto config=m_smartHotbarConfig.load(std::memory_order_acquire);
    const auto* cache=m_cache.get();
    if(!env||!binding||!cache||!hotbar::enabled(config)) return false;
    if(env->PushLocalFrame(96)<0) {clearException(env);return false;}
    struct Locals {JNIEnv* env;~Locals(){env->PopLocalFrame(nullptr);}} locals{env};
    const auto fail=[&]() noexcept {clearException(env);return false;};
    jobject minecraft=cache->minecraftInstanceField
        ? env->GetStaticObjectField(cache->minecraftClass,cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass,cache->getMinecraft);
    if(!minecraft||env->ExceptionCheck()) return fail();
    if(env->CallBooleanMethod(minecraft,cache->isMainThread)!=JNI_TRUE||
       env->ExceptionCheck()) return fail();
    jobject screen=cache->currentScreen
        ? env->GetObjectField(minecraft,cache->currentScreen):nullptr;
    bool grabbed=false;
    if(screen||env->ExceptionCheck()||!queryLwjglMouseGrabbed(env,grabbed)||!grabbed)
        return fail();
    jobject settings=env->GetObjectField(minecraft,cache->gameSettingsField);
    if(!settings||env->ExceptionCheck()) return fail();
    jobjectArray bindings=static_cast<jobjectArray>(
        env->GetObjectField(settings,cache->keyBindsHotbar));
    if(!bindings||env->ExceptionCheck()) return fail();
    const auto actions=hotbar::unpack(config);
    int triggeredSlot=-1;
    const jsize count=std::min<jsize>(9,env->GetArrayLength(bindings));
    for(jsize slot=0;slot<count;++slot) {
        jobject candidate=env->GetObjectArrayElement(bindings,slot);
        const bool matches=candidate&&env->IsSameObject(candidate,binding)==JNI_TRUE;
        if(candidate) env->DeleteLocalRef(candidate);
        if(env->ExceptionCheck()) return fail();
        if(matches&&actions[static_cast<std::size_t>(slot)]!=0) {triggeredSlot=slot;break;}
    }
    if(triggeredSlot<0) return false;
    // isPressed has already decremented the real queued press. The hook only
    // records intent; all inventory/controller calls happen at input PRE.
    m_smartHotbarRequest.store(triggeredSlot+1,std::memory_order_release);
    return true;
}

void GameBindings::setHotbarMovementPaused(JNIEnv* env,jobject player) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!c||!c->keyBindingClass||!c->setKeyBindState) return;
    for(const int code:m_hotbarPauseKeys) if(code!=0)
        env->CallStaticVoidMethod(c->keyBindingClass,c->setKeyBindState,
                                  static_cast<jint>(code),JNI_FALSE);
    if(player&&c->setSprinting) env->CallVoidMethod(player,c->setSprinting,JNI_FALSE);
    clearException(env);
}

void GameBindings::restoreHotbarMovement(JNIEnv* env) noexcept
{
    if(m_hotbarPausePhase==HotbarPausePhase::None) return;
    const auto* c=m_cache.get();
    if(env&&c&&c->keyBindingClass&&c->setKeyBindState) {
        for(const int code:m_hotbarPauseKeys) if(code!=0) {
            bool physicallyDown=false;
            if(queryMinecraftBindingDown(env,code,physicallyDown))
                env->CallStaticVoidMethod(c->keyBindingClass,c->setKeyBindState,
                    static_cast<jint>(code),physicallyDown?JNI_TRUE:JNI_FALSE);
        }
        clearException(env);
    }
    m_hotbarPauseKeys.fill(0);
    m_hotbarPausePhase=HotbarPausePhase::None;
    m_hotbarPausePacketSerial=0U;
    m_hotbarPauseStartedMs=0U;
}

bool GameBindings::processSmartHotbarRequests(JNIEnv* env,jobject minecraft) noexcept
{
    int encoded=m_smartHotbarRequest.exchange(0,std::memory_order_acq_rel);
    const bool refill=encoded==0;
    if(refill) encoded=m_smartHotbarRefillRequest.exchange(0,std::memory_order_acq_rel);
    const auto serial=m_movementPacketSerial.load(std::memory_order_acquire);
    if(m_hotbarPausePhase==HotbarPausePhase::AwaitResumePacket&&
       serial>m_hotbarPausePacketSerial) restoreHotbarMovement(env);
    const auto requeue=[&]() noexcept {
        auto& queue=refill?m_smartHotbarRefillRequest:m_smartHotbarRequest;
        int empty=0;(void)queue.compare_exchange_strong(empty,encoded,
            std::memory_order_release,std::memory_order_relaxed);
    };
    if(m_hotbarPausePhase!=HotbarPausePhase::None&&
       GetTickCount64()-m_hotbarPauseStartedMs>500U) {
        restoreHotbarMovement(env);
        m_logicalController.debug().event("HOTBAR_PAUSE_TIMEOUT",
            m_logicalController.latest(),"movement packet boundary unavailable",true);
        if(encoded>0&&encoded<=9) requeue();
        return false;
    }
    if(m_hotbarPausePhase==HotbarPausePhase::AwaitResumePacket) {
        const auto* c=m_cache.get();
        jobject player=env&&minecraft&&c&&c->playerField
            ?env->GetObjectField(minecraft,c->playerField):nullptr;
        setHotbarMovementPaused(env,player);
        if(player) env->DeleteLocalRef(player);
        clearException(env);
        if(encoded>0&&encoded<=9) requeue();
        return false;
    }
    if(encoded<=0||encoded>9) {
        if(m_hotbarPausePhase==HotbarPausePhase::AwaitNeutralPacket)
            restoreHotbarMovement(env);
        return false;
    }
    // Grim keeps the preceding use/right-click transaction open until a
    // subsequent movement packet. Never send CLICK_WINDOW or HELD_ITEM_CHANGE
    // from either source before that boundary, or while an action is held.
    const bool actionHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0||
        (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0||
        m_logicalController.pendingAttack().kind!=silent::InteractionCommandKind::None;
    if(actionHeld||(refill&&m_movementPacketSerial.load(std::memory_order_acquire)<=
        m_refillQueuedPacketSerial.load(std::memory_order_acquire))) {
        if(actionHeld) restoreHotbarMovement(env);
        requeue();return false;
    }
    const auto* c=m_cache.get();
    if(!env||!minecraft||!c||!hotbar::enabled(
           m_smartHotbarConfig.load(std::memory_order_acquire))||
       env->PushLocalFrame(96)<0) {if(env) clearException(env);requeue();return false;}
    struct Frame {JNIEnv* e;~Frame(){if(e->ExceptionCheck())e->ExceptionClear();e->PopLocalFrame(nullptr);}} frame{env};
    jobject screen=c->currentScreen?env->GetObjectField(minecraft,c->currentScreen):nullptr;
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject inventory=player?env->GetObjectField(player,c->inventoryField):nullptr;
    auto stacks=inventory?static_cast<jobjectArray>(env->GetObjectField(inventory,c->mainInventory)):nullptr;
    jobject controller=env->GetObjectField(minecraft,c->playerControllerField);
    if(screen||!player||!inventory||!stacks||!controller||env->ExceptionCheck()) {
        restoreHotbarMovement(env);
        requeue();return false;
    }
    if(m_hotbarPausePhase==HotbarPausePhase::AwaitResumePacket) {
        setHotbarMovementPaused(env,player);
        requeue();return false;
    }
    const int destination=encoded-1;
    const auto actions=hotbar::unpack(m_smartHotbarConfig.load(std::memory_order_acquire));
    const int wanted=refill?static_cast<int>(hotbar::Action::Blocks):
        actions[static_cast<std::size_t>(destination)];
    const jsize length=std::min<jsize>(env->GetArrayLength(stacks),36);
    std::array<hotbar::ItemKind,36U> kinds{};
    for(jsize slot=0;slot<length;++slot) {
        jobject stack=env->GetObjectArrayElement(stacks,slot);
        if(!stack) continue;
        const int amount=c->stackSize?env->GetIntField(stack,c->stackSize):1;
        jobject item=amount>0?env->CallObjectMethod(stack,c->getItem):nullptr;
        if(env->ExceptionCheck()) {requeue();return false;}
        if(item) {
            if(env->IsInstanceOf(item,c->itemSwordClass)==JNI_TRUE)
                kinds[static_cast<std::size_t>(slot)]=hotbar::ItemKind::Sword;
            else if(env->IsInstanceOf(item,c->itemBlockClass)==JNI_TRUE)
                kinds[static_cast<std::size_t>(slot)]=hotbar::ItemKind::Blocks;
            else if(c->itemClass&&c->getIdFromItem) {
                const jint id=env->CallStaticIntMethod(c->itemClass,c->getIdFromItem,item);
                if(env->ExceptionCheck()) {requeue();return false;}
                kinds[static_cast<std::size_t>(slot)]=hotbar::toolKind(id);
            }
        }
    }
    const int current=std::clamp(static_cast<int>(env->GetIntField(inventory,c->currentItem)),0,8);
    if(env->ExceptionCheck()) {requeue();return false;}
    const auto available=std::span<const hotbar::ItemKind>(
        kinds.data(),static_cast<std::size_t>(length));
    const int source=refill?hotbar::selectRefillSource(available,current):
        hotbar::selectSource(available,current,wanted);
    if(source<0) {
        restoreHotbarMovement(env);
        // A configured shortcut with no matching item keeps the normal hotbar
        // selection instead of silently swallowing the player's key press.
        if(refill) return false;
        env->SetIntField(inventory,c->currentItem,destination);
        if(!env->ExceptionCheck()) env->CallVoidMethod(controller,c->syncCurrentPlayItem);
        return env->ExceptionCheck()!=JNI_TRUE;
    }
    if(source>=9) {
        if(!m_silentRotationHook.ready()) {requeue();return false;}
        // Use raw key states, not residual velocity. A held key is temporarily
        // masked across a real movement POST before CLICK_WINDOW, then restored
        // from raw LWJGL state after another POST. Held input resumes without
        // requiring a release/repress gesture.
        jobject settings=c->gameSettingsField
            ?env->GetObjectField(minecraft,c->gameSettingsField):nullptr;
        if(!settings||env->ExceptionCheck()) {requeue();return false;}
        std::array<int,6U> codes{};
        bool physicalMovement=false;
        for(std::size_t axis=0;axis<codes.size();++axis) {
            const jfieldID field=axis<5U?c->movementKeyFields[axis]:c->keyBindSprintField;
            if(!field||!c->getKeyCode) {requeue();return false;}
            jobject binding=env->GetObjectField(settings,field);
            if(!binding||env->ExceptionCheck()) {requeue();return false;}
            const int code=env->CallIntMethod(binding,c->getKeyCode);
            bool down=false;
            if(env->ExceptionCheck()||
               (code!=0&&!queryMinecraftBindingDown(env,code,down))) {
                requeue();return false;
            }
            codes[axis]=code;
            physicalMovement=physicalMovement||down;
        }
        const bool sprinting=c->isSprinting&&
            env->CallBooleanMethod(player,c->isSprinting)==JNI_TRUE;
        if(env->ExceptionCheck()) {requeue();return false;}
        if(m_hotbarPausePhase==HotbarPausePhase::None) {
            if(!c->setKeyBindState||!c->setSprinting) {requeue();return false;}
            m_hotbarPauseKeys=codes;
            m_hotbarPausePhase=HotbarPausePhase::AwaitNeutralPacket;
            m_hotbarPausePacketSerial=serial;
            m_hotbarPauseStartedMs=GetTickCount64();
            setHotbarMovementPaused(env,player);
            char detail[128]{};
            std::snprintf(detail,sizeof(detail),
                "source=%d destination=%d rawMovement=%d sprint=%d serial=%llu",
                source,destination,physicalMovement?1:0,sprinting?1:0,
                static_cast<unsigned long long>(serial));
            m_logicalController.debug().event("HOTBAR_NEUTRAL_WAIT",
                m_logicalController.latest(),detail,true);
            requeue();return false;
        }
        if(m_hotbarPausePhase==HotbarPausePhase::AwaitNeutralPacket) {
            setHotbarMovementPaused(env,player);
            if(serial<=m_hotbarPausePacketSerial||sprinting) {
                m_hotbarPausePacketSerial=serial;
                requeue();return false;
            }
        }
        jobject result=env->CallObjectMethod(controller,c->windowClick,
            0,source,destination,2,player);
        if(result) env->DeleteLocalRef(result);
        if(env->ExceptionCheck()) {
            restoreHotbarMovement(env);requeue();return false;
        }
        if(m_hotbarPausePhase==HotbarPausePhase::AwaitNeutralPacket) {
            m_hotbarPausePhase=HotbarPausePhase::AwaitResumePacket;
            m_hotbarPausePacketSerial=serial;
        }
        m_logicalController.debug().event("HOTBAR_TRANSFER",
            m_logicalController.latest(),"inventory swap after neutral packet",true);
    } else {
        restoreHotbarMovement(env);
        char detail[128]{};
        std::snprintf(detail,sizeof(detail),
            "source=%d destination=%d serial=%llu noPositionRun=%u",
            source,destination,static_cast<unsigned long long>(serial),
            m_noPositionPacketRun.load(std::memory_order_relaxed));
        m_logicalController.debug().event("HOTBAR_SWITCH",
            m_logicalController.latest(),detail,true);
    }
    env->SetIntField(inventory,c->currentItem,source<9?source:destination);
    if(!env->ExceptionCheck()) env->CallVoidMethod(controller,c->syncCurrentPlayItem);
    return env->ExceptionCheck()!=JNI_TRUE;
}

bool GameBindings::updateGameplay(JNIEnv* const env,
                                  const GameplaySettings& requested,
                                  const GameSnapshot& snapshot,
                                  const std::uint64_t tickMilliseconds) noexcept
{
    m_logicalController.debug().configure(requested.silentFileDebug,requested.silentChatDebug);
    if(!m_logicalController.debug().enabled() && m_interactionObserver.ready())
        m_interactionObserver.setEnabled(false);
    if (env == nullptr) return false;
    if(!requested.aimAssist||!requested.aimSilentLock||
       !requested.silentControlAdaptation) {
        m_sprintFeatureEnabled.store(false,std::memory_order_release);
        m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    }

    BindingCache* const cache =
        m_resolutionPhase.load(std::memory_order_acquire) == ResolutionPhase::Resolved
        ? m_cache.get() : nullptr;
    if(m_hotbarPausePhase!=HotbarPausePhase::None&&
       (!requested.smartHotbar||!cache||gameScreenOpen(env)))
        restoreHotbarMovement(env);
    // These two diagnostics are intentionally impossible to activate on a
    // remote server. The guard is duplicated here (below the UI/runtime
    // guard) so a malformed IPC frame still cannot broaden their scope.
    const bool localWorld = snapshot.integratedSinglePlayer &&
        !snapshot.hypixelServer;
    const bool localMobAuraRequested = requested.localMobAura && localWorld;
    const bool localVelocityRequested = requested.localVelocity && localWorld;
    const bool shield=requested.shieldAttackerId==-2||
        (localWorld&&requested.shieldAttackerId>=0);
    m_shieldAttacker.store(shield?requested.shieldAttackerId:-1,std::memory_order_release);
    m_shieldLocalPlayer.store(shield?snapshot.entityId:-1,std::memory_order_release);
    m_impulseHook.setEnabled(shield);
    m_velocityHook.setEnabled(shield&&requested.shieldAttackerId==-2);
    const bool aimCapability = cache != nullptr &&
        cache->minecraftClass != nullptr && cache->isMainThread != nullptr &&
        cache->playerField != nullptr && cache->gameSettingsField != nullptr &&
        cache->mouseSensitivity != nullptr && cache->rotationYaw != nullptr &&
        cache->rotationPitch != nullptr && cache->previousRotationYaw != nullptr &&
        cache->previousRotationPitch != nullptr;
    const bool freeLookCapability=aimCapability&&
        cache->entityRendererClass!=nullptr&&cache->thirdPersonView!=nullptr&&
        cache->updateCameraAndRender!=nullptr&&cache->orientCamera!=nullptr&&
        cache->setAngles!=nullptr&&cache->profile!=nullptr;
    const int freeLookHotkey=std::clamp(requested.freeLookHotkey,0,254);
    const bool freeLookHeld=freeLookHotkey>=8&&freeLookHotkey<=254&&
        (::GetAsyncKeyState(freeLookHotkey)&0x8000)!=0;
    const bool freeLookEnableEdge=requested.freeLookConfigured&&
        (!m_freeLookObservationInitialized||!m_freeLookUiRequestedObserved);
    const bool freeLookCapabilityRecovered=freeLookCapability&&
        m_freeLookObservationInitialized&&!m_freeLookCapabilityObserved;
    if(freeLookEnableEdge||freeLookCapabilityRecovered) {
        m_freeLookHookAttemptCount=0U;
        m_freeLookHookRetryLatched=false;
        m_nextFreeLookHookAttemptTick=0U;
        m_freeLookDiagnostics.event("HOOK_RETRY_RESET",
            freeLookEnableEdge?"reason=explicit-enable":"reason=capability-recovered");
    } else if(!requested.freeLookConfigured) {
        m_freeLookHookAttemptCount=0U;
        m_freeLookHookRetryLatched=false;
        m_nextFreeLookHookAttemptTick=0U;
    }
    m_freeLookDiagnostics.setVerbose(requested.silentFileDebug);
    m_freeLookHotkey.store(freeLookHotkey,
                           std::memory_order_release);
    if(!m_freeLookObservationInitialized||
       requested.freeLookConfigured!=m_freeLookUiRequestedObserved||
       requested.freeLook!=m_freeLookRuntimeRequestedObserved||
       requested.freeLookGuiOpen!=m_freeLookGuiObserved||
       requested.freeLookForeground!=m_freeLookForegroundObserved||
       freeLookCapability!=m_freeLookCapabilityObserved||
       m_freeLookHook.ready()!=m_freeLookReadyObserved||
       freeLookHeld!=m_freeLookHeldObserved) {
        char detail[320]{};
        std::snprintf(detail,sizeof(detail),
            "uiRequested=%d runtimeRequested=%d hotkey=%d held=%d foreground=%d guiOpen=%d capability=%d ready=%d",
            requested.freeLookConfigured?1:0,requested.freeLook?1:0,
            freeLookHotkey,freeLookHeld?1:0,
            requested.freeLookForeground?1:0,requested.freeLookGuiOpen?1:0,
            freeLookCapability?1:0,m_freeLookHook.ready()?1:0);
        m_freeLookDiagnostics.event("REQUEST_STATE",detail);
        if(!m_freeLookObservationInitialized||
           freeLookCapability!=m_freeLookCapabilityObserved) {
            char capability[512]{};
            std::snprintf(capability,sizeof(capability),
                "resolved=%d profile=%s minecraft=%d mainThread=%d player=%d settings=%d yaw=%d pitch=%d prevYaw=%d prevPitch=%d renderer=%d perspective=%d update=%d orient=%d setAngles=%d terrain=%d",
                freeLookCapability?1:0,
                cache&&cache->profile?cache->profile->label.c_str():"unresolved",
                cache&&cache->minecraftClass?1:0,cache&&cache->isMainThread?1:0,
                cache&&cache->playerField?1:0,cache&&cache->gameSettingsField?1:0,
                cache&&cache->rotationYaw?1:0,cache&&cache->rotationPitch?1:0,
                cache&&cache->previousRotationYaw?1:0,
                cache&&cache->previousRotationPitch?1:0,
                cache&&cache->entityRendererClass?1:0,
                cache&&cache->thirdPersonView?1:0,
                cache&&cache->updateCameraAndRender?1:0,
                cache&&cache->orientCamera?1:0,cache&&cache->setAngles?1:0,
                cache&&cache->setupTerrain?1:0);
            m_freeLookDiagnostics.event("CAPABILITY",capability);
        }
        m_freeLookObservationInitialized=true;
        m_freeLookUiRequestedObserved=requested.freeLookConfigured;
        m_freeLookRuntimeRequestedObserved=requested.freeLook;
        m_freeLookGuiObserved=requested.freeLookGuiOpen;
        m_freeLookForegroundObserved=requested.freeLookForeground;
        m_freeLookCapabilityObserved=freeLookCapability;
        m_freeLookReadyObserved=m_freeLookHook.ready();
        m_freeLookHeldObserved=freeLookHeld;
    }
    if(requested.freeLook&&freeLookCapability&&!m_freeLookHook.ready()&&
       !m_freeLookHookRetryLatched&&
       tickMilliseconds>=m_nextFreeLookHookAttemptTick) {
        const std::uint64_t attemptStarted=::GetTickCount64();
        ++m_freeLookHookAttemptCount;
        char retry[192]{};
        std::snprintf(retry,sizeof(retry),"tickMs=%llu attempt=%u profile=%s",
            static_cast<unsigned long long>(tickMilliseconds),
            static_cast<unsigned>(m_freeLookHookAttemptCount),
            cache->profile->label.c_str());
        m_freeLookDiagnostics.event("HOOK_INSTALL_BEGIN",retry);
        std::string entityOwner=cache->profile->entityName;
        std::replace(entityOwner.begin(),entityOwner.end(),'.','/');
        const std::array<const char*,4> fields{
            cache->profile->rotationYawField.c_str(),
            cache->profile->rotationPitchField.c_str(),
            cache->profile->previousRotationYawField.c_str(),
            cache->profile->previousRotationPitchField.c_str()};
        const bool installed=m_freeLookHook.install(m_vm,cache->updateCameraAndRender,
            cache->orientCamera,cache->setAngles,cache->setupTerrain,
            entityOwner.c_str(),
            cache->profile->setAngles.c_str(),fields,this,
            [](void* owner,JNIEnv* jni,jobject entity,jfloat yaw,
               jfloat pitch) noexcept {
                static_cast<GameBindings*>(owner)->rotateFreeLookCamera(
                    jni,entity,yaw,pitch);
            },
            [](void* owner,JNIEnv* jni,jobject entity,
               LiveFreeLookTransform::Angle angle) noexcept -> jfloat {
                return static_cast<GameBindings*>(owner)->freeLookCameraAngle(
                    jni,entity,angle);
            },
            [](void* owner,const char* event,const char* detail) noexcept {
                static_cast<GameBindings*>(owner)->m_freeLookDiagnostics.event(
                    event?event:"HOOK_EVENT",detail?detail:"");
            });
        const std::uint64_t duration=::GetTickCount64()-attemptStarted;
        char result[224]{};
        std::snprintf(result,sizeof(result),
            "attempt=%u success=%d terrainReady=%d durationMs=%llu lastJvmtiError=%d",
            static_cast<unsigned>(m_freeLookHookAttemptCount),installed?1:0,
            m_freeLookHook.terrainReady()?1:0,
            static_cast<unsigned long long>(duration),m_freeLookHook.lastError());
        m_freeLookDiagnostics.event("HOOK_INSTALL_END",result);
        if(m_freeLookHook.ready()) {
            m_freeLookHookAttemptCount=0U;
            m_nextFreeLookHookAttemptTick=0U;
        } else if(m_freeLookHookAttemptCount>=3U) {
            m_freeLookHookRetryLatched=true;
            m_freeLookDiagnostics.event("HOOK_RETRY_LATCHED",
                "retry requires explicit re-enable or capability recovery");
        } else {
            const std::uint64_t backoff=1500ULL<<
                static_cast<unsigned>(m_freeLookHookAttemptCount-1U);
            m_nextFreeLookHookAttemptTick=tickMilliseconds+backoff;
        }
        if(m_freeLookHook.ready()!=m_freeLookReadyObserved) {
            char ready[96]{};
            std::snprintf(ready,sizeof(ready),"ready=%d lastJvmtiError=%d",
                m_freeLookHook.ready()?1:0,m_freeLookHook.lastError());
            m_freeLookDiagnostics.event("READY_CHANGE",ready);
            m_freeLookReadyObserved=m_freeLookHook.ready();
        }
    }
    const bool freeLookRequested=requested.freeLook&&freeLookCapability&&
        m_freeLookHook.ready();
    m_freeLookRequested.store(freeLookRequested,std::memory_order_release);
    const bool freeLookNotReady=requested.freeLookConfigured&&
        !m_freeLookHook.ready();
    if(freeLookNotReady&&!m_freeLookNotReadyObserved)
        m_freeLookDiagnostics.event("REQUEST_NOT_READY",
            freeLookCapability?"hook install incomplete":"mapping capability incomplete");
    m_freeLookNotReadyObserved=freeLookNotReady;
    if(!freeLookRequested&&m_freeLookActive) {
        const char* reason=!requested.freeLookConfigured?"disabled":
            requested.freeLookGuiOpen?"gui-open":
            !requested.freeLookForeground?"window-unfocused":
            !freeLookCapability?"capability-lost":"hook-not-ready";
        endFreeLook(env,reason,true);
    }
    const bool safewalkCapability = cache != nullptr &&
        cache->gameSettingsClass != nullptr && cache->keyBindingClass != nullptr &&
        cache->gameSettingsField != nullptr && cache->keyBindSneakField != nullptr &&
        cache->getKeyCode != nullptr && cache->setKeyBindState != nullptr &&
        cache->rotationPitch != nullptr && cache->isAirBlock != nullptr;
    const bool logicalMovementCapability = cache != nullptr &&
        cache->moveFlying != nullptr && cache->isSprinting != nullptr &&
        cache->setSprinting != nullptr &&
        cache->jump != nullptr &&
        cache->getEntityId != nullptr && cache->getKeyCode != nullptr &&
        cache->onGround != nullptr && cache->entityTicks != nullptr &&
        std::all_of(cache->movementInputFields.begin(),
                    cache->movementInputFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->movementKeyFields.begin(),
                    cache->movementKeyFields.begin() + 4,
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->motionFields.begin(), cache->motionFields.end(),
                    [](jfieldID field) { return field != nullptr; });

    const auto setSneakState = [&](const int keyCode, const bool down) noexcept {
        if (!safewalkCapability || keyCode <= 0) return false;
        env->CallStaticVoidMethod(cache->keyBindingClass,
                                  cache->setKeyBindState,
                                  static_cast<jint>(keyCode),
                                  down ? JNI_TRUE : JNI_FALSE);
        const bool succeeded = env->ExceptionCheck() != JNI_TRUE;
        clearException(env);
        return succeeded;
    };
    const auto releaseForcedSneak = [&]() noexcept {
        if (!m_safewalkSneakForced) return true;
        bool physicalDown = false;
        (void)queryLwjglKeyDown(env, m_safewalkSneakKeyCode, physicalDown);
        const bool released = setSneakState(m_safewalkSneakKeyCode,
                                            physicalDown);
        m_safewalkSneakForced = false;
        m_safewalkSneakKeyCode = 0;
        m_safewalkSupportMask = 0U;
        m_safewalkReleaseAt = 0U;
        return released;
    };
    const auto releaseForcedSprint = [&]() noexcept {
        if(!m_sprintKeyForced) return;
        bool physicalDown=false;
        (void)queryLwjglKeyDown(env,m_sprintKeyCode,physicalDown);
        if(cache->keyBindingClass&&cache->setKeyBindState&&m_sprintKeyCode>0)
            env->CallStaticVoidMethod(cache->keyBindingClass,
                cache->setKeyBindState,m_sprintKeyCode,
                physicalDown?JNI_TRUE:JNI_FALSE);
        clearException(env);
        m_sprintKeyForced=false;m_sprintKeyCode=0;
    };

    const bool movementCapability = safewalkCapability &&
        std::all_of(cache->movementKeyFields.begin(), cache->movementKeyFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->motionFields.begin(), cache->motionFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        cache->mouseSensitivity != nullptr && cache->rotationYaw != nullptr &&
        cache->onGround != nullptr && cache->jump != nullptr;
    const bool placementCapability = movementCapability &&
        cache->playerControllerClass != nullptr && cache->itemBlockClass != nullptr &&
        cache->enumFacingClass != nullptr && cache->vec3Class != nullptr &&
        cache->inventoryField != nullptr && cache->onPlayerRightClick != nullptr &&
        cache->currentItem != nullptr && cache->mainInventory != nullptr &&
        cache->getItem != nullptr && cache->getBlockFromItem != nullptr &&
        cache->getIdFromBlock != nullptr && cache->getFacingByIndex != nullptr &&
        cache->vec3Constructor != nullptr && cache->playerControllerField != nullptr &&
        cache->syncCurrentPlayItem != nullptr;
    const bool bedBreakerCapability = cache != nullptr &&
        cache->playerControllerClass != nullptr && cache->itemStackClass != nullptr &&
        cache->enumFacingClass != nullptr && cache->playerControllerField != nullptr &&
        cache->inventoryField != nullptr && cache->currentItem != nullptr &&
        cache->mainInventory != nullptr && cache->getItem != nullptr &&
        cache->getFacingByIndex != nullptr && cache->clickBlock != nullptr &&
        cache->onPlayerDamageBlock != nullptr &&
        cache->resetBlockRemoving != nullptr &&
        cache->getBlockReachDistance != nullptr && cache->getStrVsBlock != nullptr &&
        cache->blockPosConstructor != nullptr && cache->getBlockState != nullptr &&
        cache->getBlock != nullptr && cache->isAirBlock != nullptr;
    const bool smartHotbarCapability=aimCapability&&cache!=nullptr&&
        cache->keyBindingClass!=nullptr&&cache->keyBindsHotbar!=nullptr&&
        cache->keyBindingIsPressed!=nullptr&&cache->getKeyCode!=nullptr&&cache->inventoryField!=nullptr&&
        cache->currentItem!=nullptr&&cache->mainInventory!=nullptr&&
        cache->getItem!=nullptr&&cache->itemSwordClass!=nullptr&&
        cache->itemBlockClass!=nullptr&&cache->playerControllerField!=nullptr&&
        cache->windowClick!=nullptr&&cache->syncCurrentPlayItem!=nullptr;
    const bool smartHotbarRequested=requested.smartHotbar&&
        smartHotbarCapability;
    m_smartHotbarConfig.store(hotbar::pack(smartHotbarRequested,
        requested.smartHotbarActions),std::memory_order_release);
    m_smartHotbarHook.setEnabled(smartHotbarRequested);
    m_refillEnabled.store(smartHotbarRequested&&requested.smartHotbarRefill,
        std::memory_order_release);
    m_itemUseHook.setEnabled(smartHotbarRequested&&requested.smartHotbarRefill);
    if(!smartHotbarRequested) {
        restoreHotbarMovement(env);
        m_smartHotbarRequest.store(0,std::memory_order_release);
        m_smartHotbarRefillRequest.store(0,std::memory_order_release);
        m_refillSlot=-1;
    }
    const bool bedBreakerRequested = requested.bedBreaker && localWorld;
    const bool movementRequested = requested.safewalk || requested.scaffold ||
        requested.fly || requested.bhop || requested.longJump ||
        localMobAuraRequested || localVelocityRequested || requested.forceSprint || shield;
    // A pending logical restore/reset is work in its own right.  Keep this
    // frame alive even after the feature toggle turns off so the authoritative
    // controller can flush the real Minecraft state before the hooks stand
    // down.
    const bool anyRequested = movementRequested || requested.aimAssist ||
        requested.silentFileDebug || requested.silentChatDebug ||
        bedBreakerRequested || m_bedBreakerTargetValid ||
        m_logicalController.requiresDrain() || freeLookRequested ||
        m_freeLookActive || smartHotbarRequested;
    if ((!anyRequested && !m_aimSensitivityModified) ||
        (!aimCapability && !movementCapability && !bedBreakerCapability &&
         !freeLookCapability && !smartHotbarCapability)) {
        if(!aimCapability||!anyRequested) deactivateSilentOutput();
        (void)releaseForcedSneak();
        releaseForcedSprint();
        m_scaffoldPlatformYValid = false;
        m_lastLocalHealth = -1.0F;
        m_lastLocalEntityId = -1;
        return false;
    }
    if (env->PushLocalFrame(96) < 0) {
        clearException(env);
        (void)releaseForcedSneak();
        releaseForcedSprint();
        return false;
    }
    const auto finish = [&](const bool result) noexcept {
        env->PopLocalFrame(nullptr);
        return result;
    };
    const auto fail = [&]() noexcept {
        deactivateSilentOutput();
        clearException(env);
        restoreHotbarMovement(env);
        m_sprintFeatureEnabled.store(false,std::memory_order_release);
        m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
        (void)releaseForcedSneak();
        releaseForcedSprint();
        m_logicalController.deactivate();
        m_bedBreakerTargetValid = false;
        return finish(false);
    };

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass,
                                    cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass,
                                      cache->getMinecraft);
    if (env->ExceptionCheck() == JNI_TRUE || minecraft == nullptr) return fail();
    const jboolean mainThread = env->CallBooleanMethod(minecraft,
                                                       cache->isMainThread);
    if (env->ExceptionCheck() == JNI_TRUE || mainThread != JNI_TRUE) return fail();

    jobject player = env->GetObjectField(minecraft, cache->playerField);
    jobject settings = env->GetObjectField(minecraft, cache->gameSettingsField);
    jobject world = env->GetObjectField(minecraft, cache->worldField);
    if (env->ExceptionCheck() == JNI_TRUE || player == nullptr ||
        settings == nullptr || world == nullptr) return fail();
    // Ask vanilla's input path to sprint. Writing setSprinting(true) from
    // moveFlying occurs after the movement-speed decision and produces a
    // client/server mismatch, especially on diagonal input.
    if(requested.forceSprint&&cache->keyBindSprintField&&
       cache->setKeyBindState&&cache->getKeyCode) {
        jobject sprintBinding=env->GetObjectField(settings,cache->keyBindSprintField);
        jobject forwardBinding=cache->movementKeyFields[0]
            ?env->GetObjectField(settings,cache->movementKeyFields[0]):nullptr;
        if(sprintBinding&&forwardBinding&&!env->ExceptionCheck()) {
            const int sprintCode=env->CallIntMethod(sprintBinding,cache->getKeyCode);
            const int forwardCode=env->CallIntMethod(forwardBinding,cache->getKeyCode);
            bool forwardDown=false;
            if(!env->ExceptionCheck()&&
               queryMinecraftBindingDown(env,forwardCode,forwardDown)) {
                const bool silentHeld=requested.aimAssist&&requested.aimSilentLock&&
                    (::GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
                const bool desired=forwardDown&&!silentHeld&&
                    m_hotbarPausePhase==HotbarPausePhase::None;
                if(desired&&sprintCode>0) {
                    if(m_sprintKeyForced&&m_sprintKeyCode!=sprintCode)
                        releaseForcedSprint();
                    env->CallStaticVoidMethod(cache->keyBindingClass,
                        cache->setKeyBindState,sprintCode,JNI_TRUE);
                    if(!env->ExceptionCheck()) {
                        m_sprintKeyForced=true;m_sprintKeyCode=sprintCode;
                    }
                } else releaseForcedSprint();
            }
        }
        clearException(env);
    } else releaseForcedSprint();
    const jfloat pitch = env->GetFloatField(player, cache->rotationPitch);
    if(shield&&cache->knockBack&&(!m_impulseHook.ready()||
       (requested.shieldAttackerId==-2&&!m_velocityHook.ready()))&&tickMilliseconds>=m_nextImpulseHookAttempt) {
        m_nextImpulseHookAttempt=tickMilliseconds+5000U;
        (void)m_impulseHook.install(m_vm,cache->knockBack,this,
            [](void* owner,JNIEnv* jni,jobject victim,jobject source) noexcept {
                return static_cast<GameBindings*>(owner)->suppressKnownImpulse(jni,victim,source);
            });
        m_impulseHook.setEnabled(true);
        if(requested.shieldAttackerId==-2&&cache->handleEntityVelocity&&cache->velocityEntityId) {
            (void)m_velocityHook.install(m_vm,cache->handleEntityVelocity,this,
                [](void* owner,JNIEnv* jni,jobject,jobject packet) noexcept {
                    auto* bindings=static_cast<GameBindings*>(owner);
                    const auto* resolved=bindings->m_cache.get();
                    if(!packet||!resolved||bindings->m_shieldAttacker.load(std::memory_order_acquire)!=-2) return false;
                    const int id=jni->CallIntMethod(packet,resolved->velocityEntityId);
                    if(jni->ExceptionCheck()) {bindings->clearException(jni);return false;}
                    return id==bindings->m_shieldLocalPlayer.load(std::memory_order_acquire);
                });
            m_velocityHook.setEnabled(true);
        }
    }
    const jfloat yaw = env->GetFloatField(player, cache->rotationYaw);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    if(smartHotbarRequested&&(!m_smartHotbarHook.ready()||
       (requested.smartHotbarRefill&&!m_itemUseHook.ready()))&&
       cache->keyBindingIsPressed&&tickMilliseconds>=m_nextSmartHotbarHookAttemptTick) {
        m_nextSmartHotbarHookAttemptTick=tickMilliseconds+5000U;
        (void)m_smartHotbarHook.install(m_vm,cache->keyBindingIsPressed,this,
            [](void* owner,JNIEnv* jni,jobject binding) noexcept {
                return static_cast<GameBindings*>(owner)->consumeSmartHotbarPress(jni,binding);
            });
        m_smartHotbarHook.setEnabled(true);
        if(requested.smartHotbarRefill&&cache->rightClickMouse) {
            (void)m_itemUseHook.install(m_vm,cache->rightClickMouse,this,
                [](void* owner,JNIEnv* jni,jobject mc,bool entering) noexcept {
                    return static_cast<GameBindings*>(owner)->onItemUse(jni,mc,entering);
                });
            m_itemUseHook.setEnabled(true);
        }
    }

    silent::HeldItemPolicy heldItemPolicy=silent::HeldItemPolicy::Other;
    if(cache->getEquipmentInSlot&&cache->getItem) {
        jobject heldStack=env->CallObjectMethod(player,cache->getEquipmentInSlot,0);
        jobject heldItem=!env->ExceptionCheck()&&heldStack
            ?env->CallObjectMethod(heldStack,cache->getItem):nullptr;
        if(!env->ExceptionCheck()&&heldItem) {
            if(cache->itemBlockClass&&env->IsInstanceOf(
                    heldItem,cache->itemBlockClass)==JNI_TRUE) {
                heldItemPolicy=silent::HeldItemPolicy::BlockItem;
            } else if(cache->itemClass&&cache->getIdFromItem) {
                const jint id=env->CallStaticIntMethod(
                    cache->itemClass,cache->getIdFromItem,heldItem);
                // Legacy 1.8.9 IDs: pickaxes, axes and shears. These tools
                // deliberately give the camera block priority on left-click.
                constexpr std::array<int,11U> miningTools{{
                    257,258,270,271,274,275,278,279,285,286,359}};
                if(!env->ExceptionCheck()&&std::find(miningTools.begin(),
                        miningTools.end(),static_cast<int>(id))!=miningTools.end())
                    heldItemPolicy=silent::HeldItemPolicy::MiningTool;
            }
        }
        clearException(env);
        if(heldItem) env->DeleteLocalRef(heldItem);
        if(heldStack) env->DeleteLocalRef(heldStack);
    }

    if (localVelocityRequested && movementCapability) {
        const jfloat localHealth = env->CallFloatMethod(player, cache->getHealth);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        if (m_lastLocalEntityId == snapshot.entityId &&
            m_lastLocalHealth >= 0.0F &&
            localHealth + 0.01F < m_lastLocalHealth) {
            const unsigned probability=static_cast<unsigned>(std::clamp(
                requested.localVelocityProbability,0,100));
            const unsigned roll=static_cast<unsigned>((tickMilliseconds ^
                (static_cast<std::uint64_t>(snapshot.entityId)*0x9E3779B97F4A7C15ULL))%100ULL);
            const double horizontalScale = static_cast<double>(std::clamp(
                requested.localVelocityPercent, 0, 100)) / 100.0;
            const double verticalScale=static_cast<double>(std::clamp(
                requested.localVelocityVerticalPercent,0,100))/100.0;
            for (std::size_t axis=0;axis<cache->motionFields.size();++axis) {
                const jfieldID field=cache->motionFields[axis];
                const jdouble motion = env->GetDoubleField(player, field);
                if(roll<probability)
                    env->SetDoubleField(player,field,motion*
                        (axis==1U ? verticalScale : horizontalScale));
            }
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
        m_lastLocalHealth = localHealth;
        m_lastLocalEntityId = snapshot.entityId;
    } else {
        m_lastLocalHealth = -1.0F;
        m_lastLocalEntityId = snapshot.entityId;
    }

    // Silent Lock is a Lock On output mode even if an older persisted config
    // contains the contradictory Smooth+Silent combination.  Do not let a UI
    // state mismatch silently disable the authoritative logical pipeline.
    const aim::Mode mode = (requested.aimLockOnMode || requested.aimSilentLock)
        ? aim::Mode::LockOn : aim::Mode::Smooth;
    const bool wantsSilent = requested.aimAssist && requested.aimSilentLock;
    if (wantsSilent || requested.forceSprint || smartHotbarRequested ||
        m_logicalController.debug().enabled()) {
        if (!m_silentRotationHook.ready() &&
            tickMilliseconds >= m_nextSilentRotationHookAttemptTick) {
            m_nextSilentRotationHookAttemptTick = tickMilliseconds + 5000U;
            if (cache->addToSendQueue && cache->profile) {
                std::string packetInternal = cache->profile->networkPacketName;
                std::replace(packetInternal.begin(), packetInternal.end(), '.', '/');
                (void)m_silentRotationHook.install(
                    m_vm, cache->addToSendQueue, packetInternal.c_str(), this,
                    [](void* owner, JNIEnv* jni, jobject packet) noexcept -> jobject {
                        return static_cast<GameBindings*>(owner)->serializeLogicalPacket(jni, packet);
                    });
            }
        }
        if ((!m_logicalMovementHook.ready()||!m_logicalJumpHook.ready()) &&
            logicalMovementCapability &&
            tickMilliseconds >= m_nextLogicalMovementHookAttemptTick) {
            m_nextLogicalMovementHookAttemptTick = tickMilliseconds + 5000U;
            if(!m_logicalMovementHook.ready()) {
                (void)m_logicalMovementHook.install(
                    m_vm, cache->moveFlying,cache->setSprinting,this,
                    [](void* owner, JNIEnv* jni, jobject entity, jfloat strafe,
                       jfloat forward) noexcept -> jfloat {
                        return static_cast<GameBindings*>(owner)->beginLogicalMovement(
                            jni, entity, strafe, forward);
                    },
                    [](void* owner, JNIEnv* jni, jobject entity,
                       jfloat fallback) noexcept -> jfloat {
                        return static_cast<GameBindings*>(owner)->logicalMovementForward(
                            jni, entity, fallback);
                    },
                    [](void* owner, JNIEnv* jni, jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->endLogicalMovement(jni, entity);
                    },
                    [](void* owner,JNIEnv* jni,jobject entity,
                       bool sprintRequested) noexcept -> bool {
                        return static_cast<GameBindings*>(owner)->arbitrateLogicalSprint(
                            jni,entity,sprintRequested);
                    });
            }
            if(!m_logicalJumpHook.ready()) {
                (void)m_logicalJumpHook.install(
                    m_vm,cache->jump,this,
                    [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->beginLogicalJump(jni,entity);
                    },
                    [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->endLogicalJump(jni,entity);
                    });
            }
        }
        if(wantsSilent&&requested.silentControlAdaptation&&
           !m_headingHook.ready()&&cache->moveEntityWithHeading&&
           tickMilliseconds>=m_nextHeadingHookAttemptTick) {
            m_nextHeadingHookAttemptTick=tickMilliseconds+5000U;
            const bool installed=m_headingHook.install(
                m_vm,cache->moveEntityWithHeading,this,
                [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                    static_cast<GameBindings*>(owner)->beginLogicalHeading(jni,entity);
                },
                [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                    static_cast<GameBindings*>(owner)->endLogicalHeading(jni,entity);
                });
            char detail[96]{};
            std::snprintf(detail,sizeof(detail),"ready=%d jvmtiError=%d",
                installed?1:0,m_headingHook.lastError());
            m_logicalController.debug().event("HEADING_HOOK",
                m_logicalController.latest(),detail,true);
            log::info(installed?"Heading PRE/POST hook installed.":
                "Heading PRE/POST hook unavailable; SprintOwner remains Vanilla.");
        }
        if(wantsSilent&&requested.silentControlAdaptation&&
           !cache->moveEntityWithHeading&&!m_headingMappingMissingLogged) {
            m_headingMappingMissingLogged=true;
            m_logicalController.debug().event("HEADING_HOOK",
                m_logicalController.latest(),
                "ready=0 reason=moveEntityWithHeading_mapping_missing",true);
            log::info("Heading PRE/POST mapping unavailable; SprintOwner remains Vanilla.");
        }
        if (!m_logicalInteractionHook.ready() && cache->sendClickBlock &&
            tickMilliseconds >= m_nextLogicalInteractionHookAttemptTick) {
            m_nextLogicalInteractionHookAttemptTick = tickMilliseconds + 5000U;
            // Lunar's transformed clickMouse is not a reliable retransformation
            // target. Hook the per-tick held-left entry and let it consume only
            // intents already released by the fixed-CPS monotonic scheduler.
            (void)m_logicalInteractionHook.install(
                m_vm, nullptr, cache->sendClickBlock, this,
                [](void* owner, JNIEnv* jni, jobject mc,
                   LiveInteractionTransform::Entry entry, bool down) noexcept {
                    return static_cast<GameBindings*>(owner)->consumeLogicalInteraction(
                        jni, mc, entry, down);
                });
        }
        if(!m_attackOwnershipHook.ready()&&cache->attackEntity&&
           tickMilliseconds>=m_nextAttackOwnershipHookAttemptTick) {
            m_nextAttackOwnershipHookAttemptTick=tickMilliseconds+5000U;
            (void)m_attackOwnershipHook.install(m_vm,cache->attackEntity,this,
                [](void* owner,JNIEnv* jni,jobject original) noexcept -> jobject {
                    return static_cast<GameBindings*>(owner)->arbitrateLogicalAttack(
                        jni,original);
                });
        }
    }

    silent::MovementInput physicalMovement{};
    if(m_logicalController.debug().enabled() && !m_interactionObserver.ready() &&
       tickMilliseconds>=m_nextInteractionObserverAttempt) {
        m_nextInteractionObserverAttempt=tickMilliseconds+5000;
        const bool installed=m_interactionObserver.install(m_vm,
            {cache->attackEntity,cache->clickBlock,cache->onPlayerDamageBlock,cache->resetBlockRemoving},
            this,[](void* owner,JNIEnv* jni,LiveInteractionObserver::Event event,jobject argument) noexcept {
                static_cast<GameBindings*>(owner)->observeActualInteraction(jni,event,argument);
            });
        char detail[80]{}; std::snprintf(detail,sizeof(detail),"ready=%d jvmtiError=%d",installed,m_interactionObserver.error());
        m_logicalController.debug().event("ACTUAL_HOOK",m_logicalController.latest(),detail,true);
    } else if(m_interactionObserver.ready()) {
        m_interactionObserver.setEnabled(m_logicalController.debug().enabled());
    }
    silent::Vec3 currentVelocity{};
    bool logicalOnGround = false;
    bool logicalSprinting = false;
    std::uint64_t logicalPhysicsTick = 0U;
    if (logicalMovementCapability) {
        std::array<bool, 4U> keys{};
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            jobject binding = env->GetObjectField(settings,
                                                  cache->movementKeyFields[index]);
            if (!binding || env->ExceptionCheck() == JNI_TRUE) return fail();
            const jint code = env->CallIntMethod(binding, cache->getKeyCode);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            (void)queryLwjglKeyDown(env, code, keys[index]);
        }
        physicalMovement = {keys[0], keys[1], keys[2], keys[3]};
        currentVelocity = {
            env->GetDoubleField(player, cache->motionFields[0]),
            env->GetDoubleField(player, cache->motionFields[1]),
            env->GetDoubleField(player, cache->motionFields[2])};
        logicalOnGround = env->GetBooleanField(player, cache->onGround) == JNI_TRUE;
        logicalSprinting = env->CallBooleanMethod(player, cache->isSprinting) == JNI_TRUE;
        logicalPhysicsTick=cache->entityTicks
            ? static_cast<std::uint64_t>(env->GetIntField(player,cache->entityTicks))
            : 0U;
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    }

    std::array<silent::TargetCandidate, GameSnapshot::MaxEntityMarkers> candidates{};
    std::size_t candidateCount = 0U;
    std::uint32_t playerMarkers = 0U;
    std::uint32_t tabConfirmedMarkers = 0U;
    std::uint32_t colouredPlayerMarkers = 0U;
    std::uint32_t teammateMarkers = 0U;
    silent::Vec3 combatEye{};
    const bool combatEyeReady=readCombatEye(env,player,combatEye);
    const bool leftHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    (void)observeLogicalCamera(env,minecraft,leftHeld);
    const auto previousCombat=m_logicalController.latest();
    const auto observedAttackTick=m_lastAttackEntryTick.load(std::memory_order_acquire);
    const int observedAttackId=m_lastAttackEntryEntity.load(std::memory_order_acquire);
    const int diagnosticId=previousCombat.cameraMouseOverEntityId>=0
        ? previousCombat.cameraMouseOverEntityId
        : (observedAttackId>=0&&tickMilliseconds>=observedAttackTick&&
           tickMilliseconds-observedAttackTick<=1500U ? observedAttackId:-1);
    struct TargetDiagnostic final {
        int id=-1;
        bool markerFound=false,markerPlayer=false,confirmedPlayer=false;
        bool livePlayer=false;
        float health=-1.0F;
        bool self=false,teammate=false,lookup=false,boundsReady=false;
        bool candidateAdded=false,withinFov=false,withinPreAim=false;
        bool attackAvailable=false;
        double aimPointDistance=-1.0,aabbEntryDistance=-1.0,angle=-1.0;
    } targetDiag{};
    targetDiag.id=diagnosticId;
    const double configuredMaximum=std::clamp(requested.aimMaximumDistance,
        std::max(1,requested.aimMinimumDistance),128);
    const double preAimRange=requested.aimAttackViability
        ?std::min(3.5,configuredMaximum):configuredMaximum;
    const double attackReach=requested.aimAttackViability
        ?std::min(3.0,configuredMaximum):configuredMaximum;
    const auto identityHash = [](const EntityMarker& marker) noexcept {
        std::uint64_t value = 1469598103934665603ULL;
        const auto append = [&](const auto& text) noexcept {
            for (const char character : text) {
                if (!character) break;
                value ^= static_cast<unsigned char>(character);
                value *= 1099511628211ULL;
            }
        };
        if (marker.uuid[0]) append(marker.uuid);
        else append(marker.playerName);
        return value;
    };
    for (std::uint32_t index = 0U;
         index < std::min<std::uint32_t>(snapshot.entityMarkerCount,
             static_cast<std::uint32_t>(snapshot.entityMarkers.size())); ++index) {
        const EntityMarker& entity = snapshot.entityMarkers[index];
        const bool diagnostic=entity.entityId==diagnosticId;
        if(diagnostic) {
            targetDiag.markerFound=true;
            targetDiag.markerPlayer=entity.player;
            targetDiag.confirmedPlayer=entity.confirmedPlayer;
            targetDiag.health=entity.health;
            targetDiag.self=entity.entityId==snapshot.entityId;
        }
        if (entity.player) ++playerMarkers;
        if (entity.confirmedPlayer) ++tabConfirmedMarkers;
        const bool colouredPlayer = entity.player && entity.teamColor != 'u' &&
            entity.teamColor != '\0';
        if (colouredPlayer) ++colouredPlayerMarkers;
        if (colouredPlayer && snapshot.ownTeam != 'u' &&
            entity.teamColor == snapshot.ownTeam) ++teammateMarkers;
        // Hypixel may expose a nicked player's world-entity alias that differs
        // from the chosen TAB nickname.  TAB/UUID confirmation remains the
        // strongest signal, while a real player entity wearing a recognised
        // Bed Wars team colour is the safe in-match fallback. Shop NPCs and
        // opening robots have no recognised team colour and stay excluded.
        const bool validPlayer = entity.confirmedPlayer ||
            (snapshot.matchActive && colouredPlayer) ||
            (!snapshot.hypixelServer && entity.player);
        // Outside Hypixel Bed Wars, scoreboard/text colours are commonly ranks
        // or chat decoration rather than team identity (especially on
        // offline-mode servers). Applying the Bed Wars team filter globally
        // made only same-colour players mysteriously untargetable.
        const bool confirmedTeammate=silent::isConfirmedCombatTeammate(
            snapshot.hypixelServer,snapshot.matchActive,
            snapshot.ownTeam,entity.teamColor);
        if(diagnostic) targetDiag.teammate=confirmedTeammate;
        const bool eligible = validPlayer && entity.entityId != snapshot.entityId &&
            std::isfinite(entity.health) && entity.health > 0.0F &&
            !confirmedTeammate;
        if (!eligible || candidateCount >= candidates.size()) continue;
        if(!combatEyeReady||!cache->getEntityById) continue;
        jobject liveTarget=env->CallObjectMethod(world,cache->getEntityById,
                                                entity.entityId);
        if(diagnostic) {
            targetDiag.lookup=liveTarget&&env->ExceptionCheck()==JNI_FALSE;
            targetDiag.livePlayer=targetDiag.lookup&&cache->playerClass&&
                env->IsInstanceOf(liveTarget,cache->playerClass)==JNI_TRUE;
        }
        silent::Bounds bounds{};
        const bool boundsReady=liveTarget&&env->ExceptionCheck()==JNI_FALSE&&
            readCombatBounds(env,liveTarget,bounds);
        if(diagnostic) targetDiag.boundsReady=boundsReady;
        if(liveTarget) env->DeleteLocalRef(liveTarget);
        if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
        if(!boundsReady) continue;
        const auto reference=previousCombat.candidateTargetId==entity.entityId
            ? previousCombat.logical : aim::Angles{yaw,pitch};
        const auto aimPoint=silent::chooseCombatAimPoint(combatEye,bounds,
            reference,attackReach,requested.aimAttackViability,
            [&](const silent::Vec3 direction,const double limit) noexcept {
                silent::LogicalFramePlan ray{};
                ray.rayOrigin=combatEye;ray.rayDirection=direction;ray.rayLimit=limit;
                return traceLogicalBlock(env,world,ray);
            });
        if(diagnostic) {
            const double dx=aimPoint.point.x-combatEye.x;
            const double dy=aimPoint.point.y-combatEye.y;
            const double dz=aimPoint.point.z-combatEye.z;
            const double horizontal=std::hypot(dx,dz);
            targetDiag.aimPointDistance=std::hypot(horizontal,dy);
            if(targetDiag.aimPointDistance>1.0e-6&&
               std::isfinite(targetDiag.aimPointDistance)) {
                const silent::Vec3 direction{dx/targetDiag.aimPointDistance,
                    dy/targetDiag.aimPointDistance,dz/targetDiag.aimPointDistance};
                targetDiag.aabbEntryDistance=silent::RayTraceCoordinator::intersect(
                    combatEye,direction,bounds,std::max(preAimRange,attackReach));
                constexpr double degrees=180.0/3.14159265358979323846;
                const aim::Angles desired{std::atan2(dz,dx)*degrees-90.0,
                    -std::atan2(dy,horizontal)*degrees};
                targetDiag.angle=std::hypot(aim::wrap(desired.yaw-yaw),desired.pitch-pitch);
                targetDiag.withinFov=targetDiag.angle<=
                    std::clamp(requested.aimFovDegrees,1,360)*0.5+1.0e-5;
                targetDiag.withinPreAim=targetDiag.aimPointDistance<=preAimRange+1.0e-5;
            }
            targetDiag.attackAvailable=!requested.aimAttackViability||aimPoint.available;
            targetDiag.candidateAdded=true;
        }
        candidates[candidateCount++] = {
            entity.entityId,identityHash(entity),aimPoint.point,bounds,true,
            requested.aimSequentialTargets&&entity.hurtTime>0,
            !requested.aimAttackViability||aimPoint.available};
    }

    // If the camera/vanilla attack path can resolve an entity that was absent
    // from the marker pipeline, probe it directly so TARGET_DIAG identifies
    // marker classification versus world lookup/bounds failures.
    if(diagnosticId>=0&&!targetDiag.lookup&&cache->getEntityById) {
        jobject entity=env->CallObjectMethod(world,cache->getEntityById,diagnosticId);
        if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
        else if(entity) {
            targetDiag.lookup=true;
            targetDiag.livePlayer=cache->playerClass&&
                env->IsInstanceOf(entity,cache->playerClass)==JNI_TRUE;
            silent::Bounds bounds{};
            targetDiag.boundsReady=readCombatBounds(env,entity,bounds);
            if(cache->livingClass&&cache->getHealth&&
               env->IsInstanceOf(entity,cache->livingClass)==JNI_TRUE) {
                targetDiag.health=env->CallFloatMethod(entity,cache->getHealth);
                if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
            }
            env->DeleteLocalRef(entity);
        }
    }

    const bool canAim = requested.aimAssist && aimCapability && combatEyeReady &&
        snapshot.state == GameSnapshot::State::Ready && snapshot.health > 0.0F &&
        std::isfinite(yaw) && std::isfinite(pitch);
    const bool sprintFeature=canAim&&wantsSilent&&requested.silentControlAdaptation&&
        m_headingHook.ready()&&m_logicalMovementHook.ready()&&
        m_logicalJumpHook.ready();
    m_sprintFeatureEnabled.store(sprintFeature,std::memory_order_release);
    (void)syncSprintOwner();
    const jfloat sensitivity = env->GetFloatField(settings, cache->mouseSensitivity);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();
    const bool silentRotationReady = silentAvailable();
    const bool silentAttackBindingsReady=silentAttackAvailable();
    const silent::RuntimeCapabilities silentCapabilities{
        silentRotationReady,silentAttackBindingsReady,
        m_logicalInteractionHook.ready(),m_attackOwnershipHook.ready()};
    const auto monotonicMicroseconds=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    silent::LogicalFrameInput logicalInput{};
    logicalInput.tick = tickMilliseconds;
    logicalInput.physicsTick = logicalPhysicsTick;
    logicalInput.worldGeneration = snapshot.worldGeneration;
    logicalInput.localEntityId = snapshot.entityId;
    // Silent combat is activated only when both packet publication and its
    // stable input/PRE dispatch boundary exist. Falling back to vanilla attack
    // while a hidden rotation is active would attack a camera-ray target.
    logicalInput.enabled = canAim&&(!wantsSilent||
        silentCapabilities.attackSchedulerReady());
    logicalInput.silent = wantsSilent;
    logicalInput.mode = mode;
    logicalInput.nearestPriority = requested.aimNearestPriority;
    logicalInput.enforceAttackAvailability = requested.aimAttackViability;
    // Movement ownership is an explicit user policy. It is deliberately
    // independent from ray/attack availability so losing an executable hit can
    // never cause a one-tick movement or sprint direction pulse.
    logicalInput.coordinateMovement = requested.silentControlAdaptation;
    logicalInput.sequentialTargets = requested.aimSequentialTargets;
    logicalInput.leftMouseDown = leftHeld;
    logicalInput.heldItemPolicy = requested.silentControlAdaptation
        ? heldItemPolicy : silent::HeldItemPolicy::Other;
    logicalInput.minimumDistance = std::clamp(requested.aimMinimumDistance, 0, 64);
    // With availability checking enabled, 3.5 m is acquisition/pre-aim only;
    // current physics-space reach and occlusion remain a separate 3.0 m gate.
    logicalInput.maximumDistance=requested.aimAttackViability
        ? preAimRange:configuredMaximum;
    logicalInput.attackReach=requested.aimAttackViability
        ? attackReach:configuredMaximum;
    logicalInput.fovDegrees = std::clamp(requested.aimFovDegrees, 1, 360);
    logicalInput.aimSpeedPercent = requested.aimSpeedPercent;
    logicalInput.mouseSensitivity = sensitivity;
    logicalInput.camera = {yaw, pitch};
    logicalInput.eye = combatEye;
    logicalInput.physicalMovement = physicalMovement;
    logicalInput.currentVelocity = currentVelocity;
    logicalInput.sprinting = logicalSprinting;
    logicalInput.onGround = logicalOnGround;
    logicalInput.candidates = {candidates.data(), candidateCount};
    // Schedule and bind against this SAME physics snapshot. Never leave a new
    // intent waiting for the next rendered frame to choose its target/rotation.
    m_logicalController.updateAttackClock(leftHeld,
        canAim&&wantsSilent&&silentCapabilities.attackSchedulerReady(),
        monotonicMicroseconds,requested.aimAttackCps);
    const silent::LogicalFramePlan logicalPlan =
        m_logicalController.advance(logicalInput,
            [&](const silent::LogicalFramePlan& pending) noexcept {
                return traceLogicalBlock(env,world,pending);
            });

    if(m_logicalController.debug().enabled()&&diagnosticId>=0) {
        const bool cameraMissing=previousCombat.cameraMouseOverEntityId>=0&&
            logicalPlan.candidateTargetId<0;
        const bool force=cameraMissing&&(!m_targetDiagMissingLatched||
            m_lastTargetDiagMissingId!=diagnosticId);
        if(!cameraMissing) {
            m_targetDiagMissingLatched=false;
            m_lastTargetDiagMissingId=-1;
        } else if(force) {
            m_targetDiagMissingLatched=true;
            m_lastTargetDiagMissingId=diagnosticId;
        }
        if(force||tickMilliseconds>=m_nextTargetDiagTick) {
            m_nextTargetDiagTick=tickMilliseconds+2000U;
            const char* reject="none";
            if(!targetDiag.markerFound&&targetDiag.livePlayer)
                reject="marker_missing";
            else if(!targetDiag.markerPlayer) reject="not_player";
            else if(targetDiag.self) reject="self";
            else if(!std::isfinite(targetDiag.health)||targetDiag.health<=0.0F)
                reject="dead";
            else if(targetDiag.teammate) reject="teammate";
            else if(!targetDiag.lookup) reject="entity_lookup_failed";
            else if(!targetDiag.boundsReady) reject="bounds_unavailable";
            else if(!targetDiag.markerFound) reject="selector_not_chosen";
            else if(!targetDiag.withinPreAim) reject="outside_preaim_range";
            else if(!targetDiag.withinFov) reject="outside_fov";
            else if(requested.aimAttackViability&&!targetDiag.attackAvailable)
                reject=targetDiag.aabbEntryDistance<0.0||
                    targetDiag.aabbEntryDistance>attackReach+1.0e-5
                    ?"outside_attack_reach":"occluded";
            else if(logicalPlan.candidateTargetId!=diagnosticId)
                reject="selector_not_chosen";
            char detail[1024]{};
            std::snprintf(detail,sizeof(detail),
                "id=%d markerFound=%d markerPlayer=%d livePlayer=%d confirmedPlayer=%d health=%.2f self=%d teammate=%d getEntityById=%d boundsReady=%d candidateAdded=%d aimPointDistance=%.3f aabbEntryDistance=%.3f angle=%.2f withinFov=%d withinPreAimRange=%d attackAvailable=%d reject=%s players=%u candidateCount=%zu cameraEntity=%d selectedCandidate=%d",
                diagnosticId,targetDiag.markerFound?1:0,targetDiag.markerPlayer?1:0,
                targetDiag.livePlayer?1:0,targetDiag.confirmedPlayer?1:0,targetDiag.health,
                targetDiag.self?1:0,targetDiag.teammate?1:0,
                targetDiag.lookup?1:0,targetDiag.boundsReady?1:0,
                targetDiag.candidateAdded?1:0,targetDiag.aimPointDistance,
                targetDiag.aabbEntryDistance,targetDiag.angle,
                targetDiag.withinFov?1:0,targetDiag.withinPreAim?1:0,
                targetDiag.attackAvailable?1:0,reject,playerMarkers,candidateCount,
                previousCombat.cameraMouseOverEntityId,logicalPlan.candidateTargetId);
            m_logicalController.debug().event("TARGET_DIAG",
                m_logicalController.latest(),detail,force);
        }
    }

    if (m_logicalController.debug().enabled() &&
        tickMilliseconds >= m_nextAimCandidateDebugTick) {
        m_nextAimCandidateDebugTick = tickMilliseconds + 5000U;
        char detail[320]{};
        std::snprintf(detail, sizeof(detail),
            "markers=%u players=%u tabConfirmed=%u coloured=%u teammates=%u eligible=%zu match=%d own=%c packetReady=%d movementReady=%d interactionReady=%d requestedMode=%s effectiveMode=%s",
            snapshot.entityMarkerCount, playerMarkers, tabConfirmedMarkers,
            colouredPlayerMarkers, teammateMarkers, candidateCount,
            snapshot.matchActive ? 1 : 0,
            snapshot.ownTeam ? snapshot.ownTeam : 'u',
            silentRotationReady ? 1 : 0,
            (m_logicalMovementHook.ready()&&m_logicalJumpHook.ready()) ? 1 : 0,
            silentAttackAvailable() ? 1 : 0,
            requested.aimLockOnMode ? "lock" : "smooth",
            mode == aim::Mode::LockOn ? "lock" : "smooth");
        m_logicalController.debug().event("CANDIDATE_SCAN",
            m_logicalController.latest(), detail, candidateCount == 0U);
    }

    if (logicalPlan.interactionTransition.kind !=
            silent::InteractionCommandKind::None &&
        !executeLogicalInteraction(env, minecraft,
                                   logicalPlan.interactionTransition)) {
        m_logicalController.deferInteraction(
            logicalPlan.interactionTransition);
        return fail();
    }

    // Render/update owns preparation only. A rotation-confirmed AttackIntent is
    // deliberately left pending for the transformed Minecraft input/PRE
    // boundary; dispatching here can run after this tick's movement POST.

    if (logicalPlan.writeVisibleRotation) {
        const aim::Angles previous{
            env->GetFloatField(player, cache->previousRotationYaw),
            env->GetFloatField(player, cache->previousRotationPitch)};
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        const aim::Angles shifted = aim::shiftedPrevious(
            previous, {yaw, pitch}, logicalPlan.visibleRotation);
        env->SetFloatField(player, cache->previousRotationYaw,
                           static_cast<jfloat>(shifted.yaw));
        env->SetFloatField(player, cache->previousRotationPitch,
                           static_cast<jfloat>(shifted.pitch));
        env->SetFloatField(player, cache->rotationYaw,
                           static_cast<jfloat>(logicalPlan.visibleRotation.yaw));
        env->SetFloatField(player, cache->rotationPitch,
                           static_cast<jfloat>(logicalPlan.visibleRotation.pitch));
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    }
    const bool logicalMovementReady=m_logicalMovementHook.ready()&&
        m_logicalJumpHook.ready();
    // Hook ownership is feature-scoped. The physics consumer decides whether
    // the current target requires a transform or a vanilla passthrough.
    const bool logicalMovementEnabled=canAim&&wantsSilent&&
        requested.silentControlAdaptation&&logicalMovementReady;
    m_logicalMovementHook.setEnabled(logicalMovementEnabled);
    m_logicalJumpHook.setEnabled(logicalMovementEnabled);
    m_headingHook.setEnabled(sprintFeature);
    const bool logicalAttackEnabled=logicalPlan.silentActive&&
        silentCapabilities.attackSchedulerReady();
    m_logicalInteractionHook.setEnabled(
        (logicalAttackEnabled&&silentCapabilities.heldArbitrationReady())||
        smartHotbarRequested);
    m_attackOwnershipHook.setEnabled(logicalAttackEnabled&&
        silentCapabilities.ownershipArbitrationReady());
    m_silentRotationHook.setEnabled(
        (logicalPlan.silentActive && silentRotationReady) ||
        m_logicalController.restoring() ||
        m_logicalController.packetContinuityRequired() ||
        m_logicalController.debug().enabled() ||
        smartHotbarRequested);

    // Aim-only operation intentionally stops here. The remainder reads block
    // support, movement keys and inventory/controller mappings.
    if ((!movementRequested || !movementCapability) &&
        ((!bedBreakerRequested && !m_bedBreakerTargetValid) ||
         !bedBreakerCapability)) {
        (void)releaseForcedSneak();
        m_scaffoldPlatformYValid = false;
        m_lastGameplayTick = tickMilliseconds;
        return finish((requested.aimAssist && aimCapability)||freeLookRequested);
    }

    if (bedBreakerRequested && bedBreakerCapability &&
        tickMilliseconds - m_lastBedBreakerTick >= 45U) {
        m_lastBedBreakerTick = tickMilliseconds;
        jobject controller = env->GetObjectField(minecraft,
                                                  cache->playerControllerField);
        jobject inventory = env->GetObjectField(player, cache->inventoryField);
        const jfloat mappedReach = controller == nullptr ? 0.0F :
            env->CallFloatMethod(controller, cache->getBlockReachDistance);
        if (env->ExceptionCheck() == JNI_TRUE || controller == nullptr ||
            inventory == nullptr || !std::isfinite(mappedReach)) return fail();
        const double reach = std::clamp(static_cast<double>(mappedReach), 2.0, 8.0);
        const double breakerEyeX = snapshot.x;
        const double breakerEyeY = snapshot.y + 1.62;
        const double breakerEyeZ = snapshot.z;

        struct BlockCoordinate final { int x=0,y=0,z=0; };
        struct PathChoice final {
            BlockCoordinate target{};
            int solidCount = std::numeric_limits<int>::max();
            double length = std::numeric_limits<double>::max();
            bool valid = false;
        } bestPath;
        const auto isOwnBed = [&](const BedMarker& bed) noexcept {
            if (!snapshot.ownBedKnown) return false;
            return bed.y == snapshot.ownBedY &&
                ((bed.x == snapshot.ownBedX && bed.z == snapshot.ownBedZ) ||
                 (bed.footX == snapshot.ownBedX && bed.footZ == snapshot.ownBedZ));
        };
        const auto sameCoordinate = [](const BlockCoordinate& a,
                                       const BlockCoordinate& b) noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        const auto blockKind = [&](const BlockCoordinate coordinate,
                                   bool& air, bool& bed) noexcept {
            jobject positionObject = env->NewObject(cache->blockPosClass,
                cache->blockPosConstructor, coordinate.x, coordinate.y, coordinate.z);
            if (positionObject == nullptr || env->ExceptionCheck() == JNI_TRUE) {
                clearException(env); air = false; bed = false; return false;
            }
            const jboolean empty = env->CallBooleanMethod(world,
                cache->isAirBlock, positionObject);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env); env->DeleteLocalRef(positionObject);
                air = false; bed = false; return false;
            }
            air = empty == JNI_TRUE;
            bed = false;
            if (!air) {
                jobject state = env->CallObjectMethod(world, cache->getBlockState,
                                                       positionObject);
                jobject block = state == nullptr ? nullptr :
                    env->CallObjectMethod(state, cache->getBlock);
                if (env->ExceptionCheck() == JNI_TRUE) clearException(env);
                else bed = block != nullptr &&
                    env->IsInstanceOf(block, cache->bedClass) == JNI_TRUE;
                if (block != nullptr) env->DeleteLocalRef(block);
                if (state != nullptr) env->DeleteLocalRef(state);
            }
            env->DeleteLocalRef(positionObject);
            return true;
        };

        constexpr std::array<std::array<double, 3U>, 5U> offsets{{
            {{0.0,0.44,0.0}}, {{0.30,0.44,0.0}}, {{-0.30,0.44,0.0}},
            {{0.0,0.44,0.30}}, {{0.0,0.44,-0.30}}}};
        for (std::uint32_t bedIndex = 0U;
             bedIndex < std::min(snapshot.bedMarkerCount,
                 static_cast<std::uint32_t>(snapshot.bedMarkers.size())); ++bedIndex) {
            const BedMarker& bedMarker = snapshot.bedMarkers[bedIndex];
            if (isOwnBed(bedMarker)) continue;
            const std::array<BlockCoordinate, 2U> halves{{
                {bedMarker.x, bedMarker.y, bedMarker.z},
                {bedMarker.footX, bedMarker.y, bedMarker.footZ}}};
            for (const BlockCoordinate half : halves) {
                for (const auto& offset : offsets) {
                    const double goalX = half.x + 0.5 + offset[0];
                    const double goalY = half.y + offset[1];
                    const double goalZ = half.z + 0.5 + offset[2];
                    const double dx = goalX-breakerEyeX;
                    const double dy = goalY-breakerEyeY;
                    const double dz = goalZ-breakerEyeZ;
                    const double length = std::sqrt(dx*dx+dy*dy+dz*dz);
                    if (!std::isfinite(length) || length > reach || length < 0.2) continue;
                    const int samples = std::clamp(
                        static_cast<int>(std::ceil(length / 0.16)), 2, 64);
                    BlockCoordinate previous{std::numeric_limits<int>::min(),0,0};
                    BlockCoordinate firstSolid{};
                    int solids = 0;
                    bool reachedBed = false;
                    bool validRay = true;
                    for (int sampleIndex = 1; sampleIndex <= samples; ++sampleIndex) {
                        const double t = static_cast<double>(sampleIndex) /
                                         static_cast<double>(samples);
                        const BlockCoordinate coordinate{
                            static_cast<int>(std::floor(breakerEyeX + dx*t)),
                            static_cast<int>(std::floor(breakerEyeY + dy*t)),
                            static_cast<int>(std::floor(breakerEyeZ + dz*t))};
                        if (sameCoordinate(coordinate, previous)) continue;
                        previous = coordinate;
                        bool air = false, isBedBlock = false;
                        if (!blockKind(coordinate, air, isBedBlock)) {
                            validRay = false; break;
                        }
                        if (air) continue;
                        if (solids == 0) firstSolid = coordinate;
                        ++solids;
                        if (isBedBlock) { reachedBed = true; break; }
                    }
                    if (!validRay || !reachedBed || solids <= 0) continue;
                    if (!bestPath.valid || solids < bestPath.solidCount ||
                        (solids == bestPath.solidCount && length < bestPath.length)) {
                        bestPath = {firstSolid, solids, length, true};
                    }
                }
            }
        }

        if (!bestPath.valid) {
            if (m_bedBreakerTargetValid) {
                env->CallVoidMethod(controller, cache->resetBlockRemoving);
                clearException(env);
                m_bedBreakerTargetValid = false;
            }
        } else {
            jobject targetPosition = env->NewObject(cache->blockPosClass,
                cache->blockPosConstructor, bestPath.target.x,
                bestPath.target.y, bestPath.target.z);
            jobject targetState = targetPosition == nullptr ? nullptr :
                env->CallObjectMethod(world, cache->getBlockState, targetPosition);
            jobject targetBlock = targetState == nullptr ? nullptr :
                env->CallObjectMethod(targetState, cache->getBlock);
            jobjectArray hotbar = static_cast<jobjectArray>(env->GetObjectField(
                inventory, cache->mainInventory));
            if (env->ExceptionCheck() == JNI_TRUE || targetPosition == nullptr ||
                targetBlock == nullptr || hotbar == nullptr) return fail();
            int selectedSlot = std::clamp(static_cast<int>(
                env->GetIntField(inventory, cache->currentItem)), 0, 8);
            float bestStrength = -1.0F;
            const jsize hotbarLength = std::min<jsize>(env->GetArrayLength(hotbar), 9);
            for (jsize slot = 0; slot < hotbarLength; ++slot) {
                jobject stack = env->GetObjectArrayElement(hotbar, slot);
                if (stack == nullptr) continue;
                const jfloat strength = env->CallFloatMethod(
                    stack, cache->getStrVsBlock, targetBlock);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                if (std::isfinite(strength) && strength > bestStrength) {
                    bestStrength = strength; selectedSlot = static_cast<int>(slot);
                }
                env->DeleteLocalRef(stack);
            }
            env->SetIntField(inventory, cache->currentItem, selectedSlot);
            const double centreX = bestPath.target.x + 0.5;
            const double centreY = bestPath.target.y + 0.5;
            const double centreZ = bestPath.target.z + 0.5;
            const double faceX = breakerEyeX-centreX;
            const double faceY = breakerEyeY-centreY;
            const double faceZ = breakerEyeZ-centreZ;
            int facingIndex = 1;
            if (std::abs(faceY) >= std::abs(faceX) &&
                std::abs(faceY) >= std::abs(faceZ)) facingIndex = faceY >= 0 ? 1 : 0;
            else if (std::abs(faceX) >= std::abs(faceZ)) facingIndex = faceX >= 0 ? 5 : 4;
            else facingIndex = faceZ >= 0 ? 3 : 2;
            jobject facing = env->CallStaticObjectMethod(cache->enumFacingClass,
                cache->getFacingByIndex, facingIndex);
            if (env->ExceptionCheck() == JNI_TRUE || facing == nullptr) return fail();
            const bool sameTarget = m_bedBreakerTargetValid &&
                m_bedBreakerTargetX == bestPath.target.x &&
                m_bedBreakerTargetY == bestPath.target.y &&
                m_bedBreakerTargetZ == bestPath.target.z;
            if (!sameTarget) {
                if (m_bedBreakerTargetValid)
                    env->CallVoidMethod(controller, cache->resetBlockRemoving);
                env->CallBooleanMethod(controller, cache->clickBlock,
                                       targetPosition, facing);
                m_bedBreakerTargetX = bestPath.target.x;
                m_bedBreakerTargetY = bestPath.target.y;
                m_bedBreakerTargetZ = bestPath.target.z;
                m_bedBreakerTargetValid = true;
            } else {
                env->CallBooleanMethod(controller, cache->onPlayerDamageBlock,
                                       targetPosition, facing);
            }
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
    } else if (m_bedBreakerTargetValid && bedBreakerCapability) {
        jobject controller = env->GetObjectField(minecraft,
                                                  cache->playerControllerField);
        if (controller != nullptr)
            env->CallVoidMethod(controller, cache->resetBlockRemoving);
        clearException(env);
        m_bedBreakerTargetValid = false;
    }

    if (!movementRequested || !movementCapability) {
        (void)releaseForcedSneak();
        m_scaffoldPlatformYValid = false;
        m_lastGameplayTick = tickMilliseconds;
        return finish(bedBreakerRequested && bedBreakerCapability);
    }

    if (localMobAuraRequested && cache->playerControllerField != nullptr &&
        cache->hostileClass != nullptr &&
        cache->attackEntity != nullptr &&
        tickMilliseconds - m_lastLocalAttackTick >=
            static_cast<std::uint64_t>(std::clamp(
                requested.localAttackDelayMs, 100, 1500))) {
        const EntityMarker* nearest = nullptr;
        const double reach = static_cast<double>(std::clamp(
            requested.localMobReach, 3, 10));
        for (std::uint32_t index = 0U;
             index < snapshot.entityMarkerCount; ++index) {
            const EntityMarker& marker = snapshot.entityMarkers[index];
            if (!marker.hostile || marker.player || marker.health <= 0.0F ||
                marker.distance > reach) continue;
            if (nearest == nullptr || marker.distance < nearest->distance)
                nearest = &marker;
        }
        if (nearest != nullptr) {
            jobject loaded = cache->loadedEntitiesField != nullptr
                ? env->GetObjectField(world, cache->loadedEntitiesField)
                : env->CallObjectMethod(world, cache->getLoadedEntities);
            jobjectArray entities = loaded == nullptr ? nullptr :
                static_cast<jobjectArray>(env->CallObjectMethod(
                    loaded, cache->listToArray));
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            jobject targetObject = nullptr;
            if (entities != nullptr) {
                const jsize count = std::min<jsize>(
                    env->GetArrayLength(entities), 512);
                for (jsize index = 0; index < count; ++index) {
                    jobject candidate = env->GetObjectArrayElement(entities, index);
                    if (candidate == nullptr) continue;
                    const bool hostile = env->IsInstanceOf(
                        candidate, cache->hostileClass) == JNI_TRUE;
                    const jint id = hostile ? env->CallIntMethod(
                        candidate, cache->getEntityId) : -1;
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (hostile && id == nearest->entityId &&
                        env->IsInstanceOf(candidate, cache->playerClass) != JNI_TRUE) {
                        targetObject = candidate;
                        break;
                    }
                    env->DeleteLocalRef(candidate);
                }
            }
            if (targetObject != nullptr) {
                jobject controller = env->GetObjectField(
                    minecraft, cache->playerControllerField);
                if (env->ExceptionCheck() == JNI_TRUE || controller == nullptr)
                    return fail();
                env->CallVoidMethod(controller, cache->attackEntity,
                                    player, targetObject);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                m_lastLocalAttackTick = tickMilliseconds;
            }
        }
    }
    jobject sneakBinding = env->GetObjectField(settings, cache->keyBindSneakField);
    if (env->ExceptionCheck() == JNI_TRUE || sneakBinding == nullptr) return fail();
    const jint keyCode = env->CallIntMethod(sneakBinding, cache->getKeyCode);
    jobject bounds = env->CallObjectMethod(player, cache->getBounds);
    if (env->ExceptionCheck() == JNI_TRUE || keyCode <= 0 || bounds == nullptr)
        return fail();

    const double minX = env->GetDoubleField(bounds, cache->minX);
    const double minY = env->GetDoubleField(bounds, cache->minY);
    const double minZ = env->GetDoubleField(bounds, cache->minZ);
    const double maxX = env->GetDoubleField(bounds, cache->maxX);
    const double maxZ = env->GetDoubleField(bounds, cache->maxZ);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    // Read physical movement once and reuse it for edge prediction, movement
    // modules and scaffold targeting. This mirrors Minecraft's movement-input
    // stage and avoids one-frame disagreement between those systems.
    std::array<bool, 6U> input{}; // forward, back, left, right, jump, sneak
    if (movementCapability) {
        for (std::size_t index = 0U; index < 5U; ++index) {
            jobject binding = env->GetObjectField(settings,
                cache->movementKeyFields[index]);
            if (env->ExceptionCheck() == JNI_TRUE || binding == nullptr) return fail();
            const jint code = env->CallIntMethod(binding, cache->getKeyCode);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            (void)queryLwjglKeyDown(env, code, input[index]);
        }
        (void)queryLwjglKeyDown(env, keyCode, input[5U]);
    }
    const bool onGround = movementCapability &&
        env->GetBooleanField(player, cache->onGround) == JNI_TRUE;
    if (env->ExceptionCheck() == JNI_TRUE) return fail();
    const double forward = (input[0U] ? 1.0 : 0.0) - (input[1U] ? 1.0 : 0.0);
    // Minecraft's positive moveStrafing direction is left. The previous
    // right-minus-left expression inverted A and D for every movement module.
    const double strafe = (input[2U] ? 1.0 : 0.0) - (input[3U] ? 1.0 : 0.0);
    const double magnitude = std::hypot(forward, strafe);
    const double normalizedForward = magnitude > 0.001 ? forward / magnitude : 0.0;
    const double normalizedStrafe = magnitude > 0.001 ? strafe / magnitude : 0.0;
    constexpr double pi = 3.14159265358979323846;
    const double radians = static_cast<double>(yaw) * pi / 180.0;
    const double directionX = -std::sin(radians) * normalizedForward +
                              std::cos(radians) * normalizedStrafe;
    const double directionZ =  std::cos(radians) * normalizedForward +
                              std::sin(radians) * normalizedStrafe;

    // Use the motion that Minecraft actually calculated for this tick.  Key
    // intent alone is insufficient while airborne (or after sprinting over a
    // diagonal edge), because inertia can carry the player somewhere that no
    // currently pressed key points at.
    const double actualMotionX = env->GetDoubleField(
        player, cache->motionFields[0U]);
    const double actualMotionY = env->GetDoubleField(
        player, cache->motionFields[1U]);
    const double actualMotionZ = env->GetDoubleField(
        player, cache->motionFields[2U]);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    bool collisionQueryFailed = false;
    const auto hasSupport = [&](double dx, double dz, double inset) noexcept {
        if (cache->getCollidingBoxes == nullptr || cache->aabbConstructor == nullptr) {
            collisionQueryFailed = true;
            return false;
        }
        // Query real collision geometry, not "non-air": grass/water are not
        // support, whereas slabs/stairs have partial-height collision shapes.
        jobject probe = env->NewObject(cache->aabbClass, cache->aabbConstructor,
            minX + dx + inset, minY - 0.60, minZ + dz + inset,
            maxX + dx - inset, minY + 0.001, maxZ + dz - inset);
        jobject collisions = probe != nullptr && !env->ExceptionCheck()
            ? env->CallObjectMethod(world, cache->getCollidingBoxes, player, probe)
            : nullptr;
        const jint count = collisions != nullptr && !env->ExceptionCheck()
            ? env->CallIntMethod(collisions, cache->listSize) : 0;
        if (env->ExceptionCheck() || probe == nullptr || collisions == nullptr) {
            collisionQueryFailed = true;
            clearException(env);
        }
        if (collisions != nullptr) env->DeleteLocalRef(collisions);
        if (probe != nullptr) env->DeleteLocalRef(probe);
        return count > 0;
    };
    const bool guardActive = requested.safewalk && onGround && !requested.fly &&
        !input[4U]; // jumping deliberately suspends vanilla ledge protection
    const bool atEdge = guardActive && safewalk::needsSneak(
        requested.safewalkEdgeSensitivity, actualMotionX, actualMotionZ,
        directionX, directionZ, hasSupport);
    if (collisionQueryFailed) return fail();
    const bool supportRestored = guardActive && !atEdge;
    const bool pitchAllowsSafewalk = pitch >= static_cast<float>(std::clamp(
        requested.safewalkMinimumPitch, -90, 90));
    m_safewalkSupportMask = atEdge ? 0x0FU : 0U;

    if (m_safewalkSneakForced && supportRestored &&
        m_safewalkReleaseAt == 0U) {
        m_safewalkReleaseAt = tickMilliseconds + static_cast<std::uint64_t>(
            std::clamp(requested.safewalkReleaseDelayMs, 0, 750));
    }
    if (m_safewalkSneakForced && atEdge) m_safewalkReleaseAt = 0U;
    if (m_safewalkSneakForced && m_safewalkReleaseAt != 0U &&
        tickMilliseconds >= m_safewalkReleaseAt) {
        const bool released = releaseForcedSneak();
        if (!movementCapability) return finish(released);
    }

    if (!guardActive) {
        (void)releaseForcedSneak();
    } else if (!m_safewalkSneakForced && atEdge && pitchAllowsSafewalk) {
        if (setSneakState(keyCode, true)) {
            m_safewalkSneakForced = true;
            m_safewalkSneakKeyCode = keyCode;
            m_safewalkReleaseAt = 0U;
        } else return fail();
    }
    if (m_safewalkSneakForced && !pitchAllowsSafewalk) {
        const bool released = releaseForcedSneak();
        if (!movementCapability) return finish(released);
    }
    // LWJGL/KeyBinding updates may replace the synthetic state between frames.
    // Reassert it while owned; release restores the user's physical Shift key.
    if (m_safewalkSneakForced) (void)setSneakState(keyCode, true);

    if (!movementCapability) return finish(m_safewalkSneakForced);

    if (requested.fly) {
        const double speed = 0.34 * static_cast<double>(std::clamp(
            requested.flySpeedPercent, 10, 500)) / 100.0;
        env->SetDoubleField(player, cache->motionFields[0U], directionX * speed);
        env->SetDoubleField(player, cache->motionFields[2U], directionZ * speed);
        const double vertical = (input[4U] ? speed : 0.0) -
                                (input[5U] ? speed : 0.0);
        env->SetDoubleField(player, cache->motionFields[1U], vertical);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    } else if (requested.bhop && magnitude > 0.001) {
        const double airSpeed = 0.30 * static_cast<double>(std::clamp(
            requested.bhopAirSpeedPercent, 10, 300)) / 100.0;
        if (onGround && requested.bhopAutoJump) {
            env->CallVoidMethod(player, cache->jump);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            // EntityLivingBase.jump applies the vanilla sprint impulse. Clamp
            // only excess speed on the landing/jump frame so Auto Jump cannot
            // create a one-tick boost, while preserving slower player motion.
            const double jumpX = env->GetDoubleField(player, cache->motionFields[0U]);
            const double jumpZ = env->GetDoubleField(player, cache->motionFields[2U]);
            const double jumpHorizontal = std::hypot(jumpX, jumpZ);
            if (jumpHorizontal > airSpeed && jumpHorizontal > 0.0001) {
                const double scale = airSpeed / jumpHorizontal;
                env->SetDoubleField(player, cache->motionFields[0U], jumpX * scale);
                env->SetDoubleField(player, cache->motionFields[2U], jumpZ * scale);
            }
        } else if (!onGround) {
            env->SetDoubleField(player, cache->motionFields[0U], directionX * airSpeed);
            env->SetDoubleField(player, cache->motionFields[2U], directionZ * airSpeed);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
    } else if (requested.longJump && onGround && magnitude > 0.001 &&
               tickMilliseconds - m_lastLongJumpTick >= 650U) {
        const double speed = 0.72 * static_cast<double>(std::clamp(
            requested.longJumpSpeedPercent, 25, 250)) / 100.0;
        env->SetDoubleField(player, cache->motionFields[0U], directionX * speed);
        env->SetDoubleField(player, cache->motionFields[1U], 0.42);
        env->SetDoubleField(player, cache->motionFields[2U], directionZ * speed);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        m_lastLongJumpTick = tickMilliseconds;
    }

    if (requested.scaffold && placementCapability &&
        tickMilliseconds - m_lastScaffoldPlacementTick >= 35U) {
        const int supportLayer = static_cast<int>(std::floor(minY - 0.06));
        if (!m_scaffoldPlatformYValid || onGround) {
            m_scaffoldPlatformY = supportLayer;
            m_scaffoldPlatformYValid = true;
        }
        jobject inventory = env->GetObjectField(player, cache->inventoryField);
        jobject controller = env->GetObjectField(minecraft,
            cache->playerControllerField);
        jobjectArray hotbar = inventory == nullptr ? nullptr :
            static_cast<jobjectArray>(env->GetObjectField(inventory,
                cache->mainInventory));
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        auto allowedBlock = [](const int id) noexcept {
            if (id == 12 || id == 13) return false; // sand / gravel fall
            switch (id) {
            case 1: case 4: case 5: case 24: case 35: case 45:
            case 87: case 98: case 121: case 159: return true;
            default: return false;
            }
        };
        int selectedSlot = -1;
        jobject selectedStack = nullptr;
        if (hotbar != nullptr && controller != nullptr) {
            const jsize length = std::min<jsize>(9, env->GetArrayLength(hotbar));
            const int current = std::clamp(
                static_cast<int>(env->GetIntField(inventory, cache->currentItem)),
                0, std::max(0, static_cast<int>(length) - 1));
            for (jsize pass = 0; pass < length; ++pass) {
                const int slot = pass == 0 ? current :
                    (static_cast<int>(pass) <= current
                        ? static_cast<int>(pass) - 1 : static_cast<int>(pass));
                jobject stack = env->GetObjectArrayElement(hotbar, slot);
                if (stack == nullptr) continue;
                jobject itemObject = env->CallObjectMethod(stack, cache->getItem);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                if (itemObject != nullptr && env->IsInstanceOf(
                        itemObject, cache->itemBlockClass) == JNI_TRUE) {
                    jobject blockObject = env->CallObjectMethod(
                        itemObject, cache->getBlockFromItem);
                    const jint blockId = blockObject == nullptr ? -1 :
                        env->CallStaticIntMethod(cache->blockClass,
                            cache->getIdFromBlock, blockObject);
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (allowedBlock(blockId)) {
                        selectedSlot = slot;
                        selectedStack = stack;
                        break;
                    }
                }
            }
        }
        if (selectedSlot >= 0 && selectedStack != nullptr) {
            const double centerX = (minX + maxX) * 0.5;
            const double centerZ = (minZ + maxZ) * 0.5;
            std::array<std::array<int, 3U>, 32U> targets{};
            std::size_t targetCount = 0U;
            const auto addTargetAt = [&](const double x, const int layer,
                                         const double z) noexcept {
                const std::array<int, 3U> candidate{
                    static_cast<int>(std::floor(x)), layer,
                    static_cast<int>(std::floor(z))};
                for (std::size_t i = 0; i < targetCount; ++i)
                    if (targets[i] == candidate) return;
                if (targetCount < targets.size()) targets[targetCount++] = candidate;
            };
            // Start with the current footprint, then integrate the player's
            // real velocity through several vanilla-like air ticks. This keeps
            // diagonal sprint jumps covered even after the player releases or
            // changes a movement key mid-air.
            constexpr double cornerInset = 0.025;
            const double halfWidthX = std::max(0.0,
                (maxX - minX) * 0.5 - cornerInset);
            const double halfWidthZ = std::max(0.0,
                (maxZ - minZ) * 0.5 - cornerInset);
            const auto addFootprintAt = [&](const double x, const int layer,
                                            const double z) noexcept {
                addTargetAt(x, layer, z);
                addTargetAt(x - halfWidthX, layer, z - halfWidthZ);
                addTargetAt(x - halfWidthX, layer, z + halfWidthZ);
                addTargetAt(x + halfWidthX, layer, z - halfWidthZ);
                addTargetAt(x + halfWidthX, layer, z + halfWidthZ);
            };
            // Safety layer one: repair the cells immediately beneath the live
            // collision footprint, even with no movement key held. This is the
            // path that catches residual sprint/jump inertia and vertical jumps.
            if (!requested.scaffoldSameLayerOnly ||
                supportLayer == m_scaffoldPlatformY) {
                addFootprintAt(centerX, supportLayer, centerZ);
            }
            if (supportLayer != m_scaffoldPlatformY)
                addFootprintAt(centerX, m_scaffoldPlatformY, centerZ);

            double simulatedX = centerX;
            double simulatedY = minY;
            double simulatedZ = centerZ;
            double simulatedMotionX = actualMotionX;
            double simulatedMotionY = actualMotionY;
            double simulatedMotionZ = actualMotionZ;
            if (std::hypot(simulatedMotionX, simulatedMotionZ) < 0.012 &&
                magnitude > 0.001) {
                simulatedMotionX = directionX * 0.10;
                simulatedMotionZ = directionZ * 0.10;
            }
            for (int predictionTick = 0; predictionTick < 6; ++predictionTick) {
                simulatedX += simulatedMotionX;
                simulatedY += simulatedMotionY;
                simulatedZ += simulatedMotionZ;
                // Safety layer two: predict from actual motion, then add input
                // acceleration only as a secondary correction.
                addFootprintAt(simulatedX, m_scaffoldPlatformY, simulatedZ);

                // 1.8.x EntityLivingBase air motion approximation. Input is a
                // small acceleration/fallback; existing inertia remains the
                // dominant signal and therefore also covers jump momentum.
                if (magnitude > 0.001) {
                    simulatedMotionX += directionX * 0.012;
                    simulatedMotionZ += directionZ * 0.012;
                }
                simulatedMotionX *= 0.91;
                simulatedMotionZ *= 0.91;
                simulatedMotionY = (simulatedMotionY - 0.08) * 0.98;
                if (simulatedY <= static_cast<double>(m_scaffoldPlatformY) +
                                  1.02 && predictionTick >= 1) break;
            }
            constexpr std::array<std::array<int, 4U>, 5U> neighbours{{
                {{0,-1,0,1}}, {{0,0,-1,3}}, {{0,0,1,2}},
                {{-1,0,0,5}}, {{1,0,0,4}}}};
            bool placed = false;
            for (std::size_t targetIndex = 0U; targetIndex < targetCount; ++targetIndex) {
                const auto& target = targets[targetIndex];
                jobject targetPos = env->NewObject(cache->blockPosClass,
                    cache->blockPosConstructor, target[0U], target[1U], target[2U]);
                if (targetPos == nullptr || env->ExceptionCheck() == JNI_TRUE) return fail();
                if (env->CallBooleanMethod(world, cache->isAirBlock, targetPos) != JNI_TRUE) {
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    continue;
                }
                for (const auto& side : neighbours) {
                    jobject neighbour = env->NewObject(cache->blockPosClass,
                        cache->blockPosConstructor, target[0U] + side[0U],
                        target[1U] + side[1U], target[2U] + side[2U]);
                    if (neighbour == nullptr || env->ExceptionCheck() == JNI_TRUE) return fail();
                    const jboolean neighbourAir = env->CallBooleanMethod(
                        world, cache->isAirBlock, neighbour);
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (neighbourAir == JNI_TRUE) continue;
                    jobject face = env->CallStaticObjectMethod(cache->enumFacingClass,
                        cache->getFacingByIndex, side[3U]);
                    jobject hit = env->NewObject(cache->vec3Class,
                        cache->vec3Constructor, target[0U] + 0.5,
                        target[1U] + 0.5, target[2U] + 0.5);
                    if (face == nullptr || hit == nullptr ||
                        env->ExceptionCheck() == JNI_TRUE) return fail();
                    // Borrow the slot only for this placement attempt, even
                    // if placement is rejected or throws a JNI exception.
                    const jint previousSlot=env->GetIntField(inventory,cache->currentItem);
                    if(env->ExceptionCheck()) return fail();
                    env->SetIntField(inventory, cache->currentItem, selectedSlot);
                    if(env->ExceptionCheck()) return fail();
                    // Make the server-visible sequence explicit: held block,
                    // placement, original held slot. Some clients override the
                    // right-click method and do not perform vanilla's sync.
                    env->CallVoidMethod(controller,cache->syncCurrentPlayItem);
                    const jboolean accepted = env->ExceptionCheck() ? JNI_FALSE : env->CallBooleanMethod(controller,
                        cache->onPlayerRightClick, player, world, selectedStack,
                        neighbour, face, hit);
                    const bool placementFailed=env->ExceptionCheck()==JNI_TRUE;
                    clearException(env);
                    env->SetIntField(inventory,cache->currentItem,previousSlot);
                    if(!env->ExceptionCheck())
                        env->CallVoidMethod(controller,cache->syncCurrentPlayItem);
                    if(placementFailed||env->ExceptionCheck()) return fail();
                    if (accepted == JNI_TRUE) {
                        m_lastScaffoldPlacementTick = tickMilliseconds;
                        placed = true;
                        break;
                    }
                }
                if (placed) break;
            }
        }
    } else if (!requested.scaffold) {
        m_scaffoldPlatformYValid = false;
    }

    m_lastGameplayTick = tickMilliseconds;
    return finish(m_safewalkSneakForced || requested.scaffold || requested.fly ||
                  requested.bhop || requested.aimAssist || requested.longJump ||
                  localMobAuraRequested || localVelocityRequested ||
                  smartHotbarRequested);
}

void GameBindings::enqueueDebugChatLine(const std::string_view line) noexcept
{
    if (line.empty()) return;
    std::array<char, DebugLineCapacity> copy{};
    std::size_t length = 0U;
    for (const char character : line) {
        if (length + 1U >= copy.size()) break;
        if (character == '\r' || character == '\n' || character == '\t') continue;
        copy[length++] = character;
    }
    if (length == 0U) return;

    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    std::uint32_t target = 0U;
    if (m_debugQueueCount < DebugQueueCapacity) {
        target = (m_debugQueueHead + m_debugQueueCount) % DebugQueueCapacity;
        ++m_debugQueueCount;
    } else {
        target = m_debugQueueHead;
        m_debugQueueHead = (m_debugQueueHead + 1U) % DebugQueueCapacity;
    }
    m_debugQueue[target] = copy;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
}

void GameBindings::enqueueWarningChatLine(
    const std::string_view playerName,
    const std::string_view reason) noexcept
{
    if (playerName.empty()) return;
    WarningChatLine line{};
    const auto copyClean = [](const std::string_view source, char* const target,
                              const std::size_t capacity) noexcept {
        std::size_t length = 0U;
        for (const char character : source) {
            if (length + 1U >= capacity) break;
            if (character == '\r' || character == '\n' || character == '\t' ||
                character == '\0') continue;
            target[length++] = character;
        }
    };
    copyClean(playerName, line.playerName.data(), line.playerName.size());
    copyClean(reason, line.reason.data(), line.reason.size());
    if (line.playerName[0U] == '\0') return;

    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    std::uint32_t target = 0U;
    if (m_warningQueueCount < WarningQueueCapacity) {
        target = (m_warningQueueHead + m_warningQueueCount) % WarningQueueCapacity;
        ++m_warningQueueCount;
    } else {
        target = m_warningQueueHead;
        m_warningQueueHead = (m_warningQueueHead + 1U) % WarningQueueCapacity;
    }
    m_warningQueue[target] = line;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
}

void GameBindings::publishDebugChat(JNIEnv* const env, const bool enabled) noexcept
{
    std::uint32_t warningCount = 0U;
    ::AcquireSRWLockShared(&m_warningQueueLock);
    warningCount = m_warningQueueCount;
    ::ReleaseSRWLockShared(&m_warningQueueLock);
    if (!enabled) {
        // Re-enabling the option should print the current state even if the
        // roster itself did not change while the option was disabled.
        m_debugRosterGeneration = 0U;
        m_debugBedOwnershipGeneration = 0U;
        m_debugMatchProbeGeneration = 0U;
        ::AcquireSRWLockExclusive(&m_debugQueueLock);
        m_debugQueueHead = 0U;
        m_debugQueueCount = 0U;
        ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    }
    std::uint32_t pendingCount = 0U;
    ::AcquireSRWLockShared(&m_debugQueueLock);
    pendingCount = m_debugQueueCount;
    ::ReleaseSRWLockShared(&m_debugQueueLock);
    const bool probeChanged = enabled && m_matchProbeGeneration !=
        m_debugMatchProbeGeneration;
    const bool rosterChanged = enabled && m_snapshot.matchActive &&
        m_snapshot.playerRosterGeneration != 0U &&
        m_snapshot.playerRosterGeneration != m_debugRosterGeneration;
    const bool bedChanged = enabled && m_snapshot.matchActive &&
        m_bedOwnershipGeneration !=
        m_debugBedOwnershipGeneration;
    if (env == nullptr ||
        (!probeChanged && !rosterChanged && !bedChanged && pendingCount == 0U &&
         warningCount == 0U && !m_logicalController.debug().hasChat()) ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
        m_cache == nullptr) {
        return;
    }

    BindingCache* const cache = m_cache.get();
    if (cache->chatTextClass == nullptr || cache->chatTextConstructor == nullptr ||
        cache->addChatMessage == nullptr || env->PushLocalFrame(192) != JNI_OK) {
        clearException(env);
        return;
    }

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
    jobject player = minecraft == nullptr ? nullptr :
        env->GetObjectField(minecraft, cache->playerField);
    if (env->ExceptionCheck() == JNI_TRUE || player == nullptr) {
        clearException(env);
        env->PopLocalFrame(nullptr);
        return;
    }

    // Consume generations only after a usable local chat endpoint and player
    // object exist. This avoids silently discarding the exact diagnostics that
    // are needed when a transformed client temporarily exposes an incomplete
    // game state.
    for(int i=0;i<8;++i) {
        std::string text;
        if(!m_logicalController.debug().popChat(text)) break;
        text="\xC2\xA7" "b"+text;
        jstring string=env->NewStringUTF(text.c_str());
        jobject component=string ? env->NewObject(cache->chatTextClass,cache->chatTextConstructor,string) : nullptr;
        if(component && !env->ExceptionCheck()) env->CallVoidMethod(player,cache->addChatMessage,component);
        if(component) env->DeleteLocalRef(component);
        if(string) env->DeleteLocalRef(string);
        clearException(env);
    }
    if (enabled) {
        m_debugMatchProbeGeneration = m_matchProbeGeneration;
        m_debugRosterGeneration = m_snapshot.playerRosterGeneration;
        m_debugBedOwnershipGeneration = m_bedOwnershipGeneration;
    }
    std::array<std::array<char, DebugLineCapacity>, DebugQueueCapacity> queued{};
    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    const std::uint32_t queuedCount = m_debugQueueCount;
    for (std::uint32_t index = 0U; index < queuedCount; ++index) {
        queued[index] = m_debugQueue[
            (m_debugQueueHead + index) % DebugQueueCapacity];
    }
    m_debugQueueHead = 0U;
    m_debugQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    std::array<WarningChatLine, WarningQueueCapacity> warnings{};
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    const std::uint32_t queuedWarningCount = m_warningQueueCount;
    for (std::uint32_t index = 0U; index < queuedWarningCount; ++index) {
        warnings[index] = m_warningQueue[
            (m_warningQueueHead + index) % WarningQueueCapacity];
    }
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);

    // Each letter is deliberately assigned a different legacy chat color.
    // This component is added straight to EntityPlayerSP and never reaches a
    // network handler, so the message remains visible only to this client.
    constexpr std::string_view prefix{
        "[\xC2\xA7" "bD\xC2\xA7" "de\xC2\xA7" "ab\xC2\xA7" "eu\xC2\xA7" "6g\xC2\xA7" "r] "};
    auto addLine = [&](const std::string& body) noexcept {
        const std::string line = std::string(prefix) + body;
        jstring text = env->NewStringUTF(line.c_str());
        jobject component = text == nullptr ? nullptr :
            env->NewObject(cache->chatTextClass, cache->chatTextConstructor, text);
        if (component != nullptr && env->ExceptionCheck() != JNI_TRUE) {
            env->CallVoidMethod(player, cache->addChatMessage, component);
        }
        clearException(env);
    };
    const auto jsonEscape = [](const char* const source) {
        std::string escaped;
        if (source == nullptr) return escaped;
        escaped.reserve(std::strlen(source) + 8U);
        for (const unsigned char character : std::string_view(source)) {
            switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character >= 0x20U) escaped.push_back(
                    static_cast<char>(character));
                break;
            }
        }
        return escaped;
    };
    auto addWarning = [&](const WarningChatLine& warning) noexcept {
        const std::string name = jsonEscape(warning.playerName.data());
        const std::string reason = jsonEscape(warning.reason.data());
        if (cache->chatSerializerClass != nullptr &&
            cache->parseChatJson != nullptr) {
            const std::string command = "/wdr " + name;
            const std::string json =
                "{\"text\":\"\",\"clickEvent\":{\"action\":\"suggest_command\","
                "\"value\":\"" + command + "\"},\"extra\":["
                "{\"text\":\"[WARNING]\",\"color\":\"red\",\"bold\":true},"
                "{\"text\":\" Blacklisted player \"},"
                "{\"text\":\"" + name + "\",\"color\":\"gold\",\"bold\":true},"
                "{\"text\":\" — " + reason +
                " (click to prepare /wdr)\",\"color\":\"yellow\"}]}";
            jstring text = env->NewStringUTF(json.c_str());
            jobject component = text == nullptr ? nullptr :
                env->CallStaticObjectMethod(cache->chatSerializerClass,
                                            cache->parseChatJson, text);
            if (component != nullptr && env->ExceptionCheck() != JNI_TRUE) {
                env->CallVoidMethod(player, cache->addChatMessage, component);
                clearException(env);
                return;
            }
            clearException(env);
        }
        std::string fallback =
            "\xC2\xA7" "c\xC2\xA7" "l[WARNING]\xC2\xA7" "r Blacklisted player ";
        fallback += warning.playerName.data();
        fallback += " - ";
        fallback += warning.reason.data();
        fallback += " \xC2\xA7" "e(/wdr ";
        fallback += warning.playerName.data();
        fallback += ')';
        jstring text = env->NewStringUTF(fallback.c_str());
        jobject component = text == nullptr ? nullptr :
            env->NewObject(cache->chatTextClass, cache->chatTextConstructor, text);
        if (component != nullptr && env->ExceptionCheck() != JNI_TRUE)
            env->CallVoidMethod(player, cache->addChatMessage, component);
        clearException(env);
    };

    if (probeChanged) {
        const auto teamText = [](const char team) noexcept {
            return team == 'u' ? std::string("unknown") : std::string(1U, team);
        };
        std::string body = "match_probe active=";
        body += m_snapshot.matchActive ? "true" : "false";
        body += " sidebar=" + std::to_string(m_matchProbe.sidebarAvailable ? 1 : 0);
        body += " tab=" + std::to_string(m_matchProbe.tabAvailable ? 1 : 0);
        body += " lines=" + std::to_string(m_matchProbe.sidebarLines);
        body += " teams=" + std::to_string(m_matchProbe.sidebarTeams);
        body += " you=" + std::to_string(m_matchProbe.sidebarYouRows);
        body += " roster_tags=" + std::to_string(m_matchProbe.rosterTaggedPlayers);
        body += " roster_players=" + std::to_string(m_matchProbe.rosterPlayers);
        body += " roster_teams=" + std::to_string(m_matchProbe.rosterTeams);
        body += " roster_own=" + teamText(m_matchProbe.rosterOwnTeam);
        body += " armor_own=" + teamText(m_matchProbe.localArmorTeam);
        body += " armor_teams=" + std::to_string(m_matchProbe.armorTeams);
        body += " evidence=";
        if (m_matchProbe.sidebarEvidence) body += 'S';
        if (m_matchProbe.rosterEvidence) body += 'R';
        if (m_matchProbe.armorEvidence) body += 'A';
        if (!m_matchProbe.sidebarEvidence && !m_matchProbe.rosterEvidence &&
            !m_matchProbe.armorEvidence) body += '-';
        body += " stable=" + std::to_string(m_matchProbe.stableCount);
        body += " beds=" + std::to_string(m_snapshot.bedMarkerCount);
        addLine(body);
    }

    if (rosterChanged) {
        std::string matchLine = "game_started=true own_team=";
        if (m_snapshot.ownTeam == 'u') {
            matchLine += "unknown";
        } else {
            matchLine += "\xC2\xA7";
            matchLine.push_back(m_snapshot.ownTeam);
            matchLine.push_back(m_snapshot.ownTeam);
            matchLine += "\xC2\xA7r";
        }
        addLine(matchLine);

        for (std::uint32_t index = 0U; index < m_snapshot.playerCount; ++index) {
            const PlayerIdentity& identity = m_snapshot.players[index];
            if (identity.name[0U] == '\0') continue;
            const bool teammate = m_snapshot.ownTeam != 'u' &&
                identity.teamColor == m_snapshot.ownTeam;
            std::string body = "player=";
            if (identity.teamColor != 'u') {
                body += "\xC2\xA7";
                body.push_back(identity.teamColor);
            }
            body += identity.name.data();
            body += "\xC2\xA7r team=";
            if (identity.teamColor == 'u') {
                body += "unknown";
            } else {
                body += "\xC2\xA7";
                body.push_back(identity.teamColor);
                body.push_back(identity.teamColor);
                body += "\xC2\xA7r";
            }
            body += " teammate=";
            body += teammate ? "true" : "false";
            addLine(body);
        }
    }
    if (bedChanged) {
        std::string body = "own_bed=";
        if (!m_snapshot.ownBedKnown) {
            body += "unknown";
        } else {
            body += "true pos=" + std::to_string(m_snapshot.ownBedX) + "," +
                std::to_string(m_snapshot.ownBedY) + "," +
                std::to_string(m_snapshot.ownBedZ) + " source=";
            body += m_snapshot.ownBedSource == GameSnapshot::OwnBedSource::TeamWool
                ? "team_wool" : "match_spawn";
        }
        addLine(body);
    }
    for (std::uint32_t index = 0U; index < queuedCount; ++index) {
        if (queued[index][0U] != '\0') addLine(queued[index].data());
    }
    for (std::uint32_t index = 0U; index < queuedWarningCount; ++index) {
        if (warnings[index].playerName[0U] != '\0') addWarning(warnings[index]);
    }
    env->PopLocalFrame(nullptr);
}

void GameBindings::sampleCamera(JNIEnv* const env) noexcept
{
    m_snapshot.camera.valid = false;
    if (env == nullptr ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
        m_cache == nullptr) {
        return;
    }
    BindingCache* const cache = m_cache.get();
    if (cache->renderManagerObject == nullptr || cache->timerObject == nullptr ||
        cache->modelViewBuffer == nullptr ||
        cache->projectionBuffer == nullptr || cache->viewportBuffer == nullptr) {
        return;
    }

    WorldCameraSnapshot camera{};
    camera.renderX = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[0U]);
    camera.renderY = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[1U]);
    camera.renderZ = env->GetDoubleField(cache->renderManagerObject,
                                         cache->renderPosition[2U]);
    camera.partialTicks = env->GetFloatField(cache->timerObject,
                                             cache->renderPartialTicks);
    if (env->ExceptionCheck() == JNI_TRUE) {
        clearException(env);
        return;
    }

    const auto* const modelView = static_cast<const float*>(
        env->GetDirectBufferAddress(cache->modelViewBuffer));
    const auto* const projection = static_cast<const float*>(
        env->GetDirectBufferAddress(cache->projectionBuffer));
    const auto* const viewport = static_cast<const jint*>(
        env->GetDirectBufferAddress(cache->viewportBuffer));
    if (modelView == nullptr || projection == nullptr || viewport == nullptr ||
        env->GetDirectBufferCapacity(cache->modelViewBuffer) < 16 ||
        env->GetDirectBufferCapacity(cache->projectionBuffer) < 16 ||
        env->GetDirectBufferCapacity(cache->viewportBuffer) < 4) {
        clearException(env);
        return;
    }
    std::copy_n(modelView, camera.modelView.size(), camera.modelView.begin());
    std::copy_n(projection, camera.projection.size(), camera.projection.begin());
    for (std::size_t index = 0U; index < camera.viewport.size(); ++index) {
        camera.viewport[index] = viewport[index];
    }

    const bool finiteMatrices = std::all_of(
        camera.modelView.begin(), camera.modelView.end(),
        [](const float value) noexcept { return std::isfinite(value); }) &&
        std::all_of(camera.projection.begin(), camera.projection.end(),
        [](const float value) noexcept { return std::isfinite(value); });
    const bool saneViewport = camera.viewport[2U] >= 1 && camera.viewport[2U] <= 32768 &&
                              camera.viewport[3U] >= 1 && camera.viewport[3U] <= 32768;
    const bool nonEmptyMatrices = std::abs(camera.modelView[15U]) > 0.000001F &&
                                  std::abs(camera.projection[0U]) > 0.000001F &&
                                  std::abs(camera.projection[5U]) > 0.000001F;
    camera.valid = finiteMatrices && saneViewport && nonEmptyMatrices &&
                   std::isfinite(camera.renderX) && std::isfinite(camera.renderY) &&
                   std::isfinite(camera.renderZ) && std::isfinite(camera.partialTicks) &&
                   camera.partialTicks >= 0.0F && camera.partialTicks <= 1.5F;
    m_snapshot.camera = camera;
}

void GameBindings::runBedScanner(JNIEnv* const env, HANDLE const stopEvent) noexcept
{
    if (env == nullptr || stopEvent == nullptr) return;

    using ChunkBeds = std::unordered_map<std::uint64_t, std::vector<BedMarker>>;
    struct DefenseSample final {
        int x = 0;
        int y = 0;
        int z = 0;
        std::uint16_t blockId = 0U;
        std::uint8_t metadata = 0U;
    };
    using ChunkDefense = std::unordered_map<std::uint64_t, std::vector<DefenseSample>>;
    ChunkBeds bedsByChunk;
    ChunkDefense defenseByChunk;
    std::unordered_set<std::uint64_t> processedChunks;
    std::unordered_set<std::uint64_t> currentChunks;
    jobject worldIdentity = nullptr;
    std::uint64_t generation = 0U;
    std::uint64_t lastVerification = 0U;
    std::uint64_t lastDefenseRefresh = 0U;
    const HANDLE waitHandles[2]{stopEvent, m_bedRescanEvent};
    const DWORD waitHandleCount = m_bedRescanEvent != nullptr ? 2U : 1U;
    const auto stopRequested = [&](const DWORD timeout) noexcept {
        const DWORD wait = ::WaitForMultipleObjects(
            waitHandleCount, waitHandles, FALSE, timeout);
        return wait == WAIT_OBJECT_0 || wait == WAIT_FAILED;
    };

    const auto chunkKey = [](const jint x, const jint z) noexcept {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
               static_cast<std::uint32_t>(z);
    };
    const auto publish = [&]() noexcept {
        PublishedBedCache next{};
        next.loadedChunkCount = static_cast<std::uint32_t>(
            std::min<std::size_t>(processedChunks.size(), UINT32_MAX));
        next.generation = ++generation;
        for (const auto& [key, markers] : bedsByChunk) {
            (void)key;
            for (const BedMarker& marker : markers) {
                if (next.markerCount >= next.markers.size()) break;
                BedMarker enriched = marker;
                std::array<unsigned, 8U> teamWoolEvidence{};
                // The bulk-copied sparse material cache is folded into exact
                // distance rings here on the scanner thread. SwapBuffers only
                // sums at most ten small counters when drawing the panel.
                for (const auto& [defenseKey, samples] : defenseByChunk) {
                    const auto chunkX = static_cast<std::int32_t>(defenseKey >> 32U);
                    const auto chunkZ = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(defenseKey));
                    const auto floorChunk = [](const int coordinate) noexcept {
                        return coordinate >= 0 ? coordinate / 16 : (coordinate - 15) / 16;
                    };
                    const int headChunkX = floorChunk(marker.x);
                    const int headChunkZ = floorChunk(marker.z);
                    const int footChunkX = floorChunk(marker.footX);
                    const int footChunkZ = floorChunk(marker.footZ);
                    const bool nearHead = std::abs(chunkX - headChunkX) <= 1 &&
                                          std::abs(chunkZ - headChunkZ) <= 1;
                    const bool nearFoot = std::abs(chunkX - footChunkX) <= 1 &&
                                          std::abs(chunkZ - footChunkZ) <= 1;
                    if (!nearHead && !nearFoot) continue;

                    for (const DefenseSample& sample : samples) {
                        const int vertical = sample.y - marker.y;
                        if (vertical < 0 || vertical > 10) continue;
                        const int dx = std::min(std::abs(sample.x - marker.x),
                                                std::abs(sample.x - marker.footX));
                        const int dz = std::min(std::abs(sample.z - marker.z),
                                                std::abs(sample.z - marker.footZ));
                        const int ring = std::max({dx, dz, vertical});
                        if (ring < 1 || ring > 10) continue;

                        if (sample.blockId == 35U && ring <= 6) {
                            const bedwars::Team woolTeam =
                                bedwars::fromWoolMetadata(sample.metadata);
                            const std::uint8_t woolIndex = bedwars::teamIndex(woolTeam);
                            if (woolIndex < teamWoolEvidence.size()) {
                                // Nearby wool is stronger evidence than map
                                // decoration near the edge of the scan radius.
                                teamWoolEvidence[woolIndex] +=
                                    static_cast<unsigned>(7 - ring);
                            }
                        }

                        std::uint8_t normalizedMeta = sample.metadata;
                        if (sample.blockId == 17U || sample.blockId == 162U) {
                            normalizedMeta &= 0x3U;
                        }

                        BedDefenseBlock* summary = nullptr;
                        for (std::size_t index = 0U; index < enriched.defenseCount; ++index) {
                            BedDefenseBlock& candidate = enriched.defense[index];
                            if (candidate.blockId == sample.blockId &&
                                candidate.metadata == normalizedMeta) {
                                summary = &candidate;
                                break;
                            }
                        }
                        if (summary == nullptr &&
                            enriched.defenseCount < enriched.defense.size()) {
                            summary = &enriched.defense[enriched.defenseCount++];
                            summary->blockId = sample.blockId;
                            summary->metadata = normalizedMeta;
                        }
                        if (summary != nullptr) {
                            std::uint16_t& count = summary->ringCounts[
                                static_cast<std::size_t>(ring)];
                            if (count != std::numeric_limits<std::uint16_t>::max()) ++count;
                        }
                    }
                }
                unsigned bestEvidence = 0U;
                unsigned secondEvidence = 0U;
                std::uint8_t bestTeam = 0xFFU;
                for (std::uint8_t index = 0U; index < teamWoolEvidence.size(); ++index) {
                    const unsigned evidence = teamWoolEvidence[index];
                    if (evidence > bestEvidence) {
                        secondEvidence = bestEvidence;
                        bestEvidence = evidence;
                        bestTeam = index;
                    } else if (evidence > secondEvidence) {
                        secondEvidence = evidence;
                    }
                }
                // Fail closed on weak or mixed-colour evidence. This removes
                // the previous "nearest bed" false ownership assignment.
                if (bestTeam < 8U && bestEvidence >= 4U &&
                    bestEvidence >= secondEvidence + 2U) {
                    enriched.teamColor = bedwars::formatCode(
                        static_cast<bedwars::Team>(bestTeam + 1U));
                }
                next.markers[next.markerCount++] = enriched;
            }
            if (next.markerCount >= next.markers.size()) break;
        }
        ::AcquireSRWLockExclusive(&m_bedCacheLock);
        m_publishedBedCache = next;
        ::ReleaseSRWLockExclusive(&m_bedCacheLock);
    };
    const auto clearWorld = [&]() noexcept {
        processedChunks.clear();
        currentChunks.clear();
        bedsByChunk.clear();
        defenseByChunk.clear();
        publish();
    };

    while (::WaitForSingleObject(stopEvent, 0U) == WAIT_TIMEOUT) {
        if (m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
            m_cache == nullptr) {
            if (stopRequested(100U)) break;
            continue;
        }
        BindingCache* const cache = m_cache.get();
        // Only this scanner thread owns processedChunks/bedsByChunk. The UI and
        // IPC threads publish one atomic bit, so a manual refresh cannot race
        // vector/hash-table mutation or issue JNI calls from the wrong thread.
        if (m_bedRescanRequested.exchange(false, std::memory_order_acq_rel)) {
            processedChunks.clear();
        }
        if (env->PushLocalFrame(96) < 0) {
            clearException(env);
            if (stopRequested(500U)) break;
            continue;
        }

        bool cycleValid = true;
        jobject minecraft = cache->minecraftInstanceField != nullptr
            ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
            : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
        if (env->ExceptionCheck() == JNI_TRUE || minecraft == nullptr) cycleValid = false;
        jboolean singlePlayer = JNI_FALSE;
        jobject world = nullptr;
        if (cycleValid) {
            singlePlayer = JNI_TRUE;
            if (env->ExceptionCheck() == JNI_TRUE) cycleValid = false;
        }
        if (cycleValid) {
            world = env->GetObjectField(minecraft, cache->worldField);
            if (env->ExceptionCheck() == JNI_TRUE) cycleValid = false;
        }

        if (!cycleValid || singlePlayer != JNI_TRUE || world == nullptr) {
            clearException(env);
            if (worldIdentity != nullptr) {
                env->DeleteGlobalRef(worldIdentity);
                worldIdentity = nullptr;
            }
            if (!processedChunks.empty() || !bedsByChunk.empty()) clearWorld();
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }

        if (worldIdentity == nullptr || env->IsSameObject(worldIdentity, world) != JNI_TRUE) {
            if (worldIdentity != nullptr) env->DeleteGlobalRef(worldIdentity);
            worldIdentity = env->NewGlobalRef(world);
            clearWorld();
        }

        jobject provider = env->CallObjectMethod(world, cache->getChunkProvider);
        if (env->ExceptionCheck() == JNI_TRUE || provider == nullptr ||
            env->IsInstanceOf(provider, cache->chunkProviderClass) != JNI_TRUE) {
            clearException(env);
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }
        jobject listing = env->GetObjectField(provider, cache->chunkListingField);
        jobjectArray chunks = listing == nullptr ? nullptr : static_cast<jobjectArray>(
            env->CallObjectMethod(listing, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE || chunks == nullptr) {
            clearException(env);
            env->PopLocalFrame(nullptr);
            if (stopRequested(500U)) break;
            continue;
        }

        currentChunks.clear();
        const jsize chunkCount = std::min<jsize>(env->GetArrayLength(chunks), 2048);
        for (jsize chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
            jobject chunk = env->GetObjectArrayElement(chunks, chunkIndex);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env);
                continue;
            }
            if (chunk == nullptr || env->IsInstanceOf(chunk, cache->chunkClass) != JNI_TRUE) {
                if (chunk != nullptr) env->DeleteLocalRef(chunk);
                continue;
            }
            const jint chunkX = env->GetIntField(chunk, cache->chunkX);
            const jint chunkZ = env->GetIntField(chunk, cache->chunkZ);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env);
                env->DeleteLocalRef(chunk);
                continue;
            }
            const std::uint64_t key = chunkKey(chunkX, chunkZ);
            currentChunks.insert(key);
            if (!processedChunks.insert(key).second) {
                env->DeleteLocalRef(chunk);
                continue;
            }

            std::vector<BedMarker> discovered;
            std::vector<DefenseSample> defenseSamples;
            jobjectArray sections = static_cast<jobjectArray>(
                env->CallObjectMethod(chunk, cache->getStorageArrays));
            if (env->ExceptionCheck() == JNI_TRUE || sections == nullptr) {
                clearException(env);
                processedChunks.erase(key); // transient read: retry next cycle
                env->DeleteLocalRef(chunk);
                continue;
            }
            const jsize sectionCount = std::min<jsize>(env->GetArrayLength(sections), 16);
            std::array<jchar, 4096U> states{};
            bool sectionReadFailed = false;
            for (jsize sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
                jobject section = env->GetObjectArrayElement(sections, sectionIndex);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    sectionReadFailed = true;
                    continue;
                }
                if (section == nullptr) continue;
                jcharArray data = static_cast<jcharArray>(
                    env->CallObjectMethod(section, cache->getStorageData));
                if (env->ExceptionCheck() == JNI_TRUE || data == nullptr ||
                    env->GetArrayLength(data) != static_cast<jsize>(states.size())) {
                    clearException(env);
                    sectionReadFailed = true;
                    if (data != nullptr) env->DeleteLocalRef(data);
                    env->DeleteLocalRef(section);
                    continue;
                }
                // 1.8.9 stores the global block-state ID in char[4096]. A
                // single bulk copy replaces 4096 getBlockState JNI calls.
                env->GetCharArrayRegion(data, 0, static_cast<jsize>(states.size()), states.data());
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    sectionReadFailed = true;
                    env->DeleteLocalRef(data);
                    env->DeleteLocalRef(section);
                    continue;
                }
                for (std::size_t index = 0U; index < states.size(); ++index) {
                    const unsigned encoded = static_cast<unsigned>(states[index]);
                    const unsigned blockId = encoded >> 4U;
                    // Bed Wars defenses use a small, stable material palette.
                    // Retaining only those sparse samples avoids a 128 KiB
                    // dense copy per loaded chunk while still showing the
                    // blocks players actually place around a bed.
                    const bool defenseMaterial = blockId == 1U || blockId == 4U ||
                        blockId == 5U || blockId == 17U || blockId == 20U ||
                        blockId == 24U || blockId == 35U || blockId == 45U ||
                        blockId == 49U || blockId == 95U || blockId == 121U ||
                        blockId == 159U;
                    if (defenseMaterial) {
                        const int localX = static_cast<int>(index & 15U);
                        const int localZ = static_cast<int>((index >> 4U) & 15U);
                        const int localY = static_cast<int>((index >> 8U) & 15U);
                        defenseSamples.push_back(DefenseSample{
                            chunkX * 16 + localX,
                            static_cast<int>(sectionIndex) * 16 + localY,
                            chunkZ * 16 + localZ,
                            static_cast<std::uint16_t>(blockId),
                            static_cast<std::uint8_t>(encoded & 0xFU)});
                    }
                    if ((encoded >> 4U) != 26U || (encoded & 0x8U) == 0U) continue;
                    const int localX = static_cast<int>(index & 15U);
                    const int localZ = static_cast<int>((index >> 4U) & 15U);
                    const int localY = static_cast<int>((index >> 8U) & 15U);
                    const int headX = chunkX * 16 + localX;
                    const int headZ = chunkZ * 16 + localZ;
                    int footX = headX;
                    int footZ = headZ;
                    // BlockBed stores horizontal facing in metadata bits 0-1.
                    // getHorizontal maps 0=SOUTH, 1=WEST, 2=NORTH, 3=EAST;
                    // the scanned HEAD is one block along that facing from
                    // the FOOT, so walk in the opposite direction here.
                    switch (encoded & 0x3U) {
                    case 0U: --footZ; break;
                    case 1U: ++footX; break;
                    case 2U: ++footZ; break;
                    case 3U: --footX; break;
                    default: break;
                    }
                    discovered.push_back(BedMarker{
                        headX,
                        static_cast<int>(sectionIndex) * 16 + localY,
                        headZ,
                        footX,
                        footZ});
                }
                env->DeleteLocalRef(data);
                env->DeleteLocalRef(section);
            }
            bedsByChunk[key] = std::move(discovered);
            defenseByChunk[key] = std::move(defenseSamples);
            if (sectionReadFailed) {
                // Concurrent chunk mutation can invalidate one local section
                // read. Publish any safe partial result now, but remove the
                // processed marker so the complete chunk is retried in 500 ms.
                processedChunks.erase(key);
            }
            env->DeleteLocalRef(sections);
            env->DeleteLocalRef(chunk);
        }

        for (auto it = processedChunks.begin(); it != processedChunks.end();) {
            if (!currentChunks.contains(*it)) {
                bedsByChunk.erase(*it);
                defenseByChunk.erase(*it);
                it = processedChunks.erase(it);
            } else {
                ++it;
            }
        }

        const std::uint64_t now = static_cast<std::uint64_t>(::GetTickCount64());
        if (now - lastVerification >= 100U) {
            lastVerification = now;
            for (auto& [key, markers] : bedsByChunk) {
                (void)key;
                for (auto marker = markers.begin(); marker != markers.end();) {
                    jobject position = env->NewObject(cache->blockPosClass,
                                                      cache->blockPosConstructor,
                                                      marker->x, marker->y, marker->z);
                    jobject state = position == nullptr ? nullptr :
                        env->CallObjectMethod(world, cache->getBlockState, position);
                    jobject block = state == nullptr ? nullptr :
                        env->CallObjectMethod(state, cache->getBlock);
                    const bool failed = env->ExceptionCheck() == JNI_TRUE;
                    if (failed) clearException(env);
                    const bool stillBed = !failed && block != nullptr &&
                        env->IsInstanceOf(block, cache->bedClass) == JNI_TRUE;
                    if (block != nullptr) env->DeleteLocalRef(block);
                    if (state != nullptr) env->DeleteLocalRef(state);
                    if (position != nullptr) env->DeleteLocalRef(position);
                    if (!failed && !stillBed) marker = markers.erase(marker);
                    else ++marker;
                }
            }
        }

        if (now - lastDefenseRefresh >= 750U) {
            lastDefenseRefresh = now;
            const auto floorChunk = [](const int coordinate) noexcept {
                return coordinate >= 0 ? coordinate / 16 : (coordinate - 15) / 16;
            };
            // Chunk diffing remains the discovery mechanism for arbitrary
            // world data. Only the bounded 3x3 neighbourhood around an
            // already-known bed is invalidated here, so placed/broken defense
            // blocks refresh automatically without rescanning every loaded
            // chunk or issuing per-block JNI calls.
            for (const auto& [bedChunk, markers] : bedsByChunk) {
                (void)bedChunk;
                for (const BedMarker& marker : markers) {
                    const int centerChunkX = floorChunk(marker.x);
                    const int centerChunkZ = floorChunk(marker.z);
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            processedChunks.erase(chunkKey(
                                static_cast<jint>(centerChunkX + dx),
                                static_cast<jint>(centerChunkZ + dz)));
                        }
                    }
                }
            }
        }

        publish();
        env->PopLocalFrame(nullptr);
        if (stopRequested(100U)) break;
    }

    if (worldIdentity != nullptr) env->DeleteGlobalRef(worldIdentity);
    clearException(env);
}

void GameBindings::requestBedRescan() noexcept
{
    m_bedRescanRequested.store(true, std::memory_order_release);
    if (m_bedRescanEvent != nullptr) {
        ::SetEvent(m_bedRescanEvent);
    }
}

bool GameBindings::silentAvailable() const noexcept
{
    const auto* c=m_cache.get();
    const bool packetReady = m_silentRotationHook.ready() && c &&
        c->networkPacketClass && c->movementPacketClass &&
        c->positionPacketClass && c->lookPacketClass &&
        c->positionLookPacketClass && c->addToSendQueue &&
        c->movementPacketConstructor && c->positionPacketConstructor &&
        c->lookPacketConstructor &&
        c->positionLookPacketConstructor && c->packetYaw && c->packetPitch &&
        c->packetOnGround &&
        std::none_of(c->packetPosition.begin(),c->packetPosition.end(),
                     [](jfieldID value){ return value==nullptr; });
    // This public capability describes the feature's defining promise: can
    // logical yaw/pitch reach the outgoing C03/C05/C06 stream while the camera
    // stays untouched? Movement compensation and click redirection have their
    // own optional hooks and must never suppress target acquisition or packet
    // rotation on a transformed client.
    return packetReady;
}

bool GameBindings::silentAttackAvailable() const noexcept
{
    const auto* c=m_cache.get();
    // These JNI bindings are sufficient for the single scheduler-owned
    // game-thread dispatch path.  Held-left and lower attack retransforms only
    // arbitrate vanilla call sites when a client exposes them; making either
    // optional hook part of this capability used to disable Silent Lock
    // completely on otherwise supported clients.
    return c && c->attackEntity && c->getEntityById && c->swingItem &&
        c->playerControllerField && c->playerField && c->worldField;
}

void GameBindings::deactivateSilentOutput() noexcept
{
    m_logicalController.deactivate();
    m_sprintFeatureEnabled.store(false,std::memory_order_release);
    m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    m_silentRotationHook.setEnabled(m_logicalController.restoring()||
        m_logicalController.packetContinuityRequired());
    m_logicalMovementHook.setEnabled(false);
    m_logicalJumpHook.setEnabled(false);
    m_headingHook.setEnabled(false);
    m_attackOwnershipHook.setEnabled(false);
    m_logicalInteractionHook.setEnabled(false);
}

void GameBindings::refreshAttackAtPublication(JNIEnv* env) noexcept
{
    const auto pending=m_logicalController.pendingAttack();
    if(pending.kind==silent::InteractionCommandKind::None) return;
    const auto* c=m_cache.get();
    if(!c||env->PushLocalFrame(24)<0) {clearException(env);return;}
    struct Locals {JNIEnv* env;~Locals(){env->PopLocalFrame(nullptr);}} locals{env};
    const auto cancel=[&](const char* const reason) noexcept {
        clearException(env);
        (void)m_logicalController.cancelPendingAttack(pending,reason);
    };
    jobject mc=c->minecraftInstanceField
        ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
        : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
    if(!mc||env->ExceptionCheck()) {cancel("minecraft_unavailable");return;}
    if(env->CallBooleanMethod(mc,c->isMainThread)!=JNI_TRUE||env->ExceptionCheck()) {
        clearException(env);return;
    }
    jobject player=env->GetObjectField(mc,c->playerField);
    jobject world=env->GetObjectField(mc,c->worldField);
    if(!player||!world||env->ExceptionCheck()) {cancel("world_unavailable");return;}
    jobject target=env->CallObjectMethod(world,c->getEntityById,pending.entityId);
    if(!target||env->ExceptionCheck()) {
        cancel("entity_lookup_failed");return;
    }
    // Selection has already established player identity using the live world
    // player list. Some offline-server player wrappers do not satisfy the
    // optional cached EntityPlayer class test even though their entity id is
    // present in that list. Require only the living-entity methods used below.
    if(env->IsInstanceOf(target,c->livingClass)!=JNI_TRUE||env->ExceptionCheck()) {
        cancel("entity_not_living");return;
    }
    if(c->getHealth) {
        const float health=env->CallFloatMethod(target,c->getHealth);
        if(env->ExceptionCheck()||!std::isfinite(health)||health<=0.0F) {
            cancel("dead");return;
        }
    }
    silent::Vec3 eye{};silent::Bounds bounds{};
    if(!readCombatEye(env,player,eye)||!readCombatBounds(env,target,bounds)) {
        cancel("bounds_unavailable");return;
    }
    const double nearestX=std::clamp(eye.x,bounds.minX,bounds.maxX);
    const double nearestY=std::clamp(eye.y,bounds.minY,bounds.maxY);
    const double nearestZ=std::clamp(eye.z,bounds.minZ,bounds.maxZ);
    const double preAimDistance=std::hypot(
        std::hypot(nearestX-eye.x,nearestZ-eye.z),nearestY-eye.y);
    if(!std::isfinite(preAimDistance)||preAimDistance>pending.preAimReach+1.0e-5) {
        cancel("outside_preaim_range");return;
    }
    const auto trace=[&](const silent::Vec3 direction,const double reach) noexcept {
        silent::LogicalFramePlan ray{};
        ray.rayOrigin=eye;ray.rayDirection=direction;ray.rayLimit=reach;
        return traceLogicalBlock(env,world,ray);
    };
    const auto direction=silent::RayTraceCoordinator::direction(pending.committedRotation);
    const double distance=silent::RayTraceCoordinator::intersect(eye,direction,bounds,pending.reach);
    if(std::isfinite(distance)&&distance>=0.0) {
        const auto block=pending.enforceAvailability?trace(direction,pending.reach):silent::BlockRayHit{};
        if(!pending.enforceAvailability||(block.querySucceeded&&
           (block.distance<0.0||block.distance>distance+1.0e-5))) {
            (void)m_logicalController.revisePendingAttack(
                pending,pending.committedRotation,true);
            return;
        }
    }
    const auto point=silent::chooseCombatAimPoint(eye,bounds,pending.committedRotation,
        pending.reach,pending.enforceAvailability,trace);
    const double dx=point.point.x-eye.x,dy=point.point.y-eye.y,dz=point.point.z-eye.z;
    constexpr double degrees=180.0/3.14159265358979323846;
    const aim::Angles rotation{std::atan2(dz,dx)*degrees-90.0,
        -std::atan2(dy,std::hypot(dx,dz))*degrees};
    (void)m_logicalController.revisePendingAttack(pending,rotation,point.available);
}

jobject GameBindings::serializeLogicalPacket(JNIEnv* env,jobject packet) noexcept
{
    if(!env || !packet) return packet;
    observeDigPacket(env,packet);
    const auto* c=m_cache.get();
    if(!c || !c->movementPacketClass || !c->lookPacketClass ||
       !c->positionPacketClass || !c->positionLookPacketClass ||
       !c->movementPacketConstructor || !c->positionPacketConstructor ||
       !c->lookPacketConstructor || !c->positionLookPacketConstructor ||
       !c->packetOnGround || !c->packetYaw || !c->packetPitch ||
       std::any_of(c->packetPosition.begin(),c->packetPosition.end(),
                   [](jfieldID value){ return value==nullptr; })) return packet;
    if(!env->IsInstanceOf(packet,c->movementPacketClass) ||
       env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    const jboolean onGround=env->GetBooleanField(packet,c->packetOnGround);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    // A transformed client may subclass C04/C05/C06. Exact-class comparison
    // mislabels such packets as Ground, then a rotation rewrite can discard
    // their position payload and violate the 1.8 position-reminder cadence.
    const bool exactPositionLook=env->IsInstanceOf(packet,c->positionLookPacketClass)==JNI_TRUE;
    const bool exactPosition=env->IsInstanceOf(packet,c->positionPacketClass)==JNI_TRUE;
    const bool exactLook=env->IsInstanceOf(packet,c->lookPacketClass)==JNI_TRUE;
    jclass packetClass=env->GetObjectClass(packet);
    const bool exactBase=packetClass&&
        env->IsSameObject(packetClass,c->movementPacketClass)==JNI_TRUE;
    if(packetClass) env->DeleteLocalRef(packetClass);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    const bool hasPosition=exactPosition || exactPositionLook;
    const bool hasRotation=exactLook || exactPositionLook;
    const silent::PacketKind original=exactPositionLook ? silent::PacketKind::PositionLook :
        exactPosition ? silent::PacketKind::Position : exactLook ? silent::PacketKind::Look :
        exactBase ? silent::PacketKind::Ground : silent::PacketKind::Unknown;
    // Unknown movement derivatives must pass through intact; do not guess
    // their payload shape when an adapter injects a custom packet subclass.
    if(original==silent::PacketKind::Unknown) return packet;
    aim::Angles originalRotation{};
    bool originalRotationValid=false;
    if(hasRotation) {
        originalRotation={env->GetFloatField(packet,c->packetYaw),
                          env->GetFloatField(packet,c->packetPitch)};
        originalRotationValid=env->ExceptionCheck()==JNI_FALSE&&
            std::isfinite(originalRotation.yaw)&&
            std::isfinite(originalRotation.pitch);
        if(env->ExceptionCheck()==JNI_TRUE) {
            clearException(env);
            return packet;
        }
    }
    const auto record=[&](jobject value,silent::PacketKind finalKind,
                          const bool rotation,const bool restore,
                          const bool vanillaHandoff=false) noexcept {
        double x=0,y=0,z=0; float rawYaw=0,rawPitch=0;
        if(hasPosition) {
            x=env->GetDoubleField(value,c->packetPosition[0]);
            y=env->GetDoubleField(value,c->packetPosition[1]);
            z=env->GetDoubleField(value,c->packetPosition[2]);
        }
        if(rotation) {
            rawYaw=env->GetFloatField(value,c->packetYaw); rawPitch=env->GetFloatField(value,c->packetPitch);
        }
        if(env->ExceptionCheck()) {clearException(env);return;}
        m_movementPacketSerial.fetch_add(1U,std::memory_order_release);
        const auto before=m_logicalController.latest();
        const auto noPositionRun=hasPosition?0U:
            m_noPositionPacketRun.fetch_add(1U,std::memory_order_relaxed)+1U;
        if(hasPosition) m_noPositionPacketRun.store(0U,std::memory_order_relaxed);
        char detail[320]{};
        std::snprintf(detail,sizeof(detail),"originalType=%u finalType=%u hasPosition=%d hasRotation=%d noPositionRun=%u pos=(%.8f,%.8f,%.8f) yaw=%.6f pitch=%.6f",
            static_cast<unsigned>(original),static_cast<unsigned>(finalKind),hasPosition,rotation,
            noPositionRun,x,y,z,rawYaw,rawPitch);
        m_logicalController.debug().event("PACKET",before,detail);
        if(rotation) m_logicalController.acknowledgePacket({rawYaw,rawPitch},original,finalKind,hasPosition,true);
        if(restore) {
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",m_logicalController.latest(),detail,true);
        }
        if(vanillaHandoff) {
            m_waitingVanillaResume=false;
            m_logicalController.debug().event("VANILLA_ROTATION_RESUME",m_logicalController.latest(),detail,true);
        }
    };
    // Without adapted movement, sample current geometry at publication too.
    // The controller refuses to rewrite an already committed physics snapshot:
    // SCA refreshes before its first jump/moveFlying consumer instead. Never
    // emit an extra movement packet or attack during movement POST.
    refreshAttackAtPublication(env);
    const silent::PacketSerializationPlan plan=
        m_logicalController.packetPlan(hasPosition,hasRotation,
            originalRotation,originalRotationValid);
    if(plan.mutation==silent::PacketMutation::Pass) {
        record(packet,original,hasRotation,false);
        if(plan.restoring&&!hasRotation) {
            m_logicalController.acknowledgeSuppressedPacket(
                original,original,hasPosition);
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",
                m_logicalController.latest(),"duplicate rotation suppressed",true);
        }
        return packet;
    }
    const float yaw=static_cast<float>(plan.rotation.yaw);
    const float pitch=static_cast<float>(plan.rotation.pitch);
    if(!std::isfinite(yaw) || !std::isfinite(pitch)) return packet;
    jobject replacement=nullptr;
    if(plan.mutation==silent::PacketMutation::RemoveRotation) {
        if(hasPosition) {
            const jdouble x=env->GetDoubleField(packet,c->packetPosition[0]);
            const jdouble y=env->GetDoubleField(packet,c->packetPosition[1]);
            const jdouble z=env->GetDoubleField(packet,c->packetPosition[2]);
            if(env->ExceptionCheck()==JNI_FALSE)
                replacement=env->NewObject(c->positionPacketClass,
                    c->positionPacketConstructor,x,y,z,onGround);
        } else {
            replacement=env->NewObject(c->movementPacketClass,
                c->movementPacketConstructor,onGround);
        }
        if(env->ExceptionCheck()==JNI_TRUE || !replacement) {
            clearException(env); return packet;
        }
        const silent::PacketKind replacementKind=hasPosition
            ? silent::PacketKind::Position : silent::PacketKind::Ground;
        record(replacement,replacementKind,false,false);
        m_logicalController.acknowledgeSuppressedPacket(
            original,replacementKind,hasPosition);
        if(plan.restoring) {
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",
                m_logicalController.latest(),"duplicate rotation suppressed",true);
        }
        return replacement;
    }
    if(hasPosition) {
        const jdouble x=env->GetDoubleField(packet,c->packetPosition[0]);
        const jdouble y=env->GetDoubleField(packet,c->packetPosition[1]);
        const jdouble z=env->GetDoubleField(packet,c->packetPosition[2]);
        if(env->ExceptionCheck()==JNI_FALSE)
            replacement=env->NewObject(c->positionLookPacketClass,
                c->positionLookPacketConstructor,x,y,z,yaw,
                std::clamp(pitch,-90.0F,90.0F),onGround);
    } else {
        replacement=env->NewObject(c->lookPacketClass,c->lookPacketConstructor,
            yaw,std::clamp(pitch,-90.0F,90.0F),onGround);
    }
    if(env->ExceptionCheck()==JNI_TRUE || !replacement) {
        clearException(env);
        return packet;
    }
    const silent::PacketKind replacementKind=hasPosition
        ? silent::PacketKind::PositionLook : silent::PacketKind::Look;
    record(replacement,replacementKind,true,plan.restoring,
           plan.vanillaHandoff);
    return replacement;
}

bool GameBindings::setFreeLookPerspective(JNIEnv* env,const int perspective,
                                          int* previous) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!c||!c->minecraftClass||!c->gameSettingsField||
       !c->thirdPersonView) return false;
    jobject minecraft=c->minecraftInstanceField
        ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
        : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
    if(env->ExceptionCheck()||!minecraft) {
        clearException(env);
        if(minecraft) env->DeleteLocalRef(minecraft);
        return false;
    }
    jobject settings=env->GetObjectField(minecraft,c->gameSettingsField);
    if(env->ExceptionCheck()||!settings) {
        clearException(env);
        if(settings) env->DeleteLocalRef(settings);
        env->DeleteLocalRef(minecraft);
        return false;
    }
    if(previous) *previous=env->GetIntField(settings,c->thirdPersonView);
    if(!env->ExceptionCheck())
        env->SetIntField(settings,c->thirdPersonView,
                         static_cast<jint>(perspective));
    const bool succeeded=env->ExceptionCheck()!=JNI_TRUE;
    clearException(env);
    env->DeleteLocalRef(settings);
    env->DeleteLocalRef(minecraft);
    return succeeded;
}

void GameBindings::endFreeLook(JNIEnv* env,const char* reason,
                               const bool forced) noexcept
{
    if(!m_freeLookActive&&!m_freeLookPerspectiveSaved&&!m_freeLookEntity) return;
    char release[320]{};
    std::snprintf(release,sizeof(release),
        "reason=%s perspectiveSaved=%d perspective=%d cameraYaw=%.4f cameraPitch=%.4f bridgeMask=0x%02x",
        reason?reason:"unknown",m_freeLookPerspectiveSaved?1:0,
        m_freeLookPreviousPerspective,m_freeLookYaw,m_freeLookPitch,
        static_cast<unsigned int>(m_freeLookBridgeMask));
    m_freeLookDiagnostics.event(forced?"FORCED_RELEASE":"RELEASE",release);
    bool perspectiveRestored=!m_freeLookPerspectiveSaved;
    if(env&&m_freeLookPerspectiveSaved)
        perspectiveRestored=setFreeLookPerspective(env,m_freeLookPreviousPerspective);
    char restore[144]{};
    std::snprintf(restore,sizeof(restore),
        "requestedPerspective=%d restored=%d jniAvailable=%d",
        m_freeLookPreviousPerspective,perspectiveRestored?1:0,env?1:0);
    m_freeLookDiagnostics.event(perspectiveRestored?"PERSPECTIVE_RESTORE":"JNI_ERROR",
                                restore);
    if(env&&m_freeLookEntity) env->DeleteGlobalRef(m_freeLookEntity);
    m_freeLookEntity=nullptr;
    m_freeLookPerspectiveSaved=false;
    m_freeLookActive=false;
    m_freeLookActiveEventLogged=false;
    m_freeLookBridgeMask=0U;
    m_nextFreeLookVerboseTick=0U;
}

void GameBindings::rotateFreeLookCamera(JNIEnv* env,jobject entity,
                                        const jfloat yawDelta,
    const jfloat pitchDelta) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->rotationYaw||!c->rotationPitch||
       !c->previousRotationYaw||!c->previousRotationPitch) return;
    const auto applyVanillaAngles=[&]() noexcept {
        const jfloat oldYaw=env->GetFloatField(entity,c->rotationYaw);
        const jfloat oldPitch=env->GetFloatField(entity,c->rotationPitch);
        const jfloat oldPreviousYaw=env->GetFloatField(
            entity,c->previousRotationYaw);
        const jfloat oldPreviousPitch=env->GetFloatField(
            entity,c->previousRotationPitch);
        if(env->ExceptionCheck()) { clearException(env); return; }
        const jfloat newYaw=oldYaw+yawDelta*0.15F;
        const jfloat newPitch=std::clamp(
            oldPitch-pitchDelta*0.15F,-90.0F,90.0F);
        env->SetFloatField(entity,c->rotationYaw,newYaw);
        env->SetFloatField(entity,c->rotationPitch,newPitch);
        env->SetFloatField(entity,c->previousRotationYaw,
            oldPreviousYaw+(newYaw-oldYaw));
        env->SetFloatField(entity,c->previousRotationPitch,
            oldPreviousPitch+(newPitch-oldPitch));
        clearException(env);
    };
    const int hotkey=m_freeLookHotkey.load(std::memory_order_acquire);
    const bool held=hotkey>=8&&hotkey<=254&&
        (::GetAsyncKeyState(hotkey)&0x8000)!=0;
    const bool requested=m_freeLookRequested.load(std::memory_order_acquire)&&held;
    if(!requested) {
        if(m_freeLookActive) endFreeLook(env,"hotkey-released",false);
        applyVanillaAngles();
        return;
    }
    // Method-level interception sees every Entity.setAngles invocation. Only
    // the actual local render-view entity may transfer its mouse deltas to the
    // free camera; all other entities retain exact vanilla field semantics.
    bool localCameraEntity=false;
    if(c->minecraftClass&&c->playerField) {
        jobject minecraft=c->minecraftInstanceField
            ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
            : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
        jobject player=!env->ExceptionCheck()&&minecraft
            ? env->GetObjectField(minecraft,c->playerField):nullptr;
        localCameraEntity=!env->ExceptionCheck()&&player&&
            env->IsSameObject(entity,player)==JNI_TRUE;
        if(player)env->DeleteLocalRef(player);
        if(minecraft)env->DeleteLocalRef(minecraft);
        clearException(env);
    }
    if(!localCameraEntity) {
        applyVanillaAngles();
        return;
    }
    if(m_freeLookActive&&m_freeLookEntity&&
       env->IsSameObject(entity,m_freeLookEntity)!=JNI_TRUE)
        endFreeLook(env,"camera-entity-changed",true);
    if((m_freeLookBridgeMask&0x01U)==0U) {
        m_freeLookBridgeMask|=0x01U;
        m_freeLookDiagnostics.event("BRIDGE_CALL","rotateCamera reached native");
    }
    if(!m_freeLookActive) {
        const jfloat yaw=env->GetFloatField(entity,c->rotationYaw);
        const jfloat pitch=env->GetFloatField(entity,c->rotationPitch);
        const jfloat previousYaw=env->GetFloatField(entity,c->previousRotationYaw);
        const jfloat previousPitch=env->GetFloatField(entity,c->previousRotationPitch);
        jobject retained=env->NewGlobalRef(entity);
        int perspective=0;
        if(env->ExceptionCheck()||!retained||
           !std::isfinite(yaw)||!std::isfinite(pitch)||
           !std::isfinite(previousYaw)||!std::isfinite(previousPitch)||
           !setFreeLookPerspective(env,1,&perspective)) {
            clearException(env);
            if(retained) env->DeleteGlobalRef(retained);
            m_freeLookDiagnostics.event("JNI_ERROR",
                "ENTER failed while reading camera state or forcing perspective");
            applyVanillaAngles();
            return;
        }
        m_freeLookEntity=retained;
        m_freeLookYaw=yaw;
        m_freeLookPitch=pitch;
        m_freeLookPreviousYaw=previousYaw;
        m_freeLookPreviousPitch=previousPitch;
        m_freeLookPreviousPerspective=perspective;
        m_freeLookPerspectiveSaved=true;
        m_freeLookActive=true;
        m_freeLookActiveEventLogged=false;
        char enter[320]{};
        std::snprintf(enter,sizeof(enter),
            "savedPerspective=%d forcedPerspective=1 yaw=%.4f pitch=%.4f previousYaw=%.4f previousPitch=%.4f",
            perspective,yaw,pitch,previousYaw,previousPitch);
        m_freeLookDiagnostics.event("ENTER",enter);
    } else {
        // Perspective cycling while held cannot turn the camera back into a
        // player-owned view. The exact previous perspective is restored later.
        if(!setFreeLookPerspective(env,1))
            m_freeLookDiagnostics.event("JNI_ERROR",
                "failed to retain third-person perspective while active");
    }
    const float oldYaw=m_freeLookYaw;
    const float oldPitch=m_freeLookPitch;
    m_freeLookYaw+=yawDelta*0.15F;
    m_freeLookPitch=std::clamp(m_freeLookPitch-pitchDelta*0.15F,-90.0F,90.0F);
    m_freeLookPreviousYaw+=m_freeLookYaw-oldYaw;
    m_freeLookPreviousPitch+=m_freeLookPitch-oldPitch;
    if(!m_freeLookActiveEventLogged) {
        char active[224]{};
        std::snprintf(active,sizeof(active),
            "yaw=%.4f pitch=%.4f inputYaw=%.4f inputPitch=%.4f",
            m_freeLookYaw,m_freeLookPitch,yawDelta,pitchDelta);
        m_freeLookDiagnostics.event("ACTIVE",active);
        m_freeLookActiveEventLogged=true;
    }
    const std::uint64_t now=GetTickCount64();
    if(m_freeLookDiagnostics.verbose()&&now>=m_nextFreeLookVerboseTick) {
        m_nextFreeLookVerboseTick=now+500U;
        char sample[224]{};
        std::snprintf(sample,sizeof(sample),
            "yaw=%.4f pitch=%.4f previousYaw=%.4f previousPitch=%.4f",
            m_freeLookYaw,m_freeLookPitch,m_freeLookPreviousYaw,
            m_freeLookPreviousPitch);
        m_freeLookDiagnostics.event("ACTIVE_SAMPLE",sample);
    }
}

jfloat GameBindings::freeLookCameraAngle(
    JNIEnv* env,jobject entity,const LiveFreeLookTransform::Angle angle) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c) return 0.0F;
    jfieldID field=nullptr;
    switch(angle) {
    case LiveFreeLookTransform::Yaw: field=c->rotationYaw; break;
    case LiveFreeLookTransform::Pitch: field=c->rotationPitch; break;
    case LiveFreeLookTransform::PreviousYaw: field=c->previousRotationYaw; break;
    case LiveFreeLookTransform::PreviousPitch: field=c->previousRotationPitch; break;
    }
    if(!field) return 0.0F;
    const jfloat original=env->GetFloatField(entity,field);
    if(env->ExceptionCheck()) { clearException(env); return 0.0F; }
    if(!m_freeLookActive||!m_freeLookEntity||
       env->IsSameObject(entity,m_freeLookEntity)!=JNI_TRUE) return original;
    const std::uint8_t bit=static_cast<std::uint8_t>(1U<<(
        static_cast<unsigned int>(angle)+1U));
    if((m_freeLookBridgeMask&bit)==0U) {
        m_freeLookBridgeMask|=bit;
        const char* name="unknown";
        switch(angle) {
        case LiveFreeLookTransform::Yaw: name="cameraYaw"; break;
        case LiveFreeLookTransform::Pitch: name="cameraPitch"; break;
        case LiveFreeLookTransform::PreviousYaw: name="previousCameraYaw"; break;
        case LiveFreeLookTransform::PreviousPitch: name="previousCameraPitch"; break;
        }
        m_freeLookDiagnostics.event("BRIDGE_CALL",name);
    }
    switch(angle) {
    case LiveFreeLookTransform::Yaw: return m_freeLookYaw;
    case LiveFreeLookTransform::Pitch: return m_freeLookPitch;
    case LiveFreeLookTransform::PreviousYaw: return m_freeLookPreviousYaw;
    case LiveFreeLookTransform::PreviousPitch: return m_freeLookPreviousPitch;
    }
    return original;
}

jfloat GameBindings::beginLogicalMovement(JNIEnv* env,jobject entity,
                                          const jfloat strafe,
                                          const jfloat forward) noexcept
{
    g_logicalMovementHook={};
    g_logicalMovementHook.mappedForward=forward;
    if(!env || !entity) return strafe;
    const auto* c=m_cache.get();
    if(!c || !c->getEntityId || !c->rotationYaw) return strafe;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return strafe; }
    if(entityId!=m_logicalController.localPlayerId()) return strafe;
    const auto tick=c->entityTicks ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)) : 0U;
    if(env->ExceptionCheck()) { clearException(env); return strafe; }
    // The values arriving here are the inputs Minecraft will use in this exact
    // moveFlying invocation.  Resolving from them (instead of a render-frame
    // key snapshot) makes MovementCoordinator the authoritative source for the
    // current movement computation, including low-TPS/high-FPS timing gaps.
    m_logicalController.beginPhysicsTick(tick);
    refreshAttackAtPublication(env);
    const silent::MovementCommand command=m_logicalController.movementCommand(
        static_cast<double>(strafe),static_cast<double>(forward),tick);
    if(c->isSprinting&&c->motionFields[0]&&c->motionFields[2]) {
        const bool sprinting=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
        const double x=env->GetDoubleField(entity,c->motionFields[0]);
        const double z=env->GetDoubleField(entity,c->motionFields[2]);
        if(!env->ExceptionCheck()) {
            char detail[180]{};
            std::snprintf(detail,sizeof(detail),
                "tick=%llu sprinting=%d owner=%u motion=(%.7f,%.7f)",
                static_cast<unsigned long long>(tick),sprinting?1:0,
                static_cast<unsigned>(m_sprintOwner.load(std::memory_order_acquire)),x,z);
            m_logicalController.debug().event("MOVE_FLYING",
                m_logicalController.latest(),detail);
        }
        clearException(env);
    }
    if(!command.enabled || entityId!=m_logicalController.localPlayerId()) return strafe;
    g_logicalMovementHook.entityId=entityId;
    g_logicalMovementHook.originalYaw=env->GetFloatField(entity,c->rotationYaw);
    g_logicalMovementHook.mappedForward=static_cast<jfloat>(
        command.forward);
    env->SetFloatField(entity,c->rotationYaw,
                       static_cast<jfloat>(command.logicalRotation.yaw));
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        env->SetFloatField(entity,c->rotationYaw,
                           g_logicalMovementHook.originalYaw);
        clearException(env); g_logicalMovementHook={};
        g_logicalMovementHook.mappedForward=forward; return strafe;
    }
    g_logicalMovementHook.applied=true;
    return static_cast<jfloat>(command.strafe);
}

jfloat GameBindings::logicalMovementForward(JNIEnv*,jobject,
                                            const jfloat fallback) noexcept
{
    return g_logicalMovementHook.applied
        ? g_logicalMovementHook.mappedForward : fallback;
}

void GameBindings::endLogicalMovement(JNIEnv* env,jobject entity) noexcept
{
    if(!env || !entity) return;
    const auto* c=m_cache.get();
    if(c && c->getEntityId) {
        const jint entityId=env->CallIntMethod(entity,c->getEntityId);
        const auto tick=c->entityTicks&&env->ExceptionCheck()==JNI_FALSE
            ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
            : 0U;
        if(env->ExceptionCheck()==JNI_FALSE&&
           entityId==m_logicalController.localPlayerId())
            m_logicalController.endMovementPhase(tick);
        if(g_logicalMovementHook.applied&&c->rotationYaw&&
           env->ExceptionCheck()==JNI_FALSE&&
           entityId==g_logicalMovementHook.entityId) {
            env->SetFloatField(entity,c->rotationYaw,
                               g_logicalMovementHook.originalYaw);
        }
    }
    clearException(env);
    g_logicalMovementHook={};
}

bool GameBindings::arbitrateLogicalSprint(JNIEnv* env,jobject entity,
                                          const bool requested) noexcept
{
    if(!env||!entity) return requested;
    const auto* c=m_cache.get();
    if(!c||!c->getEntityId) return requested;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        return requested;
    }
    if(entityId!=m_logicalController.localPlayerId()) return requested;
    const auto tick=c->entityTicks
        ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
        : 0U;
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        return requested;
    }
    const SprintOwner owner=syncSprintOwner();
    const bool allowed=requested&&owner!=SprintOwner::SilentCombat;
    char detail[136]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu requested=%d owner=%u veto=%d result=%d",
        static_cast<unsigned long long>(tick),requested?1:0,
        static_cast<unsigned>(owner),requested&&!allowed?1:0,allowed?1:0);
    m_logicalController.debug().event("SPRINT_SET",
        m_logicalController.latest(),detail,requested&&!allowed);
    return allowed;
}

GameBindings::SprintOwner GameBindings::syncSprintOwner() noexcept
{
    const bool leftHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    const SprintOwner desired=m_sprintFeatureEnabled.load(std::memory_order_acquire)&&
        leftHeld?SprintOwner::SilentCombat:SprintOwner::Vanilla;
    const SprintOwner previous=m_sprintOwner.exchange(desired,std::memory_order_acq_rel);
    if(previous!=desired) {
        char detail[80]{};
        std::snprintf(detail,sizeof(detail),"old=%u new=%u leftHeld=%d",
            static_cast<unsigned>(previous),static_cast<unsigned>(desired),leftHeld?1:0);
        m_logicalController.debug().event("SPRINT_OWNER",
            m_logicalController.latest(),detail,true);
    }
    return desired;
}

void GameBindings::beginLogicalHeading(JNIEnv* env,jobject entity) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getEntityId||!c->isSprinting||
       !c->setSprinting) return;
    const int entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()||entityId!=m_logicalController.localPlayerId()) {
        clearException(env);return;
    }
    const auto tick=c->entityTicks
        ?static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)):0U;
    const SprintOwner owner=syncSprintOwner();
    const bool tracing=m_logicalController.debug().enabled();
    const bool before=tracing&&
        env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    if(env->ExceptionCheck()) {clearException(env);return;}
    if(owner==SprintOwner::SilentCombat)
        env->CallVoidMethod(entity,c->setSprinting,JNI_FALSE);
    if(env->ExceptionCheck()) {clearException(env);return;}
    if(!tracing) return;
    const bool after=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    const float speed=c->getAIMoveSpeed
        ?env->CallFloatMethod(entity,c->getAIMoveSpeed):0.0F;
    if(env->ExceptionCheck()) {clearException(env);return;}
    char detail[160]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu owner=%u sca=%d actualBefore=%d actualAfter=%d",
        static_cast<unsigned long long>(tick),static_cast<unsigned>(owner),
        m_sprintFeatureEnabled.load(std::memory_order_acquire)?1:0,
        before?1:0,after?1:0);
    m_logicalController.debug().event("SPRINT_PRE",
        m_logicalController.latest(),detail);
    std::snprintf(detail,sizeof(detail),
        "tick=%llu sprinting=%d moveSpeed=%.7f",
        static_cast<unsigned long long>(tick),after?1:0,speed);
    m_logicalController.debug().event("MOVE_HEADING_PRE",
        m_logicalController.latest(),detail);
}

void GameBindings::endLogicalHeading(JNIEnv* env,jobject entity) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getEntityId||!c->isSprinting||
       !c->motionFields[0]||!c->motionFields[2]) return;
    const int entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()||entityId!=m_logicalController.localPlayerId()) {
        clearException(env);return;
    }
    const auto tick=c->entityTicks
        ?static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)):0U;
    const bool sprinting=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    const double x=env->GetDoubleField(entity,c->motionFields[0]);
    const double z=env->GetDoubleField(entity,c->motionFields[2]);
    if(env->ExceptionCheck()) {clearException(env);return;}
    char detail[170]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu sprinting=%d owner=%u motion=(%.7f,%.7f)",
        static_cast<unsigned long long>(tick),sprinting?1:0,
        static_cast<unsigned>(m_sprintOwner.load(std::memory_order_acquire)),x,z);
    m_logicalController.debug().event("MOVE_HEADING_POST",
        m_logicalController.latest(),detail);
}

void GameBindings::beginLogicalJump(JNIEnv* env,jobject entity) noexcept
{
    g_logicalJumpHook={};
    if(!env||!entity) return;
    const auto* c=m_cache.get();
    if(!c||!c->getEntityId||!c->rotationYaw||!c->isSprinting||
       !c->setSprinting||
       std::any_of(c->movementInputFields.begin(),c->movementInputFields.end(),
                   [](jfieldID field){return field==nullptr;})) return;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return;}
    if(entityId!=m_logicalController.localPlayerId()) return;
    const bool physicalSprinting=
        env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    // EntityLivingBase scales both action-state axes by 0.98F between jump()
    // and moveFlying(). Snapshot the current fields now and use the exact
    // values the subsequent movement hook will receive.
    constexpr jfloat VanillaTravelScale=0.98F;
    const jfloat physicalStrafe=
        env->GetFloatField(entity,c->movementInputFields[0])*VanillaTravelScale;
    const jfloat physicalForward=
        env->GetFloatField(entity,c->movementInputFields[1])*VanillaTravelScale;
    const auto tick=c->entityTicks
        ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
        : 0U;
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return;}
    m_logicalController.beginPhysicsTick(tick);
    refreshAttackAtPublication(env);
    const silent::MovementCommand command=m_logicalController.jumpCommand(
        physicalStrafe,physicalForward,physicalSprinting,tick);
    if(!command.enabled) return;
    g_logicalJumpHook.entityId=entityId;
    g_logicalJumpHook.originalYaw=env->GetFloatField(entity,c->rotationYaw);
    env->SetFloatField(entity,c->rotationYaw,
                       static_cast<jfloat>(command.logicalRotation.yaw));
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        env->SetFloatField(entity,c->rotationYaw,g_logicalJumpHook.originalYaw);
        clearException(env);g_logicalJumpHook={};return;
    }
    g_logicalJumpHook.applied=true;
}

void GameBindings::endLogicalJump(JNIEnv* env,jobject entity) noexcept
{
    if(!env||!entity||!g_logicalJumpHook.applied) return;
    const auto* c=m_cache.get();
    if(c&&c->getEntityId&&c->rotationYaw) {
        const jint entityId=env->CallIntMethod(entity,c->getEntityId);
        if(env->ExceptionCheck()==JNI_FALSE&&
           entityId==g_logicalJumpHook.entityId) {
            env->SetFloatField(entity,c->rotationYaw,
                               g_logicalJumpHook.originalYaw);
        }
    }
    clearException(env);
    g_logicalJumpHook={};
}

bool GameBindings::readCombatEye(JNIEnv* env,jobject player,
                                 silent::Vec3& eye) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!player||!c||!c->getEyeHeight) return false;
    eye={env->GetDoubleField(player,c->positionX),
         env->GetDoubleField(player,c->positionY),
         env->GetDoubleField(player,c->positionZ)};
    eye.y+=env->CallFloatMethod(player,c->getEyeHeight);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return false;}
    return std::isfinite(eye.x)&&std::isfinite(eye.y)&&std::isfinite(eye.z);
}

bool GameBindings::readCombatBounds(JNIEnv* env,jobject entity,
                                    silent::Bounds& bounds) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getBounds) return false;
    jobject box=env->CallObjectMethod(entity,c->getBounds);
    if(!box||env->ExceptionCheck()==JNI_TRUE) {
        if(box) env->DeleteLocalRef(box);
        clearException(env);return false;
    }
    bounds={env->GetDoubleField(box,c->minX),env->GetDoubleField(box,c->minY),
        env->GetDoubleField(box,c->minZ),env->GetDoubleField(box,c->maxX),
        env->GetDoubleField(box,c->maxY),env->GetDoubleField(box,c->maxZ)};
    env->DeleteLocalRef(box);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return false;}
    return std::isfinite(bounds.minX)&&std::isfinite(bounds.minY)&&
        std::isfinite(bounds.minZ)&&std::isfinite(bounds.maxX)&&
        std::isfinite(bounds.maxY)&&std::isfinite(bounds.maxZ)&&
        bounds.maxX>bounds.minX&&bounds.maxY>bounds.minY&&bounds.maxZ>bounds.minZ;
}

silent::BlockRayHit GameBindings::traceLogicalBlock(
    JNIEnv* env,jobject world,const silent::LogicalFramePlan& plan) noexcept
{
    silent::BlockRayHit result{};
    const auto* c=m_cache.get();
    if(!env || !world || !c || !c->rayTraceBlocks || !c->rayVectorClass ||
       !c->rayVectorConstructor || !c->hitVector ||
       std::any_of(c->vectorFields.begin(),c->vectorFields.end(),
                   [](jfieldID value){return value==nullptr;})) return result;
    // Adaptive sampling may issue several rays per target. Scope every JNI
    // local to one query instead of retaining hundreds until the frame ends.
    if(env->PushLocalFrame(12)<0) {clearException(env);return result;}
    struct RayLocals {JNIEnv* env;~RayLocals(){env->PopLocalFrame(nullptr);}} locals{env};
    const double reach=std::min(3.0,std::max(0.0,plan.rayLimit));
    jobject from=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,
        plan.rayOrigin.x,plan.rayOrigin.y,plan.rayOrigin.z);
    jobject to=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,
        plan.rayOrigin.x+plan.rayDirection.x*reach,
        plan.rayOrigin.y+plan.rayDirection.y*reach,
        plan.rayOrigin.z+plan.rayDirection.z*reach);
    jobject hit=from&&to&&env->ExceptionCheck()==JNI_FALSE
        ? env->CallObjectMethod(world,c->rayTraceBlocks,from,to,
                                JNI_FALSE,JNI_TRUE,JNI_FALSE) : nullptr;
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return result; }
    result.querySucceeded=true;
    if(!hit) return result;
    jobject point=env->GetObjectField(hit,c->hitVector);
    jobject position=c->rayBlockPos ? env->GetObjectField(hit,c->rayBlockPos) : nullptr;
    jobject facing=c->raySideHit ? env->GetObjectField(hit,c->raySideHit) : nullptr;
    if(point && env->ExceptionCheck()==JNI_FALSE) {
        const double x=env->GetDoubleField(point,c->vectorFields[0]);
        const double y=env->GetDoubleField(point,c->vectorFields[1]);
        const double z=env->GetDoubleField(point,c->vectorFields[2]);
        if(env->ExceptionCheck()==JNI_FALSE)
            result.distance=std::hypot(
                std::hypot(x-plan.rayOrigin.x,y-plan.rayOrigin.y),
                z-plan.rayOrigin.z);
    }
    const bool blockMetadataReady=c->facingIndex &&
        std::none_of(c->blockPosCoordinates.begin(),c->blockPosCoordinates.end(),
                     [](jmethodID value){return value==nullptr;});
    if(blockMetadataReady && position && facing && env->ExceptionCheck()==JNI_FALSE) {
        result.target.x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
        result.target.y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
        result.target.z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
        result.target.face=env->CallIntMethod(facing,c->facingIndex);
        result.target.valid=env->ExceptionCheck()==JNI_FALSE;
    }
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env); return {};
    }
    return result;
}

bool GameBindings::executeLogicalInteraction(
    JNIEnv* env,jobject minecraft,
    const silent::InteractionCommand& command) noexcept
{
    if(command.kind==silent::InteractionCommandKind::None) return true;
    const auto* c=m_cache.get();
    const auto fail=[&](const char* reason) noexcept {
        if(command.kind==silent::InteractionCommandKind::AttackEntity) {
            // attackDispatched is the single authoritative completion record;
            // logging here as well used to emit two ATTACK_FAILED rows for one
            // intent and made dispatch counts ambiguous.
            m_logicalController.attackDispatched(command,false,reason);
        }
        if(env) clearException(env);
        return false;
    };
    if(!env || !minecraft || !c) return fail("bindings_unavailable");
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject world=env->GetObjectField(minecraft,c->worldField);
    jobject controller=env->GetObjectField(minecraft,c->playerControllerField);
    if(!player || !world || !controller || env->ExceptionCheck()==JNI_TRUE) {
        return fail("interaction_context_unavailable");
    }
    if(command.kind==silent::InteractionCommandKind::AttackEntity) {
        jobject target=env->CallObjectMethod(
            world,c->getEntityById,static_cast<jint>(command.entityId));
        if(!target||env->ExceptionCheck()==JNI_TRUE)
            return fail("logical_target_unavailable");
        if(env->IsInstanceOf(target,c->livingClass)!=JNI_TRUE)
            return fail("logical_target_replaced");
        const float targetHealth=env->CallFloatMethod(target,c->getHealth);
        if(env->ExceptionCheck()==JNI_TRUE||!std::isfinite(targetHealth)||targetHealth<=0.0F)
            return fail("logical_target_dead");
        silent::Vec3 eye{};
        silent::Bounds bounds{};
        if(!readCombatEye(env,player,eye)||!readCombatBounds(env,target,bounds))
            return fail("physics_geometry_unavailable");
        const auto direction=silent::RayTraceCoordinator::direction(
            command.committedRotation);
        const double hit=silent::RayTraceCoordinator::intersect(
            eye,direction,bounds,command.reach);
        // Validate the rotation that was actually published. A new point must
        // start a new transaction and get its own publication; never silently
        // substitute a fresh angle inside an already confirmed attack.
        if(!std::isfinite(hit)||hit<0.0)
            return fail("physics_ray_stale");
        if(command.enforceAvailability) {
            silent::LogicalFramePlan ray{};
            ray.rayOrigin=eye;ray.rayDirection=direction;ray.rayLimit=command.reach;
            const auto block=traceLogicalBlock(env,world,ray);
            if(!block.querySucceeded||
               (block.distance>=0.0&&block.distance<=hit+1.0e-5))
                return fail("physics_ray_occluded");
        }
        // Preserve the vanilla 1.8.9 click transaction: the client publishes
        // the arm swing first, then PlayerControllerMP sends the attack.  The
        // PRE dispatch boundary and frozen rotation transaction remain owned
        // by LogicalStateController; only this internal call order is restored.
        env->CallVoidMethod(player,c->swingItem);
        if(env->ExceptionCheck()==JNI_TRUE)
            return fail("swing_jni_exception");
        g_syntheticLogicalAttack=true;
        env->CallVoidMethod(controller,c->attackEntity,player,target);
        g_syntheticLogicalAttack=false;
        if(env->ExceptionCheck()==JNI_TRUE)
            return fail("attack_entity_jni_exception");
        m_logicalController.attackDispatched(command,true);
    } else if(command.kind==silent::InteractionCommandKind::ResetBlock) {
        env->CallVoidMethod(controller,c->resetBlockRemoving);
    } else {
        jobject position=env->NewObject(c->blockPosClass,c->blockPosConstructor,
            command.block.x,command.block.y,command.block.z);
        jobject facing=position ? env->CallStaticObjectMethod(
            c->enumFacingClass,c->getFacingByIndex,command.block.face) : nullptr;
        if(!position || !facing || env->ExceptionCheck()==JNI_TRUE) {
            clearException(env); return false;
        }
        const jboolean accepted=command.kind==
                silent::InteractionCommandKind::StartBlock
            ? env->CallBooleanMethod(controller,c->clickBlock,position,facing)
            : env->CallBooleanMethod(controller,c->onPlayerDamageBlock,position,facing);
        if(command.kind==silent::InteractionCommandKind::StartBlock &&
           env->ExceptionCheck()==JNI_FALSE && accepted!=JNI_TRUE)
            m_logicalController.blockRejected();
        if(env->ExceptionCheck()==JNI_FALSE &&
           (command.kind==silent::InteractionCommandKind::StartBlock ||
            accepted==JNI_TRUE))
            env->CallVoidMethod(player,c->swingItem);
        if(command.kind==silent::InteractionCommandKind::ContinueBlock &&
           env->ExceptionCheck()==JNI_FALSE &&
           env->CallBooleanMethod(world,c->isAirBlock,position)==JNI_TRUE)
            m_logicalController.blockFinished();
    }
    const bool succeeded=env->ExceptionCheck()!=JNI_TRUE;
    if(!succeeded&&command.kind==silent::InteractionCommandKind::AttackEntity)
        return fail("interaction_jni_exception");
    clearException(env);
    return succeeded;
}

jobject GameBindings::arbitrateLogicalAttack(
    JNIEnv* env,jobject originalTarget) noexcept
{
    if(!env||!originalTarget||g_syntheticLogicalAttack||
       !m_logicalController.active()) return originalTarget;
    // Vanilla/lower attackEntity is an ownership guard only. It must never
    // consume a prepared scheduler intent because not every caller is the
    // stable input/PRE boundary. The held-input hook emits the sole synthetic
    // dispatch; that call bypasses this guard via g_syntheticLogicalAttack.
    return nullptr;
}

bool GameBindings::consumeLogicalInteraction(
    JNIEnv* env,jobject minecraft,const LiveInteractionTransform::Entry entry,
    const bool heldDown) noexcept
{
    const auto* c=m_cache.get();
    // This transformed per-tick input entry is the clean boundary shared by
    // Smart Hotbar and combat. Processing the queue here keeps inventory
    // mutation out of key/right-click hooks and out of render-driven update.
    (void)processSmartHotbarRequests(env,minecraft);
    const bool down=entry==LiveInteractionTransform::Entry::Click || heldDown;
    if(!observeLogicalCamera(env,minecraft,down)) return false;
    // Input/PRE may run before heading PRE. Both boundaries honor the same
    // feature-level owner, never a transient target/rotation phase.
    if(env&&minecraft&&c&&syncSprintOwner()==SprintOwner::SilentCombat&&
       c->setSprinting&&c->playerField) {
        jobject player=env->GetObjectField(minecraft,c->playerField);
        if(player&&!env->ExceptionCheck())
            env->CallVoidMethod(player,c->setSprinting,JNI_FALSE);
        clearException(env);
    }
    m_logicalController.debug().event(entry==LiveInteractionTransform::Entry::Click
        ? "CLICK_PULSE" : "HELD_PULSE",
        m_logicalController.latest());
    if(m_logicalController.routeManualInput(down)) {
        m_silentRotationHook.setEnabled(m_logicalController.restoring()||
            m_logicalController.packetContinuityRequired()||
            m_logicalController.debug().enabled());
        // Original click / held-left owns block damage. A combat action already
        // emitted in this real tick defers digging until the next native tick.
        return !m_logicalController.manualBlockInputAllowed();
    }
    if(!m_logicalController.active()) return false;
    if(env->PushLocalFrame(32)<0) { clearException(env); return true; }
    const auto interactionTick=m_logicalController.latest().interactionTick;
    m_logicalController.beginInteractionPre(interactionTick);
    // Revalidate before consuming the CPS intent. If movement invalidated its
    // published ray, retain the same intent/target and require a fresh publish
    // rather than spending this click on a guaranteed failed dispatch.
    refreshAttackAtPublication(env);
    silent::InteractionCommand command=
        m_logicalController.clickAtInteractionPre(interactionTick);
    if(command.kind==silent::InteractionCommandKind::None)
        command=m_logicalController.held(heldDown);
    (void)executeLogicalInteraction(env,minecraft,command);
    env->PopLocalFrame(nullptr);
    // Once active, InteractionCoordinator is the sole source for both Java
    // entry points.  Even a deliberate no-op is consumed so vanilla cannot
    // produce an entity attack and block-damage event in the same logical tick.
    return true;
}

bool GameBindings::observeLogicalCamera(JNIEnv* env,jobject minecraft,bool down) noexcept
{
    const auto* c=m_cache.get();
    if(!env || !minecraft || !c || !c->playerField || !c->rotationYaw ||
       !c->rotationPitch ||
       env->PushLocalFrame(16)<0) { clearException(env); return false; }
    const auto failed=[&] {clearException(env);env->PopLocalFrame(nullptr);return false;};
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject hit=c->cameraMouseOver
        ? env->GetObjectField(minecraft,c->cameraMouseOver) : nullptr;
    silent::BlockTarget block{}; int id=-1;
    if(hit && c->cameraHitEntity && !env->ExceptionCheck()) {
        jobject entity=env->GetObjectField(hit,c->cameraHitEntity);
        if(entity && c->getEntityId) id=env->CallIntMethod(entity,c->getEntityId);
        if(env->ExceptionCheck()) return failed();
        int hitKind=-1;
        const bool typedHit=c->cameraHitType && c->enumOrdinal;
        if(typedHit) {
            jobject type=env->GetObjectField(hit,c->cameraHitType);
            hitKind=type ? env->CallIntMethod(type,c->enumOrdinal) : -1;
            if(env->ExceptionCheck()) return failed();
        }
        jobject position=c->rayBlockPos ? env->GetObjectField(hit,c->rayBlockPos) : nullptr;
        jobject face=c->raySideHit ? env->GetObjectField(hit,c->raySideHit) : nullptr;
        // When typeOfHit is available, a MISS carrying a BlockPos is rejected.
        // On transformed Lunar descriptors, fall back to the unambiguous
        // position + face + no-entity shape so camera observation remains live.
        const bool blockHit=typedHit ? hitKind==1 : position && face && !entity;
        const bool blockMetadataReady=c->facingIndex &&
            std::none_of(c->blockPosCoordinates.begin(),c->blockPosCoordinates.end(),
                         [](jmethodID value){return value==nullptr;});
        if(blockMetadataReady && blockHit && position && face && !entity &&
           !env->ExceptionCheck()) {
            block.x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
            if(env->ExceptionCheck()) return failed();
            block.y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
            if(env->ExceptionCheck()) return failed();
            block.z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
            if(env->ExceptionCheck()) return failed();
            block.face=env->CallIntMethod(face,c->facingIndex); block.valid=true;
        }
    }
    if(player && !env->ExceptionCheck()) {
        const aim::Angles camera{env->GetFloatField(player,c->rotationYaw),env->GetFloatField(player,c->rotationPitch)};
        const auto tick=c->entityTicks
            ? static_cast<std::uint64_t>(env->GetIntField(player,c->entityTicks))
            : 0U;
        const bool sneaking=c->isSneaking && env->CallBooleanMethod(player,c->isSneaking)==JNI_TRUE;
        const bool rightDown=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
        if(!env->ExceptionCheck()) m_logicalController.observeCameraInput(
            camera,block,id,down,tick,sneaking,rightDown);
    }
    const bool ok=player && !env->ExceptionCheck();
    clearException(env); env->PopLocalFrame(nullptr); return ok;
}

void GameBindings::observeActualInteraction(JNIEnv* env,LiveInteractionObserver::Event event,jobject argument) noexcept
{
    const auto* c=m_cache.get(); if(!c || !env) return;
    char detail[180]{};
    const char* name="RESET_BLOCK";
    if(event==LiveInteractionObserver::Event::Attack) {
        const int id=argument ? env->CallIntMethod(argument,c->getEntityId) : -1;
        if(env->ExceptionCheck()) {clearException(env);return;}
        m_lastAttackEntryEntity.store(id,std::memory_order_release);
        m_lastAttackEntryTick.store(::GetTickCount64(),std::memory_order_release);
        // This observer is composed at method entry and therefore sees the
        // vanilla/original argument before LiveAttackTransform substitutes the
        // committed target. The lower ownership hook records final target and
        // dispatch count; treating this entry argument as final would create a
        // false mismatch exactly when substitution is working.
        std::snprintf(detail,sizeof(detail),"entryEntity=%d arbitrationReady=%d",
            id,m_attackOwnershipHook.ready()?1:0);
        name=m_attackOwnershipHook.ready()?"ATTACK_ENTRY":"ACTUAL_ATTACK";
    } else if(argument) {
        const int x=env->CallIntMethod(argument,c->blockPosCoordinates[0]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        const int y=env->CallIntMethod(argument,c->blockPosCoordinates[1]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        const int z=env->CallIntMethod(argument,c->blockPosCoordinates[2]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        std::snprintf(detail,sizeof(detail),"block=(%d,%d,%d)",x,y,z);
        name=event==LiveInteractionObserver::Event::StartBlock ? "CLICK_BLOCK_ENTRY" : "CONTINUE_DIGGING";
    }
    if(!env->ExceptionCheck()) m_logicalController.debug().event(name,m_logicalController.latest(),detail,
        event==LiveInteractionObserver::Event::Attack);
    clearException(env);
}

void GameBindings::observeDigPacket(JNIEnv* env,jobject packet) noexcept
{
    const auto* c=m_cache.get();
    if(!c || !c->diggingPacketClass || !c->diggingAction || !c->diggingPosition || !c->enumOrdinal ||
       !env->IsInstanceOf(packet,c->diggingPacketClass)) return;
    if(env->PushLocalFrame(8)<0) { clearException(env); return; }
    const auto done=[&] {clearException(env);env->PopLocalFrame(nullptr);};
    jobject action=env->CallObjectMethod(packet,c->diggingAction);
    if(env->ExceptionCheck()) {done();return;}
    jobject position=env->CallObjectMethod(packet,c->diggingPosition);
    if(action && position && !env->ExceptionCheck()) {
        const int kind=env->CallIntMethod(action,c->enumOrdinal);
        if(env->ExceptionCheck()) {done();return;}
        const int x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
        if(env->ExceptionCheck()) {done();return;}
        const int y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
        if(env->ExceptionCheck()) {done();return;}
        const int z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
        if(kind>=0 && kind<=2 && !env->ExceptionCheck()) {
            const char* names[]{"START_DIGGING","ABORT_DIGGING","STOP_DIGGING"};
            char detail[100]{}; std::snprintf(detail,sizeof(detail),"block=(%d,%d,%d) source=C07",x,y,z);
            m_logicalController.debug().event(names[kind],m_logicalController.latest(),detail,true);
            if(kind==1 || kind==2) m_logicalController.manualBlockEnded();
        }
    }
    clearException(env); env->PopLocalFrame(nullptr);
}

void GameBindings::sampleBow(JNIEnv* env, GameSnapshot& snapshot, bool enabled) noexcept
{
    snapshot.bowTrajectory = {};
    const BindingCache* c=m_cache.get();
    if(!enabled || !env || !c || !c->isMainThread || snapshot.state!=GameSnapshot::State::Ready ||
       !snapshot.camera.valid || !c->rayTraceBlocks || !c->rayVectorClass ||
       !c->rayVectorConstructor || !c->hitVector || !c->getItemUseDuration ||
       !c->isUsingItem || !c->getEyeHeight || !c->rotationYaw || !c->rotationPitch ||
       !c->getEquipmentInSlot || !c->getItem || !c->getIdFromItem || !c->itemClass ||
       std::any_of(c->vectorFields.begin(),c->vectorFields.end(),[](jfieldID f){return !f;})) return;
    if(env->PushLocalFrame(24)<0) { clearException(env); return; }
    const auto done=[&] { clearException(env); env->PopLocalFrame(nullptr); };
    jobject mc=c->minecraftInstanceField ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
        : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
    if(!mc || env->ExceptionCheck() || !env->CallBooleanMethod(mc,c->isMainThread)) { done(); return; }
    jobject player=env->GetObjectField(mc,c->playerField);
    jobject world=env->GetObjectField(mc,c->worldField);
    if(!player || !world || env->ExceptionCheck() || !env->CallBooleanMethod(player,c->isUsingItem)) { done(); return; }
    jobject stack=env->CallObjectMethod(player,c->getEquipmentInSlot,0);
    jobject item=stack && !env->ExceptionCheck() ? env->CallObjectMethod(stack,c->getItem) : nullptr;
    if(!item || env->ExceptionCheck() || env->CallStaticIntMethod(c->itemClass,c->getIdFromItem,item)!=261) { done(); return; }
    const int ticks=env->CallIntMethod(player,c->getItemUseDuration);
    const double charge=trajectory::bowStrength(static_cast<double>(ticks)+snapshot.camera.partialTicks);
    const double eye=env->CallFloatMethod(player,c->getEyeHeight);
    constexpr double radians=3.14159265358979323846/180;
    const double yaw=env->GetFloatField(player,c->rotationYaw)*radians;
    const double pitch=env->GetFloatField(player,c->rotationPitch)*radians;
    if(env->ExceptionCheck() || charge<0.1 || !std::isfinite(yaw) || !std::isfinite(pitch) ||
       !std::isfinite(eye)) { done(); return; }
    // Same interpolated origin as the current rendered world, with the vanilla
    // bow's lateral 0.16 and vertical 0.1 offsets. No visual easing of the path.
    WorldPoint position{snapshot.camera.renderX-std::cos(yaw)*0.16,
                        snapshot.camera.renderY+eye-0.1,
                        snapshot.camera.renderZ-std::sin(yaw)*0.16};
    WorldPoint velocity{-std::sin(yaw)*std::cos(pitch)*charge*3,
                        -std::sin(pitch)*charge*3,
                        std::cos(yaw)*std::cos(pitch)*charge*3};
    BowTrajectory result; result.active=true;
    result.points[result.pointCount++]=position;
    const auto started=std::chrono::steady_clock::now();
    for(std::size_t step=1;step<result.points.size();++step) {
        const WorldPoint next{position.x+velocity.x,position.y+velocity.y,position.z+velocity.z};
        double closest=std::numeric_limits<double>::infinity();
        const EntityMarker* hitEntity=nullptr;
        for(std::size_t i=0;i<std::min<std::size_t>(snapshot.entityMarkerCount,snapshot.entityMarkers.size());++i) {
            const auto& marker=snapshot.entityMarkers[i];
            if(marker.entityId==snapshot.entityId || !std::isfinite(marker.health) || marker.health<=0 || marker.fireball) continue;
            const double t=snapshot.entityRenderTick;
            const WorldPoint offset{marker.previousX+(marker.currentX-marker.previousX)*t-marker.currentX,
                                    marker.previousY+(marker.currentY-marker.previousY)*t-marker.currentY,
                                    marker.previousZ+(marker.currentZ-marker.previousZ)*t-marker.currentZ};
            auto box=marker.bounds;
            box.minX+=offset.x; box.maxX+=offset.x; box.minY+=offset.y; box.maxY+=offset.y;
            box.minZ+=offset.z; box.maxZ+=offset.z;
            const double fraction=trajectory::segmentBox(position,next,box,0.3);
            if(fraction<closest) { closest=fraction; hitEntity=&marker; }
        }
        // Vanilla rayTraceBlocks respects slabs, fences and non-colliding
        // blocks; a solid-voxel test cannot produce the same impact point.
        if(env->PushLocalFrame(8)<0) { result={}; break; }
        jobject from=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,position.x,position.y,position.z);
        jobject to=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,next.x,next.y,next.z);
        jobject blockHit=nullptr;
        if(from && to && !env->ExceptionCheck())
            blockHit=env->CallObjectMethod(world,c->rayTraceBlocks,from,to,JNI_FALSE,JNI_TRUE,JNI_FALSE);
        if(blockHit && !env->ExceptionCheck()) {
            jobject hit=env->GetObjectField(blockHit,c->hitVector);
            if(hit && !env->ExceptionCheck()) {
                WorldPoint point{env->GetDoubleField(hit,c->vectorFields[0]),env->GetDoubleField(hit,c->vectorFields[1]),
                                 env->GetDoubleField(hit,c->vectorFields[2])};
                const double length=trajectory::distanceSquared(position,next);
                const double fraction=length>1e-12 ? std::sqrt(trajectory::distanceSquared(position,point)/length) : 0;
                if(fraction<=closest) { closest=std::clamp(fraction,0.0,1.0); hitEntity=nullptr; }
            }
        }
        const bool failed=env->ExceptionCheck()==JNI_TRUE;
        clearException(env); env->PopLocalFrame(nullptr);
        if(failed) { result={}; break; }
        if(std::isfinite(closest)) {
            result.hasImpact=true; result.impactLiving=hitEntity!=nullptr;
            result.impactPlayer=hitEntity && hitEntity->player;
            result.impactEntityId=hitEntity ? hitEntity->entityId : -1;
            result.impact=trajectory::interpolate(position,next,closest);
            result.points[result.pointCount++]=result.impact;
            break;
        }
        result.points[result.pointCount++]=next; position=next;
        velocity.x*=0.99; velocity.y=velocity.y*0.99-0.05; velocity.z*=0.99;
        if(std::chrono::steady_clock::now()-started>std::chrono::microseconds(1500)) {
            // Do not invent a landing point if this frame exhausts the budget.
            result.budgetLimited=true; break;
        }
    }
    snapshot.bowTrajectory=result;
    done();
}

const GameSnapshot& GameBindings::sample(JNIEnv* const env,
                                         const std::uint64_t tickMilliseconds) noexcept
{
    // Minecraft simulation runs at 20 TPS. Sampling once per tick keeps the
    // JNI budget bounded while the renderer uses last/current positions plus
    // renderPartialTicks to produce frame-rate-smooth hitboxes.
    constexpr std::uint64_t kSampleIntervalMs = 50U;
    if (env == nullptr ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved) {
        return m_snapshot;
    }
    BindingCache* const cache = m_cache.get();
    if (cache == nullptr || cache->profile == nullptr) {
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    }

    if (tickMilliseconds - m_lastSample < kSampleIntervalMs && m_lastSample != 0U) {
        return m_snapshot;
    }
    m_lastSample = tickMilliseconds;

    if (env->PushLocalFrame(256) < 0) {
        clearException(env);
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    }

    auto failJni = [&]() noexcept -> const GameSnapshot& {
        clearException(env);
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::JniError;
        return m_snapshot;
    };

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (minecraft == nullptr) {
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::WaitingForGameThread;
        return m_snapshot;
    }
    const jboolean onMainThread = env->CallBooleanMethod(minecraft, cache->isMainThread);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (onMainThread != JNI_TRUE) {
        env->PopLocalFrame(nullptr);
        // SwapBuffers can also belong to Forge's splash renderer. Never read
        // world state from that thread/context.
        m_snapshot.state = GameSnapshot::State::WaitingForGameThread;
        return m_snapshot;
    }

    m_snapshot.hypixelServer = false;
    if (cache->getCurrentServerData != nullptr && cache->serverIp != nullptr) {
        jobject serverData = env->CallObjectMethod(
            minecraft, cache->getCurrentServerData);
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
        } else if (serverData != nullptr) {
            jstring address = static_cast<jstring>(
                env->GetObjectField(serverData, cache->serverIp));
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
            } else if (address != nullptr) {
                const char* utf = env->GetStringUTFChars(address, nullptr);
                if (utf != nullptr) {
                    std::string host(utf);
                    env->ReleaseStringUTFChars(address, utf);
                    std::transform(host.begin(), host.end(), host.begin(),
                        [](const unsigned char value) noexcept {
                            return static_cast<char>(std::tolower(value));
                        });
                    const std::size_t colon = host.find(':');
                    if (colon != std::string::npos) host.resize(colon);
                    m_snapshot.hypixelServer = host == "hypixel.net" ||
                        (host.size() > 12U && host.ends_with(".hypixel.net"));
                }
            }
        }
    }

    jobject player = env->GetObjectField(minecraft, cache->playerField);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    jobject world = env->GetObjectField(minecraft, cache->worldField);
    if (env->ExceptionCheck() == JNI_TRUE) {
        return failJni();
    }
    if (player == nullptr || world == nullptr) {
        if (m_lastWorld != nullptr) {
            env->DeleteWeakGlobalRef(m_lastWorld);
            m_lastWorld = nullptr;
        }
        m_sidebarCandidateTeam = bedwars::Team::Unknown;
        m_sidebarStableCount = 0U;
        m_sidebarMissingCount = 0U;
        if (!(m_matchProbe == MatchProbeState{})) {
            m_matchProbe = {};
            ++m_matchProbeGeneration;
        }
        m_matchAnchorValid = false;
        m_lockedOwnBedKnown = false;
        m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        if (m_snapshot.matchActive || m_snapshot.playerCount != 0U ||
            m_snapshot.ownTeam != 'u' ||
            m_snapshot.localPlayerName[0U] != '\0') {
            m_snapshot.matchActive = false;
            m_snapshot.playerCount = 0U;
            m_snapshot.players = {};
            m_snapshot.localPlayerName = {};
            m_snapshot.ownTeam = 'u';
            m_snapshot.ownBedKnown = false;
            m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
            m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
        }
        env->PopLocalFrame(nullptr);
        m_snapshot.state = GameSnapshot::State::NoPlayer;
        return m_snapshot;
    }

    // A WorldClient identity change is the hard lifecycle boundary for every
    // match-derived value. No translated server text or /rejoin command needs
    // to be recognized, and stale team/bed state cannot leak into the next map.
    if (m_lastWorld == nullptr || env->IsSameObject(m_lastWorld, world) != JNI_TRUE) {
        m_snapshot.worldGeneration = ++m_worldGeneration;
        m_knockbackTracks = {};
        m_snapshot.knockbackDamageEvents = m_snapshot.knockbackImpulseEvents =
            m_snapshot.knockbackConfirmedEvents = 0;
        if (m_lastWorld != nullptr) env->DeleteWeakGlobalRef(m_lastWorld);
        m_lastWorld = env->NewWeakGlobalRef(world);
        m_sidebarCandidateTeam = bedwars::Team::Unknown;
        m_sidebarStableCount = 0U;
        m_sidebarMissingCount = 0U;
        m_matchProbe = {};
        ++m_matchProbeGeneration;
        m_matchAnchorValid = false;
        m_lockedOwnBedKnown = false;
        m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        m_snapshot.matchActive = false;
        m_snapshot.ownTeam = 'u';
        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
        m_snapshot.players = {};
        m_snapshot.playerCount = 0U;
        m_debugRosterGeneration = 0U;
        m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
    }

    const jfloat health = env->CallFloatMethod(player, cache->getHealth);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jfloat maxHealth = env->CallFloatMethod(player, cache->getMaxHealth);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jint entityId = env->CallIntMethod(player, cache->getEntityId);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionX = env->GetDoubleField(player, cache->positionX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionY = env->GetDoubleField(player, cache->positionY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jdouble positionZ = env->GetDoubleField(player, cache->positionZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    jobject bounds = env->CallObjectMethod(player, cache->getBounds);
    if (env->ExceptionCheck() == JNI_TRUE || bounds == nullptr) return failJni();

    AxisAlignedBox box;
    box.minX = env->GetDoubleField(bounds, cache->minX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.minY = env->GetDoubleField(bounds, cache->minY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.minZ = env->GetDoubleField(bounds, cache->minZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxX = env->GetDoubleField(bounds, cache->maxX);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxY = env->GetDoubleField(bounds, cache->maxY);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    box.maxZ = env->GetDoubleField(bounds, cache->maxZ);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();

    jobject loadedEntities = cache->loadedEntitiesField != nullptr
        ? env->GetObjectField(world, cache->loadedEntitiesField)
        : env->CallObjectMethod(world, cache->getLoadedEntities);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    const jint loadedEntityCount = loadedEntities == nullptr
        ? 0 : env->CallIntMethod(loadedEntities, cache->listSize);
    if (env->ExceptionCheck() == JNI_TRUE) return failJni();
    jboolean integratedSinglePlayer = env->CallBooleanMethod(
        minecraft, cache->isSingleplayer);
    if (env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
        integratedSinglePlayer = JNI_FALSE;
    }
    const jboolean singlePlayer = JNI_TRUE;
    if (!std::isfinite(health) || !std::isfinite(maxHealth)) return failJni();

    m_snapshot.singlePlayer = singlePlayer == JNI_TRUE;
    m_snapshot.integratedSinglePlayer = integratedSinglePlayer == JNI_TRUE;

    auto readEntityMarker = [&](jobject const entity, EntityMarker& marker,
                                const bool livingEntity) noexcept {
        marker = {};
        marker.entityId = env->CallIntMethod(entity, cache->getEntityId);
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            return false;
        }
        if (livingEntity) {
            marker.health = env->CallFloatMethod(entity, cache->getHealth);
            marker.maxHealth = env->CallFloatMethod(entity, cache->getMaxHealth);
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                marker.health = 0.0F;
                marker.maxHealth = 0.0F;
            }
            if (cache->hurtTime != nullptr) {
                marker.hurtTime = env->GetIntField(entity, cache->hurtTime);
                if (env->ExceptionCheck()) { clearException(env); marker.hurtTime = -1; }
            }
        }
        if (cache->isInvisible != nullptr) {
            marker.invisible = env->CallBooleanMethod(entity, cache->isInvisible) == JNI_TRUE;
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                marker.invisible = false;
            }
        }
        jobject entityBounds = env->CallObjectMethod(entity, cache->getBounds);
        if (env->ExceptionCheck() == JNI_TRUE || entityBounds == nullptr) {
            env->ExceptionClear();
            return false;
        }
        marker.bounds.minX = env->GetDoubleField(entityBounds, cache->minX);
        marker.bounds.minY = env->GetDoubleField(entityBounds, cache->minY);
        marker.bounds.minZ = env->GetDoubleField(entityBounds, cache->minZ);
        marker.bounds.maxX = env->GetDoubleField(entityBounds, cache->maxX);
        marker.bounds.maxY = env->GetDoubleField(entityBounds, cache->maxY);
        marker.bounds.maxZ = env->GetDoubleField(entityBounds, cache->maxZ);
        marker.currentX = env->GetDoubleField(entity, cache->positionX);
        marker.currentY = env->GetDoubleField(entity, cache->positionY);
        marker.currentZ = env->GetDoubleField(entity, cache->positionZ);
        marker.previousX = env->GetDoubleField(entity, cache->previousPosition[0U]);
        marker.previousY = env->GetDoubleField(entity, cache->previousPosition[1U]);
        marker.previousZ = env->GetDoubleField(entity, cache->previousPosition[2U]);
        if (cache->motionFields[0U] != nullptr &&
            cache->motionFields[1U] != nullptr &&
            cache->motionFields[2U] != nullptr) {
            marker.motionX = env->GetDoubleField(entity, cache->motionFields[0U]);
            marker.motionY = env->GetDoubleField(entity, cache->motionFields[1U]);
            marker.motionZ = env->GetDoubleField(entity, cache->motionFields[2U]);
        }
        if (cache->onGround != nullptr) {
            marker.onGround = env->GetBooleanField(entity, cache->onGround) == JNI_TRUE;
            marker.groundKnown = !env->ExceptionCheck();
        }
        const bool failed = env->ExceptionCheck() == JNI_TRUE;
        if (failed) env->ExceptionClear();
        env->DeleteLocalRef(entityBounds);
        if (failed) return false;
        const double centerEntityX = (marker.bounds.minX + marker.bounds.maxX) * 0.5;
        const double centerEntityY = (marker.bounds.minY + marker.bounds.maxY) * 0.5;
        const double centerEntityZ = (marker.bounds.minZ + marker.bounds.maxZ) * 0.5;
        const double deltaX = centerEntityX - positionX;
        const double deltaY = centerEntityY - positionY;
        const double deltaZ = centerEntityZ - positionZ;
        marker.distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        return std::isfinite(marker.distance) &&
               std::isfinite(marker.currentX) && std::isfinite(marker.currentY) &&
               std::isfinite(marker.currentZ) && std::isfinite(marker.previousX) &&
               std::isfinite(marker.previousY) && std::isfinite(marker.previousZ) &&
               std::isfinite(marker.motionX) && std::isfinite(marker.motionY) &&
               std::isfinite(marker.motionZ) &&
               std::isfinite(marker.bounds.minX) && std::isfinite(marker.bounds.minY) &&
               std::isfinite(marker.bounds.minZ) && std::isfinite(marker.bounds.maxX) &&
               std::isfinite(marker.bounds.maxY) && std::isfinite(marker.bounds.maxZ);
    };

    // Read the actual dyed leather chestplate data, never the rendered/glint
    // colour. The same routine is used for the local player (match fallback)
    // and remote players (teammate/threat classification), which prevents the
    // two paths from drifting apart.
    auto readPlayerArmorTeam = [&](jobject const playerObject,
                                   bool& chestplatePresent,
                                   std::uint8_t& protectionLevel) noexcept {
        chestplatePresent = false;
        protectionLevel = 0U;
        if (playerObject == nullptr || cache->getEquipmentInSlot == nullptr ||
            cache->getItem == nullptr ||
            cache->itemArmorClass == nullptr || cache->hasColor == nullptr ||
            cache->getColor == nullptr) {
            return bedwars::Team::Unknown;
        }

        bedwars::Team result = bedwars::Team::Unknown;
        // Remote inventories are private server state. S04PacketEntityEquipment
        // is instead applied to EntityLivingBase's public equipment slots, so
        // getEquipmentInSlot is the authoritative path for other players.
        // Slot layout in 1.8.9 is 0=held, 1=boots, 2=leggings,
        // 3=chestplate, 4=helmet.
        jobject chestplate = env->CallObjectMethod(
            playerObject, cache->getEquipmentInSlot, 3);
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            chestplate = nullptr;
        }
        if (chestplate != nullptr) {
            chestplatePresent = true;
            jobject item = env->CallObjectMethod(chestplate, cache->getItem);
            if (env->ExceptionCheck() != JNI_TRUE && item != nullptr &&
                env->IsInstanceOf(item, cache->itemArmorClass) == JNI_TRUE) {
                const jboolean coloured = env->CallBooleanMethod(
                    item, cache->hasColor, chestplate);
                if (env->ExceptionCheck() != JNI_TRUE && coloured == JNI_TRUE) {
                    const jint rgb = env->CallIntMethod(item, cache->getColor, chestplate);
                    if (env->ExceptionCheck() != JNI_TRUE) {
                        result = bedwars::fromLeatherRgb(
                            static_cast<std::uint32_t>(rgb));
                    }
                }
            }
            clearException(env);
            if (item != nullptr) env->DeleteLocalRef(item);
        }

        if (cache->enchantmentHelperClass != nullptr &&
            cache->getEnchantmentLevel != nullptr) {
            // Protection can be carried by any visible armour piece. Query all
            // four packet-backed slots and keep the strongest level instead of
            // assuming that a transformed client mirrors the chestplate into
            // InventoryPlayer. Protection's legacy enchantment ID is 0.
            for (jint slot = 1; slot <= 4; ++slot) {
                jobject armorStack = env->CallObjectMethod(
                    playerObject, cache->getEquipmentInSlot, slot);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    clearException(env);
                    armorStack = nullptr;
                }
                if (armorStack != nullptr) {
                    const jint level = env->CallStaticIntMethod(
                        cache->enchantmentHelperClass,
                        cache->getEnchantmentLevel, 0, armorStack);
                    if (env->ExceptionCheck() != JNI_TRUE) {
                        protectionLevel = std::max(
                            protectionLevel,
                            static_cast<std::uint8_t>(std::clamp(level, 0, 10)));
                    }
                    clearException(env);
                    env->DeleteLocalRef(armorStack);
                }
            }
        }
        if (chestplate != nullptr) env->DeleteLocalRef(chestplate);
        clearException(env);
        return result;
    };

    auto copyUuidString = [&](jobject const uuidObject,
                              std::array<char, 37U>& destination) noexcept {
        if (uuidObject == nullptr || cache->uuidToString == nullptr) return false;
        jstring text = static_cast<jstring>(
            env->CallObjectMethod(uuidObject, cache->uuidToString));
        if (env->ExceptionCheck() == JNI_TRUE || text == nullptr) {
            clearException(env);
            if (text != nullptr) env->DeleteLocalRef(text);
            return false;
        }
        bool copied = false;
        const char* const utf8 = env->GetStringUTFChars(text, nullptr);
        if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
            const std::string_view value(utf8);
            if (value.size() == 36U) {
                std::copy(value.begin(), value.end(), destination.begin());
                copied = true;
            }
        }
        if (utf8 != nullptr) env->ReleaseStringUTFChars(text, utf8);
        clearException(env);
        env->DeleteLocalRef(text);
        return copied;
    };

    jobject textureManagerObject = nullptr;
    if (cache->getTextureManager != nullptr) {
        textureManagerObject = env->CallObjectMethod(
            minecraft, cache->getTextureManager);
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            textureManagerObject = nullptr;
        }
    }

    auto readSkinTextureId = [&](jobject const playerObject) noexcept -> std::uint32_t {
        if (playerObject == nullptr || textureManagerObject == nullptr ||
            cache->abstractClientPlayerClass == nullptr ||
            cache->getLocationSkin == nullptr || cache->getTexture == nullptr ||
            cache->getGlTextureId == nullptr ||
            env->IsInstanceOf(playerObject, cache->abstractClientPlayerClass) != JNI_TRUE) {
            clearException(env);
            return 0U;
        }
        jobject location = env->CallObjectMethod(playerObject, cache->getLocationSkin);
        jobject texture = nullptr;
        if (env->ExceptionCheck() != JNI_TRUE && location != nullptr) {
            texture = env->CallObjectMethod(textureManagerObject,
                                             cache->getTexture, location);
        }
        jint textureId = 0;
        if (env->ExceptionCheck() != JNI_TRUE && texture != nullptr) {
            textureId = env->CallIntMethod(texture, cache->getGlTextureId);
        }
        if (env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            textureId = 0;
        }
        if (texture != nullptr) env->DeleteLocalRef(texture);
        if (location != nullptr) env->DeleteLocalRef(location);
        return textureId > 0 ? static_cast<std::uint32_t>(textureId) : 0U;
    };

    // Collect live world entities on all supported servers. Player entity type
    // and nickname do not depend on online authentication or a TAB roster join.
    // A single List.toArray() avoids one virtual JNI call per list index. The
    // fixed 128-marker cap and 20 Hz cadence are both deterministic; smooth
    // motion is reconstructed in OverlayRenderer from previous/current tick
    // coordinates and the per-frame Timer.renderPartialTicks value.
    // Knockback evidence keeps a bounded per-identity history so asynchronous
    // hurt/status and movement updates can be correlated across snapshots.
    if (singlePlayer == JNI_TRUE && loadedEntities != nullptr && loadedEntityCount > 0) {
        jobject playerList = env->GetObjectField(world, cache->playerEntities);
        jobjectArray playerObjects = playerList == nullptr ? nullptr :
            static_cast<jobjectArray>(env->CallObjectMethod(playerList, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            playerObjects = nullptr;
        }
        jobjectArray entities = static_cast<jobjectArray>(
            env->CallObjectMethod(loadedEntities, cache->listToArray));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            entities = nullptr;
        }
        m_snapshot.entityMarkerCount = 0U;
        if (entities != nullptr) {
            const jsize entityLimit = std::min<jsize>(env->GetArrayLength(entities), 4096);
            // Real players take priority in bounded marker storage; a server's
            // decorative mobs must not crowd them out before target selection.
            for(int pass=0;pass<2;++pass)
            for (jsize index = 0; index < entityLimit &&
                 m_snapshot.entityMarkerCount < GameSnapshot::MaxEntityMarkers; ++index) {
                jobject entity = env->GetObjectArrayElement(entities, index);
                if (env->ExceptionCheck() == JNI_TRUE) {
                    env->ExceptionClear();
                    continue;
                }
                if (entity == nullptr) continue;
                const bool isLocalPlayer = env->IsSameObject(entity, player) == JNI_TRUE;
                const bool isPlayer=env->IsInstanceOf(entity,cache->playerClass)==JNI_TRUE;
                const bool isLiving = env->IsInstanceOf(entity, cache->livingClass) == JNI_TRUE;
                const bool isHostile = cache->hostileClass != nullptr &&
                    env->IsInstanceOf(entity, cache->hostileClass) == JNI_TRUE;
                const bool isFireball = cache->fireballClass != nullptr &&
                    env->IsInstanceOf(entity, cache->fireballClass) == JNI_TRUE;
                if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
                if (!isLocalPlayer && (isLiving || isFireball) && (pass==0?isPlayer:!isPlayer)) {
                    EntityMarker marker;
                    if (readEntityMarker(entity, marker, isLiving)) {
                        marker.fireball = isFireball;
                        marker.hostile = isHostile;
                        // Entity type is authoritative on offline/custom servers;
                        // a truncated TAB/world-list join must not exclude real players.
                        marker.player=isPlayer;
                        if (!marker.player && playerObjects != nullptr) {
                            const jsize playerLimit = std::min<jsize>(
                                env->GetArrayLength(playerObjects), 64);
                            for (jsize playerIndex = 0; playerIndex < playerLimit;
                                 ++playerIndex) {
                                jobject candidate = env->GetObjectArrayElement(
                                    playerObjects, playerIndex);
                                if (candidate != nullptr) {
                                    marker.player = env->IsSameObject(entity, candidate) == JNI_TRUE;
                                    env->DeleteLocalRef(candidate);
                                }
                                if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
                                if (marker.player) break;
                            }
                        }

                        if (marker.player) {
                            jstring markerName = static_cast<jstring>(
                                env->CallObjectMethod(entity, cache->getName));
                            if (env->ExceptionCheck() != JNI_TRUE && markerName != nullptr) {
                                const char* const utf8 = env->GetStringUTFChars(markerName, nullptr);
                                if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                                    const std::string_view nameView(utf8);
                                    std::size_t displayLength=std::min(nameView.size(),marker.displayName.size()-1U);
                                    if(displayLength<nameView.size())
                                        while(displayLength&&
                                            (static_cast<unsigned char>(nameView[displayLength])&0xC0U)==0x80U) --displayLength;
                                    std::copy_n(nameView.data(),displayLength,marker.displayName.data());
                                    if (!nameView.empty() && nameView.size() <= 16U) {
                                        std::copy(nameView.begin(), nameView.end(),
                                                  marker.playerName.begin());
                                    }
                                }
                                if (utf8 != nullptr) env->ReleaseStringUTFChars(markerName, utf8);
                            }
                            clearException(env);
                            if (markerName != nullptr) env->DeleteLocalRef(markerName);

                            if (cache->getUniqueId != nullptr) {
                                jobject uuidObject = env->CallObjectMethod(
                                    entity, cache->getUniqueId);
                                if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                                    (void)copyUuidString(uuidObject, marker.uuid);
                                }
                                clearException(env);
                                if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                            }
                            marker.skinTextureId = readSkinTextureId(entity);
                        }

                        if (marker.player) {
                            const bedwars::Team armorTeam =
                                readPlayerArmorTeam(entity, marker.hasArmor,
                                                    marker.protectionLevel);
                            marker.armorTeam = bedwars::formatCode(armorTeam);
                            if (cache->getEquipmentInSlot != nullptr &&
                                cache->getIdFromItem != nullptr &&
                                cache->stackSize != nullptr &&
                                cache->getItemDamage != nullptr) {
                                jobject heldStack = env->CallObjectMethod(
                                    entity, cache->getEquipmentInSlot, 0);
                                if (env->ExceptionCheck() != JNI_TRUE && heldStack != nullptr) {
                                    jobject heldItem = env->CallObjectMethod(
                                        heldStack, cache->getItem);
                                    if (env->ExceptionCheck() != JNI_TRUE && heldItem != nullptr) {
                                        const jint itemId = env->CallStaticIntMethod(
                                            cache->itemClass, cache->getIdFromItem, heldItem);
                                        const jint count = env->GetIntField(
                                            heldStack, cache->stackSize);
                                        const jint damage = env->CallIntMethod(
                                            heldStack, cache->getItemDamage);
                                        if (env->ExceptionCheck() != JNI_TRUE) {
                                            marker.heldItemId = static_cast<std::int16_t>(
                                                std::clamp(itemId, -1, 32767));
                                            marker.heldItemCount = static_cast<std::uint8_t>(
                                                std::clamp(count, 0, 255));
                                            marker.heldItemDamage = static_cast<std::uint16_t>(
                                                std::clamp(damage, 0, 65535));
                                        }
                                    }
                                    clearException(env);
                                    if (heldItem != nullptr) env->DeleteLocalRef(heldItem);
                                    env->DeleteLocalRef(heldStack);
                                } else {
                                    clearException(env);
                                }
                            }
                        }

                        marker.teamColor = marker.armorTeam;
                        if (marker.playerName[0U] != '\0' || marker.uuid[0U] != '\0') {
                            for (std::uint32_t rosterIndex = 0U;
                                 rosterIndex < m_snapshot.playerCount; ++rosterIndex) {
                                const PlayerIdentity& identity = m_snapshot.players[rosterIndex];
                                const bool uuidMatch = marker.uuid[0U] != '\0' &&
                                    identity.uuid[0U] != '\0' &&
                                    std::strcmp(identity.uuid.data(), marker.uuid.data()) == 0;
                                const bool nameMatch = marker.playerName[0U] != '\0' &&
                                    std::strcmp(identity.name.data(),
                                                marker.playerName.data()) == 0;
                                if (uuidMatch || nameMatch) {
                                    marker.teamColor = identity.teamColor;
                                    marker.confirmedPlayer = true;
                                    // TAB owns the player-facing nickname.
                                    // Use it for alerts/nametags after the UUID
                                    // join instead of exposing Hypixel's entity
                                    // alias when the two strings differ.
                                    std::copy(identity.name.begin(), identity.name.end(),
                                              marker.playerName.begin());
                                    marker.displayName={};
                                    std::copy(identity.name.begin(),identity.name.end(),marker.displayName.begin());
                                    break;
                                }
                            }
                        }
                        
                        m_snapshot.entityMarkers[m_snapshot.entityMarkerCount++] = marker;
                    }
                }
                env->DeleteLocalRef(entity);
            }
            env->DeleteLocalRef(entities);
        }
        if (playerObjects != nullptr) env->DeleteLocalRef(playerObjects);
        if (playerList != nullptr) env->DeleteLocalRef(playerList);
    } else {
        m_snapshot.entityMarkerCount = 0U;
    }

    // Knockback evidence stays on the bounded 20 TPS sampler. Bow aiming is
    // different: sampleBow traces the current view once per presented frame.
    m_snapshot.knockbackTrajectoryCount = 0U;
    m_snapshot.knockbackHurtAvailable = cache->hurtTime != nullptr;
    const bool trajectoryBlocksAvailable = cache->isAirBlock != nullptr &&
        cache->blockPosClass != nullptr && cache->blockPosConstructor != nullptr;
    const auto blockIsAir = [&](const double x, const double y,
                                const double z, bool& air) noexcept {
        if (!trajectoryBlocksAvailable) return false;
        jobject position = env->NewObject(cache->blockPosClass,
            cache->blockPosConstructor,
            static_cast<jint>(std::floor(x)),
            static_cast<jint>(std::floor(y)),
            static_cast<jint>(std::floor(z)));
        if (position == nullptr || env->ExceptionCheck() == JNI_TRUE) {
            clearException(env);
            return false;
        }
        air = env->CallBooleanMethod(world, cache->isAirBlock, position) == JNI_TRUE;
        const bool valid = env->ExceptionCheck() != JNI_TRUE;
        clearException(env);
        env->DeleteLocalRef(position);
        return valid;
    };

    if (trajectoryBlocksAvailable) {
        for (std::uint32_t markerIndex = 0U;
             markerIndex < m_snapshot.entityMarkerCount &&
             m_snapshot.knockbackTrajectoryCount <
                 GameSnapshot::MaxKnockbackTrajectories; ++markerIndex) {
            const EntityMarker& marker = m_snapshot.entityMarkers[markerIndex];
            if (!marker.player || (m_snapshot.hypixelServer && !marker.confirmedPlayer)) continue;
            auto trackIt = std::find_if(m_knockbackTracks.begin(), m_knockbackTracks.end(),
                [&](const KnockbackTrack& track) {
                    return track.entityId == marker.entityId &&
                        (marker.uuid[0] ? track.uuid == marker.uuid : track.name == marker.playerName);
                });
            if (trackIt == m_knockbackTracks.end()) {
                trackIt = std::min_element(m_knockbackTracks.begin(), m_knockbackTracks.end(),
                    [](const KnockbackTrack& a, const KnockbackTrack& b) { return a.lastSeen < b.lastSeen; });
                *trackIt = {};
                trackIt->entityId = marker.entityId;
                trackIt->uuid = marker.uuid; trackIt->name = marker.playerName;
            }
            auto& track = *trackIt;
            prediction::Velocity velocity{marker.motionX, marker.motionY, marker.motionZ};
            // EntityOtherPlayerMP may interpolate server positions without
            // useful motion fields. Use measured displacement only for a
            // recent sample; never extrapolate an unloaded/teleported player.
            const auto elapsed = tickMilliseconds - track.lastSeen;
            prediction::Velocity measured{};
            const bool measuredValid=track.lastSeen&&elapsed>=15&&elapsed<=150;
            if(measuredValid) {
                const double samplesPerTick=50.0/static_cast<double>(elapsed);
                measured={(marker.currentX-track.position.x)*samplesPerTick,
                          (marker.currentY-track.position.y)*samplesPerTick,
                          (marker.currentZ-track.position.z)*samplesPerTick};
            }
            if (measuredValid &&
                std::hypot(velocity.x, velocity.z) < 0.001 && std::abs(velocity.y) < 0.001) {
                velocity=measured;
            }
            track.position = {marker.currentX, marker.currentY, marker.currentZ};
            track.lastSeen = tickMilliseconds;
            const bool plausible = std::hypot(velocity.x, velocity.z) < 4.0 && std::abs(velocity.y) < 4.0;
            const auto evidence = track.evidence.update({marker.health, marker.hurtTime,
                marker.onGround, marker.groundKnown && plausible, velocity}, tickMilliseconds);
            m_snapshot.knockbackDamageEvents += evidence.damage ? 1U : 0U;
            m_snapshot.knockbackImpulseEvents += evidence.impulse ? 1U : 0U;
            if(evidence.triggered) {
                // One edge pair is enough to nominate a knockback event, but
                // not enough to extrapolate a remote interpolated player.
                track.pendingImpulse=velocity;
                track.pendingAt=tickMilliseconds;
                track.pendingPrediction=true;
                continue;
            }
            if(!track.pendingPrediction) continue;
            if(marker.onGround||tickMilliseconds<=track.pendingAt||
               tickMilliseconds-track.pendingAt>160U) {
                track.pendingPrediction=false;
                continue;
            }
            const prediction::VelocityConfidence confidence=measuredValid
                ?prediction::confirmVelocity(track.pendingImpulse,measured,elapsed)
                :prediction::VelocityConfidence{};
            if(!confidence.confident) {
                if(tickMilliseconds-track.pendingAt>=90U)
                    track.pendingPrediction=false;
                continue;
            }
            track.pendingPrediction=false;
            velocity=confidence.residual;
            ++m_snapshot.knockbackConfirmedEvents;
            KnockbackTrajectory prediction;
            prediction.entityId = marker.entityId;
            prediction.startBounds = marker.bounds;
            double simulatedX = marker.currentX;
            double simulatedY = marker.bounds.minY;
            double simulatedZ = marker.currentZ;
            double velocityX = velocity.x;
            double velocityY = velocity.y;
            double velocityZ = velocity.z;
            prediction.points[prediction.pointCount++] = {
                simulatedX, simulatedY, simulatedZ};
            const double halfWidth=std::clamp(
                (marker.bounds.maxX-marker.bounds.minX)*0.5,0.20,0.60);
            const double height=std::clamp(
                marker.bounds.maxY-marker.bounds.minY,0.6,2.4);
            for (std::size_t step = 1U;
                 step < std::min<std::size_t>(prediction.points.size(),7U); ++step) {
                simulatedX += velocityX;
                simulatedY += velocityY;
                simulatedZ += velocityZ;
                if(velocityY<=0.0) {
                    bool airBelow=true;
                    const double probeY=simulatedY-0.06;
                    if(blockIsAir(simulatedX,probeY,simulatedZ,airBelow)&&
                       !airBelow) {
                        const double blockTop=std::floor(probeY)+1.0;
                        if(simulatedY<=blockTop+0.16) {
                            simulatedY=blockTop;
                            prediction.points[prediction.pointCount++]={
                                simulatedX,simulatedY,simulatedZ};
                            prediction.landed=true;
                            break;
                        }
                    }
                }
                bool volumeClear=true;
                bool volumeKnown=true;
                constexpr std::array<double,3> verticalFractions{0.04,0.50,0.94};
                const std::array<double,2> horizontalOffsets{
                    -halfWidth*0.88,halfWidth*0.88};
                for(const double fraction:verticalFractions) {
                    for(const double offsetX:horizontalOffsets) {
                        for(const double offsetZ:horizontalOffsets) {
                            bool air=true;
                            if(!blockIsAir(simulatedX+offsetX,
                                simulatedY+height*fraction,simulatedZ+offsetZ,air))
                                volumeKnown=false;
                            else if(!air) volumeClear=false;
                        }
                    }
                }
                // Unknown or occupied volume ends the high-confidence horizon;
                // never draw through a wall or unloaded collision query.
                if(!volumeKnown||!volumeClear) break;
                prediction.points[prediction.pointCount++] = {
                    simulatedX, simulatedY, simulatedZ};

                // 1.8 living-entity airborne approximation. The visual is a
                // client prediction; server corrections naturally replace it
                // on the next immutable entity snapshot.
                velocityX *= 0.91;
                velocityZ *= 0.91;
                velocityY = (velocityY - 0.08) * 0.98;
            }
            if(prediction.pointCount>=2U)
                m_snapshot.knockbackTrajectories[
                    m_snapshot.knockbackTrajectoryCount++] = prediction;
        }
    }

    // Bow physics is sampled separately at render cadence, not this 20 TPS sampler.
    m_snapshot.entitySampleGeneration = ++m_entitySampleGeneration;

    // Multiplayer discovery is metadata-only and independent of ESP. At 2 Hz,
    // two stable Sidebar snapshots activate a match in about one second while
    // keeping all collection outside the per-frame renderer path.
    if (tickMilliseconds - m_lastPlayerScan >= 500U || m_lastPlayerScan == 0U) {
        m_lastPlayerScan = tickMilliseconds;
        const bool previousMatchActive = m_snapshot.matchActive;
        const char previousOwnTeam = m_snapshot.ownTeam;
        std::array<PlayerIdentity, GameSnapshot::MaxDiscoveredPlayers> nextPlayers{};
        std::array<std::string, GameSnapshot::MaxDiscoveredPlayers> rosterFormatted{};
        std::array<char, 17U> nextLocalName{};
        std::uint32_t nextCount = 0U;
        jstring localName = static_cast<jstring>(env->CallObjectMethod(player, cache->getName));
        if (env->ExceptionCheck() == JNI_TRUE) {
            env->ExceptionClear();
            localName = nullptr;
        }
        if (localName != nullptr) {
            const char* const localUtf8 = env->GetStringUTFChars(localName, nullptr);
            if (env->ExceptionCheck() != JNI_TRUE && localUtf8 != nullptr) {
                const std::string_view localView(localUtf8);
                if (!localView.empty() && localView.size() <= 16U) {
                    std::copy(localView.begin(), localView.end(), nextLocalName.begin());
                }
            }
            if (localUtf8 != nullptr) env->ReleaseStringUTFChars(localName, localUtf8);
            if (env->ExceptionCheck() == JNI_TRUE) env->ExceptionClear();
            env->DeleteLocalRef(localName);
        }
        auto appendRosterPlayer = [&](const std::string_view nameView,
                                      const std::string_view formattedView,
                                      const std::array<char, 37U>& uuidValue) noexcept {
            const bool validName = !nameView.empty() && nameView.size() <= 16U &&
                std::all_of(nameView.begin(), nameView.end(), [](const char c) noexcept {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') || c == '_';
                });
            if (!validName || nextCount >= nextPlayers.size()) return;
            for (std::uint32_t existing = 0U; existing < nextCount; ++existing) {
                const bool sameUuid = uuidValue[0U] != '\0' &&
                    nextPlayers[existing].uuid[0U] != '\0' &&
                    std::strcmp(uuidValue.data(), nextPlayers[existing].uuid.data()) == 0;
                if (sameUuid || nameView == nextPlayers[existing].name.data()) return;
            }
            PlayerIdentity identity{};
            std::copy(nameView.begin(), nameView.end(), identity.name.begin());
            identity.uuid = uuidValue;
            identity.teamColor = 'u';
            rosterFormatted[nextCount].assign(formattedView);
            nextPlayers[nextCount++] = identity;
        };

        // Prefer the server-maintained TAB collection. Unlike playerEntities,
        // it contains roster members before their entity is spawned/tracked by
        // the client and remains available when they are far outside render
        // distance. Mid-match additions are picked up by this continuous 2 Hz
        // scan as well.
        bool tabRosterRead = false;
        if (cache->getNetHandler != nullptr && cache->getPlayerInfoMap != nullptr &&
            cache->getGameProfile != nullptr && cache->gameProfileGetName != nullptr) {
            jobject netHandler = env->CallObjectMethod(minecraft, cache->getNetHandler);
            jobject playerInfoMap = nullptr;
            jobjectArray playerInfos = nullptr;
            if (env->ExceptionCheck() != JNI_TRUE && netHandler != nullptr) {
                playerInfoMap = env->CallObjectMethod(netHandler, cache->getPlayerInfoMap);
            }
            if (env->ExceptionCheck() != JNI_TRUE && playerInfoMap != nullptr) {
                playerInfos = static_cast<jobjectArray>(env->CallObjectMethod(
                    playerInfoMap, cache->collectionToArray));
            }
            if (env->ExceptionCheck() != JNI_TRUE && playerInfos != nullptr) {
                const jsize count = std::min<jsize>(env->GetArrayLength(playerInfos),
                    static_cast<jsize>(nextPlayers.size()));
                for (jsize index = 0; index < count; ++index) {
                    jobject info = env->GetObjectArrayElement(playerInfos, index);
                    jobject profileObject = info == nullptr ? nullptr :
                        env->CallObjectMethod(info, cache->getGameProfile);
                    jstring name = profileObject == nullptr ? nullptr : static_cast<jstring>(
                        env->CallObjectMethod(profileObject, cache->gameProfileGetName));
                    std::array<char, 37U> profileUuid{};
                    if (profileObject != nullptr && cache->gameProfileGetId != nullptr) {
                        jobject uuidObject = env->CallObjectMethod(
                            profileObject, cache->gameProfileGetId);
                        if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                            (void)copyUuidString(uuidObject, profileUuid);
                        }
                        clearException(env);
                        if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                    }
                    if (env->ExceptionCheck() != JNI_TRUE && name != nullptr) {
                        const char* const utf8 = env->GetStringUTFChars(name, nullptr);
                        if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                            appendRosterPlayer(utf8, {}, profileUuid);
                            env->ReleaseStringUTFChars(name, utf8);
                        }
                    }
                    clearException(env);
                    if (name != nullptr) env->DeleteLocalRef(name);
                    if (profileObject != nullptr) env->DeleteLocalRef(profileObject);
                    if (info != nullptr) env->DeleteLocalRef(info);
                }
                tabRosterRead = nextCount != 0U;
            }
            clearException(env);
            if (playerInfos != nullptr) env->DeleteLocalRef(playerInfos);
            if (playerInfoMap != nullptr) env->DeleteLocalRef(playerInfoMap);
            if (netHandler != nullptr) env->DeleteLocalRef(netHandler);
        }

        // Compatibility fallback for transformed clients without a usable TAB
        // mapping. This path sees only currently spawned player entities.
        if (!tabRosterRead) {
            jobject playerList = env->GetObjectField(world, cache->playerEntities);
            jobjectArray playerArray = playerList == nullptr ? nullptr :
                static_cast<jobjectArray>(env->CallObjectMethod(
                    playerList, cache->listToArray));
            if (env->ExceptionCheck() == JNI_TRUE) {
                env->ExceptionClear();
                playerArray = nullptr;
            }
            if (playerArray != nullptr) {
                const jsize count = std::min<jsize>(env->GetArrayLength(playerArray),
                    static_cast<jsize>(nextPlayers.size()));
                for (jsize index = 0; index < count; ++index) {
                    jobject remotePlayer = env->GetObjectArrayElement(playerArray, index);
                    if (env->ExceptionCheck() == JNI_TRUE || remotePlayer == nullptr) {
                        clearException(env);
                        continue;
                    }
                    jstring name = static_cast<jstring>(
                        env->CallObjectMethod(remotePlayer, cache->getName));
                    jobject display = env->CallObjectMethod(remotePlayer, cache->getDisplayName);
                    jstring formatted = display == nullptr ? nullptr : static_cast<jstring>(
                        env->CallObjectMethod(display, cache->getFormattedText));
                    if (env->ExceptionCheck() != JNI_TRUE && name != nullptr) {
                        const char* const nameUtf8 = env->GetStringUTFChars(name, nullptr);
                        const char* const formattedUtf8 = formatted == nullptr ? nullptr :
                            env->GetStringUTFChars(formatted, nullptr);
                        if (env->ExceptionCheck() != JNI_TRUE && nameUtf8 != nullptr) {
                            std::array<char, 37U> entityUuid{};
                            if (cache->getUniqueId != nullptr) {
                                jobject uuidObject = env->CallObjectMethod(
                                    remotePlayer, cache->getUniqueId);
                                if (env->ExceptionCheck() != JNI_TRUE && uuidObject != nullptr) {
                                    (void)copyUuidString(uuidObject, entityUuid);
                                }
                                clearException(env);
                                if (uuidObject != nullptr) env->DeleteLocalRef(uuidObject);
                            }
                            appendRosterPlayer(nameUtf8,
                                formattedUtf8 == nullptr ? std::string_view{} :
                                std::string_view(formattedUtf8), entityUuid);
                        }
                        if (formattedUtf8 != nullptr)
                            env->ReleaseStringUTFChars(formatted, formattedUtf8);
                        if (nameUtf8 != nullptr)
                            env->ReleaseStringUTFChars(name, nameUtf8);
                    }
                    clearException(env);
                    if (formatted != nullptr) env->DeleteLocalRef(formatted);
                    if (display != nullptr) env->DeleteLocalRef(display);
                    if (name != nullptr) env->DeleteLocalRef(name);
                    env->DeleteLocalRef(remotePlayer);
                }
                env->DeleteLocalRef(playerArray);
            }
            if (playerList != nullptr) env->DeleteLocalRef(playerList);
        }
        std::array<std::string, 32U> sidebarStorage{};
        std::array<std::string_view, 32U> sidebarViews{};
        std::size_t sidebarLineCount = 0U;
        const bool sidebarAvailable =
            cache->getScoreboard != nullptr &&
            cache->getObjectiveInDisplaySlot != nullptr &&
            cache->getPlayersTeam != nullptr &&
            cache->getSortedScores != nullptr &&
            cache->getPlayerName != nullptr &&
            cache->formatPlayerName != nullptr &&
            cache->scorePlayerTeamClass != nullptr;
        jobject scoreboard = sidebarAvailable
            ? env->CallObjectMethod(world, cache->getScoreboard)
            : nullptr;
        if (env->ExceptionCheck() != JNI_TRUE && scoreboard != nullptr) {
            // TAB's GameProfile only carries the raw account name. Resolve its
            // current team through the world scoreboard and format it exactly
            // as Minecraft does; entries without a recognised colour/tag are
            // intentionally excluded later (lobby NPCs and start robots).
            for (std::uint32_t index = 0U; index < nextCount; ++index) {
                if (!rosterFormatted[index].empty()) continue;
                jstring name = env->NewStringUTF(nextPlayers[index].name.data());
                jobject team = name == nullptr ? nullptr : env->CallObjectMethod(
                    scoreboard, cache->getPlayersTeam, name);
                jstring formatted = team == nullptr ? nullptr : static_cast<jstring>(
                    env->CallStaticObjectMethod(cache->scorePlayerTeamClass,
                        cache->formatPlayerName, team, name));
                if (env->ExceptionCheck() != JNI_TRUE && formatted != nullptr) {
                    const char* const utf8 = env->GetStringUTFChars(formatted, nullptr);
                    if (env->ExceptionCheck() != JNI_TRUE && utf8 != nullptr) {
                        rosterFormatted[index] = utf8;
                        env->ReleaseStringUTFChars(formatted, utf8);
                    }
                }
                clearException(env);
                if (formatted != nullptr) env->DeleteLocalRef(formatted);
                if (team != nullptr) env->DeleteLocalRef(team);
                if (name != nullptr) env->DeleteLocalRef(name);
            }
            jobject objective = env->CallObjectMethod(scoreboard, cache->getObjectiveInDisplaySlot, 1);
            if (env->ExceptionCheck() != JNI_TRUE && objective != nullptr) {
                jobject scores = env->CallObjectMethod(scoreboard, cache->getSortedScores, objective);
                if (env->ExceptionCheck() != JNI_TRUE && scores != nullptr) {
                    jobjectArray scoresArray = static_cast<jobjectArray>(env->CallObjectMethod(
                        scores, cache->collectionToArray));
                    if (env->ExceptionCheck() != JNI_TRUE && scoresArray != nullptr) {
                        const jsize len = std::min<jsize>(
                            env->GetArrayLength(scoresArray),
                            static_cast<jsize>(sidebarStorage.size()));
                        for (jsize i = 0; i < len; i++) {
                            jobject scoreObj = env->GetObjectArrayElement(scoresArray, i);
                            if (env->ExceptionCheck() != JNI_TRUE && scoreObj != nullptr) {
                                jstring playerNameStr = static_cast<jstring>(env->CallObjectMethod(scoreObj, cache->getPlayerName));
                                if (env->ExceptionCheck() != JNI_TRUE && playerNameStr != nullptr) {
                                    jobject team = env->CallObjectMethod(scoreboard, cache->getPlayersTeam, playerNameStr);
                                    if (env->ExceptionCheck() != JNI_TRUE && team != nullptr) {
                                        jstring formattedStr = static_cast<jstring>(env->CallStaticObjectMethod(cache->scorePlayerTeamClass, cache->formatPlayerName, team, playerNameStr));
                                        if (env->ExceptionCheck() != JNI_TRUE && formattedStr != nullptr) {
                                            const char* formattedUtf8 = env->GetStringUTFChars(formattedStr, nullptr);
                                            if (formattedUtf8 != nullptr) {
                                                if (sidebarLineCount < sidebarStorage.size()) {
                                                    sidebarStorage[sidebarLineCount] = formattedUtf8;
                                                    sidebarViews[sidebarLineCount] =
                                                        sidebarStorage[sidebarLineCount];
                                                    ++sidebarLineCount;
                                                }
                                                env->ReleaseStringUTFChars(formattedStr, formattedUtf8);
                                            }
                                            env->DeleteLocalRef(formattedStr);
                                        }
                                        env->DeleteLocalRef(team);
                                    }
                                    env->DeleteLocalRef(playerNameStr);
                                }
                                env->DeleteLocalRef(scoreObj);
                            }
                        }
                        env->DeleteLocalRef(scoresArray);
                    }
                    env->DeleteLocalRef(scores);
                }
                env->DeleteLocalRef(objective);
            }
            env->DeleteLocalRef(scoreboard);
        }
        env->ExceptionClear();

        bool localChestplatePresent = false;
        std::uint8_t localProtectionLevel = 0U;
        const bedwars::Team localArmorTeam =
            readPlayerArmorTeam(player, localChestplatePresent,
                                localProtectionLevel);
        (void)localChestplatePresent;

        bedwars::Team rosterTaggedOwnTeam = bedwars::Team::Unknown;
        bedwars::Team rosterColourOwnTeam = bedwars::Team::Unknown;
        std::uint16_t rosterTeamMask = 0U;
        std::uint32_t taggedPlayers = 0U;
        for (std::uint32_t index = 0U; index < nextCount; ++index) {
            const bedwars::Team tagged =
                bedwars::parseRosterTeamTag(rosterFormatted[index]);
            const std::uint8_t teamIndex = bedwars::teamIndex(tagged);
            if (teamIndex >= 8U) continue;
            ++taggedPlayers;
            rosterTeamMask |= static_cast<std::uint16_t>(1U << teamIndex);
            if (nextLocalName[0U] != '\0' &&
                std::strcmp(nextPlayers[index].name.data(), nextLocalName.data()) == 0) {
                rosterTaggedOwnTeam = tagged;
                rosterColourOwnTeam =
                    bedwars::parseRosterTeam(rosterFormatted[index]);
            }
        }
        std::uint8_t rosterDistinctTeams = 0U;
        for (std::uint16_t mask = rosterTeamMask; mask != 0U; mask >>= 1U)
            rosterDistinctTeams += static_cast<std::uint8_t>(mask & 1U);
        // Restore the former roster detector: explicit [R]/[B]/... tags are
        // match evidence even when Lunar inserts an unrelated colour before
        // the tag. Prefer an explicit local tag, then the actual dyed local
        // chestplate, and only then the formatted-name colour.
        const bedwars::Team rosterOwnTeam =
            rosterTaggedOwnTeam != bedwars::Team::Unknown ? rosterTaggedOwnTeam :
            localArmorTeam != bedwars::Team::Unknown ? localArmorTeam :
            rosterColourOwnTeam;
        const bool rosterValid = taggedPlayers >= 2U &&
            rosterDistinctTeams >= 2U && rosterOwnTeam != bedwars::Team::Unknown;

        const bedwars::SidebarSnapshot sidebar = bedwars::parseSidebar(
            std::span<const std::string_view>(sidebarViews.data(), sidebarLineCount));

        // Last-resort match evidence for transformed clients whose scoreboard
        // wrappers remove both Sidebar suffixes and roster tags. It is accepted
        // only when (a) the local leather colour is known, (b) at least two
        // distinct live player armour teams are present, and (c) the world bed
        // scanner has found a bed. This avoids treating lobby rank colours as a
        // match while keeping team detection usable on Lunar.
        std::uint16_t armorTeamMask = 0U;
        const std::uint8_t localArmorIndex = bedwars::teamIndex(localArmorTeam);
        if (localArmorIndex < 8U)
            armorTeamMask |= static_cast<std::uint16_t>(1U << localArmorIndex);
        for (std::uint32_t index = 0U; index < m_snapshot.entityMarkerCount; ++index) {
            const EntityMarker& marker = m_snapshot.entityMarkers[index];
            if (!marker.player) continue;
            const std::uint8_t armorIndex = bedwars::teamIndex(
                bedwars::fromFormatCode(marker.armorTeam));
            if (armorIndex < 8U)
                armorTeamMask |= static_cast<std::uint16_t>(1U << armorIndex);
        }
        std::uint8_t armorDistinctTeams = 0U;
        for (std::uint16_t mask = armorTeamMask; mask != 0U; mask >>= 1U)
            armorDistinctTeams += static_cast<std::uint8_t>(mask & 1U);
        std::uint32_t publishedBedCount = 0U;
        ::AcquireSRWLockShared(&m_bedCacheLock);
        publishedBedCount = m_publishedBedCache.markerCount;
        ::ReleaseSRWLockShared(&m_bedCacheLock);
        const bool armorValid = localArmorTeam != bedwars::Team::Unknown &&
            armorDistinctTeams >= 2U && publishedBedCount > 0U;

        const bool matchEvidenceValid = sidebar.valid || rosterValid || armorValid;
        const bedwars::Team candidateTeam = sidebar.valid ? sidebar.ownTeam :
            rosterValid ? rosterOwnTeam : localArmorTeam;
        if (matchEvidenceValid) {
            m_sidebarMissingCount = 0U;
            if (candidateTeam == m_sidebarCandidateTeam) {
                if (m_sidebarStableCount < UINT8_MAX) ++m_sidebarStableCount;
            } else {
                m_sidebarCandidateTeam = candidateTeam;
                m_sidebarStableCount = 1U;
            }
            // A valid Sidebar "YOU" row is server-authored and can activate
            // immediately. Roster/armor fallbacks still require two samples.
            const std::uint8_t requiredStableSamples = sidebar.valid ? 1U : 2U;
            if (m_sidebarStableCount >= requiredStableSamples &&
                !m_snapshot.matchActive) {
                m_snapshot.matchActive = true;
                m_snapshot.ownTeam = bedwars::formatCode(candidateTeam);
            }
        } else {
            if (m_snapshot.matchActive) {
                // Entity tracking, TAB refreshes and Sidebar packet changes are
                // all transient. Once a match has been confirmed, only a
                // WorldClient lifecycle boundary may clear it; otherwise a far
                // enemy or a respawning teammate would flip the entire session
                // back into lobby mode.
                if (m_sidebarMissingCount < UINT8_MAX) ++m_sidebarMissingCount;
            } else {
                m_sidebarCandidateTeam = bedwars::Team::Unknown;
                m_sidebarStableCount = 0U;
            }
        }

        MatchProbeState nextProbe{};
        nextProbe.sidebarAvailable = sidebarAvailable;
        nextProbe.tabAvailable = cache->getNetHandler != nullptr &&
            cache->getPlayerInfoMap != nullptr && cache->getGameProfile != nullptr &&
            cache->gameProfileGetName != nullptr;
        nextProbe.sidebarEvidence = sidebar.valid;
        nextProbe.rosterEvidence = rosterValid;
        nextProbe.armorEvidence = armorValid;
        nextProbe.sidebarLines = static_cast<std::uint8_t>(
            std::min<std::size_t>(sidebarLineCount, UINT8_MAX));
        nextProbe.sidebarTeams = sidebar.distinctTeams;
        nextProbe.sidebarYouRows = sidebar.youRows;
        nextProbe.rosterTaggedPlayers = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(taggedPlayers, UINT8_MAX));
        nextProbe.rosterPlayers = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(nextCount, UINT8_MAX));
        nextProbe.rosterTeams = rosterDistinctTeams;
        nextProbe.rosterOwnTeam = bedwars::formatCode(rosterOwnTeam);
        nextProbe.localArmorTeam = bedwars::formatCode(localArmorTeam);
        nextProbe.armorTeams = armorDistinctTeams;
        nextProbe.stableCount = m_sidebarStableCount;
        if (!(nextProbe == m_matchProbe)) {
            m_matchProbe = nextProbe;
            ++m_matchProbeGeneration;
        }

        const bool nextMatchActive = m_snapshot.matchActive;
        const char nextOwnTeam = nextMatchActive ? m_snapshot.ownTeam : 'u';
        if (nextMatchActive && !previousMatchActive) {
            m_matchAnchorValid = true;
            m_matchAnchorX = positionX;
            m_matchAnchorY = positionY;
            m_matchAnchorZ = positionZ;
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
            ++m_bedOwnershipGeneration;
        } else if (!nextMatchActive && previousMatchActive) {
            m_matchAnchorValid = false;
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        }
        if (nextMatchActive) {
            std::uint32_t compactCount = 0U;
            for (std::uint32_t index = 0U; index < nextCount; ++index) {
                const bedwars::Team team = bedwars::parseRosterTeam(rosterFormatted[index]);
                if (team == bedwars::Team::Unknown) continue;
                nextPlayers[index].teamColor = bedwars::formatCode(team);
                if (compactCount != index) nextPlayers[compactCount] = nextPlayers[index];
                ++compactCount;
            }
            nextCount = compactCount;

            // A confirmed world's roster is monotonic: TAB can temporarily
            // omit an entry during respawn/network refresh, but that must not
            // turn a known teammate into an enemy or trigger duplicate API
            // queries. New, colour-tagged players are appended; uncoloured
            // startup NPCs/bots are never admitted.
            for (std::uint32_t oldIndex = 0U;
                 oldIndex < m_snapshot.playerCount &&
                 nextCount < nextPlayers.size(); ++oldIndex) {
                const PlayerIdentity& oldPlayer = m_snapshot.players[oldIndex];
                bool alreadyPresent = false;
                for (std::uint32_t index = 0U; index < nextCount; ++index) {
                    const bool sameUuid = nextPlayers[index].uuid[0U] != '\0' &&
                        oldPlayer.uuid[0U] != '\0' &&
                        std::strcmp(nextPlayers[index].uuid.data(),
                                    oldPlayer.uuid.data()) == 0;
                    if (sameUuid || std::strcmp(nextPlayers[index].name.data(),
                                                oldPlayer.name.data()) == 0) {
                        // Preserve the first confirmed team for this world.
                        nextPlayers[index].teamColor = oldPlayer.teamColor;
                        alreadyPresent = true;
                        break;
                    }
                }
                if (!alreadyPresent) nextPlayers[nextCount++] = oldPlayer;
            }
        } else {
            // Outside a confirmed match we deliberately publish no team
            // decisions, preventing lobby ranks/NPCs from reaching Debug chat
            // or the automatic Hypixel query pipeline.
            nextPlayers = {};
            nextCount = 0U;
        }

        std::sort(nextPlayers.begin(), nextPlayers.begin() + nextCount,
                  [](const PlayerIdentity& first, const PlayerIdentity& second) noexcept {
                      return std::strcmp(first.name.data(), second.name.data()) < 0;
                  });
        bool rosterChanged = nextCount != m_snapshot.playerCount;
        for (std::uint32_t index = 0U; !rosterChanged && index < nextCount; ++index) {
            rosterChanged = std::strcmp(nextPlayers[index].name.data(),
                                        m_snapshot.players[index].name.data()) != 0 ||
                            std::strcmp(nextPlayers[index].uuid.data(),
                                        m_snapshot.players[index].uuid.data()) != 0 ||
                            nextPlayers[index].teamColor != m_snapshot.players[index].teamColor;
        }

        const bool statusChanged = nextMatchActive != previousMatchActive ||
            nextOwnTeam != previousOwnTeam ||
            std::strcmp(nextLocalName.data(), m_snapshot.localPlayerName.data()) != 0;
        if (rosterChanged || statusChanged) {
            m_snapshot.players = nextPlayers;
            m_snapshot.playerCount = nextCount;
            m_snapshot.localPlayerName = nextLocalName;
            m_snapshot.matchActive = nextMatchActive;
            m_snapshot.ownTeam = nextOwnTeam;
            m_snapshot.playerRosterGeneration = ++m_playerRosterGeneration;
        }
    }

    // The scanner thread publishes immutable fixed storage. SwapBuffers only
    // takes a short shared SRW lock and performs no block JNI calls.
    if (singlePlayer == JNI_TRUE) {
        const bool previousOwnBedKnown = m_snapshot.ownBedKnown;
        const int previousOwnBedX = m_snapshot.ownBedX;
        const int previousOwnBedY = m_snapshot.ownBedY;
        const int previousOwnBedZ = m_snapshot.ownBedZ;
        const GameSnapshot::OwnBedSource previousOwnBedSource =
            m_snapshot.ownBedSource;
        ::AcquireSRWLockShared(&m_bedCacheLock);
        m_snapshot.bedMarkerCount = m_publishedBedCache.markerCount;
        m_snapshot.bedCount = m_publishedBedCache.markerCount;
        std::copy_n(m_publishedBedCache.markers.begin(),
                    m_publishedBedCache.markerCount, m_snapshot.bedMarkers.begin());
        m_snapshot.bedScanProgress = m_publishedBedCache.generation == 0U ? 0.0F : 1.0F;
        ::ReleaseSRWLockShared(&m_bedCacheLock);

        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
        if (m_snapshot.matchActive && m_snapshot.ownTeam != 'u') {
            const BedMarker* teamCandidate = nullptr;
            std::uint32_t matchingBeds = 0U;
            for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                const BedMarker& bed = m_snapshot.bedMarkers[index];
                if (bed.teamColor != m_snapshot.ownTeam) continue;
                teamCandidate = &bed;
                ++matchingBeds;
            }

            // Team-coloured wool is authoritative whenever exactly one bed
            // matches. If a bed is initially unprotected, lock the nearest
            // bed to the stable match-start position and upgrade that lock
            // later when team-wool evidence arrives.
            if (matchingBeds == 1U && teamCandidate != nullptr &&
                (!m_lockedOwnBedKnown ||
                 m_lockedOwnBedSource == GameSnapshot::OwnBedSource::MatchSpawn)) {
                m_lockedOwnBedKnown = true;
                m_lockedOwnBedX = teamCandidate->x;
                m_lockedOwnBedY = teamCandidate->y;
                m_lockedOwnBedZ = teamCandidate->z;
                m_lockedOwnBedSource = GameSnapshot::OwnBedSource::TeamWool;
            }
            if (!m_lockedOwnBedKnown && m_matchAnchorValid) {
                const BedMarker* nearest = nullptr;
                double nearestDistanceSq = 36.0 * 36.0;
                for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                    const BedMarker& bed = m_snapshot.bedMarkers[index];
                    const double centerX =
                        (static_cast<double>(bed.x + bed.footX) + 1.0) * 0.5;
                    const double centerZ =
                        (static_cast<double>(bed.z + bed.footZ) + 1.0) * 0.5;
                    const double dx = centerX - m_matchAnchorX;
                    const double dy = static_cast<double>(bed.y) - m_matchAnchorY;
                    const double dz = centerZ - m_matchAnchorZ;
                    if (std::abs(dy) > 16.0) continue;
                    const double distanceSq = dx * dx + dy * dy + dz * dz;
                    if (distanceSq < nearestDistanceSq) {
                        nearestDistanceSq = distanceSq;
                        nearest = &bed;
                    }
                }
                if (nearest != nullptr) {
                    m_lockedOwnBedKnown = true;
                    m_lockedOwnBedX = nearest->x;
                    m_lockedOwnBedY = nearest->y;
                    m_lockedOwnBedZ = nearest->z;
                    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::MatchSpawn;
                }
            }

            if (m_lockedOwnBedKnown) {
                bool lockedMarkerLoaded = false;
                for (std::uint32_t index = 0U; index < m_snapshot.bedMarkerCount; ++index) {
                    const BedMarker& bed = m_snapshot.bedMarkers[index];
                    if (bed.x != m_lockedOwnBedX || bed.y != m_lockedOwnBedY ||
                        bed.z != m_lockedOwnBedZ) continue;
                    lockedMarkerLoaded = true;
                    break;
                }
                // Bed caches are intentionally evicted when their chunk
                // unloads. Keep the ownership lock while the local player is
                // far away, but treat a missing marker in nearby/loaded range
                // as a real bed destruction so alerts disappear promptly.
                const double bedDx = (static_cast<double>(m_lockedOwnBedX) + 0.5) - positionX;
                const double bedDz = (static_cast<double>(m_lockedOwnBedZ) + 0.5) - positionZ;
                const bool bedShouldBeLoaded = bedDx * bedDx + bedDz * bedDz <= 96.0 * 96.0;
                if (!lockedMarkerLoaded && bedShouldBeLoaded) {
                    m_lockedOwnBedKnown = false;
                    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
                } else {
                    m_snapshot.ownBedKnown = true;
                    m_snapshot.ownBedX = m_lockedOwnBedX;
                    m_snapshot.ownBedY = m_lockedOwnBedY;
                    m_snapshot.ownBedZ = m_lockedOwnBedZ;
                    m_snapshot.ownBedSource = m_lockedOwnBedSource;
                }
            }
        } else {
            m_lockedOwnBedKnown = false;
            m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
        }
        if (previousOwnBedKnown != m_snapshot.ownBedKnown ||
            previousOwnBedX != m_snapshot.ownBedX ||
            previousOwnBedY != m_snapshot.ownBedY ||
            previousOwnBedZ != m_snapshot.ownBedZ ||
            previousOwnBedSource != m_snapshot.ownBedSource) {
            ++m_bedOwnershipGeneration;
        }
    } else {
        m_snapshot.bedCount = 0U;
        m_snapshot.bedMarkerCount = 0U;
        m_snapshot.bedScanProgress = 0.0F;
        m_snapshot.ownBedKnown = false;
        m_snapshot.ownBedSource = GameSnapshot::OwnBedSource::Unknown;
    }
    if (textureManagerObject != nullptr) env->DeleteLocalRef(textureManagerObject);

    env->PopLocalFrame(nullptr);
    m_snapshot.health = health;
    m_snapshot.maxHealth = maxHealth;
    m_snapshot.entityId = entityId;
    m_snapshot.x = positionX;
    m_snapshot.y = positionY;
    m_snapshot.z = positionZ;
    m_snapshot.bounds = box;
    m_snapshot.loadedEntities = loadedEntityCount;
    m_snapshot.mappingAttempt = m_mappingAttempt.load(std::memory_order_acquire);
    m_snapshot.mappingRetryInMs = 0U;
    m_snapshot.mapping = cache->profile->label.c_str();
    m_snapshot.state = GameSnapshot::State::Ready;
    return m_snapshot;
}

void GameBindings::deleteGlobalRefs(JNIEnv* const env, BindingCache& cache) noexcept
{
    if (env == nullptr) {
        return;
    }
    if (cache.minecraftClass != nullptr) env->DeleteGlobalRef(cache.minecraftClass);
    if (cache.rayVectorClass != nullptr) env->DeleteGlobalRef(cache.rayVectorClass);
    if (cache.rayHitClass != nullptr) env->DeleteGlobalRef(cache.rayHitClass);
    if (cache.diggingPacketClass != nullptr) env->DeleteGlobalRef(cache.diggingPacketClass);
    if (cache.networkPacketClass != nullptr) env->DeleteGlobalRef(cache.networkPacketClass);
    if (cache.movementPacketClass != nullptr) env->DeleteGlobalRef(cache.movementPacketClass);
    if (cache.positionPacketClass != nullptr) env->DeleteGlobalRef(cache.positionPacketClass);
    if (cache.lookPacketClass != nullptr) env->DeleteGlobalRef(cache.lookPacketClass);
    if (cache.positionLookPacketClass != nullptr) env->DeleteGlobalRef(cache.positionLookPacketClass);
    if (cache.packetNetHandlerClass != nullptr) env->DeleteGlobalRef(cache.packetNetHandlerClass);
    cache.rayVectorClass=nullptr; cache.rayHitClass=nullptr;
    cache.networkPacketClass=nullptr; cache.movementPacketClass=nullptr;
    cache.positionPacketClass=nullptr; cache.lookPacketClass=nullptr;
    cache.positionLookPacketClass=nullptr; cache.packetNetHandlerClass=nullptr;
    if (cache.playerClass != nullptr) env->DeleteGlobalRef(cache.playerClass);
    if (cache.livingClass != nullptr) env->DeleteGlobalRef(cache.livingClass);
    if (cache.hostileClass != nullptr) env->DeleteGlobalRef(cache.hostileClass);
    if (cache.entityClass != nullptr) env->DeleteGlobalRef(cache.entityClass);
    if (cache.fireballClass != nullptr) env->DeleteGlobalRef(cache.fireballClass);
    if (cache.aabbClass != nullptr) env->DeleteGlobalRef(cache.aabbClass);
    if (cache.worldClass != nullptr) env->DeleteGlobalRef(cache.worldClass);
    if (cache.worldClientClass != nullptr) env->DeleteGlobalRef(cache.worldClientClass);
    if (cache.stateClass != nullptr) env->DeleteGlobalRef(cache.stateClass);
    if (cache.blockClass != nullptr) env->DeleteGlobalRef(cache.blockClass);
    if (cache.blockPosClass != nullptr) env->DeleteGlobalRef(cache.blockPosClass);
    if (cache.bedClass != nullptr) env->DeleteGlobalRef(cache.bedClass);
    if (cache.chunkProviderClass != nullptr) env->DeleteGlobalRef(cache.chunkProviderClass);
    if (cache.chunkClass != nullptr) env->DeleteGlobalRef(cache.chunkClass);
    if (cache.storageClass != nullptr) env->DeleteGlobalRef(cache.storageClass);
    if (cache.activeRenderInfoClass != nullptr) env->DeleteGlobalRef(cache.activeRenderInfoClass);
    if (cache.renderManagerClass != nullptr) env->DeleteGlobalRef(cache.renderManagerClass);
    if (cache.timerClass != nullptr) env->DeleteGlobalRef(cache.timerClass);
    if (cache.gameSettingsClass != nullptr)
        env->DeleteGlobalRef(cache.gameSettingsClass);
    if (cache.entityRendererClass != nullptr)
        env->DeleteGlobalRef(cache.entityRendererClass);
    if (cache.renderGlobalClass != nullptr)
        env->DeleteGlobalRef(cache.renderGlobalClass);
    if (cache.keyBindingClass != nullptr)
        env->DeleteGlobalRef(cache.keyBindingClass);
    if (cache.playerControllerClass != nullptr)
        env->DeleteGlobalRef(cache.playerControllerClass);
    if (cache.serverDataClass != nullptr)
        env->DeleteGlobalRef(cache.serverDataClass);
    if (cache.itemBlockClass != nullptr)
        env->DeleteGlobalRef(cache.itemBlockClass);
    if (cache.enumFacingClass != nullptr)
        env->DeleteGlobalRef(cache.enumFacingClass);
    if (cache.vec3Class != nullptr)
        env->DeleteGlobalRef(cache.vec3Class);
    if (cache.chatComponentClass != nullptr) env->DeleteGlobalRef(cache.chatComponentClass);
    if (cache.chatTextClass != nullptr) env->DeleteGlobalRef(cache.chatTextClass);
    if (cache.chatSerializerClass != nullptr)
        env->DeleteGlobalRef(cache.chatSerializerClass);
    if (cache.scoreboardClass != nullptr) env->DeleteGlobalRef(cache.scoreboardClass);
    if (cache.scoreObjectiveClass != nullptr) env->DeleteGlobalRef(cache.scoreObjectiveClass);
    if (cache.scoreClass != nullptr) env->DeleteGlobalRef(cache.scoreClass);
    if (cache.scorePlayerTeamClass != nullptr) env->DeleteGlobalRef(cache.scorePlayerTeamClass);
    if (cache.netHandlerClass != nullptr) env->DeleteGlobalRef(cache.netHandlerClass);
    if (cache.networkPlayerInfoClass != nullptr) env->DeleteGlobalRef(cache.networkPlayerInfoClass);
    if (cache.gameProfileClass != nullptr) env->DeleteGlobalRef(cache.gameProfileClass);
    if (cache.itemStackClass != nullptr) env->DeleteGlobalRef(cache.itemStackClass);
    if (cache.itemClass != nullptr) env->DeleteGlobalRef(cache.itemClass);
    if (cache.itemArmorClass != nullptr) env->DeleteGlobalRef(cache.itemArmorClass);
    if (cache.itemSwordClass != nullptr) env->DeleteGlobalRef(cache.itemSwordClass);
    if (cache.inventoryPlayerClass != nullptr) env->DeleteGlobalRef(cache.inventoryPlayerClass);
    if (cache.enchantmentHelperClass != nullptr)
        env->DeleteGlobalRef(cache.enchantmentHelperClass);
    if (cache.abstractClientPlayerClass != nullptr)
        env->DeleteGlobalRef(cache.abstractClientPlayerClass);
    if (cache.resourceLocationClass != nullptr)
        env->DeleteGlobalRef(cache.resourceLocationClass);
    if (cache.textureManagerClass != nullptr)
        env->DeleteGlobalRef(cache.textureManagerClass);
    if (cache.textureObjectClass != nullptr)
        env->DeleteGlobalRef(cache.textureObjectClass);
    if (cache.uuidClass != nullptr) env->DeleteGlobalRef(cache.uuidClass);
    if (cache.renderManagerObject != nullptr) env->DeleteGlobalRef(cache.renderManagerObject);
    if (cache.timerObject != nullptr) env->DeleteGlobalRef(cache.timerObject);
    if (cache.modelViewBuffer != nullptr) env->DeleteGlobalRef(cache.modelViewBuffer);
    if (cache.projectionBuffer != nullptr) env->DeleteGlobalRef(cache.projectionBuffer);
    if (cache.viewportBuffer != nullptr) env->DeleteGlobalRef(cache.viewportBuffer);

    cache.minecraftClass = nullptr;
    cache.playerClass = nullptr;
    cache.livingClass = nullptr;
    cache.hostileClass = nullptr;
    cache.entityClass = nullptr;
    cache.fireballClass = nullptr;
    cache.aabbClass = nullptr;
    cache.worldClass = nullptr;
    cache.worldClientClass = nullptr;
    cache.stateClass = nullptr;
    cache.blockClass = nullptr;
    cache.blockPosClass = nullptr;
    cache.bedClass = nullptr;
    cache.chunkProviderClass = nullptr;
    cache.chunkClass = nullptr;
    cache.storageClass = nullptr;
    cache.activeRenderInfoClass = nullptr;
    cache.renderManagerClass = nullptr;
    cache.timerClass = nullptr;
    cache.gameSettingsClass = nullptr;
    cache.entityRendererClass = nullptr;
    cache.renderGlobalClass = nullptr;
    cache.keyBindingClass = nullptr;
    cache.playerControllerClass = nullptr;
    cache.serverDataClass = nullptr;
    cache.itemBlockClass = nullptr;
    cache.enumFacingClass = nullptr;
    cache.vec3Class = nullptr;
    cache.chatComponentClass = nullptr;
    cache.chatTextClass = nullptr;
    cache.chatSerializerClass = nullptr;
    cache.scoreboardClass = nullptr;
    cache.scoreObjectiveClass = nullptr;
    cache.scoreClass = nullptr;
    cache.scorePlayerTeamClass = nullptr;
    cache.netHandlerClass = nullptr;
    cache.networkPlayerInfoClass = nullptr;
    cache.gameProfileClass = nullptr;
    cache.itemStackClass = nullptr;
    cache.itemClass = nullptr;
    cache.itemArmorClass = nullptr;
    cache.itemSwordClass = nullptr;
    cache.inventoryPlayerClass = nullptr;
    cache.enchantmentHelperClass = nullptr;
    cache.abstractClientPlayerClass = nullptr;
    cache.resourceLocationClass = nullptr;
    cache.textureManagerClass = nullptr;
    cache.textureObjectClass = nullptr;
    cache.uuidClass = nullptr;
    cache.renderManagerObject = nullptr;
    cache.timerObject = nullptr;
    cache.modelViewBuffer = nullptr;
    cache.projectionBuffer = nullptr;
    cache.viewportBuffer = nullptr;
}

void GameBindings::release(JNIEnv* const env) noexcept
{
    // Drain an authoritative block reset while the bindings and transformed
    // entry points are still valid.  Stopping the hooks first would clear only
    // native bookkeeping and could leave PlayerControllerMP mid-dig.
    if (env != nullptr &&
        (m_logicalController.requiresDrain() || m_safewalkSneakForced ||
         m_sprintKeyForced ||
         m_aimSensitivityModified)) {
        (void)updateGameplay(env, GameplaySettings{}, m_snapshot, 0U);
    }
    m_freeLookRequested.store(false,std::memory_order_release);
    endFreeLook(env,"detach",true);
    m_freeLookHook.stop();
    deactivateSilentOutput();
    m_attackOwnershipHook.stop();
    m_interactionObserver.stop();
    m_logicalInteractionHook.stop();
    m_logicalJumpHook.stop();
    m_headingHook.stop();
    m_logicalMovementHook.stop();
    m_smartHotbarHook.stop();
    restoreHotbarMovement(env);
    m_itemUseHook.stop();
    m_impulseHook.stop();
    m_velocityHook.stop();
    m_smartHotbarConfig.store(0U,std::memory_order_release);
    m_smartHotbarRequest.store(0,std::memory_order_release);
    m_smartHotbarRefillRequest.store(0,std::memory_order_release);
    m_refillSlot=-1;
    m_silentRotationHook.stop();
    m_logicalController.reset();
    // AgentRuntime guarantees the resolver has joined and all other frame
    // callbacks have drained before this method can destroy published globals.
    m_resolutionPhase.store(ResolutionPhase::Stopped, std::memory_order_release);
    if (env != nullptr && m_cache != nullptr) {
        deleteGlobalRefs(env, *m_cache);
    }
    if (env != nullptr && m_lastWorld != nullptr) {
        env->DeleteWeakGlobalRef(m_lastWorld);
    }
    m_lastWorld = nullptr;
    m_cache.reset();
    m_snapshot = {};
    m_mappingAttempt.store(0U, std::memory_order_relaxed);
    m_retryAtMilliseconds.store(0U, std::memory_order_relaxed);
    m_lastSample = 0U;
    m_entitySampleGeneration = 0U;
    m_lastPlayerScan = 0U;
    m_playerRosterGeneration = 0U;
    m_debugRosterGeneration = 0U;
    m_bedOwnershipGeneration = 0U;
    m_debugBedOwnershipGeneration = 0U;
    m_matchProbe = {};
    m_matchProbeGeneration = 0U;
    m_debugMatchProbeGeneration = 0U;
    m_sidebarCandidateTeam = bedwars::Team::Unknown;
    m_sidebarStableCount = 0U;
    m_sidebarMissingCount = 0U;
    m_matchAnchorValid = false;
    m_lockedOwnBedKnown = false;
    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    m_debugQueue = {};
    m_debugQueueHead = 0U;
    m_debugQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    m_warningQueue = {};
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
    m_bedRescanRequested.store(false, std::memory_order_relaxed);
    ::AcquireSRWLockExclusive(&m_bedCacheLock);
    m_publishedBedCache = {};
    ::ReleaseSRWLockExclusive(&m_bedCacheLock);
    if (env != nullptr && m_lwjglMouseClass != nullptr) {
        env->DeleteGlobalRef(m_lwjglMouseClass);
    }
    m_lwjglMouseClass = nullptr;
    m_lwjglIsButtonDown = nullptr;
    m_lwjglSetGrabbed = nullptr;
    m_lwjglIsGrabbed = nullptr;
    if (env != nullptr && m_lwjglKeyboardClass != nullptr) {
        env->DeleteGlobalRef(m_lwjglKeyboardClass);
    }
    m_lwjglKeyboardClass = nullptr;
    m_lwjglIsKeyDown = nullptr;
    m_overlayInputSessionActive = false;
    m_inputGrabStateKnown = false;
    m_inputWasGrabbed = true;
    m_safewalkSneakForced = false;
    m_sprintKeyForced = false;
    m_sprintKeyCode = 0;
    m_safewalkSneakKeyCode = 0;
    m_safewalkSupportMask = 0U;
    m_safewalkReleaseAt = 0U;
    m_nextFreeLookHookAttemptTick = 0U;
    m_freeLookHookAttemptCount=0U;
    m_freeLookHookRetryLatched=false;
    m_freeLookObservationInitialized=false;
    m_nextAttackOwnershipHookAttemptTick = 0U;
    m_lastScaffoldPlacementTick = 0U;
    m_lastGameplayTick = 0U;
    m_lastBedBreakerTick = 0U;
    m_bedBreakerTargetValid = false;
    m_originalMouseSensitivity = 0.5F;
    m_aimSensitivityModified = false;
    m_freeLookDiagnostics.stop("release");
}

void GameBindings::abandon() noexcept
{
    m_freeLookRequested.store(false,std::memory_order_relaxed);
    endFreeLook(nullptr,"detach-no-jni",true);
    m_freeLookHook.abandon();
    m_freeLookEntity=nullptr;
    m_freeLookPerspectiveSaved=false;
    m_freeLookActive=false;
    m_interactionObserver.abandon();
    m_attackOwnershipHook.abandon();
    m_logicalInteractionHook.abandon();
    m_logicalJumpHook.abandon();
    m_headingHook.abandon();
    m_logicalMovementHook.abandon();
    m_sprintFeatureEnabled.store(false,std::memory_order_release);
    m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    m_smartHotbarHook.abandon();
    m_itemUseHook.abandon();
    m_impulseHook.abandon();
    m_velocityHook.abandon();
    m_smartHotbarConfig.store(0U,std::memory_order_release);
    m_silentRotationHook.abandon();
    m_logicalController.reset();
    // Used only when the JVM is already shutting down and no JNIEnv can be
    // obtained. The VM owns and releases its reference table at process exit.
    m_resolutionPhase.store(ResolutionPhase::Stopped, std::memory_order_release);
    m_cache.reset();
    m_lastWorld = nullptr;
    m_matchAnchorValid = false;
    m_lockedOwnBedKnown = false;
    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    m_warningQueue = {};
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
    m_lwjglMouseClass = nullptr;
    m_lwjglSetGrabbed = nullptr;
    m_lwjglIsGrabbed = nullptr;
    m_lwjglKeyboardClass = nullptr;
    m_lwjglIsKeyDown = nullptr;
    m_overlayInputSessionActive = false;
    m_inputGrabStateKnown = false;
    m_inputWasGrabbed = true;
    m_safewalkSneakForced = false;
    m_safewalkSneakKeyCode = 0;
    m_safewalkSupportMask = 0U;
    m_safewalkReleaseAt = 0U;
    m_nextFreeLookHookAttemptTick = 0U;
    m_freeLookHookAttemptCount=0U;
    m_freeLookHookRetryLatched=false;
    m_freeLookObservationInitialized=false;
    m_nextAttackOwnershipHookAttemptTick = 0U;
    m_lastScaffoldPlacementTick = 0U;
    m_lastGameplayTick = 0U;
    m_lastBedBreakerTick = 0U;
    m_bedBreakerTargetValid = false;
    m_originalMouseSensitivity = 0.5F;
    m_aimSensitivityModified = false;
    m_freeLookDiagnostics.stop("abandon");
}

} // namespace mcoverlay

