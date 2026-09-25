#include "../agent/bindings/LiveInteractionTransform.h"
#include "../agent/bindings/LiveAttackTransform.h"
#include "../agent/bindings/LiveHotbarTransform.h"
#include "../agent/bindings/LiveImpulseTransform.h"
#include "../agent/bindings/LiveInteractionObserver.h"
#include "../agent/bindings/LiveJumpTransform.h"
#include "../agent/bindings/LiveHeadingTransform.h"
#include "../agent/bindings/LiveFreeLookTransform.h"
#include "../agent/bindings/LiveMovementTransform.h"
#include "../agent/bindings/LivePacketTransform.h"
#include "../agent/bindings/SilentLockCoordinator.h"

#include <Windows.h>
#include <jni.h>
#include <jvmti.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
struct State final {
    mcoverlay::silent::LogicalStateController controller;
    jfieldID yaw = nullptr;
    jfieldID sprinting = nullptr;
    jfieldID command = nullptr;
    float originalYaw = 0.0F;
    float mappedForward = 0.0F;
    bool movementApplied = false;
    float originalJumpYaw = 0.0F;
    bool originalJumpSprinting = false;
    bool jumpApplied = false;
    int headingPreCalls=0;
    int headingPostCalls=0;
    bool headingOwner=false;
    double previousPacketYaw = 0.0;
    bool havePacketYaw = false;
    bool packetContinuous = true;
    int serializedPackets = 0;
    bool finishBlockOnContinue = false;
    mcoverlay::silent::BlockTarget cameraBlock{};
    std::uint64_t minecraftTick=1;
    int actualAttacks=0;
    std::array<int,4> observedEntries{};
    bool actualTargetMatches=false;
    jobject expectedTarget=nullptr;
    jobject arbitratedTarget=nullptr;
    int attackArbitrations=0;
    bool cancelAttack=false;
    jobject freeLookEntity=nullptr;
    std::array<float,4> cameraAngles{{101.0F,-31.0F,98.0F,-29.0F}};
    std::array<int,4> cameraReads{};
    int cameraRotations=0;
    bool cameraEntityMatches=false;
    float cameraYawDelta=0.0F;
    float cameraPitchDelta=0.0F;
    static void actual(void* owner,JNIEnv* env,mcoverlay::LiveInteractionObserver::Event event,jobject target) noexcept {
        auto& s=*static_cast<State*>(owner);
        ++s.observedEntries[static_cast<std::size_t>(event)];
        if(event==mcoverlay::LiveInteractionObserver::Event::Attack) {
            ++s.actualAttacks;
            s.actualTargetMatches=env->IsSameObject(target,s.expectedTarget)==JNI_TRUE;
            s.controller.debug().event("ACTUAL_ATTACK",s.controller.latest(),"actualEntity=42",true);
        }
    }
    static jobject arbitrateAttack(void* owner,JNIEnv*,jobject original) noexcept {
        auto& state=*static_cast<State*>(owner);
        ++state.attackArbitrations;
        if(state.cancelAttack) return nullptr;
        return state.arbitratedTarget?state.arbitratedTarget:original;
    }

    static jfloat begin(void* owner, JNIEnv* env, jobject fixture,
                        const jfloat strafe, const jfloat forward) noexcept {
        auto& state = *static_cast<State*>(owner);
        const auto command = state.controller.movementCommand(
            static_cast<double>(strafe), static_cast<double>(forward),state.minecraftTick);
        if (!command.enabled) return strafe;
        state.originalYaw = env->GetFloatField(fixture, state.yaw);
        state.mappedForward = static_cast<jfloat>(command.forward);
        env->SetFloatField(fixture, state.yaw,
                           static_cast<jfloat>(command.logicalRotation.yaw));
        if(command.physicalSprinting!=command.sprinting)
            env->SetBooleanField(fixture,state.sprinting,
                                 command.sprinting?JNI_TRUE:JNI_FALSE);
        state.movementApplied = !env->ExceptionCheck();
        return static_cast<jfloat>(command.strafe);
    }
    static jfloat forward(void* owner, JNIEnv*, jobject,
                          const jfloat fallback) noexcept {
        auto& state = *static_cast<State*>(owner);
        return state.movementApplied ? state.mappedForward : fallback;
    }
    static void end(void* owner, JNIEnv* env, jobject fixture) noexcept {
        auto& state = *static_cast<State*>(owner);
        if (state.movementApplied)
            env->SetFloatField(fixture, state.yaw, state.originalYaw);
        state.movementApplied = false;
        state.controller.endMovementPhase(state.minecraftTick);
    }
    static bool arbitrateSprint(void* owner,JNIEnv*,jobject,
                                const bool requested) noexcept {
        auto& state=*static_cast<State*>(owner);
        if(state.headingOwner) return false;
        return state.controller.arbitrateSprint(requested,state.minecraftTick);
    }
    static void beginJump(void* owner,JNIEnv* env,jobject fixture) noexcept {
        auto& state=*static_cast<State*>(owner);
        state.jumpApplied=false;
        const bool sprinting=env->GetBooleanField(fixture,state.sprinting)==JNI_TRUE;
        const auto command=state.controller.jumpCommand(
            0.0,0.98,sprinting,state.minecraftTick);
        if(!command.enabled||env->ExceptionCheck()) return;
        state.originalJumpYaw=env->GetFloatField(fixture,state.yaw);
        state.originalJumpSprinting=sprinting;
        env->SetFloatField(fixture,state.yaw,
                           static_cast<jfloat>(command.logicalRotation.yaw));
        env->SetBooleanField(fixture,state.sprinting,
                             command.sprinting?JNI_TRUE:JNI_FALSE);
        state.jumpApplied=!env->ExceptionCheck();
    }
    static void endJump(void* owner,JNIEnv* env,jobject fixture) noexcept {
        auto& state=*static_cast<State*>(owner);
        if(state.jumpApplied)
            env->SetFloatField(fixture,state.yaw,state.originalJumpYaw);
        state.jumpApplied=false;
    }
    static void headingPre(void* owner,JNIEnv* env,jobject fixture) noexcept {
        auto& state=*static_cast<State*>(owner);
        ++state.headingPreCalls;
        env->SetBooleanField(fixture,state.sprinting,JNI_FALSE);
    }
    static void headingPost(void* owner,JNIEnv*,jobject) noexcept {
        ++static_cast<State*>(owner)->headingPostCalls;
    }
    static bool interact(void* owner, JNIEnv* env, jobject fixture,
                         const mcoverlay::LiveInteractionTransform::Entry entry,
                         const bool down) noexcept {
        auto& state = *static_cast<State*>(owner);
        const bool left=entry==mcoverlay::LiveInteractionTransform::Entry::Click || down;
        state.controller.observeCameraInput({0,0},state.cameraBlock,-1,left,state.minecraftTick);
        if(state.controller.routeManualInput(left)) return !state.controller.manualBlockInputAllowed();
        if(!state.controller.active()) return false;
        state.controller.beginInteractionPre(state.minecraftTick);
        auto command=state.controller.clickAtInteractionPre(state.minecraftTick);
        if(command.kind==mcoverlay::silent::InteractionCommandKind::None)
            command=state.controller.held(down);
        env->SetIntField(fixture, state.command,
                         static_cast<jint>(command.kind));
        if (state.finishBlockOnContinue && command.kind ==
                mcoverlay::silent::InteractionCommandKind::ContinueBlock)
            state.controller.blockFinished();
        return true;
    }
    static jobject packet(void* owner, JNIEnv* env, jobject original) noexcept {
        auto& state = *static_cast<State*>(owner);
        const auto plan = state.controller.packetPlan(false, false);
        if (plan.mutation != mcoverlay::silent::PacketMutation::InjectRotation)
            return original;
        if (state.havePacketYaw &&
            std::abs(plan.rotation.yaw - state.previousPacketYaw) > 180.0)
            state.packetContinuous = false;
        state.previousPacketYaw = plan.rotation.yaw;
        state.havePacketYaw = true;
        ++state.serializedPackets;
        state.controller.acknowledgePacket(
            plan.rotation, mcoverlay::silent::PacketKind::Ground,
            mcoverlay::silent::PacketKind::Look, false, true);
        return env->NewStringUTF("logical-state-packet");
    }
    static void rotateCamera(void* owner,JNIEnv* env,jobject entity,
                             jfloat yawDelta,jfloat pitchDelta) noexcept {
        auto& state=*static_cast<State*>(owner);
        ++state.cameraRotations;
        state.cameraEntityMatches=state.freeLookEntity&&
            env->IsSameObject(entity,state.freeLookEntity)==JNI_TRUE;
        state.cameraYawDelta=yawDelta;
        state.cameraPitchDelta=pitchDelta;
    }
    static jfloat cameraAngle(void* owner,JNIEnv*,jobject,
                              mcoverlay::LiveFreeLookTransform::Angle angle) noexcept {
        auto& state=*static_cast<State*>(owner);
        const auto index=static_cast<std::size_t>(angle);
        ++state.cameraReads[index];
        return state.cameraAngles[index];
    }
};
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if (argc != 3) return 2;
    int checks = 0;
    int failures = 0;
    const auto check = [&](const bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL %s\n", message); }
    };
    HMODULE library = LoadLibraryA(argv[1]);
    using Create = jint(JNICALL*)(JavaVM**, void**, void*);
    const auto create = library
        ? reinterpret_cast<Create>(GetProcAddress(library, "JNI_CreateJavaVM"))
        : nullptr;
    if (!create) return 3;
    std::string classpath = "-Djava.class.path=" + std::string(argv[2]);
    JavaVMOption options[2]{{classpath.data(), nullptr},
                            {const_cast<char*>("-Xcheck:jni"), nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_8, 2, options, JNI_FALSE};
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    if (create(&vm, reinterpret_cast<void**>(&env), &args) != JNI_OK) return 4;

    jclass fixtureClass = env->FindClass("LogicalPipelineFixture");
    jmethodID constructor = fixtureClass
        ? env->GetMethodID(fixtureClass, "<init>", "()V") : nullptr;
    jobject fixture = constructor ? env->NewObject(fixtureClass, constructor) : nullptr;
    jobject alternateTarget=constructor
        ? env->NewObject(fixtureClass,constructor):nullptr;
    if (!fixtureClass || !fixture || !alternateTarget || env->ExceptionCheck()) return 5;
    jmethodID move = env->GetMethodID(fixtureClass, "moveFlying", "(FFF)V");
    jmethodID heading=env->GetMethodID(fixtureClass,"moveEntityWithHeading","(FF)V");
    jmethodID setSprinting=env->GetMethodID(fixtureClass,"setSprinting","(Z)V");
    jmethodID jump = env->GetMethodID(fixtureClass, "jump", "()V");
    jmethodID click = env->GetMethodID(fixtureClass, "click", "()V");
    jmethodID held = env->GetMethodID(fixtureClass, "held", "(Z)V");
    jmethodID queuePacket = env->GetMethodID(
        fixtureClass, "queuePacket", "(Ljava/lang/Object;)V");
    jmethodID attackMethod=env->GetMethodID(fixtureClass,"attackEntity",
        "(Ljava/lang/Object;LLogicalPipelineFixture;)V");
    State state;
    // Callback enters a different JNI native frame; retain an actual global ref.
    state.expectedTarget=env->NewGlobalRef(fixture);
    state.arbitratedTarget=env->NewGlobalRef(alternateTarget);
    state.yaw = env->GetFieldID(fixtureClass, "yaw", "F");
    state.sprinting = env->GetFieldID(fixtureClass, "sprinting", "Z");
    jfieldID observedHeadingSprint=env->GetFieldID(
        fixtureClass,"observedHeadingSprint","Z");
    jfieldID observedLunarSprintRequest=env->GetFieldID(
        fixtureClass,"observedLunarSprintRequest","Z");
    jfieldID observedHeadingSpeed=env->GetFieldID(
        fixtureClass,"observedHeadingSpeed","F");
    state.command = env->GetFieldID(fixtureClass, "vanillaHeld", "I");
    jfieldID observedYaw = env->GetFieldID(fixtureClass, "observedYaw", "F");
    jfieldID observedStrafe = env->GetFieldID(fixtureClass, "observedStrafe", "F");
    jfieldID observedForward = env->GetFieldID(fixtureClass, "observedForward", "F");
    jfieldID observedJumpYaw = env->GetFieldID(fixtureClass, "observedJumpYaw", "F");
    jfieldID observedJumpSprinting = env->GetFieldID(
        fixtureClass, "observedJumpSprinting", "Z");
    jfieldID jumpImpulseX = env->GetFieldID(fixtureClass, "jumpImpulseX", "F");
    jfieldID jumpImpulseZ = env->GetFieldID(fixtureClass, "jumpImpulseZ", "F");
    jfieldID vanillaClicks = env->GetFieldID(fixtureClass, "vanillaClicks", "I");
    jfieldID vanillaAttacks=env->GetFieldID(fixtureClass,"vanillaAttacks","I");
    jfieldID lastAttackTarget=env->GetFieldID(
        fixtureClass,"lastAttackTarget","Ljava/lang/Object;");
    jfieldID queuedPacket = env->GetFieldID(
        fixtureClass, "queuedPacket", "Ljava/lang/Object;");
    check(move && heading && setSprinting && jump && click && held && queuePacket && attackMethod && state.yaw &&
          state.sprinting && state.command && queuedPacket && observedJumpYaw &&
          observedHeadingSprint && observedLunarSprintRequest && observedHeadingSpeed &&
          observedJumpSprinting && jumpImpulseX && jumpImpulseZ &&
          vanillaAttacks && lastAttackTarget && state.expectedTarget &&
          state.arbitratedTarget &&
          !env->ExceptionCheck(), "fixture entry points resolve");

    jclass freeLookEntityClass=env->FindClass("FreeLookEntityFixture");
    jclass freeLookRendererClass=env->FindClass("FreeLookRendererFixture");
    jclass freeLookTerrainClass=env->FindClass("FreeLookTerrainFixture");
    jmethodID freeLookEntityCtor=freeLookEntityClass
        ? env->GetMethodID(freeLookEntityClass,"<init>","()V") : nullptr;
    jmethodID freeLookRendererCtor=freeLookRendererClass
        ? env->GetMethodID(freeLookRendererClass,"<init>","()V") : nullptr;
    jobject freeLookEntity=freeLookEntityCtor
        ? env->NewObject(freeLookEntityClass,freeLookEntityCtor) : nullptr;
    jobject freeLookRenderer=freeLookRendererCtor
        ? env->NewObject(freeLookRendererClass,freeLookRendererCtor) : nullptr;
    jmethodID freeLookTerrainCtor=freeLookTerrainClass
        ? env->GetMethodID(freeLookTerrainClass,"<init>","()V") : nullptr;
    jobject freeLookTerrain=freeLookTerrainCtor
        ? env->NewObject(freeLookTerrainClass,freeLookTerrainCtor) : nullptr;
    jmethodID updateCamera=freeLookRendererClass ? env->GetMethodID(
        freeLookRendererClass,"updateCameraAndRender",
        "(LFreeLookEntityFixture;FF)V") : nullptr;
    jmethodID orientCamera=freeLookRendererClass ? env->GetMethodID(
        freeLookRendererClass,"orientCamera","(LFreeLookEntityFixture;F)V") : nullptr;
    jmethodID setupTerrain=freeLookTerrainClass ? env->GetMethodID(
        freeLookTerrainClass,"setupTerrain",
        "(LFreeLookEntityFixture;DLjava/lang/Object;IZ)V") : nullptr;
    jfieldID setAnglesCalls=freeLookEntityClass
        ? env->GetFieldID(freeLookEntityClass,"setAnglesCalls","I") : nullptr;
    const auto floatField=[&](jclass type,const char* name) {
        return type ? env->GetFieldID(type,name,"F") : nullptr;
    };
    std::array<jfieldID,4> observedCamera{{
        floatField(freeLookRendererClass,"observedYaw"),
        floatField(freeLookRendererClass,"observedPitch"),
        floatField(freeLookRendererClass,"observedPreviousYaw"),
        floatField(freeLookRendererClass,"observedPreviousPitch")}};
    std::array<jfieldID,4> entityCamera{{
        floatField(freeLookEntityClass,"rotationYaw"),
        floatField(freeLookEntityClass,"rotationPitch"),
        floatField(freeLookEntityClass,"prevRotationYaw"),
        floatField(freeLookEntityClass,"prevRotationPitch")}};
    std::array<jfieldID,2> observedTerrain{{
        floatField(freeLookTerrainClass,"observedYaw"),
        floatField(freeLookTerrainClass,"observedPitch")}};
    state.freeLookEntity=freeLookEntity ? env->NewGlobalRef(freeLookEntity) : nullptr;
    check(freeLookEntityClass&&freeLookRendererClass&&freeLookTerrainClass&&
          freeLookEntity&&freeLookRenderer&&freeLookTerrain&&updateCamera&&
          orientCamera&&setupTerrain&&setAnglesCalls&&
          state.freeLookEntity&&std::all_of(observedCamera.begin(),
              observedCamera.end(),[](jfieldID field){return field!=nullptr;})&&
          std::all_of(entityCamera.begin(),entityCamera.end(),
              [](jfieldID field){return field!=nullptr;})&&
          std::all_of(observedTerrain.begin(),observedTerrain.end(),
              [](jfieldID field){return field!=nullptr;})&&!env->ExceptionCheck(),
          "FreeLook fixture entry points resolve");

    const mcoverlay::silent::TargetCandidate target{
        42, 42U, {2.0, 1.62, 0.0}, {1.7, 0.0, -0.3, 2.3, 1.9, 0.3}, true};
    mcoverlay::silent::LogicalFrameInput input{};
    input.tick = 1000;
    input.physicsTick = state.minecraftTick;
    input.worldGeneration = 1;
    input.localEntityId = 1;
    input.enabled = true;
    input.silent = true;
    input.leftMouseDown = true;
    input.mode = mcoverlay::aim::Mode::LockOn;
    input.maximumDistance = 6.0;
    input.fovDegrees = 360.0;
    input.camera = {0.0, 0.0};
    input.eye = {0.0, 1.62, 0.0};
    input.physicalMovement.forward = true;
    input.currentVelocity = {0.0, 0.0, 0.2};
    input.onGround = true;
    input.sprinting = true;
    input.candidates = {&target, 1};
    const auto clearTrace=[](const mcoverlay::silent::LogicalFramePlan&) noexcept {
        return mcoverlay::silent::BlockRayHit{true,-1.0,{}};
    };
    const auto plan = state.controller.advance(input,clearTrace);
    check(plan.silentActive, "pure Silent Lock creates an authoritative plan");

    mcoverlay::LiveMovementTransform movementHook;
    mcoverlay::LiveJumpTransform jumpHook;
    mcoverlay::LiveHeadingTransform headingHook;
    mcoverlay::LiveInteractionTransform interactionHook;
    mcoverlay::LiveAttackTransform attackHook;
    mcoverlay::LivePacketTransform packetHook;
    mcoverlay::LiveFreeLookTransform freeLookHook;
    check(movementHook.install(vm, move,setSprinting,&state,&State::begin,
                               &State::forward,&State::end,&State::arbitrateSprint),
          "live moveFlying transform installs");
    check(jumpHook.install(vm,jump,&state,&State::beginJump,&State::endJump),
          "live jump transform installs");
    check(headingHook.install(vm,heading,&state,&State::headingPre,&State::headingPost),
          "live moveEntityWithHeading PRE/POST transform installs");
    check(interactionHook.install(vm, click, held, &state, &State::interact),
          "live click and held transform installs");
    check(attackHook.install(vm,attackMethod,&state,&State::arbitrateAttack),
          "live lower attack transform installs");
    check(packetHook.install(vm, queuePacket, "java/lang/Object", &state,
                             &State::packet),
          "live packet-queue transform installs");
    jmethodID freeLookSetAngles=freeLookEntityClass ? env->GetMethodID(
        freeLookEntityClass,"setAngles","(FF)V") : nullptr;
    check(freeLookHook.install(vm,updateCamera,orientCamera,freeLookSetAngles,setupTerrain,
        "FreeLookEntityFixture","setAngles",
        {"rotationYaw","rotationPitch","prevRotationYaw","prevRotationPitch"},
        &state,&State::rotateCamera,&State::cameraAngle),
        "live FreeLook camera transform installs");
    check(!env->ExceptionCheck(),
          "all live transform installs leave the JNI exception state clear");
    if (!movementHook.ready() || !jumpHook.ready() || !headingHook.ready() ||
        !interactionHook.ready() ||
        !attackHook.ready() ||
        !packetHook.ready() || !freeLookHook.ready()||!freeLookHook.terrainReady()) {
        std::printf("JVMTI errors movement=%d jump=%d interaction=%d attack=%d packet=%d freelook=%d\n",
                    movementHook.lastError(),jumpHook.lastError(),
                    interactionHook.lastError(),attackHook.error(),packetHook.lastError(),
                    freeLookHook.lastError());
        return 6;
    }
    movementHook.setEnabled(true);
    jumpHook.setEnabled(true);
    headingHook.setEnabled(true);
    interactionHook.setEnabled(true);
    attackHook.setEnabled(true);
    packetHook.setEnabled(true);

    // The fixture's Force Sprint call must be vetoed independently of target
    // acquisition; heading itself commits no movement snapshot in this test.
    state.headingOwner=true;
    env->SetBooleanField(fixture,state.sprinting,JNI_TRUE);
    env->CallVoidMethod(fixture,heading,0.0F,0.98F);
    check(!env->ExceptionCheck()&&state.headingPreCalls==1&&
          state.headingPostCalls==1&&
          env->GetBooleanField(fixture,observedHeadingSprint)==JNI_FALSE&&
          env->GetBooleanField(fixture,observedLunarSprintRequest)==JNI_FALSE&&
          std::abs(env->GetFloatField(fixture,observedHeadingSpeed)-0.10F)<0.0001F&&
          env->GetBooleanField(fixture,state.sprinting)==JNI_FALSE,
          "heading PRE veto reaches native speed calculation; POST does not restore sprint");
    state.headingOwner=false;

    env->SetIntField(fixture,vanillaAttacks,0);
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    const bool substitutedCallOk=!env->ExceptionCheck();
    jobject substitutedTarget=env->GetObjectField(fixture,lastAttackTarget);
    check(substitutedCallOk&&!env->ExceptionCheck()&&state.attackArbitrations==1&&
          env->GetIntField(fixture,vanillaAttacks)==1&&substitutedTarget&&
          env->IsSameObject(substitutedTarget,alternateTarget)==JNI_TRUE,
          "lower attack boundary substitutes one committed target into one vanilla dispatch");
    if(substitutedTarget)env->DeleteLocalRef(substitutedTarget);
    state.cancelAttack=true;
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    check(!env->ExceptionCheck()&&state.attackArbitrations==2&&
          env->GetIntField(fixture,vanillaAttacks)==1,
          "null attack arbitration cancels before the original dispatch body");
    state.cancelAttack=false;
    attackHook.setEnabled(false);
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    const bool vanillaCallOk=!env->ExceptionCheck();
    jobject vanillaTarget=env->GetObjectField(fixture,lastAttackTarget);
    check(vanillaCallOk&&!env->ExceptionCheck()&&state.attackArbitrations==2&&
          env->GetIntField(fixture,vanillaAttacks)==2&&vanillaTarget&&
          env->IsSameObject(vanillaTarget,fixture)==JNI_TRUE,
          "disabled attack arbitration preserves the unmodified vanilla target");
    if(vanillaTarget)env->DeleteLocalRef(vanillaTarget);
    attackHook.setEnabled(true);

    env->CallVoidMethod(freeLookRenderer,updateCamera,freeLookEntity,8.0F,-4.0F);
    check(!env->ExceptionCheck()&&state.cameraRotations==1&&
          state.cameraEntityMatches&&std::abs(state.cameraYawDelta-8.0F)<0.001F&&
          std::abs(state.cameraPitchDelta+4.0F)<0.001F,
          "FreeLook consumes the original setAngles call with both mouse deltas");
    const jint consumedSetAngles=env->GetIntField(freeLookEntity,setAnglesCalls);
    const bool consumedCountOk=!env->ExceptionCheck();
    const jfloat unchangedPlayerYaw=env->GetFloatField(
        freeLookEntity,entityCamera[0]);
    check(consumedCountOk&&!env->ExceptionCheck()&&consumedSetAngles==0&&
          std::abs(unchangedPlayerYaw-11.0F)<0.001F,
          "FreeLook leaves the player's real orientation untouched");
    env->CallVoidMethod(freeLookRenderer,orientCamera,freeLookEntity,0.5F);
    const bool redirectedOrientOk=!env->ExceptionCheck();
    bool cameraValues=true;
    for(std::size_t index=0;index<observedCamera.size();++index) {
        const jfloat observed=env->GetFloatField(
            freeLookRenderer,observedCamera[index]);
        cameraValues=cameraValues&&!env->ExceptionCheck()&&
            state.cameraReads[index]==1&&
            std::abs(observed-state.cameraAngles[index])<0.001F;
    }
    check(redirectedOrientOk&&!env->ExceptionCheck()&&cameraValues,
          "FreeLook redirects all four renderer orientation reads only");
    env->CallVoidMethod(freeLookTerrain,setupTerrain,freeLookEntity,0.0,
                        nullptr,0,JNI_FALSE);
    const bool terrainCallOk=!env->ExceptionCheck();
    const jfloat terrainYaw=env->GetFloatField(
        freeLookTerrain,observedTerrain[0]);
    const jfloat terrainPitch=env->GetFloatField(
        freeLookTerrain,observedTerrain[1]);
    check(terrainCallOk&&!env->ExceptionCheck()&&state.cameraReads[0]==2&&
          state.cameraReads[1]==2&&
          std::abs(terrainYaw-state.cameraAngles[0])<0.001F&&
          std::abs(terrainPitch-state.cameraAngles[1])<0.001F,
          "FreeLook terrain culling follows the visual camera behind the player");

    // The target advances every 240 Hz render sample while only one Java
    // queue call is made per twelve samples (20 TPS).  This exercises the
    // transformed network entry point and its packet-to-packet continuity.
    for (int frame = 0; frame < 240; ++frame) {
        const double phase = static_cast<double>(frame) * 0.0125;
        mcoverlay::silent::TargetCandidate moving = target;
        moving.aimPoint = {std::sin(phase) * 2.5, 1.62,
                           std::cos(phase) * 2.5};
        moving.bounds = {moving.aimPoint.x - 0.3, 0.0,
                         moving.aimPoint.z - 0.3,
                         moving.aimPoint.x + 0.3, 1.9,
                         moving.aimPoint.z + 0.3};
        input.tick = 1000 + static_cast<std::uint64_t>(frame * 1000 / 240);
        input.candidates = {&moving, 1};
        (void)state.controller.advance(input,clearTrace);
        if (frame % 12 != 0) continue;
        jstring original = env->NewStringUTF("camera-packet");
        env->CallVoidMethod(fixture, queuePacket, original);
        check(!env->ExceptionCheck(),
              "transformed 20 TPS packet queue leaves no exception");
        jobject observed = env->GetObjectField(fixture, queuedPacket);
        check(observed != nullptr && !env->IsSameObject(observed, original),
              "Java packet queue receives serialization from logical state");
        if (observed) env->DeleteLocalRef(observed);
        env->DeleteLocalRef(original);
    }
    check(state.serializedPackets == 20,
          "every 20 TPS sample serializes an existing logical rotation");
    check(state.packetContinuous,
          "240 Hz logical updates remain continuous packet-to-packet at 20 TPS");

    input.tick = 2000;
    input.physicsTick = state.minecraftTick;
    input.candidates = {&target, 1};
    (void)state.controller.advance(input,clearTrace);

    env->SetFloatField(fixture, state.yaw, 0.0F);
    env->SetBooleanField(fixture,state.sprinting,JNI_TRUE);
    env->CallVoidMethod(fixture,setSprinting,JNI_TRUE);
    check(!env->ExceptionCheck()&&
          env->GetBooleanField(fixture,state.sprinting)==JNI_FALSE,
          "pre-consumer sprint veto does not depend on a stale movement snapshot");
    env->SetBooleanField(fixture,state.sprinting,JNI_TRUE);
    env->CallVoidMethod(fixture, move, 0.0F, 1.0F, 0.91F);
    check(!env->ExceptionCheck(), "transformed moveFlying leaves no exception");
    check(std::abs(env->GetFloatField(fixture, observedYaw) + 90.0F) < 0.01F,
          "vanilla movement body executes under logical yaw");
    check(std::abs(env->GetFloatField(fixture, observedStrafe) + 1.0F) < 0.01F &&
          std::abs(env->GetFloatField(fixture, observedForward)) < 0.01F,
          "side target preserves world intent with D instead of forced W+D");
    check(env->GetBooleanField(fixture,state.sprinting)==JNI_FALSE,
          "side remap lets Minecraft leave sprint instead of sacrificing intent");
    check(std::abs(env->GetFloatField(fixture, state.yaw)) < 0.01F,
          "visible yaw is restored after actual movement computation");

    env->CallVoidMethod(fixture,setSprinting,JNI_TRUE);
    check(!env->ExceptionCheck()&&
          env->GetBooleanField(fixture,state.sprinting)==JNI_FALSE,
          "held SilentCombat vetoes a Lunar-style setSprinting(true) request");
    input.leftMouseDown=false;
    input.tick=2010;
    (void)state.controller.advance(input,clearTrace);
    env->CallVoidMethod(fixture,setSprinting,JNI_TRUE);
    check(!env->ExceptionCheck()&&
          env->GetBooleanField(fixture,state.sprinting)==JNI_TRUE,
          "mouse release immediately returns sprint ownership to vanilla");
    input.leftMouseDown=true;
    input.tick=2020;
    (void)state.controller.advance(input,clearTrace);

    env->SetFloatField(fixture,state.yaw,0.0F);
    env->SetBooleanField(fixture,state.sprinting,JNI_TRUE);
    env->SetFloatField(fixture,jumpImpulseX,0.0F);
    env->SetFloatField(fixture,jumpImpulseZ,0.0F);
    state.minecraftTick=2;
    input.tick=2050;
    input.physicsTick=state.minecraftTick;
    input.sprinting=true;
    (void)state.controller.advance(input,clearTrace);
    env->CallVoidMethod(fixture,jump);
    check(!env->ExceptionCheck(),"transformed jump leaves no exception");
    check(std::abs(env->GetFloatField(fixture,observedJumpYaw)+90.0F)<0.01F&&
          env->GetBooleanField(fixture,observedJumpSprinting)==JNI_FALSE,
          "side sprint jump uses logical yaw and the resolved non-sprint state");
    check(std::abs(env->GetFloatField(fixture,jumpImpulseX))<0.001F&&
          std::abs(env->GetFloatField(fixture,jumpImpulseZ))<0.001F,
          "resolved side input cannot receive a camera-oriented sprint impulse");
    check(std::abs(env->GetFloatField(fixture,state.yaw))<0.01F,
          "visible yaw is restored after the original jump body");
    check(env->GetBooleanField(fixture,state.sprinting)==JNI_FALSE,
          "jump suppression persists instead of restoring an invalid sprint");

    env->SetIntField(fixture, vanillaClicks, 0);
    env->SetIntField(fixture, state.command, 0);
    state.controller.updateAttackClock(true,true,1000000U,10);
    input.tick=2051;
    (void)state.controller.advance(input,clearTrace);
    env->CallVoidMethod(fixture,queuePacket,fixture);
    check(!env->ExceptionCheck()&&state.serializedPackets>0,
          "attack transaction publishes rotation through the packet hook first");
    state.controller.endMovementPhase(state.minecraftTick);
    check(state.controller.clickAtInteractionPre(state.minecraftTick).kind==
              mcoverlay::silent::InteractionCommandKind::None,
          "POST-stage consumer cannot dispatch a prepared attack transaction");
    env->CallVoidMethod(fixture, click);
    check(!env->ExceptionCheck(), "transformed click leaves no exception");
    check(env->GetIntField(fixture, vanillaClicks) == 0,
          "vanilla click body is suppressed by the sole interaction owner");
    check(env->GetIntField(fixture, state.command) ==
              static_cast<jint>(mcoverlay::silent::InteractionCommandKind::AttackEntity),
          "stable transformed PRE boundary emits the published attack transaction");
    env->CallVoidMethod(fixture, held, JNI_TRUE);
    check(!env->ExceptionCheck(), "same-tick held entry leaves no exception");
    check(env->GetIntField(fixture, state.command) ==
              static_cast<jint>(mcoverlay::silent::InteractionCommandKind::None),
          "same logical tick cannot also emit block damage");
    state.cameraBlock={1,2,3,4,true};
    env->CallVoidMethod(fixture,held,JNI_TRUE);
    check(!env->ExceptionCheck() && env->GetIntField(fixture,vanillaClicks)==0 &&
        env->GetIntField(fixture,state.command)==0,
        "camera change after an attack cannot add vanilla digging in that same tick");
    state.cameraBlock={};
    env->CallVoidMethod(fixture,held,JNI_FALSE);
    check(!env->ExceptionCheck(),"same-tick arbitration release leaves no exception");

    // A combat ray intersecting a block is not a manual block intent.
    input.tick = 2100;
    (void)state.controller.advance(input,
        [](const mcoverlay::silent::LogicalFramePlan&) noexcept {
            return mcoverlay::silent::BlockRayHit{
                true,0.5,{1,2,3,4,true}};
        });
    env->CallVoidMethod(fixture, click);
    check(!env->ExceptionCheck(),"combat block click leaves no exception");
    check(env->GetIntField(fixture,state.command)==0 &&
          env->GetIntField(fixture,vanillaClicks)==0,
          "combat logical block neither starts a dig nor leaks a vanilla entity attack");

    state.cameraBlock={1,2,3,4,true};
    state.minecraftTick=43;
    env->CallVoidMethod(fixture,click);
    check(!env->ExceptionCheck(),"manual click leaves no exception");
    check(env->GetIntField(fixture,vanillaClicks)==1,
          "camera block click reaches the ORIGINAL Java click body");
    check(!state.controller.active() &&
          state.controller.latest().interactionOwner==mcoverlay::silent::InteractionOwner::ManualBlock,
          "actual transformed click releases silent combat before vanilla block handling");
    env->SetIntField(fixture,state.command,0);
    for(int frame=0;frame<24;++frame) {
        input.tick=2150+static_cast<std::uint64_t>(frame*1000/240);
        input.physicsTick=state.minecraftTick;
        (void)state.controller.advance(input,clearTrace);
        if(frame%12==0) {
            ++state.minecraftTick;
            env->CallVoidMethod(fixture,held,JNI_TRUE);
            check(!env->ExceptionCheck(),"manual held entry leaves no exception");
        }
        check(!state.controller.active(),"high-FPS frames cannot reacquire during manual digging");
    }
    check(env->GetIntField(fixture,state.command)==2,
          "continuous held-left executes original Java damage path on successive ticks");
    state.cameraBlock={};
    env->CallVoidMethod(fixture,held,JNI_FALSE);
    check(!env->ExceptionCheck(),"manual release leaves no exception");
    input.tick=2300; (void)state.controller.advance(input,clearTrace);
    check(state.controller.active(),"release returns ownership to silent combat");
    state.controller.manualBlockEnded();

    // Observe a real method body reached directly from Java, not a planned
    // InteractionCommand. This is the same selective observer used in-game.
    auto startMethod=env->GetMethodID(fixtureClass,"clickBlock","(Ljava/lang/Object;Ljava/lang/Object;)Z");
    auto damageMethod=env->GetMethodID(fixtureClass,"onPlayerDamageBlock","(Ljava/lang/Object;Ljava/lang/Object;)Z");
    auto resetMethod=env->GetMethodID(fixtureClass,"resetBlockRemoving","()V");
    mcoverlay::LiveInteractionObserver observer;
    check(observer.install(vm,{attackMethod,startMethod,damageMethod,resetMethod},&state,
        &State::actual),"debug observer attaches to actual attackEntity entry");
    check(!env->ExceptionCheck(),
          "diagnostic observer install leaves the JNI exception state clear");
    state.controller.debug().configure(false,true);
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    check(!env->ExceptionCheck() && state.actualAttacks==1 &&
        state.actualTargetMatches,"actual attack callback receives the original entry target");
    std::string chat;
    bool sawActual=false;
    while(state.controller.debug().popChat(chat))
        sawActual|=chat.find("ACTUAL_ATTACK")!=std::string::npos;
    check(sawActual,"central recorder receives actual entry, not just logical attack intent");
    const bool startResult=env->CallBooleanMethod(fixture,startMethod,fixture,fixture)==JNI_TRUE;
    check(!env->ExceptionCheck() && startResult,"observer preserves clickBlock return value");
    const bool damageResult=env->CallBooleanMethod(fixture,damageMethod,fixture,fixture)==JNI_TRUE;
    check(!env->ExceptionCheck() && damageResult,"observer preserves continuous damage return value");
    env->CallVoidMethod(fixture,resetMethod);
    check(!env->ExceptionCheck() && state.observedEntries==std::array<int,4>{1,1,1,1},
        "all real attack/start/damage/reset entries are observed exactly once");
    // Debug toggles must not retransform this class while the lower attack
    // arbitration transform is active.  Keep the observer installed and gate
    // only its callback, matching the runtime lifecycle in GameBindings.
    observer.setEnabled(false);
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    check(!env->ExceptionCheck()&&state.actualAttacks==1&&
          state.attackArbitrations==4,
          "disabling diagnostics keeps lower attack arbitration installed");
    observer.setEnabled(true);
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    check(!env->ExceptionCheck()&&state.actualAttacks==2&&
          state.attackArbitrations==5,
          "re-enabling diagnostics observes once without retransformation");

    // Remove the lower transform first so the observer's final restore cannot
    // erase another active rewrite of the same method.
    attackHook.stop();
    check(!env->ExceptionCheck(),
          "lower attack hook stop leaves the JNI exception state clear");
    env->CallVoidMethod(fixture,attackMethod,fixture,fixture);
    const bool restoredAttackCallOk=!env->ExceptionCheck();
    jobject restoredAttackTarget=env->GetObjectField(fixture,lastAttackTarget);
    const bool restoredAttackSame=restoredAttackTarget&&
        env->IsSameObject(restoredAttackTarget,fixture)==JNI_TRUE;
    if(env->ExceptionCheck()||state.attackArbitrations!=5||!restoredAttackSame)
        std::printf("attack detach detail: exception=%d arbitrations=%d same=%d vanilla=%d\n",
            env->ExceptionCheck()?1:0,state.attackArbitrations,
            restoredAttackSame?1:0,env->GetIntField(fixture,vanillaAttacks));
    check(restoredAttackCallOk&&!env->ExceptionCheck()&&state.attackArbitrations==5&&
          restoredAttackSame,
          "attack hook detach restores the original lower interaction bytecode");
    if(restoredAttackTarget)env->DeleteLocalRef(restoredAttackTarget);
    observer.stop();

    const auto recorderRoot=std::filesystem::path(argv[2]).parent_path()/"silent-recorder-test";
    SetEnvironmentVariableW(L"LOCALAPPDATA",recorderRoot.c_str()); // this test process only
    mcoverlay::silent::SilentDebugRecorder fileRecorder;
    check(!fileRecorder.enabled(),"diagnostics default off");
    fileRecorder.configure(true,false);
    for(int row=0;row<100;++row)
        fileRecorder.event("ACTUAL_ATTACK",state.controller.latest(),"actualEntity=42",true);
    check(!fileRecorder.hasChat(),"file and chat switches are independent");
    fileRecorder.stop(); // drains pending rows before joining
    bool persisted=false;
    const auto directory=recorderRoot/"Overlay Studio"/"MinecraftOverlayManager"/"diagnostics";
    if(std::filesystem::exists(directory)) for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        std::ifstream inputFile(entry.path());
        const std::string text{std::istreambuf_iterator<char>(inputFile),std::istreambuf_iterator<char>()};
        std::size_t at=0,count=0;
        while((at=text.find("[ACTUAL_ATTACK]",at))!=std::string::npos) {++count;++at;}
        persisted|=count>=100 && text.find("lastReported=")!=std::string::npos;
    }
    check(!fileRecorder.fileError() && persisted,"async recorder drains actual events and rotation context to disk");

    freeLookHook.stop();
    env->CallVoidMethod(freeLookRenderer,updateCamera,freeLookEntity,8.0F,-4.0F);
    const bool restoredUpdateOk=!env->ExceptionCheck();
    env->CallVoidMethod(freeLookRenderer,orientCamera,freeLookEntity,0.5F);
    const bool restoredOrientOk=!env->ExceptionCheck();
    const jint restoredSetAngles=env->GetIntField(freeLookEntity,setAnglesCalls);
    const bool restoredCountOk=!env->ExceptionCheck();
    const jfloat restoredObservedYaw=env->GetFloatField(
        freeLookRenderer,observedCamera[0]);
    check(restoredUpdateOk&&restoredOrientOk&&restoredCountOk&&
          !env->ExceptionCheck()&&restoredSetAngles==1&&
          std::abs(restoredObservedYaw-12.2F)<0.001F,
          "FreeLook detach restores vanilla mouse and camera bytecode");
    mcoverlay::LiveFreeLookTransform partialFreeLook;
    const bool combined=partialFreeLook.install(vm,updateCamera,orientCamera,
        freeLookSetAngles,nullptr,"FreeLookEntityFixture","missingSetAngles",
        {"rotationYaw","rotationPitch","prevRotationYaw","prevRotationPitch"},
        &state,&State::rotateCamera,&State::cameraAngle);
    check(!combined&&partialFreeLook.renderReady()&&
          !partialFreeLook.inputReady(),
          "failed FreeLook input matching retains the independent render hook");
    env->CallVoidMethod(freeLookRenderer,orientCamera,freeLookEntity,0.5F);
    check(!env->ExceptionCheck()&&std::abs(env->GetFloatField(
              freeLookRenderer,observedCamera[0])-state.cameraAngles[0])<0.001F,
          "partial FreeLook render hook remains executable until explicit detach");
    partialFreeLook.stop();
    packetHook.stop();
    interactionHook.stop();
    jumpHook.stop();
    headingHook.stop();
    movementHook.stop();
    env->SetIntField(fixture, vanillaClicks, 0);
    env->CallVoidMethod(fixture, click);
    check(!env->ExceptionCheck(), "restored vanilla click leaves no exception");
    check(env->GetIntField(fixture, vanillaClicks) == 1,
          "detach restores the original interaction bytecode");
    env->DeleteGlobalRef(state.expectedTarget);
    env->DeleteGlobalRef(state.arbitratedTarget);
    env->DeleteGlobalRef(state.freeLookEntity);
    {
        jclass type=env->FindClass("HotbarBindingFixture");
        jmethodID ctor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
        jmethodID pressed=type?env->GetMethodID(type,"isPressed","()Z"):nullptr;
        jmethodID select=type?env->GetMethodID(type,"vanillaSelect","(I)V"):nullptr;
        jfieldID pressTime=type?env->GetFieldID(type,"pressTime","I"):nullptr;
        jfieldID current=type?env->GetFieldID(type,"currentSlot","I"):nullptr;
        jobject binding=ctor?env->NewObject(type,ctor):nullptr;
        check(binding&&pressed&&select&&pressTime&&current&&!env->ExceptionCheck(),
            "hotbar KeyBinding fixture resolves");
        if(binding&&pressed&&select&&pressTime&&current&&!env->ExceptionCheck()) {
            struct Shortcut {jfieldID current;bool replace=true;int calls=0;} shortcut{current};
            mcoverlay::LiveHotbarTransform hook;
            check(hook.install(vm,pressed,&shortcut,
                [](void* owner,JNIEnv* jni,jobject key) noexcept {
                    auto& state=*static_cast<Shortcut*>(owner);
                    ++state.calls;
                    if(!state.replace) return false;
                    jni->SetIntField(key,state.current,7);
                    return true;
                }),"hotbar return filter installs on verified JVM bytecode");
            hook.setEnabled(true);
            for(int repeat=0;repeat<4;++repeat) {
                env->SetIntField(binding,pressTime,1);
                env->CallVoidMethod(binding,select,3);
                check(!env->ExceptionCheck()&&env->GetIntField(binding,current)==7&&
                    env->GetIntField(binding,pressTime)==0,
                    "consumed key press/repeat cannot be overwritten by vanilla selection");
            }
            const int before=shortcut.calls;
            env->CallVoidMethod(binding,select,3);
            check(!env->ExceptionCheck()&&shortcut.calls==before,"released key does not produce synthetic edges");
            shortcut.replace=false;
            env->SetIntField(binding,pressTime,1);
            env->CallVoidMethod(binding,select,3);
            check(!env->ExceptionCheck()&&env->GetIntField(binding,current)==3,
                "unmapped or unavailable category preserves vanilla selection");
            shortcut.replace=true;hook.setEnabled(false);
            env->SetIntField(binding,pressTime,1);
            env->CallVoidMethod(binding,select,5);
            check(!env->ExceptionCheck()&&env->GetIntField(binding,current)==5,"disabled shortcut restores vanilla immediately");
            hook.stop();
            env->SetIntField(binding,pressTime,1);
            env->CallVoidMethod(binding,select,2);
            check(!env->ExceptionCheck()&&env->GetIntField(binding,current)==2,
                "detaching hotbar hook restores original bytecode");
        } else if(env->ExceptionCheck()) env->ExceptionClear();
    }
    {
        jclass type=env->FindClass("AssistFeaturesFixture");
        jmethodID ctor=type?env->GetMethodID(type,"<init>","()V"):nullptr;
        jmethodID use=type?env->GetMethodID(type,"rightClickMouse","()V"):nullptr;
        jmethodID observe=type?env->GetMethodID(type,"observeUse","(Z)V"):nullptr;
        jmethodID knock=type?env->GetMethodID(type,"knockBack","(Ljava/lang/Object;FDD)V"):nullptr;
        jmethodID receive=type?env->GetMethodID(type,"handleVelocity","(Ljava/lang/Object;)V"):nullptr;
        jfieldID selected=type?env->GetFieldID(type,"selected","I"):nullptr;
        jfieldID cleanup=type?env->GetFieldID(type,"cleanupSlot","I"):nullptr;
        jfieldID velocity=type?env->GetFieldID(type,"velocity","D"):nullptr;
        jobject instance=ctor?env->NewObject(type,ctor):nullptr;
        jobject source=ctor?env->NewObject(type,ctor):nullptr;
        check(instance&&source&&use&&observe&&knock&&receive&&selected&&cleanup&&velocity&&!env->ExceptionCheck(),
            "item-use and impulse fixtures resolve");
        if(instance&&source&&use&&observe&&knock&&receive&&selected&&cleanup&&velocity&&!env->ExceptionCheck()) {
            mcoverlay::LiveItemUseTransform useHook;
            check(useHook.install(vm,use,&observe,
                [](void* owner,JNIEnv* jni,jobject mc,bool entering) noexcept {
                    jni->CallVoidMethod(mc,*static_cast<jmethodID*>(owner),entering?JNI_TRUE:JNI_FALSE);
                    if(jni->ExceptionCheck()) jni->ExceptionClear();
                    return false;
                }),"right-click entry/exit transform installs");
            useHook.setEnabled(true);
            env->CallVoidMethod(instance,use);
            check(!env->ExceptionCheck()&&env->GetIntField(instance,selected)==1&&
                env->GetIntField(instance,cleanup)==0,
                "refill runs after vanilla clears the old slot, never clears replacement");
            useHook.stop();
            struct Impulse {jobject selected;bool wildcard=false;} choice{env->NewGlobalRef(source)};
            mcoverlay::LiveImpulseTransform impulse;
            check(impulse.install(vm,knock,&choice,
                [](void* owner,JNIEnv* jni,jobject,jobject attacker) noexcept {
                    auto& choice=*static_cast<Impulse*>(owner);
                    return choice.wildcard||(attacker&&jni->IsSameObject(attacker,choice.selected));
                }),"confirmed-source impulse entry verifies in the JVM");
            impulse.setEnabled(true);
            env->SetDoubleField(instance,velocity,0.375);
            env->CallVoidMethod(instance,knock,source,1.0,2.0,3.0);
            check(!env->ExceptionCheck()&&env->GetDoubleField(instance,velocity)==0.375,
                "selected source cannot alter pre-existing velocity even transiently");
            env->CallVoidMethod(instance,knock,nullptr,1.0,2.0,3.0);
            check(!env->ExceptionCheck()&&env->GetDoubleField(instance,velocity)>0.375,
                "unknown source passes through in confirmed-only mode");
            choice.wildcard=true;
            env->SetDoubleField(instance,velocity,0.375);
            env->CallVoidMethod(instance,knock,nullptr,1.0,2.0,3.0);
            check(!env->ExceptionCheck()&&env->GetDoubleField(instance,velocity)==0.375,
                "explicit wildcard cancels unknown-source impulse at entry");
            mcoverlay::LiveVelocityTransform velocityHook;
            check(velocityHook.install(vm,receive,&choice,
                [](void* owner,JNIEnv*,jobject,jobject) noexcept {
                    return static_cast<Impulse*>(owner)->wildcard;
                }),"wildcard packet transform coexists with direct impulse transform");
            velocityHook.setEnabled(true);
            env->CallVoidMethod(instance,receive,source);
            check(!env->ExceptionCheck()&&env->GetDoubleField(instance,velocity)==0.375,
                "wildcard packet cancellation preserves ongoing velocity");
            velocityHook.stop();impulse.stop();
            env->DeleteGlobalRef(choice.selected);
            env->CallVoidMethod(instance,receive,source);
            check(!env->ExceptionCheck()&&env->GetDoubleField(instance,velocity)==99.0,
                "detach restores server velocity handling");
        } else if(env->ExceptionCheck()) env->ExceptionClear();
    }
    vm->DestroyJavaVM();
    std::printf("Logical pipeline JVM: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
