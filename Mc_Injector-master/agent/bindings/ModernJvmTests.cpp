#include "GameBindings.h"
#include "MappingPack.h"
#include <filesystem>
#include <fstream>
#include <iostream>

// A private synthetic JVM fixture, not Minecraft. Uses the production resolver,
// adapter, JNI sampling, capability gates and release path with -Xcheck:jni.
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    auto library=LoadLibraryW(std::filesystem::path(argv[1]).c_str());
    if (!library) {std::cerr<<"Cannot load fixture JVM: "<<GetLastError()<<'\n';return 2;}
    const auto create=reinterpret_cast<jint(JNICALL*)(JavaVM**,void**,void*)>(GetProcAddress(library,"JNI_CreateJavaVM"));
    JavaVM* vm=nullptr;JNIEnv* env=nullptr;
    std::string classpath="-Djava.class.path="+std::string(argv[2]);
    JavaVMOption options[]={{classpath.data(),nullptr},{const_cast<char*>("-Xcheck:jni"),nullptr}};
    JavaVMInitArgs args{};args.version=JNI_VERSION_1_8;args.nOptions=2;args.options=options;
    if (!create || create(&vm,reinterpret_cast<void**>(&env),&args)!=JNI_OK) return 2;
    int checks=0,failed=0;
    const auto check=[&](bool value,const char* label) {++checks;if(!value){++failed;std::cerr<<"FAIL "<<label<<'\n';}};
    auto boot=env->FindClass("FixtureBoot");
    check(boot && !env->ExceptionCheck(),"synthetic fixture loaded");
    if (!boot || env->ExceptionCheck()) {env->ExceptionClear();vm->DestroyJavaVM();return 2;}
    const auto call=[&](const char* name) {
        auto method=env->GetStaticMethodID(boot,name,"()V");
        if (env->ExceptionCheck() || !method) {env->ExceptionDescribe();env->ExceptionClear();return false;}
        env->CallStaticVoidMethod(boot,method);
        if (env->ExceptionCheck()) {env->ExceptionDescribe();env->ExceptionClear();return false;}
        return true;
    };
    check(call("boot"),"fixture initialized");
    jvmtiEnv* ti=nullptr;
    check(vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)==JNI_OK,"JVMTI 1.2 available");
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    {
        mcoverlay::GameBindings bindings(vm,ti,std::filesystem::path(argv[3]));
        check(!bindings.updateGameplay(env,{}, {},1),"pre-resolver update cannot run gameplay");
        const auto pendingDiagnostics=bindings.gameplayDiagnostics();
        check(!pendingDiagnostics.bindingMask && !pendingDiagnostics.missingBindings && pendingDiagnostics.updateCalls==1,
              "pre-resolver update does not mark optional predicates as observed");
        bindings.runResolver(env,stop);
        check(bindings.bindingsReady(),"modern production JNI resolver ready");
        check(bindings.versionCapabilities().supports(mcoverlay::bindings::VersionCapability::EntitySnapshots),"entity capability enabled");
        check(!bindings.versionCapabilities().supports(mcoverlay::bindings::VersionCapability::InventoryAccess),"legacy inventory disabled");
        check(!bindings.versionCapabilities().supports(mcoverlay::bindings::VersionCapability::LegacyGameplay),"legacy gameplay disabled");
        const auto idleDiagnostics=bindings.gameplayDiagnostics();
        check(idleDiagnostics.bindingMask==8 && idleDiagnostics.updateCalls==1 && !idleDiagnostics.hookMask && !idleDiagnostics.missingBindings,
              "published modern bindings do not imply observed legacy optional bindings or hooks");
        if (bindings.bindingsReady()) {
            const auto first=bindings.sample(env,100);
            check(first.state==mcoverlay::GameSnapshot::State::Ready,"world sample ready");
            check(first.x==11 && first.y==22 && first.z==33,"position methods used");
            check(first.yaw==90 && first.pitch==15,"rotation methods used");
            check(first.health==17 && first.maxHealth==20,"living entity state");
            check(first.bounds.minX==10.7 && first.bounds.maxY==23.8,"exact AABB fields");
            check(first.loadedEntities==3 && first.entityMarkerCount==1,"Iterable enumeration skips self and removed entity");
            check(first.entityMarkers[0].player,"remote Player base classified independently of LocalPlayer");
            check(!first.camera.valid && !first.silentAimAvailable,"unimplemented projection/interactions remain unavailable");
            check(bindings.setInputCaptured(env,true),"modern mouse release");
            check(bindings.maintainInputReleased(env),"modern mouse release maintenance");
            check(bindings.setInputCaptured(env,false),"modern mouse restore");
            check(!bindings.updateGameplay(env,{},first,100),"no legacy gameplay enters modern objects");
            const auto blocked=bindings.gameplayDiagnostics();
            check(blocked.updateCalls==2 && blocked.lastBlockReason==mcoverlay::GameplayBlockReason::UnsupportedVersion,
                  "diagnostic reports actual Adapter gate after a real update invocation");
            check(blocked.bindingMask==8 && !blocked.missingBindings,
                  "modern Adapter rejection clears stale legacy observations and cannot report legacy binding gaps");
            check(!blocked.hotbarCallbacks && !blocked.hotbarActions && !blocked.scaffoldAttempts && !blocked.scaffoldAccepted,
                  "Adapter rejection cannot invent feature callbacks or successful actions");
            check(call("badPosition"),"NaN fixture enabled");
            check(bindings.sample(env,200).state==mcoverlay::GameSnapshot::State::JniError,"invalid position fails closed");
            check(call("resetPosition"),"NaN fixture reset");
            check(bindings.sample(env,300).state==mcoverlay::GameSnapshot::State::Ready,"sample recovers without cached JNI mutation");
            check(call("clearWorld"),"world detached in fixture");
            const auto empty=bindings.sample(env,400);
            check(empty.state==mcoverlay::GameSnapshot::State::NoPlayer && !empty.entityMarkerCount,"world unload clears entity generation");
        }
        bindings.release(env);
        check(!bindings.bindingsReady(),"bindings release clears publication");
        check(!(bindings.gameplayDiagnostics().bindingMask&8U),"diagnostic core publication clears after release");
    }
    check(call("wrongVersion"),"fixture reports 1.20.2");
    {
        mcoverlay::GameBindings bindings(vm,ti,std::filesystem::path(argv[3]));
        bindings.runResolver(env,stop);
        check(!bindings.bindingsReady(),"nearby runtime version cannot become ready with 1.20.1 dictionary");
        bindings.release(env);
    }
    check(call("boot"),"fixture version reset");
    auto pack=mcoverlay::bindings::loadMappingPack(std::filesystem::path(argv[3]));
    pack.providers[0].dictionaries[0].getBounds="missingRequiredBounds";
    const auto bad=std::filesystem::path(argv[2])/"missing-required.json";
    {std::ofstream out(bad);out<<mcoverlay::bindings::mappingPackJson(pack).dump();}
    {
        mcoverlay::GameBindings bindings(vm,ti,bad);
        bindings.runResolver(env,stop);
        check(!bindings.bindingsReady(),"missing required JNI method cannot become ready");
        bindings.release(env);
    }
    check(!env->ExceptionCheck(),"no pending JNI exception on detach");
    CloseHandle(stop);env->DeleteLocalRef(boot);
    check(vm->DestroyJavaVM()==JNI_OK,"private JVM drained");
    std::cout<<checks<<" private modern JVM checks, "<<failed<<" failures (synthetic fixture, not Minecraft)\n";
    return failed?1:0;
}
