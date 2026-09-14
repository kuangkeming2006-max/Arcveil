#pragma once

#include "InteractionMethodWeaver.h"
#include "NativeLogicalBridgeBytes.h"

#include <Windows.h>
#include <jvmti.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace mcoverlay {

class LiveInteractionTransform final {
public:
    enum class Entry : std::uint8_t { Click, Held };
    using Callback = bool(*)(void*, JNIEnv*, jobject, Entry, bool) noexcept;

    ~LiveInteractionTransform() { stop(); }
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] int lastError() const noexcept { return m_error; }
    void setEnabled(const bool value) noexcept {
        m_enabled.store(value, std::memory_order_release);
    }

    bool install(JavaVM* vm, jmethodID click, jmethodID held, void* owner,
                 Callback callback) noexcept {
        if (m_ready) return true;
        if (!vm || (!click && !held) || !callback) return false;
        JNIEnv* jni = nullptr;
        if (vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni)
            return false;
        if (vm->GetEnv(reinterpret_cast<void**>(&m_env), JVMTI_VERSION_1_2) != JNI_OK || !m_env)
            return false;
        m_vm = vm;
        jvmtiCapabilities capabilities{};
        capabilities.can_retransform_classes = 1;
        capabilities.can_generate_all_class_hook_events = 1;
        if ((m_error = m_env->AddCapabilities(&capabilities)) != JVMTI_ERROR_NONE) {
            stop(); return false;
        }
        m_clickName.clear(); m_clickDescriptor.clear();
        m_heldName.clear(); m_heldDescriptor.clear();
        if ((click && !readMethod(m_env, click, m_clickName, m_clickDescriptor)) ||
            (held && !readMethod(m_env, held, m_heldName, m_heldDescriptor))) {
            stop(); return false;
        }
        jclass clickClass = nullptr;
        jclass heldClass = nullptr;
        jobject loader = nullptr;
        if ((click && (m_env->GetMethodDeclaringClass(click, &clickClass) != JVMTI_ERROR_NONE || !clickClass)) ||
            (held && (m_env->GetMethodDeclaringClass(held, &heldClass) != JVMTI_ERROR_NONE || !heldClass))) {
            if (clickClass) jni->DeleteLocalRef(clickClass);
            if (heldClass) jni->DeleteLocalRef(heldClass);
            stop(); return false;
        }
        if (clickClass && heldClass && jni->IsSameObject(clickClass, heldClass) != JNI_TRUE) {
            jni->DeleteLocalRef(clickClass); jni->DeleteLocalRef(heldClass);
            stop(); return false;
        }
        jclass targetClass = clickClass ? clickClass : heldClass;
        if (!targetClass || m_env->GetClassLoader(targetClass, &loader) != JVMTI_ERROR_NONE) {
            if (clickClass) jni->DeleteLocalRef(clickClass);
            if (heldClass) jni->DeleteLocalRef(heldClass);
            stop(); return false;
        }
        m_target = static_cast<jclass>(jni->NewGlobalRef(targetClass));
        if (clickClass) jni->DeleteLocalRef(clickClass);
        if (heldClass) jni->DeleteLocalRef(heldClass);
        if (!m_target || jni->ExceptionCheck()) {
            jni->ExceptionClear(); stop(); return false;
        }

        jclass classType = jni->FindClass("java/lang/Class");
        jmethodID forName = classType ? jni->GetStaticMethodID(classType, "forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
        jstring bridgeName = jni->NewStringUTF("mcoverlay.NativeLogicalBridge_v32");
        jclass bridge = forName && bridgeName && !jni->ExceptionCheck()
            ? static_cast<jclass>(jni->CallStaticObjectMethod(
                  classType, forName, bridgeName, JNI_FALSE, loader))
            : nullptr;
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        if (!bridge) bridge = jni->DefineClass("mcoverlay/NativeLogicalBridge_v32", loader,
            reinterpret_cast<const jbyte*>(nativeLogicalBridgeBytes),
            sizeof(nativeLogicalBridgeBytes));
        if (loader) jni->DeleteLocalRef(loader);
        if (bridgeName) jni->DeleteLocalRef(bridgeName);
        if (classType) jni->DeleteLocalRef(classType);
        if (!bridge || jni->ExceptionCheck()) {
            jni->ExceptionClear(); stop(); return false;
        }
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&dispatchClick), &module)) {
            jni->DeleteLocalRef(bridge); stop(); return false;
        }
        JNINativeMethod natives[] = {
            {const_cast<char*>("consumeClick"),
             const_cast<char*>("(Ljava/lang/Object;)Z"),
             reinterpret_cast<void*>(&dispatchClick)},
            {const_cast<char*>("consumeHeld"),
             const_cast<char*>("(Ljava/lang/Object;Z)Z"),
             reinterpret_cast<void*>(&dispatchHeld)}
        };
        const bool registered = jni->RegisterNatives(bridge, natives, 2) == JNI_OK;
        jni->DeleteLocalRef(bridge);
        if (!registered || jni->ExceptionCheck()) {
            jni->ExceptionClear(); stop(); return false;
        }
        AcquireSRWLockExclusive(&s_gate);
        const bool occupied = s_instance != nullptr;
        if (!occupied) { s_instance = this; m_owner = owner; m_callback = callback; }
        ReleaseSRWLockExclusive(&s_gate);
        if (occupied) { stop(); return false; }

        jvmtiEventCallbacks events{};
        events.ClassFileLoadHook = &transform;
        if ((m_error = m_env->SetEventCallbacks(&events, sizeof(events))) != JVMTI_ERROR_NONE ||
            (m_error = m_env->SetEventNotificationMode(
                JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr)) != JVMTI_ERROR_NONE) {
            stop(); return false;
        }
        m_transforming.store(true, std::memory_order_release);
        m_error = m_env->RetransformClasses(1, &m_target);
        m_ready = m_error == JVMTI_ERROR_NONE && m_transformed;
        if (!m_ready) { stop(); return false; }
        return true;
    }

    void abandon() noexcept {
        setEnabled(false);
        AcquireSRWLockExclusive(&s_gate);
        if (s_instance == this) s_instance = nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        m_env = nullptr; m_target = nullptr; m_ready = false;
    }

    void stop() noexcept {
        setEnabled(false);
        if (!m_env) return;
        m_transforming.store(false, std::memory_order_release);
        if (m_transformed && m_target) m_env->RetransformClasses(1, &m_target);
        m_env->SetEventNotificationMode(
            JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);
        AcquireSRWLockExclusive(&s_gate);
        if (s_instance == this) s_instance = nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        JNIEnv* jni = nullptr;
        if (m_vm && m_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) ==
                JNI_OK && m_target)
            jni->DeleteGlobalRef(m_target);
        m_env->DisposeEnvironment();
        m_env = nullptr; m_target = nullptr; m_ready = false; m_transformed = false;
    }

private:
    static bool readMethod(jvmtiEnv* env, jmethodID method, std::string& name,
                           std::string& descriptor) noexcept {
        char* rawName = nullptr;
        char* rawDescriptor = nullptr;
        if (env->GetMethodName(method, &rawName, &rawDescriptor, nullptr) !=
            JVMTI_ERROR_NONE) return false;
        try { name = rawName; descriptor = rawDescriptor; }
        catch (...) {
            env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
            env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
            return false;
        }
        env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
        env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
        return true;
    }
    static jboolean JNICALL dispatchClick(JNIEnv* jni, jclass,
                                          jobject minecraft) noexcept {
        return dispatch(jni, minecraft, Entry::Click, false);
    }
    static jboolean JNICALL dispatchHeld(JNIEnv* jni, jclass, jobject minecraft,
                                         const jboolean down) noexcept {
        return dispatch(jni, minecraft, Entry::Held, down == JNI_TRUE);
    }
    static jboolean dispatch(JNIEnv* jni, jobject minecraft, const Entry entry,
                             const bool down) noexcept {
        bool consume = false;
        AcquireSRWLockShared(&s_gate);
        LiveInteractionTransform* self = s_instance;
        if (self && self->m_callback &&
            self->m_enabled.load(std::memory_order_acquire))
            consume = self->m_callback(self->m_owner, jni, minecraft, entry, down);
        ReleaseSRWLockShared(&s_gate);
        return consume ? JNI_TRUE : JNI_FALSE;
    }
    static void JNICALL transform(jvmtiEnv* env, JNIEnv* jni, jclass target,
                                  jobject, const char*, jobject, jint length,
                                  const unsigned char* bytes, jint* newLength,
                                  unsigned char** newBytes) noexcept {
        if (!target) return;
        struct Guard final {
            Guard() { AcquireSRWLockShared(&s_gate); }
            ~Guard() { ReleaseSRWLockShared(&s_gate); }
        } guard;
        LiveInteractionTransform* self = s_instance;
        if (!self || self->m_env != env ||
            !self->m_transforming.load(std::memory_order_acquire) ||
            !jni->IsSameObject(target, self->m_target) || length <= 0) return;
        if (jni->PushLocalFrame(256) < 0) { jni->ExceptionClear(); return; }
        jbyteArray output = weaveInteractionMethods(jni,
            {bytes, static_cast<std::size_t>(length)}, self->m_clickName.c_str(),
            self->m_clickDescriptor.c_str(), self->m_heldName.c_str(),
            self->m_heldDescriptor.c_str());
        if (output && !jni->ExceptionCheck()) {
            const jsize size = jni->GetArrayLength(output);
            unsigned char* allocated = nullptr;
            if (size > 0 && env->Allocate(static_cast<jlong>(size), &allocated) ==
                    JVMTI_ERROR_NONE) {
                jni->GetByteArrayRegion(output, 0, size,
                    reinterpret_cast<jbyte*>(allocated));
                if (!jni->ExceptionCheck()) {
                    *newLength = size; *newBytes = allocated; self->m_transformed = true;
                } else env->Deallocate(allocated);
            }
        }
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        jni->PopLocalFrame(nullptr);
    }

    inline static SRWLOCK s_gate = SRWLOCK_INIT;
    inline static LiveInteractionTransform* s_instance = nullptr;
    JavaVM* m_vm = nullptr;
    jvmtiEnv* m_env = nullptr;
    jclass m_target = nullptr;
    void* m_owner = nullptr;
    Callback m_callback = nullptr;
    std::string m_clickName;
    std::string m_clickDescriptor;
    std::string m_heldName;
    std::string m_heldDescriptor;
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_transforming{false};
    bool m_ready = false;
    bool m_transformed = false;
    int m_error = 0;
};

} // namespace mcoverlay
