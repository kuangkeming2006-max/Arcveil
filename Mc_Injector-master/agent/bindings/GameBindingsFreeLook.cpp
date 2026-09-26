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


} // namespace mcoverlay
