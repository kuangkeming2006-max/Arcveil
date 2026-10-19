#pragma once

#include <jni.h>

namespace mcoverlay::aim {

enum class ForcePlayerValidation {
    Valid,
    ContextUnavailable,
    Self,
    NotPlayer,
    PlayerListUnavailable,
    JniException
};

[[nodiscard]] inline constexpr const char* forcePlayerValidationReason(
    ForcePlayerValidation result) noexcept {
    switch(result) {
    case ForcePlayerValidation::Valid: return "force_player_valid";
    case ForcePlayerValidation::ContextUnavailable: return "force_player_context_unavailable";
    case ForcePlayerValidation::Self: return "force_player_self";
    case ForcePlayerValidation::NotPlayer: return "force_player_not_player";
    case ForcePlayerValidation::PlayerListUnavailable: return "force_player_list_unavailable";
    case ForcePlayerValidation::JniException: return "force_player_jni_exception";
    }
    return "force_player_invalid";
}

// The caller has just resolved target through the current world's entity ID.
// Recheck the same class / exact world-player membership classification used
// by snapshot collection; never use names, TAB, team state or health here.
// References are borrowed except for the scoped list / element local refs.
// The calling operation owns exception clearing and all geometry validation.
[[nodiscard]] inline ForcePlayerValidation validateForcePlayerEntity(
    JNIEnv* env,jobject world,jobject localPlayer,jobject target,
    jclass playerClass,jfieldID worldPlayerEntities,
    jmethodID listSize,jmethodID listGet) noexcept {
    if(!env||!world||!localPlayer||!target)
        return ForcePlayerValidation::ContextUnavailable;
    if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
    const bool self=env->IsSameObject(target,localPlayer)==JNI_TRUE;
    if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
    if(self) return ForcePlayerValidation::Self;
    if(playerClass) {
        const bool player=env->IsInstanceOf(target,playerClass)==JNI_TRUE;
        if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
        if(player) return ForcePlayerValidation::Valid;
    }
    if(!worldPlayerEntities||!listSize||!listGet)
        return ForcePlayerValidation::PlayerListUnavailable;
    jobject players=env->GetObjectField(world,worldPlayerEntities);
    struct LocalRef final {
        JNIEnv* env;
        jobject object;
        ~LocalRef() {if(object) env->DeleteLocalRef(object);}
    } playerList{env,players};
    if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
    if(!players) return ForcePlayerValidation::PlayerListUnavailable;
    const jint count=env->CallIntMethod(players,listSize);
    if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
    if(count<0) return ForcePlayerValidation::PlayerListUnavailable;
    // Snapshot collection already bounds loaded entities to 4096 and its
    // fallback player join to 64. Keep final revalidation bounded as well.
    const jint limit=count<4096?count:4096;
    for(jint index=0;index<limit;++index) {
        jobject candidate=env->CallObjectMethod(players,listGet,index);
        const LocalRef element{env,candidate};
        if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
        const bool player=candidate&&env->IsSameObject(target,candidate)==JNI_TRUE;
        if(env->ExceptionCheck()==JNI_TRUE) return ForcePlayerValidation::JniException;
        if(player) return ForcePlayerValidation::Valid;
    }
    return ForcePlayerValidation::NotPlayer;
}

} // namespace mcoverlay::aim
