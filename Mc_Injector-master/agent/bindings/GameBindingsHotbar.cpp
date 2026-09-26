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
    // This is the vanilla hotbar key phase. A hotbar-only change writes the
    // selected slot here and lets vanilla synchronize it before the next use.
    // Main-inventory transfers remain queued for the guarded input boundary.
    m_smartHotbarRequest.store(triggeredSlot+1,std::memory_order_release);
    (void)processSmartHotbarRequests(env,minecraft,true);
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

bool GameBindings::processSmartHotbarRequests(JNIEnv* env,jobject minecraft,
                                               const bool hotbarKeyPhase) noexcept
{
    int encoded=m_smartHotbarRequest.exchange(0,std::memory_order_acq_rel);
    const bool refill=encoded==0&&!hotbarKeyPhase;
    if(refill) encoded=m_smartHotbarRefillRequest.exchange(0,std::memory_order_acq_rel);
    const auto serial=m_movementPacketSerial.load(std::memory_order_acquire);
    if(m_hotbarPausePhase==HotbarPausePhase::AwaitResumePacket&&
       serial>m_hotbarPausePacketSerial) restoreHotbarMovement(env);
    const auto requeue=[&]() noexcept {
        auto& queue=refill?m_smartHotbarRefillRequest:m_smartHotbarRequest;
        int empty=0;(void)queue.compare_exchange_strong(empty,encoded,
            std::memory_order_release,std::memory_order_relaxed);
    };
    if(!hotbarKeyPhase&&m_hotbarPausePhase!=HotbarPausePhase::None&&
       GetTickCount64()-m_hotbarPauseStartedMs>500U) {
        restoreHotbarMovement(env);
        m_logicalController.debug().event("HOTBAR_PAUSE_TIMEOUT",
            m_logicalController.latest(),"movement packet boundary unavailable",true);
        if(encoded>0&&encoded<=9) requeue();
        return false;
    }
    if(!hotbarKeyPhase&&m_hotbarPausePhase==HotbarPausePhase::AwaitResumePacket) {
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
    // A manual slot selection supersedes an older automatic refill request.
    if(refill&&current!=destination) return false;
    const auto available=std::span<const hotbar::ItemKind>(
        kinds.data(),static_cast<std::size_t>(length));
    const int source=refill?hotbar::selectRefillSource(available,current):
        hotbar::selectSource(available,current,wanted);
    if(source<0) {
        if(m_hotbarPausePhase==HotbarPausePhase::AwaitNeutralPacket) restoreHotbarMovement(env);
        // A configured shortcut with no matching item keeps the normal hotbar
        // selection instead of silently swallowing the player's key press.
        if(refill) return false;
        env->SetIntField(inventory,c->currentItem,destination);
        // No eager C09: the vanilla controller owns held-item publication.
        return env->ExceptionCheck()!=JNI_TRUE;
    }
    if(source>=9) {
        if(hotbarKeyPhase) {requeue();return false;}
        // Inventory clicks require action release and a post-use boundary.
        const bool actionHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0||
            (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0||
            m_logicalController.pendingAttack().kind!=silent::InteractionCommandKind::None;
        if(actionHeld||(refill&&serial<=
            m_refillQueuedPacketSerial.load(std::memory_order_acquire))) {
            if(actionHeld) restoreHotbarMovement(env);
            requeue();return false;
        }
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
        // Cancel an unexecuted transfer, but preserve the resume boundary of
        // an already-sent inventory click. Neither blocks this slot selection.
        if(m_hotbarPausePhase==HotbarPausePhase::AwaitNeutralPacket) restoreHotbarMovement(env);
        char detail[128]{};
        std::snprintf(detail,sizeof(detail),
            "source=%d destination=%d serial=%llu noPositionRun=%u",
            source,destination,static_cast<unsigned long long>(serial),
            m_noPositionPacketRun.load(std::memory_order_relaxed));
        m_logicalController.debug().event("HOTBAR_SWITCH",
            m_logicalController.latest(),detail,true);
    }
    env->SetIntField(inventory,c->currentItem,source<9?source:destination);
    if(source>=9&&!env->ExceptionCheck()) env->CallVoidMethod(controller,c->syncCurrentPlayItem);
    return env->ExceptionCheck()!=JNI_TRUE;
}


} // namespace mcoverlay
