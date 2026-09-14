#include "MappingProvider.h"

#include <cstdio>
#include <string>
#include <utility>

namespace {

using namespace mcoverlay::bindings;

[[nodiscard]] MappingDictionary testLunarDictionary()
{
    MappingDictionary mapping;
    mapping.id = "test-lunar-1.8.9";
    mapping.label = "Test Lunar mapping";
    mapping.family = ClientFamily::Lunar;
    mapping.detection = {
        {DetectionMatch::LaunchHintContains, ".lunarclient", 250U}};
    mapping.minecraftName = "example.lunar.Minecraft";
    mapping.minecraftSignature = "Lexample/lunar/Minecraft;";
    mapping.playerName = "example.lunar.Player";
    mapping.playerSignature = "Lexample/lunar/Player;";
    mapping.livingName = "example.lunar.Living";
    mapping.livingSignature = "Lexample/lunar/Living;";
    mapping.entityName = "example.lunar.Entity";
    mapping.entitySignature = "Lexample/lunar/Entity;";
    mapping.entityPlayerSignature = "Lexample/lunar/Player;";
    mapping.aabbName = "example.lunar.Box";
    mapping.aabbSignature = "Lexample/lunar/Box;";
    mapping.worldName = "example.lunar.World";
    mapping.worldSignature = "Lexample/lunar/World;";
    mapping.worldClientName = "example.lunar.WorldClient";
    mapping.worldClientSignature = "Lexample/lunar/WorldClient;";
    mapping.stateName = "example.lunar.State";
    mapping.stateSignature = "Lexample/lunar/State;";
    mapping.blockName = "example.lunar.Block";
    mapping.blockSignature = "Lexample/lunar/Block;";
    mapping.blockPosName = "example.lunar.BlockPos";
    mapping.blockPosSignature = "Lexample/lunar/BlockPos;";
    mapping.bedName = "example.lunar.Bed";
    mapping.bedSignature = "Lexample/lunar/Bed;";
    mapping.chunkProviderName = "example.lunar.ChunkProvider";
    mapping.chunkProviderSignature = "Lexample/lunar/ChunkProvider;";
    mapping.chunkProviderInterfaceSignature = "Lexample/lunar/IChunkProvider;";
    mapping.chunkName = "example.lunar.Chunk";
    mapping.chunkSignature = "Lexample/lunar/Chunk;";
    mapping.storageName = "example.lunar.Storage";
    mapping.storageSignature = "Lexample/lunar/Storage;";
    mapping.activeRenderInfoName = "example.lunar.ActiveRenderInfo";
    mapping.activeRenderInfoSignature = "Lexample/lunar/ActiveRenderInfo;";
    mapping.renderManagerName = "example.lunar.RenderManager";
    mapping.renderManagerSignature = "Lexample/lunar/RenderManager;";
    mapping.timerName = "example.lunar.Timer";
    mapping.timerSignature = "Lexample/lunar/Timer;";
    mapping.chatComponentName = "example.lunar.Chat";
    mapping.chatComponentSignature = "Lexample/lunar/Chat;";
    mapping.getMinecraft = "instance";
    mapping.playerField = "player";
    mapping.getHealth = "health";
    mapping.getMaxHealth = "maxHealth";
    mapping.getEntityId = "entityId";
    mapping.getBounds = "bounds";
    mapping.isMainThread = "isMainThread";
    mapping.isSingleplayer = "isSingleplayer";
    mapping.worldField = "world";
    mapping.positionFields = {"x", "y", "z"};
    mapping.previousPositionFields = {"previousX", "previousY", "previousZ"};
    mapping.getLoadedEntities = "loadedEntities";
    mapping.playerEntitiesField = "players";
    mapping.getName = "name";
    mapping.getDisplayName = "displayName";
    mapping.getFormattedText = "formattedText";
    mapping.getBlockState = "blockState";
    mapping.getBlock = "block";
    mapping.getBlockMetadata = "metadata";
    mapping.setIngameFocus = "focus";
    mapping.setIngameNotInFocus = "unfocus";
    mapping.getChunkProvider = "chunkProvider";
    mapping.chunkListingField = "chunks";
    mapping.chunkCoordinateFields = {"chunkX", "chunkZ"};
    mapping.getStorageArrays = "sections";
    mapping.getStorageData = "data";
    mapping.getRenderManager = "renderManager";
    mapping.renderPositionFields = {"renderX", "renderY", "renderZ"};
    mapping.activeModelViewField = "modelView";
    mapping.activeProjectionField = "projection";
    mapping.activeViewportField = "viewport";
    mapping.timerField = "timer";
    mapping.renderPartialTicksField = "partialTicks";
    mapping.aabbFields = {"minX", "minY", "minZ", "maxX", "maxY", "maxZ"};
    return mapping;
}

[[nodiscard]] bool expect(const bool condition, const char* const message)
{
    if (!condition) std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

} // namespace

int main()
{
    using namespace mcoverlay::bindings;
    bool passed = true;

    MappingRegistry builtins;
    passed &= expect(builtins.healthy(), "builtin registry is healthy");
    passed &= expect(builtins.freeze(), "builtin registry freezes");
    ClientEnvironment forge;
    builtins.observeClassSignature("Lnet/minecraftforge/common/MinecraftForge;", forge);
    builtins.observeClassSignature("Lnet/minecraft/client/Minecraft;", forge);
    const MappingCandidates forgeCandidates = builtins.candidatesForAnchor(
        "Lnet/minecraft/client/Minecraft;", forge);
    passed &= expect(forge.family() == ClientFamily::Forge, "Forge environment detection");
    passed &= expect(forgeCandidates.count == 1U, "Forge provider selection");
    passed &= expect(forgeCandidates.items[0] &&
        forgeCandidates.items[0]->previousRotationYawField == "field_70126_B" &&
        forgeCandidates.items[0]->previousRotationPitchField == "field_70127_C",
        "Forge current/previous camera rotations mapped independently");
    passed &= expect(forgeCandidates.items[0] &&
        forgeCandidates.items[0]->clickMouse == "func_147116_af" &&
        forgeCandidates.items[0]->sendClickBlock == "func_147115_a" &&
        forgeCandidates.items[0]->moveFlying == "func_70060_a" &&
        forgeCandidates.items[0]->isSprinting == "func_70051_ag" &&
        forgeCandidates.items[0]->setSprinting == "func_70031_b" &&
        forgeCandidates.items[0]->entityRendererName ==
            "net.minecraft.client.renderer.EntityRenderer" &&
        forgeCandidates.items[0]->updateCameraAndRender == "func_181560_a" &&
        forgeCandidates.items[0]->orientCamera == "func_78467_g" &&
        forgeCandidates.items[0]->setAngles == "func_70082_c" &&
        forgeCandidates.items[0]->renderGlobalName ==
            "net.minecraft.client.renderer.RenderGlobal" &&
        forgeCandidates.items[0]->setupTerrain == "func_174970_a" &&
        forgeCandidates.items[0]->setupTerrainDescriptor ==
            "(Lnet/minecraft/entity/Entity;DLnet/minecraft/client/renderer/culling/ICamera;IZ)V" &&
        forgeCandidates.items[0]->thirdPersonViewField == "field_74320_O" &&
        forgeCandidates.items[0]->movementInputFields[0] == "field_70702_br" &&
        forgeCandidates.items[0]->movementInputFields[1] == "field_70701_bs" &&
        forgeCandidates.items[0]->jump == "func_70664_aZ" &&
        forgeCandidates.items[0]->rayBlockPosField == "field_178783_e" &&
        forgeCandidates.items[0]->raySideHitField == "field_178784_b" &&
        forgeCandidates.items[0]->rayTraceBlocks == "func_147447_a",
        "Forge authoritative movement, interaction and world ray mappings");
    passed &= expect(forgeCandidates.items[0] &&
        forgeCandidates.items[0]->movementPacketName ==
            "net.minecraft.network.play.client.C03PacketPlayer" &&
        forgeCandidates.items[0]->writeMovementPacket == "func_148840_b" &&
        forgeCandidates.items[0]->packetYawField == "field_149476_e" &&
        forgeCandidates.items[0]->packetPitchField == "field_149473_f" &&
        forgeCandidates.items[0]->packetRotatingField == "field_149481_i",
        "Forge silent output maps only the outgoing movement packet");

    MappingRegistry lunarBuiltins;
    ClientEnvironment lunar;
    lunarBuiltins.observeLaunchHint(
        "C:/Users/test/.lunarclient/jre/bin/javaw.exe", lunar);
    lunarBuiltins.observeClassSignature("Lave;", lunar);
    const MappingCandidates lunarCandidates = lunarBuiltins.candidatesForAnchor(
        "Lave;", lunar);
    passed &= expect(lunar.family() == ClientFamily::Lunar, "Lunar launch-hint detection");
    passed &= expect(lunarBuiltins.hasMappingsForFamily(ClientFamily::Lunar),
                     "Lunar legacy namespace is registered");
    passed &= expect(lunarCandidates.count == 1U,
                      "Lunar legacy provider reuses the verified Notch namespace");
    passed &= expect(lunarCandidates.items[0] &&
        lunarCandidates.items[0]->previousRotationYawField == "A" &&
        lunarCandidates.items[0]->previousRotationPitchField == "B",
        "Notch previous camera rotations verified against MCP SRG");
    passed &= expect(lunarCandidates.items[0] &&
        lunarCandidates.items[0]->clickMouse == "aw" &&
        lunarCandidates.items[0]->sendClickBlock == "b" &&
        lunarCandidates.items[0]->moveFlying == "a" &&
        lunarCandidates.items[0]->isSprinting == "aw" &&
        lunarCandidates.items[0]->setSprinting == "d" &&
        lunarCandidates.items[0]->entityRendererName == "bfk" &&
        lunarCandidates.items[0]->updateCameraAndRender == "a" &&
        lunarCandidates.items[0]->orientCamera == "f" &&
        lunarCandidates.items[0]->setAngles == "c" &&
        lunarCandidates.items[0]->renderGlobalName == "bfr" &&
        lunarCandidates.items[0]->setupTerrain == "a" &&
        lunarCandidates.items[0]->setupTerrainDescriptor ==
            "(Lpk;DLbia;IZ)V" &&
        lunarCandidates.items[0]->thirdPersonViewField == "aB" &&
        lunarCandidates.items[0]->movementInputFields[0] == "aZ" &&
        lunarCandidates.items[0]->movementInputFields[1] == "ba" &&
        lunarCandidates.items[0]->jump == "bF" &&
        lunarCandidates.items[0]->swingItem == "bw" &&
        lunarCandidates.items[0]->blockPosCoordinateMethods[0] == "n" &&
        lunarCandidates.items[0]->facingIndexMethod == "a" &&
        lunarCandidates.items[0]->rayHitName == "auh" &&
        lunarCandidates.items[0]->getItemUseDuration == "bT" &&
        lunarCandidates.items[0]->getEyeHeight == "aS",
        "Notch click, hit result and live bow charge mappings");
    passed &= expect(lunarCandidates.items[0] &&
        lunarCandidates.items[0]->movementPacketName == "ip" &&
        lunarCandidates.items[0]->writeMovementPacket == "b" &&
        lunarCandidates.items[0]->packetYawField == "d" &&
        lunarCandidates.items[0]->packetPitchField == "e" &&
        lunarCandidates.items[0]->packetRotatingField == "h",
        "Notch silent output packet mappings match MCP 1.8.9 SRG");
    passed &= expect(lunarCandidates.items[0] != nullptr &&
                         lunarCandidates.items[0]->scoreObjectiveName == "auk" &&
                         lunarCandidates.items[0]->scoreObjectiveSignature == "Lauk;" &&
                         lunarCandidates.items[0]->scoreName == "aum" &&
                         lunarCandidates.items[0]->scoreSignature == "Laum;",
                     "verified 1.8.9 Notch scoreboard class mappings");
    passed &= expect(lunarCandidates.items[0] != nullptr &&
                         lunarCandidates.items[0]->scorePlayerTeamName == "aul" &&
                         lunarCandidates.items[0]->scorePlayerTeamSignature == "Laul;" &&
                         lunarCandidates.items[0]->teamSignature == "Lauq;" &&
                         lunarCandidates.items[0]->getScoreboard == "Z" &&
                         lunarCandidates.items[0]->getObjectiveInDisplaySlot == "a" &&
                         lunarCandidates.items[0]->getPlayersTeam == "h" &&
                         lunarCandidates.items[0]->getSortedScores == "i" &&
                         lunarCandidates.items[0]->formatPlayerName == "a",
                     "verified 1.8.9 Notch Sidebar method/signature mappings");
    passed &= expect(lunarCandidates.items[0] != nullptr &&
                         lunarCandidates.items[0]->netHandlerName == "bcy" &&
                         lunarCandidates.items[0]->networkPlayerInfoName == "bdc" &&
                         lunarCandidates.items[0]->getNetHandler == "u" &&
                         lunarCandidates.items[0]->getPlayerInfoMap == "d" &&
                         lunarCandidates.items[0]->getGameProfile == "a",
                     "verified 1.8.9 Notch TAB roster mappings");
    passed &= expect(lunarCandidates.items[0] != nullptr &&
                         lunarCandidates.items[0]->gameSettingsName == "avh" &&
                         lunarCandidates.items[0]->keyBindingName == "avb" &&
                         lunarCandidates.items[0]->gameSettingsField == "t" &&
                         lunarCandidates.items[0]->keyBindSneakField == "ad" &&
                         lunarCandidates.items[0]->getKeyCode == "i" &&
                         lunarCandidates.items[0]->setKeyBindState == "a" &&
                         lunarCandidates.items[0]->rotationPitchField == "z" &&
                         lunarCandidates.items[0]->isAirBlock == "d",
                     "verified 1.8.9 Notch Safewalk mappings");

    MappingRegistry lunarNamedBuiltins;
    ClientEnvironment lunarNamed;
    lunarNamedBuiltins.observeLaunchHint(
        "C:/Users/test/.lunarclient/jre/bin/javaw.exe", lunarNamed);
    lunarNamedBuiltins.observeClassSignature(
        "Lnet/minecraft/client/Minecraft;", lunarNamed);
    const MappingCandidates lunarNamedCandidates =
        lunarNamedBuiltins.candidatesForAnchor(
            "Lnet/minecraft/client/Minecraft;", lunarNamed);
    passed &= expect(lunarNamed.family() == ClientFamily::Lunar,
                     "Lunar named environment detection");
    passed &= expect(lunarNamedCandidates.count == 1U,
                      "Lunar MCP-named provider selection");
    passed &= expect(lunarNamedCandidates.items[0] &&
        lunarNamedCandidates.items[0]->previousRotationYawField == "prevRotationYaw" &&
        lunarNamedCandidates.items[0]->previousRotationPitchField == "prevRotationPitch",
        "Lunar named previous camera rotations");
    passed &= expect(lunarNamedCandidates.items[0] &&
        lunarNamedCandidates.items[0]->clickMouse == "clickMouse" &&
        lunarNamedCandidates.items[0]->sendClickBlock ==
            "sendClickBlockToController" &&
        lunarNamedCandidates.items[0]->moveFlying == "moveFlying" &&
        lunarNamedCandidates.items[0]->isSprinting == "isSprinting" &&
        lunarNamedCandidates.items[0]->setSprinting == "setSprinting" &&
        lunarNamedCandidates.items[0]->movementInputFields[0] == "moveStrafing" &&
        lunarNamedCandidates.items[0]->movementInputFields[1] == "moveForward" &&
        lunarNamedCandidates.items[0]->jump == "jump" &&
        lunarNamedCandidates.items[0]->swingItem == "swingItem" &&
        lunarNamedCandidates.items[0]->rayTraceBlocks == "rayTraceBlocks" &&
        lunarNamedCandidates.items[0]->getItemUseDuration == "getItemInUseDuration",
        "Lunar named click and bow bindings");
    passed &= expect(lunarNamedCandidates.items[0] &&
        lunarNamedCandidates.items[0]->writeMovementPacket == "writePacketData" &&
        lunarNamedCandidates.items[0]->packetYawField == "yaw" &&
        lunarNamedCandidates.items[0]->packetPitchField == "pitch" &&
        lunarNamedCandidates.items[0]->packetRotatingField == "rotating",
        "Lunar named silent output packet members");
    passed &= expect(lunarNamedCandidates.items[0] != nullptr &&
                         lunarNamedCandidates.items[0]->id ==
                             "minecraft-1.8.9-lunar-mcp",
                     "Lunar named anchor selects the MCP dictionary");
    passed &= expect(lunarNamedCandidates.items[0] != nullptr &&
                         lunarNamedCandidates.items[0]->minecraftInstanceField ==
                             "theMinecraft" &&
                         lunarNamedCandidates.items[0]->loadedEntitiesField ==
                             "loadedEntityList",
                     "Lunar named dictionary uses verified static/list fields");
    passed &= expect(lunarNamedCandidates.items[0] != nullptr &&
                         lunarNamedCandidates.items[0]->gameSettingsField ==
                             "gameSettings" &&
                         lunarNamedCandidates.items[0]->keyBindSneakField ==
                             "keyBindSneak" &&
                         lunarNamedCandidates.items[0]->setKeyBindState ==
                             "setKeyBindState" &&
                         lunarNamedCandidates.items[0]->isAirBlock == "isAirBlock",
                     "Lunar named dictionary exposes optional Safewalk mappings");
    passed &= expect(lunarNamedCandidates.items[0] != nullptr &&
        lunarNamedCandidates.items[0]->attackEntity == "attackEntity" &&
        lunarNamedCandidates.items[0]->currentScreenField == "currentScreen" &&
        lunarNamedCandidates.items[0]->getCollidingBoundingBoxes == "getCollidingBoundingBoxes",
        "Lunar must not inherit SRG attack/input/collision member names");
    passed &= expect(lunarNamedCandidates.items[0] &&
        lunarNamedCandidates.items[0]->entityRendererName ==
            "net.minecraft.client.renderer.EntityRenderer" &&
        lunarNamedCandidates.items[0]->updateCameraAndRender == "updateCameraAndRender" &&
        lunarNamedCandidates.items[0]->orientCamera == "orientCamera" &&
        lunarNamedCandidates.items[0]->setAngles == "setAngles" &&
        lunarNamedCandidates.items[0]->renderGlobalName ==
            "net.minecraft.client.renderer.RenderGlobal" &&
        lunarNamedCandidates.items[0]->setupTerrain == "setupTerrain" &&
        lunarNamedCandidates.items[0]->thirdPersonViewField == "thirdPersonView",
        "Lunar named FreeLook mappings remain namespace-correct");

    MappingRegistry external;
    std::string error;
    MappingDictionary lunarMapping = testLunarDictionary();
    passed &= expect(lunarMapping.validate(&error), "valid external dictionary");
    passed &= expect(external.registerDictionary(std::move(lunarMapping), &error) ==
                         MappingRegistrationResult::Accepted,
                     "external dictionary registration");
    passed &= expect(external.freeze(), "external registry freezes");
    ClientEnvironment externalLunar;
    external.observeLaunchHint(".lunarclient", externalLunar);
    external.observeClassSignature("Lexample/lunar/Minecraft;", externalLunar);
    const MappingCandidates externalCandidates = external.candidatesForAnchor(
        "Lexample/lunar/Minecraft;", externalLunar);
    passed &= expect(externalCandidates.count == 1U, "external Lunar provider selection");

    MappingDictionary lateMapping = testLunarDictionary();
    lateMapping.id = "test-lunar-late";
    passed &= expect(external.registerDictionary(std::move(lateMapping), &error) ==
                         MappingRegistrationResult::Frozen,
                     "registration is rejected after freeze");

    MappingRegistry invalidRegistry;
    MappingDictionary invalid = testLunarDictionary();
    invalid.minecraftSignature = "Lwrong/Signature;";
    passed &= expect(invalidRegistry.registerDictionary(std::move(invalid), &error) ==
                         MappingRegistrationResult::Invalid,
                     "invalid JNI signature is rejected");

    if (passed) std::puts("Mapping provider tests passed.");
    return passed ? 0 : 1;
}
