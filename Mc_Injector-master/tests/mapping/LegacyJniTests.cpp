#include "../../mapping/Json.h"
#include <jni.h>
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <sstream>
using mcoverlay::mapping::Json;
// Owned JVM, official game classes, no game instance/Attach/render/input.
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* reason){++checks;if(!ok){++failures;std::printf("FAIL %s\n",reason);}};
    auto library=LoadLibraryW(std::filesystem::path(argv[1]).c_str());
    if(!library)return 2;
    auto create=reinterpret_cast<jint(JNICALL*)(JavaVM**,void**,void*)>(GetProcAddress(library,"JNI_CreateJavaVM"));
    if(!create)return 2;
    std::ifstream paths(argv[2]);std::stringstream content;content<<paths.rdbuf();
    std::string classpath="-Djava.class.path="+content.str();
    JavaVMOption options[]{{classpath.data(),nullptr},{const_cast<char*>("-Xcheck:jni"),nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_8,2,options,JNI_FALSE};JavaVM* vm=nullptr;JNIEnv* env=nullptr;
    if(create(&vm,reinterpret_cast<void**>(&env),&args)!=JNI_OK)return 2;
    const auto pack=Json::read(std::filesystem::path(argv[3]));
    const auto contracts=Json::read(std::filesystem::path(argv[4]));Json symbols;
    for(const auto& provider:pack.at("providers").array())if(provider.at("family").string()=="Vanilla")
        for(const auto& dictionary:provider.at("dictionaries").array())if(dictionary.at("id").string()=="minecraft-1.8.9-vanilla-notch")symbols=dictionary.at("symbols");
    for(const auto* key:{"syncCurrentPlayItem","onPlayerRightClick","windowClick"}) {
        const auto& spec=contracts.at("symbols").at(key);
        const auto owner=symbols.at(spec.at("owner").string()).string();
        auto klass=env->FindClass(owner.c_str());const bool loaded=!env->ExceptionCheck() && klass;
        check(loaded,"official controller class loads in owned JVM");
        if(!loaded){env->ExceptionDescribe();env->ExceptionClear();continue;}
        std::string descriptor=spec.at("descriptor").string();
        for(std::size_t start=0;(start=descriptor.find('{',start))!=std::string::npos;) {
            const auto end=descriptor.find('}',start);
            descriptor.replace(start,end-start+1,symbols.at(descriptor.substr(start+1,end-start-1)).string());
        }
        auto method=env->GetMethodID(klass,symbols.at(key).string().c_str(),descriptor.c_str());
        const bool bound=!env->ExceptionCheck() && method;check(bound,key);
        if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();}
        env->DeleteLocalRef(klass);
    }
    auto controller=env->FindClass("bda");check(!env->ExceptionCheck()&&controller,"controller remains usable");
    if(env->ExceptionCheck())env->ExceptionClear();
    if(controller) {
        auto bad=env->GetMethodID(controller,"j","()V");
        check(!bad&&env->ExceptionCheck(),"historical j()V is rejected by real JNI");env->ExceptionClear();
        bad=env->GetMethodID(controller,"n","()Z");
        check(!bad&&env->ExceptionCheck(),"wrong descriptor n()Z is rejected by real JNI");env->ExceptionClear();
        auto boolean=env->GetMethodID(controller,"j","()Z");
        check(boolean&&!env->ExceptionCheck(),"j really returns boolean; no signature bypass");
        if(env->ExceptionCheck())env->ExceptionClear();env->DeleteLocalRef(controller);
    }
    check(!env->ExceptionCheck(),"no pending JNI exception");
    check(vm->DestroyJavaVM()==JNI_OK,"owned JVM drains");
    std::printf("Legacy JNI: %d checks, %d failures (official classes, owned JVM; no gameplay)\n",checks,failures);
    return failures?1:0;
}
