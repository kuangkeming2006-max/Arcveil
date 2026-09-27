#include "../Json.h"
#include <jni.h>
#include <jvmti.h>
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <memory>
#include <thread>
#include <cctype>

using mcoverlay::mapping::Json;
namespace {
std::mutex captureMutex;
void require(jvmtiError result, const char* operation) {
    if (result != JVMTI_ERROR_NONE)
        throw std::runtime_error(std::string(operation)+": JVMTI "+std::to_string(result));
}
template<class T> struct Buffer {
    jvmtiEnv* ti; T* data=nullptr;
    ~Buffer(){if(data)ti->Deallocate(reinterpret_cast<unsigned char*>(data));}
};
std::string hex(const unsigned char* data, int count) {
    if(count<0||count>16*1024*1024)throw std::runtime_error("JVM buffer limit");
    const char* digits="0123456789abcdef";std::string out;out.reserve(std::size_t(count)*2);
    for(int i=0;i<count;++i){out+=digits[data[i]>>4];out+=digits[data[i]&15];}return out;
}
std::string signature(jvmtiEnv* ti,jclass klass) {
    if(!klass)return {};
    Buffer<char> value{ti};require(ti->GetClassSignature(klass,&value.data,nullptr),"GetClassSignature");
    return value.data?value.data:"";
}
struct Capture {
    JNIEnv* env; jvmtiEnv* ti;
    std::vector<jobject> loaders;
    ~Capture(){for(auto loader:loaders)env->DeleteLocalRef(loader);}
    int loaderId(jclass klass) {
        jobject loader=nullptr;require(ti->GetClassLoader(klass,&loader),"GetClassLoader");
        if(!loader)return 0;
        for(std::size_t i=0;i<loaders.size();++i)if(env->IsSameObject(loader,loaders[i])){
            env->DeleteLocalRef(loader);return int(i+1);
        }
        loaders.push_back(loader);return int(loaders.size());
    }
    Json classRef(jclass klass){return Json::Object{{"name",signature(ti,klass)},{"loader",klass?loaderId(klass):0}};}
    Json members(jclass klass) {
        Json::Array fields,methods,interfaces;
        jint count=0;Buffer<jfieldID> fieldIds{ti};
        require(ti->GetClassFields(klass,&count,&fieldIds.data),"GetClassFields");
        for(int i=0;i<count;++i){
            Buffer<char> name{ti},desc{ti};jint mods=0;
            require(ti->GetFieldName(klass,fieldIds.data[i],&name.data,&desc.data,nullptr),"GetFieldName");
            require(ti->GetFieldModifiers(klass,fieldIds.data[i],&mods),"GetFieldModifiers");
            fields.push_back(Json::Object{{"name",name.data},{"descriptor",desc.data},{"modifiers",mods}});
        }
        Buffer<jmethodID> methodIds{ti};require(ti->GetClassMethods(klass,&count,&methodIds.data),"GetClassMethods");
        for(int i=0;i<count;++i){
            Buffer<char> name{ti},desc{ti};jint mods=0;
            require(ti->GetMethodName(methodIds.data[i],&name.data,&desc.data,nullptr),"GetMethodName");
            require(ti->GetMethodModifiers(methodIds.data[i],&mods),"GetMethodModifiers");
            jboolean obsolete=JNI_FALSE;require(ti->IsMethodObsolete(methodIds.data[i],&obsolete),"IsMethodObsolete");
            if(obsolete)throw std::runtime_error("class changed during capture");
            std::string code;
            if(!(mods&(0x100|0x400))){
                jint length=0;Buffer<unsigned char> bytes{ti};
                require(ti->GetBytecodes(methodIds.data[i],&length,&bytes.data),"GetBytecodes");
                code=hex(bytes.data,length);
            }
            methods.push_back(Json::Object{{"name",name.data},{"descriptor",desc.data},{"modifiers",mods},{"bytecode",std::move(code)}});
        }
        auto order=[](const Json& a,const Json& b){return a.dump()<b.dump();};
        std::sort(fields.begin(),fields.end(),order);std::sort(methods.begin(),methods.end(),order);
        Buffer<jclass> interfaceIds{ti};require(ti->GetImplementedInterfaces(klass,&count,&interfaceIds.data),"GetImplementedInterfaces");
        for(int i=0;i<count;++i){interfaces.push_back(classRef(interfaceIds.data[i]));env->DeleteLocalRef(interfaceIds.data[i]);}
        std::sort(interfaces.begin(),interfaces.end(),order);
        jclass super=env->GetSuperclass(klass);auto superRef=classRef(super);if(super)env->DeleteLocalRef(super);
        jint cpCount=0,cpBytes=0,major=0,minor=0,mods=0;Buffer<unsigned char> pool{ti};
        require(ti->GetConstantPool(klass,&cpCount,&cpBytes,&pool.data),"GetConstantPool");
        require(ti->GetClassVersionNumbers(klass,&minor,&major),"GetClassVersionNumbers");
        require(ti->GetClassModifiers(klass,&mods),"GetClassModifiers");
        return Json::Object{{"name",signature(ti,klass)},{"loader",loaderId(klass)},
            {"super",superRef},{"interfaces",interfaces},{"modifiers",mods},{"major",major},{"minor",minor},
            {"fields",fields},{"methods",methods},{"constantPoolCount",cpCount},{"constantPool",hex(pool.data,cpBytes)}};
    }
    void warmup(const Json& requests){
        jint count=0;Buffer<jclass> classes{ti};require(ti->GetLoadedClasses(&count,&classes.data),"warmup GetLoadedClasses");
        struct Locals {JNIEnv* env;jclass* values;int count;~Locals(){for(int i=0;i<count;++i)env->DeleteLocalRef(values[i]);}} locals{env,classes.data,count};
        for(int i=0;i<count;++i){
            const auto name=signature(ti,classes.data[i]);
            for(const auto& request:requests.array())if(name==request.at("anchor").string()){
                jobject loader=nullptr;
                if(ti->GetClassLoader(classes.data[i],&loader)==JVMTI_ERROR_NONE&&loader){
                    jclass type=env->GetObjectClass(loader);auto load=type?env->GetMethodID(type,"loadClass","(Ljava/lang/String;)Ljava/lang/Class;"):nullptr;
                    if(env->ExceptionCheck())env->ExceptionClear();
                    if(load)for(const auto& binary:request.at("classes").array()){
                        jstring text=env->NewStringUTF(binary.string().c_str());
                        if(text){auto loaded=env->CallObjectMethod(loader,load,text);if(env->ExceptionCheck())env->ExceptionClear();if(loaded)env->DeleteLocalRef(loaded);env->DeleteLocalRef(text);}
                        if(env->ExceptionCheck())env->ExceptionClear();
                    }
                    if(type)env->DeleteLocalRef(type);env->DeleteLocalRef(loader);
                }
            }
        }
    }
    Json run() {
        jint count=0;Buffer<jclass> classes{ti};require(ti->GetLoadedClasses(&count,&classes.data),"GetLoadedClasses");
        struct Locals {JNIEnv* env;jclass* values;int count;~Locals(){for(int i=0;i<count;++i)env->DeleteLocalRef(values[i]);}} locals{env,classes.data,count};
        if(count>100000)throw std::runtime_error("loaded class limit");
        Json::Array output;std::size_t bytes=0;
        for(int i=0;i<count;++i){
            const auto name=signature(ti,classes.data[i]);
            if(name.empty()||name.front()!='L'||!loaderId(classes.data[i]))continue;
            jint status=0;require(ti->GetClassStatus(classes.data[i],&status),"GetClassStatus");
            if(!(status&JVMTI_CLASS_STATUS_PREPARED))continue;
            auto first=members(classes.data[i]);auto second=members(classes.data[i]);
            if(first!=second)throw std::runtime_error("unstable transformed class: "+name);
            bytes+=first.dump().size();if(bytes>48U*1024U*1024U)throw std::runtime_error("snapshot size limit");
            output.push_back(std::move(first));
        }
        Json::Array loaderInfo;
        for(std::size_t i=0;i<loaders.size();++i){jclass type=env->GetObjectClass(loaders[i]);loaderInfo.push_back(Json::Object{{"id",int(i+1)},{"type",signature(ti,type)}});env->DeleteLocalRef(type);}
        return Json::Object{{"classes",output},{"loaders",loaderInfo}};
    }
};
std::string decode(const char* options){
    if(!options)throw std::runtime_error("missing probe request");std::string text(options);
    if(text.size()>65536||text.size()%2)throw std::runtime_error("bad probe request");
    auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("bad request hex");};
    std::string out;for(std::size_t i=0;i<text.size();i+=2)out+=char(digit(text[i])*16+digit(text[i+1]));return out;
}
}
extern "C" __declspec(dllexport) jint JNICALL Agent_OnAttach(JavaVM* vm,char* options,void*) {
    std::unique_lock lock(captureMutex,std::try_to_lock);if(!lock.owns_lock())return 20;
    jvmtiEnv* ti=nullptr;JNIEnv* env=nullptr;std::filesystem::path errorPath;
    try {
        const auto request=Json::parse(decode(options));
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(request.at("output").string().c_str())));
        if(!path.is_absolute())throw std::runtime_error("absolute output required");
        errorPath=path.wstring()+L".error";
        if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_8)!=JNI_OK)throw std::runtime_error("JNI unavailable");
        if(vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)throw std::runtime_error("JVMTI unavailable");
        jvmtiCapabilities wanted{};wanted.can_get_bytecodes=1;wanted.can_get_constant_pool=1;
        require(ti->AddCapabilities(&wanted),"AddCapabilities");
        Json snapshot;
        {Capture capture{env,ti,{}};if(request.contains("warmup"))capture.warmup(request.at("warmup"));snapshot=capture.run();}
        Json::Array evidence;
        if(request.contains("detection"))for(const auto* property:{"java.home","java.class.path","sun.java.command"}){
            Buffer<char> hint{ti};if(ti->GetSystemProperty(property,&hint.data)!=JVMTI_ERROR_NONE||!hint.data)continue;
            std::string value(hint.data);std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});
            for(const auto& pattern:request.at("detection").array()){
                auto needle=pattern.at("value").string();std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
                if(!needle.empty()&&value.find(needle)!=std::string::npos)evidence.push_back(Json::Object{{"family",pattern.at("family")},{"confidence",pattern.at("confidence")}});
            }
        }
        snapshot["launchEvidence"]=evidence; // Never persist raw command lines, paths or property values.
        snapshot["snapshotVersion"]=1;snapshot["captureKind"]="jvmti-installed-double-read";
        snapshot["requestId"]=request.at("requestId");snapshot["pid"]=double(GetCurrentProcessId());
        FILETIME created{},exit{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))throw std::runtime_error("process identity unavailable");
        snapshot["processStart"]=std::to_string((static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime);
        snapshot["complete"]=true;
        // The probe owns a dedicated environment and installs no hooks or callbacks.
        ti->DisposeEnvironment();ti=nullptr;
        const auto temporary=path.wstring()+L".tmp";
        {std::ofstream file(std::filesystem::path(temporary),std::ios::binary|std::ios::trunc);auto text=snapshot.dump();file.write(text.data(),std::streamsize(text.size()));file.flush();if(!file)throw std::runtime_error("snapshot write failed");}
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("snapshot publish failed");
        return JNI_OK;
    } catch(const std::exception& e) {
        if(env&&env->ExceptionCheck())env->ExceptionClear();if(ti)ti->DisposeEnvironment();
        if(!errorPath.empty())try{std::ofstream file(errorPath,std::ios::binary);file<<Json(Json::Object{{"reason",e.what()}}).dump();}catch(...){}
        return JNI_ERR;
    } catch(...) {if(env&&env->ExceptionCheck())env->ExceptionClear();if(ti)ti->DisposeEnvironment();return JNI_ERR;}
}

// Existing native loader fallback, with a copied request and no main Agent runtime.
// The loaded probe remains resident like any LoadLibrary-loaded JVMTI helper.
extern "C" __declspec(dllexport) DWORD WINAPI McOverlay_Start(LPVOID rawOptions){
    try{
        const auto* options=static_cast<const char*>(rawOptions);if(!options)return 1;
        std::string copy(options);if(copy.size()>65536)return 1;
        using GetVms=jint(JNICALL*)(JavaVM**,jsize,jsize*);
        auto module=GetModuleHandleW(L"jvm.dll");auto get=module?reinterpret_cast<GetVms>(GetProcAddress(module,"JNI_GetCreatedJavaVMs")):nullptr;
        JavaVM* vm=nullptr;jsize count=0;if(!get||get(&vm,1,&count)!=JNI_OK||count!=1||!vm)return 1;
        std::thread([vm,copy=std::move(copy)]()mutable{
            JNIEnv* env=nullptr;bool attached=false;
            if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_8)!=JNI_OK){
                if(vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env),nullptr)!=JNI_OK)return;attached=true;
            }
            (void)Agent_OnAttach(vm,copy.data(),nullptr);
            if(attached)vm->DetachCurrentThread();
        }).detach();return 0;
    }catch(...){return 1;}
}
