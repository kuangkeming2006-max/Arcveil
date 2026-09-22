#pragma once

#include "ImpulseMethodWeaver.h"
#include "NativeImpulseBridgeBytes.h"

#include <Windows.h>
#include <jvmti.h>

#include <atomic>
#include <string>

namespace mcoverlay {

// Cancel at the original velocity-writing entry, never roll back a later tick.
template<bool Velocity=false> class BasicImpulseTransform final {
public:
    using Callback=bool(*)(void*,JNIEnv*,jobject,jobject) noexcept;
    ~BasicImpulseTransform(){stop();}
    [[nodiscard]] bool ready() const noexcept{return m_ready;}
    [[nodiscard]] int error() const noexcept{return m_error;}
    void setEnabled(bool value) noexcept{m_enabled.store(value,std::memory_order_release);}

    bool install(JavaVM* vm,jmethodID method,void* owner,Callback callback) noexcept {
        if(m_ready) return true;
        if(!vm||!method||!callback) return false;
        JNIEnv* jni=nullptr;
        if(vm->GetEnv(reinterpret_cast<void**>(&jni),JNI_VERSION_1_6)!=JNI_OK||!jni)
            return false;
        if(vm->GetEnv(reinterpret_cast<void**>(&m_env),JVMTI_VERSION_1_2)!=JNI_OK||!m_env)
            return false;
        m_vm=vm;
        jvmtiCapabilities capabilities{};
        capabilities.can_retransform_classes=1;
        capabilities.can_generate_all_class_hook_events=1;
        if((m_error=m_env->AddCapabilities(&capabilities))!=JVMTI_ERROR_NONE){stop();return false;}
        char* rawName=nullptr;char* rawDescriptor=nullptr;
        if((m_error=m_env->GetMethodName(method,&rawName,&rawDescriptor,nullptr))!=JVMTI_ERROR_NONE||
           !rawName||!rawDescriptor){stop();return false;}
        try{m_name=rawName;m_descriptor=rawDescriptor;}
        catch(...){
            m_env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
            m_env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
            stop();return false;
        }
        m_env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
        m_env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
        if(m_descriptor.find(Velocity?";)V":";FDD)V")==std::string::npos){stop();return false;}
        m_targetInternal="java/lang/Object";
        jclass declaring=nullptr;jobject loader=nullptr;
        if((m_error=m_env->GetMethodDeclaringClass(method,&declaring))!=JVMTI_ERROR_NONE||
           !declaring||(m_error=m_env->GetClassLoader(declaring,&loader))!=JVMTI_ERROR_NONE){
            if(declaring)jni->DeleteLocalRef(declaring);
            stop();return false;
        }
        m_target=static_cast<jclass>(jni->NewGlobalRef(declaring));
        jni->DeleteLocalRef(declaring);
        if(!m_target||jni->ExceptionCheck()){jni->ExceptionClear();stop();return false;}
        jclass classType=jni->FindClass("java/lang/Class");
        jmethodID forName=classType?jni->GetStaticMethodID(classType,"forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;"):nullptr;
        jstring bridgeName=jni->NewStringUTF("mcoverlay.NativeImpulseBridge_v50");
        jclass bridge=forName&&bridgeName&&!jni->ExceptionCheck()
            ?static_cast<jclass>(jni->CallStaticObjectMethod(classType,forName,
                bridgeName,JNI_FALSE,loader)):nullptr;
        if(jni->ExceptionCheck())jni->ExceptionClear();
        if(!bridge)bridge=jni->DefineClass("mcoverlay/NativeImpulseBridge_v50",loader,
            reinterpret_cast<const jbyte*>(nativeImpulseBridgeBytes),
            sizeof(nativeImpulseBridgeBytes));
        if(loader)jni->DeleteLocalRef(loader);
        if(bridgeName)jni->DeleteLocalRef(bridgeName);
        if(classType)jni->DeleteLocalRef(classType);
        if(!bridge||jni->ExceptionCheck()){jni->ExceptionClear();stop();return false;}
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
            GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&dispatch),&module)){
            jni->DeleteLocalRef(bridge);stop();return false;
        }
        JNINativeMethod native{const_cast<char*>(Velocity?"cancelVelocity":"cancelImpulse"),
            const_cast<char*>("(Ljava/lang/Object;Ljava/lang/Object;)Z"),
            reinterpret_cast<void*>(&dispatch)};
        const bool registered=jni->RegisterNatives(bridge,&native,1)==JNI_OK;
        jni->DeleteLocalRef(bridge);
        if(!registered||jni->ExceptionCheck()){jni->ExceptionClear();stop();return false;}
        AcquireSRWLockExclusive(&s_gate);
        const bool occupied=s_instance!=nullptr;
        if(!occupied){s_instance=this;m_owner=owner;m_callback=callback;}
        ReleaseSRWLockExclusive(&s_gate);
        if(occupied){stop();return false;}
        jvmtiEventCallbacks events{};events.ClassFileLoadHook=&transform;
        if((m_error=m_env->SetEventCallbacks(&events,sizeof(events)))!=JVMTI_ERROR_NONE||
           (m_error=m_env->SetEventNotificationMode(JVMTI_ENABLE,
             JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,nullptr))!=JVMTI_ERROR_NONE){stop();return false;}
        m_transforming=true;
        m_error=m_env->RetransformClasses(1,&m_target);
        m_ready=m_error==JVMTI_ERROR_NONE&&m_transformed;
        if(!m_ready){stop();return false;}
        return true;
    }

    void abandon() noexcept {
        setEnabled(false);AcquireSRWLockExclusive(&s_gate);
        if(s_instance==this)s_instance=nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        m_env=nullptr;m_target=nullptr;m_ready=false;
    }
    void stop() noexcept {
        setEnabled(false);if(!m_env)return;m_transforming=false;
        if(m_transformed&&m_target)m_env->RetransformClasses(1,&m_target);
        m_env->SetEventNotificationMode(JVMTI_DISABLE,
            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,nullptr);
        AcquireSRWLockExclusive(&s_gate);if(s_instance==this)s_instance=nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        JNIEnv* jni=nullptr;
        if(m_vm&&m_vm->GetEnv(reinterpret_cast<void**>(&jni),JNI_VERSION_1_6)==JNI_OK&&m_target)
            jni->DeleteGlobalRef(m_target);
        m_env->DisposeEnvironment();m_env=nullptr;m_target=nullptr;
        m_ready=false;m_transformed=false;
    }
private:
    static jboolean JNICALL dispatch(JNIEnv* env,jclass,jobject entity,jobject attacker) noexcept {
        jboolean result=JNI_FALSE;AcquireSRWLockShared(&s_gate);
        auto* self=s_instance;
        if(self&&self->m_callback&&self->m_enabled.load(std::memory_order_acquire))
            result=self->m_callback(self->m_owner,env,entity,attacker)?JNI_TRUE:JNI_FALSE;
        ReleaseSRWLockShared(&s_gate);return result;
    }
    static void JNICALL transform(jvmtiEnv* env,JNIEnv* jni,jclass target,
        jobject,const char*,jobject,jint length,const unsigned char* bytes,
        jint* newLength,unsigned char** newBytes) noexcept {
        if(!target)return;
        struct Guard{Guard(){AcquireSRWLockShared(&s_gate);}~Guard(){ReleaseSRWLockShared(&s_gate);}}guard;
        auto* self=s_instance;
        if(!self||self->m_env!=env||!self->m_transforming.load()||
           !jni->IsSameObject(target,self->m_target)||length<=0)return;
        if(jni->PushLocalFrame(256)<0){jni->ExceptionClear();return;}
        jbyteArray output=weaveImpulseMethod(jni,
            {bytes,static_cast<std::size_t>(length)},self->m_name.c_str(),
            self->m_descriptor.c_str(),self->m_targetInternal.c_str(),Velocity);
        if(output&&!jni->ExceptionCheck()){
            const jsize size=jni->GetArrayLength(output);unsigned char* allocated=nullptr;
            if(size>0&&env->Allocate(size,&allocated)==JVMTI_ERROR_NONE){
                jni->GetByteArrayRegion(output,0,size,reinterpret_cast<jbyte*>(allocated));
                if(!jni->ExceptionCheck()){
                    *newLength=size;*newBytes=allocated;self->m_transformed=true;
                }else env->Deallocate(allocated);
            }
        }
        if(jni->ExceptionCheck())jni->ExceptionClear();
        jni->PopLocalFrame(nullptr);
    }
    inline static SRWLOCK s_gate=SRWLOCK_INIT;
    inline static BasicImpulseTransform* s_instance=nullptr;
    JavaVM* m_vm=nullptr;jvmtiEnv* m_env=nullptr;jclass m_target=nullptr;
    void* m_owner=nullptr;Callback m_callback=nullptr;
    std::string m_name,m_descriptor,m_targetInternal;
    std::atomic<bool> m_enabled{false},m_transforming{false};
    bool m_ready=false,m_transformed=false;int m_error=0;
};
using LiveImpulseTransform=BasicImpulseTransform<false>;
using LiveVelocityTransform=BasicImpulseTransform<true>;

} // namespace mcoverlay
