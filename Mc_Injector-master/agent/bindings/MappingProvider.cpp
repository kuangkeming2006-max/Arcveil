#include "MappingProvider.h"
#include "MappingPack.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <utility>

namespace mcoverlay::bindings {
namespace {

constexpr std::size_t kMaxProviders = 32U;
constexpr std::size_t kMaxDictionariesPerProvider = 16U;
constexpr std::size_t kMaxDetectionPatterns = 24U;
constexpr std::size_t kMaxIdentifierLength = 64U;
constexpr std::size_t kMaxLabelLength = 128U;
constexpr std::size_t kMaxSymbolLength = 512U;

void assignError(std::string* const error, const std::string_view message) noexcept
{
    if (error == nullptr) {
        return;
    }
    try {
        error->assign(message);
    } catch (...) {
        error->clear();
    }
}

[[nodiscard]] bool validIdentifier(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > kMaxIdentifierLength) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) noexcept {
        const unsigned char byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '.' ||
               character == '_' || character == '-';
    });
}

[[nodiscard]] bool validBinaryName(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > kMaxSymbolLength ||
        value.front() == '.' || value.back() == '.') {
        return false;
    }
    for (const char character : value) {
        if (character == '/' || character == ';' || character == '[' ||
            character == '(' || character == ')' || character == '\0') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validMemberName(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > kMaxSymbolLength) {
        return false;
    }
    for (const char character : value) {
        if (character == '.' || character == '/' || character == ';' ||
            character == '[' || character == '(' || character == ')' ||
            character == '<' || character == '>' || character == '\0') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool signatureMatchesBinaryName(const std::string_view binaryName,
                                              const std::string_view signature) noexcept
{
    if (!validBinaryName(binaryName) || signature.size() != binaryName.size() + 2U ||
        signature.front() != 'L' || signature.back() != ';') {
        return false;
    }
    for (std::size_t index = 0U; index < binaryName.size(); ++index) {
        const char expected = binaryName[index] == '.' ? '/' : binaryName[index];
        if (signature[index + 1U] != expected) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool containsCaseInsensitive(const std::string_view haystack,
                                           const std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > haystack.size()) {
        return false;
    }
    const auto equal = [](const char left, const char right) noexcept {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
    };
    return std::search(haystack.begin(), haystack.end(),
                       needle.begin(), needle.end(), equal) != haystack.end();
}

[[nodiscard]] bool matchesPattern(const DetectionPattern& pattern,
                                  const std::string_view input,
                                  const bool launchHint) noexcept
{
    switch (pattern.match) {
    case DetectionMatch::ExactClassSignature:
        return !launchHint && input == pattern.value;
    case DetectionMatch::ClassSignaturePrefix:
        return !launchHint && input.starts_with(pattern.value);
    case DetectionMatch::LaunchHintContains:
        return launchHint && containsCaseInsensitive(input, pattern.value);
    }
    return false;
}

class OwnedMappingProvider final : public MappingProvider {
public:
    OwnedMappingProvider(std::string id,
                         const ClientFamily family,
                         const int priority,
                         std::vector<DetectionPattern> detection,
                         std::vector<MappingDictionary> dictionaries)
        : m_id(std::move(id)),
          m_family(family),
          m_priority(priority),
          m_detection(std::move(detection)),
          m_dictionaries(std::move(dictionaries))
    {
    }

    [[nodiscard]] std::string_view id() const noexcept override { return m_id; }
    [[nodiscard]] ClientFamily family() const noexcept override { return m_family; }
    [[nodiscard]] int priority() const noexcept override { return m_priority; }
    [[nodiscard]] std::span<const MappingDictionary> dictionaries() const noexcept override
    {
        return m_dictionaries;
    }

    void observeClassSignature(const std::string_view signature,
                               ClientEnvironment& environment) const noexcept override
    {
        observe(signature, false, environment);
    }

    void observeLaunchHint(const std::string_view hint,
                           ClientEnvironment& environment) const noexcept override
    {
        observe(hint, true, environment);
    }

private:
    void observe(const std::string_view value,
                 const bool launchHint,
                 ClientEnvironment& environment) const noexcept
    {
        for (const DetectionPattern& pattern : m_detection) {
            if (matchesPattern(pattern, value, launchHint)) {
                environment.addEvidence(m_family, pattern.confidence);
            }
        }
        for (const MappingDictionary& dictionary : m_dictionaries) {
            for (const DetectionPattern& pattern : dictionary.detection) {
                if (matchesPattern(pattern, value, launchHint)) {
                    environment.addEvidence(m_family, pattern.confidence);
                }
            }
        }
    }

    std::string m_id;
    ClientFamily m_family = ClientFamily::Unknown;
    int m_priority = 0;
    std::vector<DetectionPattern> m_detection;
    std::vector<MappingDictionary> m_dictionaries;
};

[[nodiscard]] std::unique_ptr<MappingProvider> makeProvider(
    std::string id,
    const ClientFamily family,
    const int priority,
    std::vector<DetectionPattern> detection,
    std::vector<MappingDictionary> dictionaries)
{
    return std::make_unique<OwnedMappingProvider>(
        std::move(id), family, priority,
        std::move(detection), std::move(dictionaries));
}

} // namespace

const char* clientFamilyName(const ClientFamily family) noexcept
{
    switch (family) {
    case ClientFamily::Vanilla: return "Vanilla";
    case ClientFamily::Forge: return "Forge";
    case ClientFamily::Lunar: return "Lunar";
    case ClientFamily::Unknown: return "Unknown";
    }
    return "Unknown";
}

void ClientEnvironment::addEvidence(const ClientFamily family,
                                    const std::uint16_t confidenceValue) noexcept
{
    const std::size_t index = static_cast<std::size_t>(family);
    if (family == ClientFamily::Unknown || index >= m_confidence.size()) {
        return;
    }
    m_confidence[index] = std::max(m_confidence[index], confidenceValue);
}

ClientFamily ClientEnvironment::family() const noexcept
{
    ClientFamily result = ClientFamily::Unknown;
    std::uint16_t strongest = 0U;
    // Deliberate tie order: a positively detected transformed client must not
    // silently fall back to Forge merely because it shares a named anchor.
    for (const ClientFamily candidate :
         {ClientFamily::Vanilla, ClientFamily::Forge, ClientFamily::Lunar}) {
        const std::uint16_t value = confidence(candidate);
        if (value >= strongest && value != 0U) {
            strongest = value;
            result = candidate;
        }
    }
    return result;
}

std::uint16_t ClientEnvironment::confidence(const ClientFamily family) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(family);
    return index < m_confidence.size() ? m_confidence[index] : 0U;
}

bool MappingDictionary::validate(std::string* const error) const noexcept
{
    auto reject = [&](const std::string_view message) noexcept {
        assignError(error, message);
        return false;
    };
    if (!validIdentifier(id)) return reject("mapping id must be 1-64 ASCII identifier characters");
    if (label.empty() || label.size() > kMaxLabelLength) return reject("mapping label is empty or too long");
    if (family == ClientFamily::Unknown) return reject("mapping client family is unknown");
    if (detection.size() > kMaxDetectionPatterns) return reject("too many mapping detection patterns");
    for (const DetectionPattern& pattern : detection) {
        if (pattern.value.empty() || pattern.value.size() > kMaxSymbolLength ||
            pattern.confidence == 0U) {
            return reject("invalid mapping detection pattern");
        }
        if (pattern.match != DetectionMatch::LaunchHintContains && pattern.value.front() != 'L') {
            return reject("class detection patterns must use JVM object signatures");
        }
    }

    // Core classes define whether the client profile itself is usable.  BedWars
    // sidebar/armor support is an optional capability and must never turn an
    // otherwise valid Minecraft profile into "unsupported client mappings".
    const std::array<std::pair<std::string_view, std::string_view>, 18U> coreClasses{{
        {minecraftName, minecraftSignature}, {playerName, playerSignature},
        {livingName, livingSignature}, {entityName, entitySignature},
        {aabbName, aabbSignature}, {worldName, worldSignature},
        {worldClientName, worldClientSignature}, {stateName, stateSignature},
        {blockName, blockSignature}, {blockPosName, blockPosSignature},
        {bedName, bedSignature}, {chunkProviderName, chunkProviderSignature},
        {chunkName, chunkSignature}, {storageName, storageSignature},
        {activeRenderInfoName, activeRenderInfoSignature},
        {renderManagerName, renderManagerSignature},
        {timerName, timerSignature}, {chatComponentName, chatComponentSignature}}};
    for (const auto& [binaryName, signature] : coreClasses) {
        if (!signatureMatchesBinaryName(binaryName, signature)) {
            return reject("core class binary name and JNI signature do not match");
        }
    }

    const std::array<std::pair<std::string_view, std::string_view>, 29U> featureClasses{{
        {rayHitName, rayHitSignature},
        {movementPacketName, movementPacketSignature},
        {hostileName, hostileSignature},
        {scoreboardName, scoreboardSignature}, {scoreObjectiveName, scoreObjectiveSignature},
        {scoreName, scoreSignature}, {scorePlayerTeamName, scorePlayerTeamSignature},
        {netHandlerName, netHandlerSignature},
        {networkPlayerInfoName, networkPlayerInfoSignature},
        {itemStackName, itemStackSignature}, {itemName, itemSignature},
        {itemArmorName, itemArmorSignature}, {inventoryPlayerName, inventoryPlayerSignature},
        {enchantmentHelperName, enchantmentHelperSignature},
        {chatTextName, chatTextSignature},
        {chatSerializerName, chatSerializerSignature},
        {abstractClientPlayerName, abstractClientPlayerSignature},
        {resourceLocationName, resourceLocationSignature},
        {textureManagerName, textureManagerSignature},
        {textureObjectName, textureObjectSignature},
        {gameSettingsName, gameSettingsSignature},
        {entityRendererName, entityRendererSignature},
        {renderGlobalName, renderGlobalSignature},
        {keyBindingName, keyBindingSignature},
        {playerControllerName, playerControllerSignature},
        {serverDataName, serverDataSignature},
        {itemBlockName, itemBlockSignature},
        {enumFacingName, enumFacingSignature},
        {vec3Name, vec3Signature}}};
    for (const auto& [binaryName, signature] : featureClasses) {
        // A feature class may be omitted by an external/core-only mapping pack,
        // but a partially specified pair is still malformed.
        if (binaryName.empty() && signature.empty()) continue;
        if (binaryName.empty() || signature.empty() ||
            !signatureMatchesBinaryName(binaryName, signature)) {
            return reject("optional feature class binary name and JNI signature do not match");
        }
    }
    if (chunkProviderInterfaceSignature.size() < 3U ||
        chunkProviderInterfaceSignature.front() != 'L' ||
        chunkProviderInterfaceSignature.back() != ';') {
        return reject("invalid chunk-provider interface JNI signature");
    }
    if (!teamSignature.empty() &&
        (teamSignature.size() < 3U || teamSignature.front() != 'L' ||
         teamSignature.back() != ';')) {
        return reject("invalid Team JNI signature");
    }
    if (!packetBufferSignature.empty() &&
        (packetBufferSignature.size() < 3U || packetBufferSignature.front() != 'L' ||
         packetBufferSignature.back() != ';')) {
        return reject("invalid PacketBuffer JNI signature");
    }
    if (entityPlayerSignature.size() < 3U ||
        entityPlayerSignature.front() != 'L' ||
        entityPlayerSignature.back() != ';') {
        return reject("invalid EntityPlayer JNI signature");
    }

    if ((getMinecraft.empty() && minecraftInstanceField.empty()) ||
        (!getMinecraft.empty() && !minecraftInstanceField.empty())) {
        return reject("mapping must define exactly one Minecraft singleton accessor");
    }
    if ((getLoadedEntities.empty() && loadedEntitiesField.empty()) ||
        (!getLoadedEntities.empty() && !loadedEntitiesField.empty())) {
        return reject("mapping must define exactly one loaded-entity accessor");
    }
    if (!guiScreenSignature.empty() && (guiScreenSignature.size() < 3U ||
        guiScreenSignature.front() != 'L' || guiScreenSignature.back() != ';'))
        return reject("invalid GuiScreen JNI signature");
    const auto members = std::to_array<std::string_view>({
        clickMouse,
        sendClickBlock, moveFlying, moveEntityWithHeading, getAIMoveSpeed,
        isSprinting, setSprinting, swingItem,
        rayBlockPosField, raySideHitField,
        blockPosCoordinateMethods[0], blockPosCoordinateMethods[1],
        blockPosCoordinateMethods[2], facingIndexMethod,
        rayTraceBlocks, getEntityById, getItemUseDuration, isUsingItem,
        getEyeHeight, hitVectorField,
        vectorFields[0], vectorFields[1], vectorFields[2],
        playerField, getHealth, hurtTimeField, getMaxHealth, getEntityId,
        getBounds, isMainThread, isSingleplayer, worldField, getLoadedEntities,
        playerEntitiesField, getName, isInvisible, getDisplayName, getFormattedText,
        addChatMessage, parseChatJson,
        getBlockState, getBlock, getBlockMetadata, setIngameFocus,
        setIngameNotInFocus, getChunkProvider, chunkListingField,
        getStorageArrays, getStorageData, getRenderManager,
        activeModelViewField, activeProjectionField, activeViewportField,
        timerField, renderPartialTicksField, minecraftInstanceField,
        loadedEntitiesField, getScoreboard, getObjectiveInDisplaySlot,
        getPlayersTeam, getSortedScores, getPlayerName, formatPlayerName,
        getNetHandler, getPlayerInfoMap, getGameProfile,
        inventoryField, armorInventoryField,
        getItem, hasColor, getColor, getEnchantmentLevel,
        getEquipmentInSlot, getIdFromItem, stackSizeField, getItemDamage,
        getUniqueId, getLocationSkin, getTextureManager, getTexture,
        getGlTextureId, gameSettingsField, thirdPersonViewField,
        updateCameraAndRender, orientCamera, setAngles, setupTerrain,
        keyBindSneakField, getKeyCode,
        setKeyBindState, mouseSensitivityField, rotationYawField,
        rotationPitchField, previousRotationYawField, previousRotationPitchField,
        writeMovementPacket, packetYawField, packetPitchField,
        packetRotatingField,
        onGroundField, jump, isAirBlock,
        currentScreenField, getCollidingBoundingBoxes,
        getCurrentServerData, serverIpField, playerControllerField, attackEntity,
        currentItemField, mainInventoryField, getBlockFromItem,
        getIdFromBlock, getFacingByIndex, onPlayerRightClick});
    for (const std::string_view member : members) {
        if (!member.empty() && !validMemberName(member))
            return reject("invalid method or field name");
    }
    for (const std::string& member : positionFields) {
        if (!validMemberName(member)) return reject("invalid position field name");
    }
    for (const std::string& member : previousPositionFields) {
        if (!validMemberName(member)) return reject("invalid previous-position field name");
    }
    if(!setupTerrainDescriptor.empty()&&
       (setupTerrainDescriptor.front()!='('||
        setupTerrainDescriptor.find(')')==std::string::npos))
        return reject("invalid setupTerrain descriptor");
    for (const std::string& member : movementInputFields) {
        if (!member.empty() && !validMemberName(member))
            return reject("invalid movement-input field name");
    }
    for (const std::string& member : chunkCoordinateFields) {
        if (!validMemberName(member)) return reject("invalid chunk coordinate field name");
    }
    for (const std::string& member : renderPositionFields) {
        if (!validMemberName(member)) return reject("invalid render-position field name");
    }
    for (const std::string& member : movementKeyFields) {
        if (!member.empty() && !validMemberName(member))
            return reject("invalid movement-key field name");
    }
    if(!keyBindSprintField.empty()&&!validMemberName(keyBindSprintField))
        return reject("invalid sprint-key field name");
    for (const std::string& member : motionFields) {
        if (!member.empty() && !validMemberName(member))
            return reject("invalid motion field name");
    }
    for (const std::string& member : aabbFields) {
        if (!validMemberName(member)) return reject("invalid AABB field name");
    }
    for(const auto* names:{&cameraMouseOverCandidates,&cameraHitEntityCandidates,
        &cameraHitTypeCandidates,&entityTicksCandidates,&isSneakingCandidates,
        &diggingPositionCandidates,&diggingActionCandidates})
        for(const auto& name:*names)if(!name.empty()&&!validMemberName(name))
            return reject("invalid resolver alternative");
    if(!diggingPacketName.empty()&&!validBinaryName(diggingPacketName))
        return reject("invalid digging packet class");
    if(!hitTypeSignature.empty()&&(hitTypeSignature.size()<3||hitTypeSignature.front()!='L'||hitTypeSignature.back()!=';'))
        return reject("invalid hit type signature");
    if(!diggingActionSignature.empty()&&(diggingActionSignature.size()<5||!diggingActionSignature.starts_with("()L")||diggingActionSignature.back()!=';'))
        return reject("invalid digging action signature");
    assignError(error, {});
    return true;
}

MappingRegistry::MappingRegistry() noexcept
{
    try {
        auto pack=loadMappingPack(defaultMappingPackPath());
        for(auto& p:pack.providers) {
            if(registerProvider(makeProvider(std::move(p.id),p.family,p.priority,
                std::move(p.detection),std::move(p.dictionaries)))!=MappingRegistrationResult::Accepted)
                throw std::runtime_error("mapping pack registration failed");
        }
    } catch (...) {m_healthy=false;m_providers.clear();}
}

MappingRegistry::MappingRegistry(const std::filesystem::path& packFile) noexcept
{
    try {
        auto pack=loadMappingPack(packFile);
        for(auto& p:pack.providers) {
            if(registerProvider(makeProvider(std::move(p.id),p.family,p.priority,
                std::move(p.detection),std::move(p.dictionaries)))!=MappingRegistrationResult::Accepted)
                throw std::runtime_error("mapping pack registration failed");
        }
    } catch (...) {m_healthy=false;m_providers.clear();}
}

MappingRegistry::~MappingRegistry() = default;

void MappingRegistry::setError(std::string* const error,
                               const std::string_view message) const noexcept
{
    assignError(error, message);
}

bool MappingRegistry::containsDictionaryId(const std::string_view id) const noexcept
{
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        if (provider != nullptr) {
            if (provider->id() == id) return true;
            for (const MappingDictionary& dictionary : provider->dictionaries()) {
                if (dictionary.id == id) return true;
            }
        }
    }
    return false;
}

MappingRegistrationResult MappingRegistry::registerDictionary(
    MappingDictionary dictionary, std::string* const error) noexcept
{
    std::string validationError;
    if (!dictionary.validate(&validationError)) {
        setError(error, validationError);
        return MappingRegistrationResult::Invalid;
    }
    try {
        const std::string providerId = std::string("external-") + dictionary.id;
        const ClientFamily family = dictionary.family;
        std::vector<MappingDictionary> dictionaries;
        dictionaries.push_back(std::move(dictionary));
        return registerProvider(makeProvider(providerId, family, 500,
                                             {}, std::move(dictionaries)), error);
    } catch (...) {
        setError(error, "out of memory while registering mapping dictionary");
        return MappingRegistrationResult::OutOfMemory;
    }
}

MappingRegistrationResult MappingRegistry::registerProvider(
    std::unique_ptr<MappingProvider> provider, std::string* const error) noexcept
{
    if (provider == nullptr || !validIdentifier(provider->id()) ||
        provider->family() == ClientFamily::Unknown) {
        setError(error, "invalid mapping provider metadata");
        return MappingRegistrationResult::Invalid;
    }
    const std::span<const MappingDictionary> dictionaries = provider->dictionaries();
    if (dictionaries.size() > kMaxDictionariesPerProvider) {
        setError(error, "mapping provider dictionary capacity exceeded");
        return MappingRegistrationResult::CapacityExceeded;
    }
    for (const MappingDictionary& dictionary : dictionaries) {
        std::string validationError;
        if (dictionary.family != provider->family() || !dictionary.validate(&validationError)) {
            setError(error, validationError.empty()
                ? "mapping dictionary family does not match provider" : validationError);
            return MappingRegistrationResult::Invalid;
        }
    }

    std::lock_guard lock(m_mutex);
    if (m_frozen) {
        setError(error, "mapping registry is already frozen");
        return MappingRegistrationResult::Frozen;
    }
    if (m_providers.size() >= kMaxProviders) {
        setError(error, "mapping provider capacity exceeded");
        return MappingRegistrationResult::CapacityExceeded;
    }
    if (containsDictionaryId(provider->id())) {
        setError(error, "duplicate mapping provider id");
        return MappingRegistrationResult::Duplicate;
    }
    for (const MappingDictionary& dictionary : dictionaries) {
        if (containsDictionaryId(dictionary.id)) {
            setError(error, "duplicate mapping dictionary id");
            return MappingRegistrationResult::Duplicate;
        }
    }
    try {
        m_providers.push_back(std::move(provider));
    } catch (...) {
        setError(error, "out of memory while storing mapping provider");
        return MappingRegistrationResult::OutOfMemory;
    }
    setError(error, {});
    return MappingRegistrationResult::Accepted;
}

bool MappingRegistry::freeze() noexcept
{
    std::lock_guard lock(m_mutex);
    if (!m_healthy) {
        return false;
    }
    if (!m_frozen) {
        std::sort(m_providers.begin(), m_providers.end(),
                  [](const std::unique_ptr<MappingProvider>& left,
                     const std::unique_ptr<MappingProvider>& right) noexcept {
            return left->priority() > right->priority();
        });
        m_frozen = true;
    }
    return true;
}

bool MappingRegistry::healthy() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_healthy;
}

void MappingRegistry::observeClassSignature(const std::string_view signature,
                                            ClientEnvironment& environment) const noexcept
{
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        provider->observeClassSignature(signature, environment);
    }

    // An unambiguous anchor is weak evidence. If multiple client families use
    // the same anchor, launch/class markers decide; with no markers the
    // resolver safely tries all exact-anchor candidates once.
    ClientFamily anchorFamily = ClientFamily::Unknown;
    bool ambiguous = false;
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        for (const MappingDictionary& dictionary : provider->dictionaries()) {
            if (dictionary.minecraftSignature == signature) {
                if (anchorFamily == ClientFamily::Unknown) {
                    anchorFamily = provider->family();
                } else if (anchorFamily != provider->family()) {
                    ambiguous = true;
                }
            }
        }
    }
    if (!ambiguous && anchorFamily != ClientFamily::Unknown) {
        environment.addEvidence(anchorFamily, 50U);
    }
}

void MappingRegistry::observeLaunchHint(const std::string_view hint,
                                        ClientEnvironment& environment) const noexcept
{
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        provider->observeLaunchHint(hint, environment);
    }
}

bool MappingRegistry::hasMappingsForFamily(const ClientFamily family) const noexcept
{
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        if (provider->family() == family && !provider->dictionaries().empty()) {
            return true;
        }
    }
    return false;
}

bool MappingRegistry::isMinecraftAnchor(const std::string_view signature) const noexcept
{
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        for (const MappingDictionary& dictionary : provider->dictionaries()) {
            if (dictionary.minecraftSignature == signature) return true;
        }
    }
    return false;
}

MappingCandidates MappingRegistry::candidatesForAnchor(
    const std::string_view signature,
    const ClientEnvironment& environment) const noexcept
{
    MappingCandidates result;
    const ClientFamily selectedFamily = environment.family();
    for (const std::unique_ptr<MappingProvider>& provider : m_providers) {
        if (selectedFamily != ClientFamily::Unknown && provider->family() != selectedFamily) {
            continue;
        }
        for (const MappingDictionary& dictionary : provider->dictionaries()) {
            if (dictionary.minecraftSignature != signature) continue;
            if (result.count < result.items.size()) {
                result.items[result.count++] = &dictionary;
            } else {
                result.truncated = true;
            }
        }
    }
    return result;
}

} // namespace mcoverlay::bindings
