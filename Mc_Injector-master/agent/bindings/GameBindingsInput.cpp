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


} // namespace mcoverlay
