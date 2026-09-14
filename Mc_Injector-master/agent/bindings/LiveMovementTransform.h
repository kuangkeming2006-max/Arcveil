#pragma once

#include "MovementMethodWeaver.h"
#include "NativeLogicalBridgeBytes.h"

#include <Windows.h>
#include <jvmti.h>

#include <atomic>
#include <string>

namespace mcoverlay {

// Retransforms Entity::moveFlying(float,float,float).  The native callback
// supplies the authoritative logical strafe/forward pair before vanilla does
// any acceleration calculation and restores the visible camera afterwards.
class LiveMovementTransform final {
public:
    using BeginCallback = jfloat(*)(void*, JNIEnv*, jobject, jfloat, jfloat) noexcept;
    using ForwardCallback = jfloat(*)(void*, JNIEnv*, jobject, jfloat) noexcept;
    using EndCallback = void(*)(void*, JNIEnv*, jobject) noexcept;
    using SprintCallback = bool(*)(void*, JNIEnv*, jobject, bool) noexcept;

    ~LiveMovementTransform() { stop(); }
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] int lastError() const noexcept { return m_error; }
    void setEnabled(const bool value) noexcept {
        m_enabled.store(value, std::memory_order_release);
    }

    bool install(JavaVM* vm, jmethodID method, jmethodID sprintMethod, void* owner,
                 BeginCallback begin, ForwardCallback forward,
                 EndCallback end, SprintCallback sprint) noexcept {
        if (m_ready) return true;
        if (!vm || !method || !sprintMethod || !begin || !forward || !end ||
            !sprint) return false;
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
        char* name = nullptr;
        char* descriptor = nullptr;
        if ((m_error = m_env->GetMethodName(method, &name, &descriptor, nullptr)) !=
            JVMTI_ERROR_NONE) {
            stop(); return false;
        }
        try { m_name = name; m_descriptor = descriptor; }
        catch (...) {
            m_env->Deallocate(reinterpret_cast<unsigned char*>(name));
            m_env->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
            stop(); return false;
        }
        m_env->Deallocate(reinterpret_cast<unsigned char*>(name));
        m_env->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
        name = nullptr;
        descriptor = nullptr;
        if ((m_error = m_env->GetMethodName(
                 sprintMethod, &name, &descriptor, nullptr)) != JVMTI_ERROR_NONE) {
            stop(); return false;
        }
        try { m_sprintName = name; m_sprintDescriptor = descriptor; }
        catch (...) {
            m_env->Deallocate(reinterpret_cast<unsigned char*>(name));
            m_env->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
            stop(); return false;
        }
        m_env->Deallocate(reinterpret_cast<unsigned char*>(name));
        m_env->Deallocate(reinterpret_cast<unsigned char*>(descriptor));

        jclass target = nullptr;
        jclass sprintTarget = nullptr;
        jobject loader = nullptr;
        if (m_env->GetMethodDeclaringClass(method, &target) != JVMTI_ERROR_NONE || !target ||
            m_env->GetMethodDeclaringClass(sprintMethod, &sprintTarget) != JVMTI_ERROR_NONE ||
            !sprintTarget || jni->IsSameObject(target, sprintTarget) != JNI_TRUE ||
            m_env->GetClassLoader(target, &loader) != JVMTI_ERROR_NONE) {
            if (target) jni->DeleteLocalRef(target);
            if (sprintTarget) jni->DeleteLocalRef(sprintTarget);
            stop(); return false;
        }
        m_target = static_cast<jclass>(jni->NewGlobalRef(target));
        jni->DeleteLocalRef(target);
        jni->DeleteLocalRef(sprintTarget);
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
                reinterpret_cast<LPCWSTR>(&dispatchBegin), &module)) {
            jni->DeleteLocalRef(bridge); stop(); return false;
        }
        JNINativeMethod natives[] = {
            {const_cast<char*>("beginMovement"),
             const_cast<char*>("(Ljava/lang/Object;FF)F"),
             reinterpret_cast<void*>(&dispatchBegin)},
            {const_cast<char*>("mappedForward"),
             const_cast<char*>("(Ljava/lang/Object;)F"),
             reinterpret_cast<void*>(&dispatchForward)},
            {const_cast<char*>("endMovement"),
             const_cast<char*>("(Ljava/lang/Object;)V"),
             reinterpret_cast<void*>(&dispatchEnd)},
            {const_cast<char*>("arbitrateSprint"),
             const_cast<char*>("(Ljava/lang/Object;Z)Z"),
             reinterpret_cast<void*>(&dispatchSprint)}
        };
        const bool registered = jni->RegisterNatives(bridge, natives, 4) == JNI_OK;
        jni->DeleteLocalRef(bridge);
        if (!registered || jni->ExceptionCheck()) {
            jni->ExceptionClear(); stop(); return false;
        }

        AcquireSRWLockExclusive(&s_gate);
        const bool occupied = s_instance != nullptr;
        if (!occupied) {
            s_instance = this; m_owner = owner; m_begin = begin;
            m_forward = forward; m_end = end; m_sprint = sprint;
        }
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
    static jfloat JNICALL dispatchBegin(JNIEnv* jni, jclass, jobject subject,
                                        const jfloat strafe,
                                        const jfloat forward) noexcept {
        s_originalForward = forward;
        jfloat result = strafe;
        AcquireSRWLockShared(&s_gate);
        LiveMovementTransform* self = s_instance;
        if (self && self->m_begin && self->m_enabled.load(std::memory_order_acquire))
            result = self->m_begin(self->m_owner, jni, subject, strafe, forward);
        ReleaseSRWLockShared(&s_gate);
        return result;
    }
    static jfloat JNICALL dispatchForward(JNIEnv* jni, jclass,
                                          jobject subject) noexcept {
        jfloat result = s_originalForward;
        AcquireSRWLockShared(&s_gate);
        LiveMovementTransform* self = s_instance;
        if (self && self->m_forward)
            result = self->m_forward(self->m_owner, jni, subject, result);
        ReleaseSRWLockShared(&s_gate);
        return result;
    }
    static void JNICALL dispatchEnd(JNIEnv* jni, jclass, jobject subject) noexcept {
        AcquireSRWLockShared(&s_gate);
        LiveMovementTransform* self = s_instance;
        // Always notify the exit: enabled may change while moveFlying is on the
        // Java stack, but a yaw temporarily installed at entry must still be
        // restored.
        if (self && self->m_end) self->m_end(self->m_owner, jni, subject);
        ReleaseSRWLockShared(&s_gate);
    }
    static jboolean JNICALL dispatchSprint(JNIEnv* jni, jclass, jobject subject,
                                           const jboolean requested) noexcept {
        bool result=requested==JNI_TRUE;
        AcquireSRWLockShared(&s_gate);
        LiveMovementTransform* self=s_instance;
        if(self&&self->m_sprint&&self->m_enabled.load(std::memory_order_acquire))
            result=self->m_sprint(self->m_owner,jni,subject,result);
        ReleaseSRWLockShared(&s_gate);
        return result?JNI_TRUE:JNI_FALSE;
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
        LiveMovementTransform* self = s_instance;
        if (!self || self->m_env != env ||
            !self->m_transforming.load(std::memory_order_acquire) ||
            !jni->IsSameObject(target, self->m_target) || length <= 0) return;
        if (jni->PushLocalFrame(256) < 0) { jni->ExceptionClear(); return; }
        jbyteArray output = weaveMovementMethod(jni,
            {bytes, static_cast<std::size_t>(length)}, self->m_name.c_str(),
            self->m_descriptor.c_str(),self->m_sprintName.c_str(),
            self->m_sprintDescriptor.c_str());
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
    inline static LiveMovementTransform* s_instance = nullptr;
    inline static thread_local jfloat s_originalForward = 0.0F;
    JavaVM* m_vm = nullptr;
    jvmtiEnv* m_env = nullptr;
    jclass m_target = nullptr;
    void* m_owner = nullptr;
    BeginCallback m_begin = nullptr;
    ForwardCallback m_forward = nullptr;
    EndCallback m_end = nullptr;
    SprintCallback m_sprint = nullptr;
    std::string m_name;
    std::string m_descriptor;
    std::string m_sprintName;
    std::string m_sprintDescriptor;
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_transforming{false};
    bool m_ready = false;
    bool m_transformed = false;
    int m_error = 0;
};

} // namespace mcoverlay
