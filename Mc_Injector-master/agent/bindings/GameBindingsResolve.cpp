#include "GameBindingsCache.internal.h"
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

bindings::MappingRegistrationResult GameBindings::registerMappingDictionary(
    bindings::MappingDictionary dictionary, std::string* const error) noexcept
{
    return m_mappingRegistry.registerDictionary(std::move(dictionary), error);
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


} // namespace mcoverlay
