#pragma once

#include "DiagnosticMethodWeaver.h"
#include "NativeDiagnosticBridgeBytes.h"

#include <Windows.h>
#include <jvmti.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace mcoverlay {

// Debug-only entry observation for ALL callers of the actual controller methods.
// No global MethodEntry events or late-attach breakpoint capabilities required.
class LiveInteractionObserver final {
public:
    enum class Event { Attack, StartBlock, DamageBlock, ResetBlock };
    using Callback = void(*)(void*, JNIEnv*, Event, jobject) noexcept;

    ~LiveInteractionObserver() { stop(); }
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] int error() const noexcept { return m_error; }
    void setEnabled(const bool value) noexcept {
        m_enabled.store(value, std::memory_order_release);
    }

    bool install(JavaVM* vm, std::array<jmethodID,4> methods, void* owner,
                 Callback callback) noexcept {
        if (m_ready) return true;
        if (!vm || !methods[0] || !callback) return false;
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
        m_names={};m_descriptors={};
        for(std::size_t i=0;i<methods.size();++i) if(methods[i]) {
            if(!readMethod(m_env,methods[i],m_names[i],m_descriptors[i])) {stop();return false;}
            jclass declaring=nullptr;
            if((m_error=m_env->GetMethodDeclaringClass(methods[i],&declaring))!=JVMTI_ERROR_NONE || !declaring) {
                stop();return false;
            }
            const bool same=!m_target || jni->IsSameObject(m_target,declaring)==JNI_TRUE;
            if(!m_target) m_target=static_cast<jclass>(jni->NewGlobalRef(declaring));
            jni->DeleteLocalRef(declaring);
            if(!same || !m_target || jni->ExceptionCheck()) {jni->ExceptionClear();stop();return false;}
        }
        jobject loader=nullptr;
        if((m_error=m_env->GetClassLoader(m_target,&loader))!=JVMTI_ERROR_NONE) {stop();return false;}

        jclass classType = jni->FindClass("java/lang/Class");
        jmethodID forName = classType ? jni->GetStaticMethodID(classType, "forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
        jstring bridgeName = jni->NewStringUTF("mcoverlay.NativeDiagnosticBridge_v33");
        jclass bridge = forName && bridgeName && !jni->ExceptionCheck()
            ? static_cast<jclass>(jni->CallStaticObjectMethod(
                  classType, forName, bridgeName, JNI_FALSE, loader))
            : nullptr;
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        if (!bridge) bridge = jni->DefineClass("mcoverlay/NativeDiagnosticBridge_v33", loader,
            reinterpret_cast<const jbyte*>(nativeDiagnosticBridgeBytes),
            sizeof(nativeDiagnosticBridgeBytes));
        if (loader) jni->DeleteLocalRef(loader);
        if (bridgeName) jni->DeleteLocalRef(bridgeName);
        if (classType) jni->DeleteLocalRef(classType);
        if (!bridge || jni->ExceptionCheck()) {
            jni->ExceptionClear(); stop(); return false;
        }
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&dispatch), &module)) {
            jni->DeleteLocalRef(bridge); stop(); return false;
        }
        JNINativeMethod natives[] = {
            {const_cast<char*>("observe"),const_cast<char*>("(ILjava/lang/Object;)V"),
             reinterpret_cast<void*>(&dispatch)}
        };
        const bool registered = jni->RegisterNatives(bridge,natives,1)==JNI_OK;
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
        setEnabled(true);
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
    static void JNICALL dispatch(JNIEnv* jni,jclass,jint event,jobject argument) noexcept {
        AcquireSRWLockShared(&s_gate);
        auto* self=s_instance;
        if(self && self->m_enabled.load(std::memory_order_acquire) && self->m_callback &&
            event>=0 && event<4 && !jni->ExceptionCheck()) {
            self->m_callback(self->m_owner,jni,static_cast<Event>(event),argument);
            // Diagnostics must not inject an exception into gameplay.
            if(jni->ExceptionCheck()) jni->ExceptionClear();
        }
        ReleaseSRWLockShared(&s_gate);
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
        LiveInteractionObserver* self = s_instance;
        if (!self || self->m_env != env ||
            !self->m_transforming.load(std::memory_order_acquire) ||
            !jni->IsSameObject(target, self->m_target) || length <= 0) return;
        if (jni->PushLocalFrame(256) < 0) { jni->ExceptionClear(); return; }
        jbyteArray output = weaveDiagnosticMethods(jni,
            {bytes, static_cast<std::size_t>(length)}, self->m_names,self->m_descriptors);
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
    inline static LiveInteractionObserver* s_instance = nullptr;
    JavaVM* m_vm = nullptr;
    jvmtiEnv* m_env = nullptr;
    jclass m_target = nullptr;
    void* m_owner = nullptr;
    Callback m_callback = nullptr;
    std::array<std::string,4> m_names{},m_descriptors{};
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_transforming{false};
    bool m_ready = false;
    bool m_transformed = false;
    int m_error = 0;
};

} // namespace mcoverlay
