#include "../agent/bindings/SafeWalkPolicy.h"
#include "../agent/UiPreferences.h"
#include "../src/ApiKeyFormat.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
    };
    using namespace mcoverlay;
    check(unpackUiPreferences(0x43U).blur == 65, "old config retains default blur");
    for (int blur : {0, 25, 65, 100}) for (int x : {0, 128, 255}) for (int y : {0, 128, 255}) {
        auto bits = packUiPreferences({blur, x, y});
        auto p = unpackUiPreferences(bits | 0xffU);
        check(p.blur == blur && p.imeX == x && p.imeY == y, "preferences exact roundtrip incl high unsigned bit");
        check((bits & 0xffU) == 0, "new preferences never overwrite feature flags");
    }
    check(normalizeHypixelApiKey(QString(1024, 'x')).size() == 1024,
          "personal key opaque >256 bytes accepted");
    check(normalizeHypixelApiKey("  test_token.a-b_c  ") == "test_token.a-b_c", "credential not rewritten");
    check(normalizeHypixelApiKey("a\r\nAPI-Key: bad").isEmpty(), "reject header injection");
    check(normalizeHypixelApiKey(QString(8193, 'x')).isEmpty(), "bounded credential input");
    check(normalizeHypixelApiKey(QString::fromUtf8("错误key")).isEmpty(), "reject accidental Unicode paste");

    const auto source = [&](const char* file) {
        std::ifstream input(std::string(MC_TEST_PROJECT_SOURCE_DIR)+
            "/agent/bindings/"+file,std::ios::binary);
        check(input.good(), file);
        return std::string((std::istreambuf_iterator<char>(input)),{});
    };
    const auto function = [&](const std::string& text, const char* name) {
        const auto begin=text.find(std::string("GameBindings::")+name+"(");
        const auto end=begin==std::string::npos ? std::string::npos : text.find("\n}",begin);
        check(begin!=std::string::npos&&end!=std::string::npos,name);
        return begin==std::string::npos||end==std::string::npos
            ? std::string{} : text.substr(begin,end+2-begin);
    };
    const auto logicalSource=source("GameBindingsLogical.cpp");
    const auto gameplaySource=source("GameBindingsGameplay.cpp");
    const auto hotbarSource=source("GameBindingsHotbar.cpp");
    const auto movementSource=source("GameBindingsMovement.cpp");
    const auto snapshotSource=source("GameBindingsSnapshot.cpp");
    auto bindingSource=function(logicalSource,"executeLogicalInteraction");
    const std::size_t interaction=bindingSource.find(
        "GameBindings::executeLogicalInteraction");
    const std::size_t swing=bindingSource.find(
        "CallVoidMethod(player,c->swingItem)",interaction);
    const std::size_t attack=bindingSource.find(
        "CallVoidMethod(controller,c->attackEntity,player,target)",interaction);
    check(interaction!=std::string::npos&&swing!=std::string::npos&&
          attack!=std::string::npos&&swing<attack,
          "synthetic attack transaction preserves vanilla swing-before-attack order");
    const auto validate=bindingSource.find("physics_ray_stale",interaction);
    check(validate!=std::string::npos&&validate<swing,
          "PRE physics revalidation precedes swing and attack");
    bindingSource=function(gameplaySource,"updateGameplay");
    const auto preparation=bindingSource.find("std::array<silent::TargetCandidate");
    const auto schedule=bindingSource.find("m_logicalController.updateAttackClock",preparation);
    const auto bind=bindingSource.find("m_logicalController.advance(logicalInput",preparation);
    const auto geometry=bindingSource.substr(preparation,bind-preparation);
    check(schedule!=std::string::npos&&schedule<bind,
          "new attack intent binds in the same preparation frame");
    check(geometry.find("renderX")==std::string::npos&&
          geometry.find("entityRenderTick")==std::string::npos&&
          geometry.find("readCombatEye")!=std::string::npos&&
          geometry.find("readCombatBounds")!=std::string::npos,
          "combat preparation never uses interpolated render geometry");

    const auto scaffold=bindingSource.find("const jint previousSlot=env->GetIntField");
    const auto place=bindingSource.find("cache->onPlayerRightClick, player",scaffold);
    const auto restoreSlot=bindingSource.find("cache->currentItem,previousSlot",place);
    const auto syncSlot=bindingSource.find("cache->syncCurrentPlayItem",restoreSlot);
    const auto accepted=bindingSource.find("if (accepted == JNI_TRUE)",place);
    const auto preSync=bindingSource.find("cache->syncCurrentPlayItem",scaffold);
    check(scaffold<preSync&&preSync<place,
        "scaffold publishes block slot before the placement call");
    check(scaffold!=std::string::npos&&scaffold<place&&place<restoreSlot&&
        restoreSlot<syncSlot&&syncSlot<accepted,
        "scaffold restores and synchronizes original slot before both success and rejection exits");
    check(bindingSource.find("m_smartHotbarKeyDown")==std::string::npos&&
        bindingSource.find("consumeSmartHotbarPress")!=std::string::npos,
        "smart hotbar uses actual consumed key binding events, never render polling");
    check(function(hotbarSource,"consumeSmartHotbarPress").find("windowClick")==std::string::npos,
        "hotbar key hook only enqueues and never mutates inventory");
    check(function(hotbarSource,"onItemUse").find("windowClick")==std::string::npos,
        "right-click refill hook only enqueues and cannot create PacketOrderE");
    const auto hotbarBody=function(hotbarSource,"processSmartHotbarRequests");
    check(hotbarBody.find("if(actionHeld")!=std::string::npos&&
          hotbarBody.find("m_refillQueuedPacketSerial")!=std::string::npos,
        "hotbar changes await action release and a post-use movement boundary");
    check(hotbarBody.find("HotbarPausePhase::AwaitNeutralPacket")!=std::string::npos&&
          hotbarBody.find("setHotbarMovementPaused(env,player)")!=std::string::npos&&
          hotbarBody.find("std::hypot(velocityX,velocityZ)")==std::string::npos,
        "inventory transfer waits for neutral packet, not residual horizontal velocity");
    const auto packetBody=function(logicalSource,"serializeLogicalPacket");
    check(packetBody.find("IsInstanceOf(packet,c->positionPacketClass)")!=std::string::npos&&
          packetBody.find("IsInstanceOf(packet,c->positionLookPacketClass)")!=std::string::npos&&
          packetBody.find("if(original==silent::PacketKind::Unknown) return packet;")!=std::string::npos,
        "packet rewrite preserves position for adapted C04/C06 subclasses and passes unknown payloads");
    const auto headingBody=function(movementSource,"beginLogicalHeading");
    check(headingBody.find("setSprinting,JNI_FALSE")!=std::string::npos&&
          headingBody.find("c->getAIMoveSpeed")!=std::string::npos&&
          headingBody.find("setSprinting,JNI_FALSE")<headingBody.find("c->getAIMoveSpeed"),
        "heading PRE clears sprint before vanilla movement speed is observed");
    check(function(movementSource,"beginLogicalMovement").find(
              "CallVoidMethod(entity,c->setSprinting")==std::string::npos,
        "moveFlying never applies a late sprint-speed patch");
    check(bindingSource.find("(!snapshot.hypixelServer && entity.player)")!=std::string::npos&&
        function(snapshotSource,"sample").find("IsInstanceOf(entity,cache->playerClass)")!=std::string::npos,
        "offline combat recognizes player class without online TAB membership");
    bindingSource=function(logicalSource,"consumeLogicalInteraction");
    const auto pre=bindingSource.find("m_logicalController.beginInteractionPre");
    const auto refresh=bindingSource.find("refreshAttackAtPublication(env)",pre);
    const auto dispatch=bindingSource.find("m_logicalController.clickAtInteractionPre",pre);
    check(pre<refresh&&refresh<dispatch,
        "PRE refresh happens before consuming the attack intent");
    std::ifstream tsfFile(std::string(MC_TEST_PROJECT_SOURCE_DIR)+"/agent/tsf_candidates.cpp");
    const std::string tsf((std::istreambuf_iterator<char>(tsfFile)),{});
    std::ifstream tsfHeaderFile(std::string(MC_TEST_PROJECT_SOURCE_DIR)+"/agent/tsf_candidates.h");
    const std::string tsfHeader((std::istreambuf_iterator<char>(tsfHeaderFile)),{});
    check(tsfHeader.find("struct CandidateListElement : ITfUIElement")!=std::string::npos&&
          tsfHeader.find("virtual HRESULT STDMETHODCALLTYPE GetDescription")==std::string::npos,
        "candidate COM declaration preserves SDK inherited slots exactly once");
    const auto update=tsf.find("HRESULT TsfCandidates::UpdateUIElement");
    const auto end=tsf.find("HRESULT TsfCandidates::EndUIElement",update);
    check(tsf.substr(update,end-update).find("read(id)")!=std::string::npos&&
          tsf.substr(update,end-update).find("PostMessageW")==std::string::npos&&
          tsfHeader.find("m_pendingId")==std::string::npos,
        "TIP update reads only its live ID; no deferred ID is retained");
    check(tsf.find("restoreHiddenOnWindowThread();")!=std::string::npos&&
          tsf.find("if(m_transitioning) m_transitioning=false;")!=std::string::npos,
        "layout reset restores native UI and deferred settle cannot wait forever for Begin");

    // Box collision oracle: one 1x1 cube under a standard 0.6m player body.
    // Sweep all four cardinal directions and both positive/negative corners.
    for (int sx : {-1, 0, 1}) for (int sz : {-1, 0, 1}) {
        if (sx == 0 && sz == 0) continue;
        double previousTrigger = -1;
        for (int sensitivity : {0, 25, 50, 75, 100}) {
            double trigger = 2;
            for (int step = 0; step <= 1400; ++step) {
                const double distance = step * 0.001;
                const double cx = 0.5 + sx * distance, cz = 0.5 + sz * distance;
                const auto supported = [&](double dx, double dz, double inset) {
                    return cx + dx + 0.3 - inset > 0 && cx + dx - 0.3 + inset < 1 &&
                           cz + dz + 0.3 - inset > 0 && cz + dz - 0.3 + inset < 1;
                };
                if (safewalk::needsSneak(sensitivity, sx * 0.09, sz * 0.09,
                                         sx * 0.7, sz * 0.7, supported)) { trigger = distance; break; }
            }
            check(trigger >= previousTrigger - 0.001, "higher sensitivity triggers later, not earlier");
            check(trigger < 0.8, "all sensitivities guard before physical body leaves support");
            previousTrigger = trigger;
        }
    }
    check(safewalk::predictedMotion(0.2, 0) == 0.2, "released-key inertia remains in prediction");
    check(safewalk::predictedMotion(0.16, 1) > 0.25, "next tick input acceleration is included");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
