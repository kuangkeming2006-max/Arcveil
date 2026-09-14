#pragma once

#include "FreeLookMethodWeaver.h"
#include "NativeFreeLookBridgeBytes.h"

#include <Windows.h>
#include <jvmti.h>

#include <atomic>
#include <array>
#include <cstdio>
#include <string>

namespace mcoverlay {

// Camera-only FreeLook hook. EntityRenderer keeps running its original render
// body; only mouse ownership and its four orientation reads are redirected.
class LiveFreeLookTransform final {
public:
    enum Angle : int { Yaw=0, Pitch=1, PreviousYaw=2, PreviousPitch=3 };
    using RotateCallback=void(*)(void*,JNIEnv*,jobject,jfloat,jfloat) noexcept;
    using AngleCallback=jfloat(*)(void*,JNIEnv*,jobject,Angle) noexcept;
    using DiagnosticCallback=void(*)(void*,const char*,const char*) noexcept;

    ~LiveFreeLookTransform() { stop(); }
    [[nodiscard]] bool ready() const noexcept {
        return m_renderReady&&m_inputReady&&terrainReady();
    }
    [[nodiscard]] bool renderReady() const noexcept { return m_renderReady; }
    [[nodiscard]] bool inputReady() const noexcept { return m_inputReady; }
    [[nodiscard]] bool terrainReady() const noexcept {
        return !m_terrainTarget||m_terrainReady;
    }
    [[nodiscard]] int lastError() const noexcept { return m_error; }

    bool install(JavaVM* vm,jmethodID update,jmethodID orient,
                  jmethodID setAnglesMethod,
                  jmethodID terrainMethod,
                  const char* entityOwner,const char* setAngles,
                  const std::array<const char*,4>& fields,void* owner,
                  RotateCallback rotate,AngleCallback angle,
                  DiagnosticCallback diagnostic=nullptr) noexcept {
        m_owner=owner;m_diagnostic=diagnostic;
        diagnose("HOOK_INSTALL","begin");
        if(ready()) return true;
        if(m_env&&m_renderTarget&&m_inputTarget) return retransformMissing();
        if(!vm||!update||!orient||!setAnglesMethod||
           !entityOwner||!setAngles||!rotate||!angle) {
            diagnose("HOOK_ERROR","invalid install arguments");
            return false;
        }
        for(const char* field:fields) if(!field||!*field) {
            diagnose("HOOK_ERROR","empty camera field mapping");
            return false;
        }
        JNIEnv* jni=nullptr;
        const jint jniStatus=vm->GetEnv(
            reinterpret_cast<void**>(&jni),JNI_VERSION_1_6);
        if(jniStatus!=JNI_OK||!jni) {
            char detail[96]{};
            std::snprintf(detail,sizeof(detail),"GetEnv JNI status=%d",jniStatus);
            diagnose("JNI_ERROR",detail);
            return false;
        }
        const jint jvmtiStatus=vm->GetEnv(
            reinterpret_cast<void**>(&m_env),JVMTI_VERSION_1_2);
        if(jvmtiStatus!=JNI_OK||!m_env) {
            char detail[96]{};
            std::snprintf(detail,sizeof(detail),"GetEnv JVMTI status=%d",jvmtiStatus);
            diagnose("JVMTI_ERROR",detail);
            return false;
        }
        m_vm=vm;
        jvmtiCapabilities capabilities{};
        capabilities.can_retransform_classes=1;
        capabilities.can_generate_all_class_hook_events=1;
        if((m_error=m_env->AddCapabilities(&capabilities))!=JVMTI_ERROR_NONE) {
            diagnoseError("AddCapabilities",m_error);
            stop(); return false;
        }
        const auto readMethod=[&](jmethodID method,std::string& name,
                                  std::string& descriptor) noexcept {
            char* rawName=nullptr;
            char* rawDescriptor=nullptr;
            const auto error=m_env->GetMethodName(
                method,&rawName,&rawDescriptor,nullptr);
            if(error!=JVMTI_ERROR_NONE||!rawName||!rawDescriptor) return false;
            try { name=rawName; descriptor=rawDescriptor; }
            catch(...) {
                m_env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
                m_env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
                return false;
            }
            m_env->Deallocate(reinterpret_cast<unsigned char*>(rawName));
            m_env->Deallocate(reinterpret_cast<unsigned char*>(rawDescriptor));
            return true;
        };
        if(!readMethod(update,m_updateName,m_updateDescriptor)||
           !readMethod(orient,m_orientName,m_orientDescriptor)||
           (terrainMethod&&!readMethod(terrainMethod,m_terrainName,
                                      m_terrainDescriptor))) {
            diagnose("HOOK_ERROR","GetMethodName failed");
            stop(); return false;
        }
        {
            char detail[512]{};
            std::snprintf(detail,sizeof(detail),
                "class=EntityRenderer update=%s%s orient=%s%s owner=%s setAngles=%s terrain=%s%s",
                m_updateName.c_str(),m_updateDescriptor.c_str(),
                m_orientName.c_str(),m_orientDescriptor.c_str(),
                entityOwner,setAngles,m_terrainName.c_str(),
                m_terrainDescriptor.c_str());
            diagnose("HOOK_IDENTITY",detail);
        }
        jclass updateClass=nullptr;
        jclass orientClass=nullptr;
        jclass inputClass=nullptr;
        jclass terrainClass=nullptr;
        jobject loader=nullptr;
        if(m_env->GetMethodDeclaringClass(update,&updateClass)!=JVMTI_ERROR_NONE||
           m_env->GetMethodDeclaringClass(orient,&orientClass)!=JVMTI_ERROR_NONE||
           m_env->GetMethodDeclaringClass(setAnglesMethod,&inputClass)!=JVMTI_ERROR_NONE||
           (terrainMethod&&m_env->GetMethodDeclaringClass(
               terrainMethod,&terrainClass)!=JVMTI_ERROR_NONE)||
           !updateClass||!orientClass||!inputClass||
           (terrainMethod&&!terrainClass)||
           jni->IsSameObject(updateClass,orientClass)!=JNI_TRUE||
           m_env->GetClassLoader(updateClass,&loader)!=JVMTI_ERROR_NONE) {
            if(updateClass) jni->DeleteLocalRef(updateClass);
            if(orientClass) jni->DeleteLocalRef(orientClass);
            if(inputClass) jni->DeleteLocalRef(inputClass);
            if(terrainClass) jni->DeleteLocalRef(terrainClass);
            diagnose("HOOK_ERROR","declaring class or loader mismatch");
            stop(); return false;
        }
        m_renderTarget=static_cast<jclass>(jni->NewGlobalRef(updateClass));
        m_inputTarget=static_cast<jclass>(jni->NewGlobalRef(inputClass));
        if(terrainClass)
            m_terrainTarget=static_cast<jclass>(jni->NewGlobalRef(terrainClass));
        jni->DeleteLocalRef(updateClass);
        jni->DeleteLocalRef(orientClass);
        jni->DeleteLocalRef(inputClass);
        if(terrainClass)jni->DeleteLocalRef(terrainClass);
        if(!m_renderTarget||!m_inputTarget||
           (terrainMethod&&!m_terrainTarget)||jni->ExceptionCheck()) {
            diagnose("JNI_ERROR","NewGlobalRef FreeLook target classes failed");
            jni->ExceptionClear(); stop(); return false;
        }
        try {
            m_entityOwner=entityOwner;
            m_setAngles=setAngles;
            for(std::size_t index=0;index<m_fields.size();++index)
                m_fields[index]=fields[index];
        } catch(...) {
            diagnose("HOOK_ERROR","mapping identity allocation failed");
            stop(); return false;
        }

        jclass classType=jni->FindClass("java/lang/Class");
        jmethodID forName=classType?jni->GetStaticMethodID(classType,"forName",
            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;"):nullptr;
        jstring bridgeName=jni->NewStringUTF("mcoverlay.NativeFreeLookBridge_v39");
        jclass bridge=forName&&bridgeName&&!jni->ExceptionCheck()
            ? static_cast<jclass>(jni->CallStaticObjectMethod(
                classType,forName,bridgeName,JNI_FALSE,loader)) : nullptr;
        if(jni->ExceptionCheck()) jni->ExceptionClear();
        if(!bridge) bridge=jni->DefineClass("mcoverlay/NativeFreeLookBridge_v39",loader,
            reinterpret_cast<const jbyte*>(nativeFreeLookBridgeBytes),
            sizeof(nativeFreeLookBridgeBytes));
        if(loader) jni->DeleteLocalRef(loader);
        if(bridgeName) jni->DeleteLocalRef(bridgeName);
        if(classType) jni->DeleteLocalRef(classType);
        if(!bridge||jni->ExceptionCheck()) {
            diagnose("JNI_ERROR","load or define NativeFreeLookBridge_v39 failed");
            jni->ExceptionClear(); stop(); return false;
        }
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&dispatchRotate),&module)) {
            diagnose("HOOK_ERROR","failed to pin native bridge module");
            jni->DeleteLocalRef(bridge); stop(); return false;
        }
        JNINativeMethod natives[]{
            {const_cast<char*>("rotateCamera"),
             const_cast<char*>("(Ljava/lang/Object;FF)V"),
             reinterpret_cast<void*>(&dispatchRotate)},
            {const_cast<char*>("cameraYaw"),
             const_cast<char*>("(Ljava/lang/Object;)F"),
             reinterpret_cast<void*>(&dispatchYaw)},
            {const_cast<char*>("cameraPitch"),
             const_cast<char*>("(Ljava/lang/Object;)F"),
             reinterpret_cast<void*>(&dispatchPitch)},
            {const_cast<char*>("previousCameraYaw"),
             const_cast<char*>("(Ljava/lang/Object;)F"),
             reinterpret_cast<void*>(&dispatchPreviousYaw)},
            {const_cast<char*>("previousCameraPitch"),
             const_cast<char*>("(Ljava/lang/Object;)F"),
             reinterpret_cast<void*>(&dispatchPreviousPitch)}};
        const bool registered=jni->RegisterNatives(bridge,natives,5)==JNI_OK;
        jni->DeleteLocalRef(bridge);
        if(!registered||jni->ExceptionCheck()) {
            diagnose("JNI_ERROR","RegisterNatives failed");
            jni->ExceptionClear(); stop(); return false;
        }
        AcquireSRWLockExclusive(&s_gate);
        const bool occupied=s_instance!=nullptr;
        if(!occupied) {
            s_instance=this; m_owner=owner; m_rotate=rotate; m_angle=angle;
        }
        ReleaseSRWLockExclusive(&s_gate);
        if(occupied) {
            diagnose("HOOK_ERROR","another FreeLook transform owns the bridge");
            stop(); return false;
        }
        jvmtiEventCallbacks events{};
        events.ClassFileLoadHook=&transform;
        if((m_error=m_env->SetEventCallbacks(&events,sizeof(events)))!=JVMTI_ERROR_NONE||
           (m_error=m_env->SetEventNotificationMode(
               JVMTI_ENABLE,JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,nullptr))!=JVMTI_ERROR_NONE) {
            diagnoseError("enable ClassFileLoadHook",m_error);
            stop(); return false;
        }
        m_transforming.store(true,std::memory_order_release);
        return retransformMissing();
    }

    [[nodiscard]] bool retransformMissing() noexcept {
        if(!m_env||!m_renderTarget||!m_inputTarget) return false;
        m_transforming.store(true,std::memory_order_release);
        if(!m_renderReady) {
            diagnose("RETRANSFORM","hook=render requested=1");
            const jvmtiError error=m_env->RetransformClasses(1,&m_renderTarget);
            if(error!=JVMTI_ERROR_NONE) {
                m_error=error;
                diagnoseError("RetransformClasses(render)",error);
            }
            m_renderReady=error==JVMTI_ERROR_NONE&&m_renderTransformed;
        }
        if(!m_inputReady) {
            diagnose("RETRANSFORM","hook=input requested=1");
            const jvmtiError error=m_env->RetransformClasses(1,&m_inputTarget);
            if(error!=JVMTI_ERROR_NONE) {
                m_error=error;
                diagnoseError("RetransformClasses(input)",error);
            }
            m_inputReady=error==JVMTI_ERROR_NONE&&m_inputTransformed;
        }
        if(m_terrainTarget&&!m_terrainReady) {
            diagnose("RETRANSFORM","hook=terrain requested=1");
            const jvmtiError error=m_env->RetransformClasses(1,&m_terrainTarget);
            if(error!=JVMTI_ERROR_NONE) {
                m_error=error;
                diagnoseError("RetransformClasses(terrain)",error);
            }
            m_terrainReady=error==JVMTI_ERROR_NONE&&m_terrainTransformed;
        }
        char detail[192]{};
        std::snprintf(detail,sizeof(detail),
            "renderReady=%d inputReady=%d terrainReady=%d combinedReady=%d",
            m_renderReady?1:0,m_inputReady?1:0,terrainReady()?1:0,
            ready()?1:0);
        diagnose(ready()?"READY_CHANGE":"HOOK_PARTIAL",detail);
        return ready();
    }

    void abandon() noexcept {
        diagnose("DETACH","abandon: JVM environment unavailable");
        AcquireSRWLockExclusive(&s_gate);
        if(s_instance==this) s_instance=nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        m_env=nullptr; m_renderTarget=nullptr; m_inputTarget=nullptr;
        m_terrainTarget=nullptr;
        m_renderReady=false;m_inputReady=false;m_terrainReady=false;
    }
    void stop() noexcept {
        if(!m_env) {
            m_renderReady=false;m_inputReady=false;m_terrainReady=false;
            m_renderTransformed=false;m_inputTransformed=false;
            m_terrainTransformed=false;
            m_renderTarget=nullptr;m_inputTarget=nullptr;
            m_terrainTarget=nullptr;m_vm=nullptr;
            return;
        }
        m_transforming.store(false,std::memory_order_release);
        jvmtiError renderRestore=JVMTI_ERROR_NONE;
        jvmtiError inputRestore=JVMTI_ERROR_NONE;
        jvmtiError terrainRestore=JVMTI_ERROR_NONE;
        if(m_renderTransformed&&m_renderTarget)
            renderRestore=m_env->RetransformClasses(1,&m_renderTarget);
        if(m_inputTransformed&&m_inputTarget)
            inputRestore=m_env->RetransformClasses(1,&m_inputTarget);
        if(m_terrainTransformed&&m_terrainTarget)
            terrainRestore=m_env->RetransformClasses(1,&m_terrainTarget);
        {
            char detail[256]{};
            std::snprintf(detail,sizeof(detail),
                "renderRequested=%d renderJvmti=%d inputRequested=%d inputJvmti=%d terrainRequested=%d terrainJvmti=%d",
                m_renderTransformed?1:0,renderRestore,
                m_inputTransformed?1:0,inputRestore,
                m_terrainTransformed?1:0,terrainRestore);
            diagnose("DETACH_RESTORE",detail);
        }
        m_env->SetEventNotificationMode(
            JVMTI_DISABLE,JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,nullptr);
        AcquireSRWLockExclusive(&s_gate);
        if(s_instance==this) s_instance=nullptr;
        ReleaseSRWLockExclusive(&s_gate);
        JNIEnv* jni=nullptr;
        if(m_vm&&m_vm->GetEnv(reinterpret_cast<void**>(&jni),JNI_VERSION_1_6)==JNI_OK) {
            if(m_renderTarget)jni->DeleteGlobalRef(m_renderTarget);
            if(m_inputTarget)jni->DeleteGlobalRef(m_inputTarget);
            if(m_terrainTarget)jni->DeleteGlobalRef(m_terrainTarget);
        }
        m_env->DisposeEnvironment();
        m_env=nullptr;m_renderTarget=nullptr;m_inputTarget=nullptr;
        m_terrainTarget=nullptr;
        m_renderReady=false;m_inputReady=false;m_terrainReady=false;
        m_renderTransformed=false;m_inputTransformed=false;
        m_terrainTransformed=false;
    }

private:
    void diagnose(const char* event,const char* detail) const noexcept {
        if(m_diagnostic)m_diagnostic(m_owner,event,detail?detail:"");
    }
    void diagnoseError(const char* operation,const int error) const noexcept {
        char detail[160]{};
        std::snprintf(detail,sizeof(detail),"operation=%s jvmti=%d",operation,error);
        diagnose("JVMTI_ERROR",detail);
    }
    static void JNICALL dispatchRotate(JNIEnv* env,jclass,jobject entity,
                                       jfloat yaw,jfloat pitch) noexcept {
        AcquireSRWLockShared(&s_gate);
        LiveFreeLookTransform* self=s_instance;
        if(self&&self->m_rotate) self->m_rotate(self->m_owner,env,entity,yaw,pitch);
        ReleaseSRWLockShared(&s_gate);
    }
    static jfloat dispatchAngle(JNIEnv* env,jobject entity,Angle angle) noexcept {
        jfloat value=0.0F;
        AcquireSRWLockShared(&s_gate);
        LiveFreeLookTransform* self=s_instance;
        if(self&&self->m_angle) value=self->m_angle(self->m_owner,env,entity,angle);
        ReleaseSRWLockShared(&s_gate);
        return value;
    }
    static jfloat JNICALL dispatchYaw(JNIEnv* env,jclass,jobject entity) noexcept {
        return dispatchAngle(env,entity,Yaw);
    }
    static jfloat JNICALL dispatchPitch(JNIEnv* env,jclass,jobject entity) noexcept {
        return dispatchAngle(env,entity,Pitch);
    }
    static jfloat JNICALL dispatchPreviousYaw(JNIEnv* env,jclass,jobject entity) noexcept {
        return dispatchAngle(env,entity,PreviousYaw);
    }
    static jfloat JNICALL dispatchPreviousPitch(JNIEnv* env,jclass,jobject entity) noexcept {
        return dispatchAngle(env,entity,PreviousPitch);
    }
    static void JNICALL transform(jvmtiEnv* env,JNIEnv* jni,jclass target,
                                  jobject,const char*,jobject,jint length,
                                  const unsigned char* bytes,jint* newLength,
                                  unsigned char** newBytes) noexcept {
        if(!target) return;
        struct Guard final {
            Guard(){AcquireSRWLockShared(&s_gate);}
            ~Guard(){ReleaseSRWLockShared(&s_gate);}
        } guard;
        LiveFreeLookTransform* self=s_instance;
        if(!self||self->m_env!=env||
           !self->m_transforming.load(std::memory_order_acquire)||
           length<=0) return;
        const bool renderTarget=jni->IsSameObject(
            target,self->m_renderTarget)==JNI_TRUE;
        const bool inputTarget=jni->IsSameObject(
            target,self->m_inputTarget)==JNI_TRUE;
        const bool terrainTarget=self->m_terrainTarget&&jni->IsSameObject(
            target,self->m_terrainTarget)==JNI_TRUE;
        if(!renderTarget&&!inputTarget&&!terrainTarget) return;
        self->diagnose("TRANSFORM_CALLBACK",
            renderTarget?"hook=render targetClassMatched=1":
            inputTarget?"hook=input targetClassMatched=1":
                        "hook=terrain targetClassMatched=1");
        if(jni->PushLocalFrame(384)<0) { jni->ExceptionClear(); return; }
        jbyteArray output=nullptr;
        if(renderTarget) {
            const std::array<const char*,4> fields{
                self->m_fields[0].c_str(),self->m_fields[1].c_str(),
                self->m_fields[2].c_str(),self->m_fields[3].c_str()};
            FreeLookWeaveReport report{};
            output=weaveFreeLookMethods(jni,
                {bytes,static_cast<std::size_t>(length)},
                self->m_updateName.c_str(),self->m_updateDescriptor.c_str(),
                self->m_orientName.c_str(),self->m_orientDescriptor.c_str(),
                self->m_entityOwner.c_str(),self->m_setAngles.c_str(),
                fields[0],fields[1],fields[2],fields[3],&report);
            char detail[1600]{};
            std::snprintf(detail,sizeof(detail),
                "hook=render stage=%s updateFound=%d orientFound=%d directSetAngles=%d yaw=%d pitch=%d prevYaw=%d prevPitch=%d observed={%s} callSites={%s}",
                report.stage,report.updateFound?1:0,report.orientFound?1:0,
                report.rotateCount,report.fieldCounts[0],report.fieldCounts[1],
                report.fieldCounts[2],report.fieldCounts[3],report.observed,
                report.callSites);
            self->diagnose(output?"WEAVER_RESULT":"WEAVER_ERROR",detail);
        } else if(inputTarget) {
            FreeLookInputWeaveReport report{};
            output=weaveFreeLookInputMethod(jni,
                {bytes,static_cast<std::size_t>(length)},
                self->m_setAngles.c_str(),&report);
            char detail[240]{};
            std::snprintf(detail,sizeof(detail),
                "hook=input stage=%s methodFound=%d replaced=%d",
                report.stage,report.methodFound?1:0,report.replacedMethods);
            self->diagnose(output?"WEAVER_RESULT":"WEAVER_ERROR",detail);
        } else {
            FreeLookTerrainWeaveReport report{};
            output=weaveFreeLookTerrainMethod(jni,
                {bytes,static_cast<std::size_t>(length)},
                self->m_terrainName.c_str(),self->m_terrainDescriptor.c_str(),
                self->m_entityOwner.c_str(),self->m_fields[0].c_str(),
                self->m_fields[1].c_str(),&report);
            char detail[640]{};
            std::snprintf(detail,sizeof(detail),
                "hook=terrain stage=%s methodFound=%d yaw=%d pitch=%d observed={%s}",
                report.stage,report.methodFound?1:0,report.yawReads,
                report.pitchReads,report.observed);
            self->diagnose(output?"WEAVER_RESULT":"WEAVER_ERROR",detail);
        }
        if(output&&!jni->ExceptionCheck()) {
            const jsize size=jni->GetArrayLength(output);
            unsigned char* allocated=nullptr;
            if(size>0&&env->Allocate(static_cast<jlong>(size),&allocated)==JVMTI_ERROR_NONE) {
                jni->GetByteArrayRegion(output,0,size,reinterpret_cast<jbyte*>(allocated));
                if(!jni->ExceptionCheck()) {
                    *newLength=size; *newBytes=allocated;
                    if(renderTarget)self->m_renderTransformed=true;
                    else if(inputTarget)self->m_inputTransformed=true;
                    else self->m_terrainTransformed=true;
                } else env->Deallocate(allocated);
            }
        }
        if(jni->ExceptionCheck()) jni->ExceptionClear();
        jni->PopLocalFrame(nullptr);
    }

    inline static SRWLOCK s_gate=SRWLOCK_INIT;
    inline static LiveFreeLookTransform* s_instance=nullptr;
    JavaVM* m_vm=nullptr;
    jvmtiEnv* m_env=nullptr;
    jclass m_renderTarget=nullptr;
    jclass m_inputTarget=nullptr;
    jclass m_terrainTarget=nullptr;
    void* m_owner=nullptr;
    RotateCallback m_rotate=nullptr;
    AngleCallback m_angle=nullptr;
    DiagnosticCallback m_diagnostic=nullptr;
    std::string m_updateName;
    std::string m_updateDescriptor;
    std::string m_orientName;
    std::string m_orientDescriptor;
    std::string m_terrainName;
    std::string m_terrainDescriptor;
    std::string m_entityOwner;
    std::string m_setAngles;
    std::array<std::string,4> m_fields{};
    std::atomic<bool> m_transforming{false};
    bool m_renderReady=false;
    bool m_inputReady=false;
    bool m_terrainReady=false;
    bool m_renderTransformed=false;
    bool m_inputTransformed=false;
    bool m_terrainTransformed=false;
    int m_error=0;
};

} // namespace mcoverlay
