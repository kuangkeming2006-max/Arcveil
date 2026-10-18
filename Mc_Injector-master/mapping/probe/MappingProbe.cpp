#include "../Json.h"
#include "../SnapshotStream.h"
#include "../ProbeProtocol.h"
#include "../InstalledProof.h"
#include <jni.h>
#include <jvmti.h>
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <memory>
#include <thread>
#include <cctype>

using namespace mcoverlay::mapping;
namespace {
std::mutex captureMutex;
std::string installedHash(const Json &klass, bool content = true) {
    const auto material = content ? installedClassMaterial(klass) : installedHierarchyMaterial(klass);
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("installed proof SHA256 provider unavailable");
    struct Cleanup { HCRYPTPROV p; HCRYPTHASH &h; ~Cleanup() { if (h) CryptDestroyHash(h); CryptReleaseContext(p, 0); } } cleanup{provider, hash};
    unsigned char digest[32]{}; DWORD size = sizeof(digest);
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) ||
        !CryptHashData(hash, reinterpret_cast<const BYTE *>(material.data()), DWORD(material.size()), 0) ||
        !CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) || size != sizeof(digest))
        throw std::runtime_error("installed proof SHA256 failed");
    std::string out; constexpr char digits[] = "0123456789abcdef";
    for (auto b : digest) { out += digits[b >> 4]; out += digits[b & 15]; }
    return out;
}
thread_local Json *callStats = nullptr;
void countCall(const char *operation) {
    if (!callStats) return;
    auto &counts = (*callStats)["jvmtiCallsByOperation"];
    counts[operation] = (counts.contains(operation) ? counts.at(operation).number() : 0) + 1;
    (*callStats)["jvmtiCalls"] = (*callStats).at("jvmtiCalls").number() + 1;
}
void progress(const std::filesystem::path &output, const char *stage) {
    std::ofstream file(std::filesystem::path(output.wstring() + L".status"), std::ios::binary);
    file << Json(Json::Object{{"stage", stage}}).dump();
}
void require(jvmtiError result, const char *operation) {
    countCall(operation);
    const auto reason = std::string(operation) + ": JVMTI " + std::to_string(result);
    if (result == JVMTI_ERROR_CLASS_NOT_PREPARED || result == JVMTI_ERROR_INVALID_CLASS ||
        result == JVMTI_ERROR_INVALID_METHODID || result == JVMTI_ERROR_INVALID_FIELDID)
        throw RuntimeChanged(reason);
    if (result != JVMTI_ERROR_NONE)
        throw std::runtime_error(reason);
}
template <class T> struct Buffer {
    jvmtiEnv *ti;
    T *data = nullptr;
    ~Buffer() {
        if (data) {
            countCall("Deallocate");
            ti->Deallocate(reinterpret_cast<unsigned char *>(data));
        }
    }
};
std::string hex(const unsigned char *data, int count, const std::string &kind) {
    if (count < 0 || count > 16 * 1024 * 1024)
        throw SnapshotLimit("JVM buffer", kind, count < 0 ? 0 : std::size_t(count),
                            16 * 1024 * 1024);
    const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(std::size_t(count) * 2);
    for (int i = 0; i < count; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
std::string signature(jvmtiEnv *ti, jclass klass) {
    if (!klass)
        return {};
    Buffer<char> value{ti};
    require(ti->GetClassSignature(klass, &value.data, nullptr), "GetClassSignature");
    return value.data ? value.data : "";
}
// These names are stable evidence APIs in the audited vanilla artifacts, not
// candidate mapping symbols. A resource/getter alone never authorizes bindings.
struct EvidenceFrame {
    JNIEnv *env;
    explicit EvidenceFrame(JNIEnv *value) : env(value) {
        if (env->PushLocalFrame(64) != JNI_OK)
            throw std::runtime_error("runtime evidence JNI frame unavailable");
    }
    ~EvidenceFrame() { if (env->ExceptionCheck()) env->ExceptionClear(); env->PopLocalFrame(nullptr); }
};
bool evidenceException(JNIEnv *env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear(); return true;
}
std::string evidenceString(JNIEnv *env, jstring text, std::size_t limit) {
    if (!text || env->GetStringUTFLength(text) > jint(limit)) return {};
    const char *bytes = env->GetStringUTFChars(text, nullptr);
    if (!bytes) { evidenceException(env); return {}; }
    std::string out(bytes); env->ReleaseStringUTFChars(text, bytes); return out;
}
bool semanticVersion(const std::string &text) {
    if (text.empty() || text.size() > 32) return false;
    unsigned parts = 1, digits = 0;
    for (unsigned char ch : text) {
        if (ch == '.') { if (!digits || ++parts > 3) return false; digits = 0; }
        else if (ch >= '0' && ch <= '9') { if (++digits > 4) return false; }
        else return false;
    }
    return parts == 3 && digits;
}
std::string evidenceExceptionType(JNIEnv *env) {
    if (!env->ExceptionCheck()) return {};
    auto error=env->ExceptionOccurred(); env->ExceptionClear();
    auto type=env->GetObjectClass(error), classType=env->FindClass("java/lang/Class");
    auto getName=classType?env->GetMethodID(classType,"getName","()Ljava/lang/String;"):nullptr;
    std::string result="JNI exception";
    if (!evidenceException(env) && getName) {
        auto name=static_cast<jstring>(env->CallObjectMethod(type,getName));
        if (!evidenceException(env)) result=evidenceString(env,name,128);
        if(name)env->DeleteLocalRef(name);
    }
    if(type)env->DeleteLocalRef(type); if(classType)env->DeleteLocalRef(classType); if(error)env->DeleteLocalRef(error);
    return result;
}
jclass evidenceClass(JNIEnv *env, jvmtiEnv *ti, jobject loader, const char *name,
                     std::string *diagnostic=nullptr) {
    const auto failed=[&](std::string reason)->jclass {
        const auto error=evidenceExceptionType(env); if(!error.empty())reason+="; "+error;
        if(diagnostic)*diagnostic=std::move(reason); return nullptr;
    };
    auto type = env->FindClass("java/lang/Class");
    auto load = type ? env->GetStaticMethodID(type, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
    if (env->ExceptionCheck() || !load) return failed("Class.forName unavailable");
    auto text = env->NewStringUTF(name);
    if (env->ExceptionCheck() || !text) return failed("class name allocation failed");
    auto klass = static_cast<jclass>(env->CallStaticObjectMethod(type, load, text, JNI_FALSE, loader));
    if (env->ExceptionCheck() || !klass) return failed(std::string("class not resolvable: ")+name);
    jobject defining = nullptr;
    countCall("GetClassLoader:runtime-evidence");
    const auto result=ti->GetClassLoader(klass, &defining);
    if (result!=JVMTI_ERROR_NONE) return failed("defining loader lookup failed: "+std::to_string(result));
    if (!defining || !env->IsSameObject(loader, defining)) return failed("resolved class belongs to a different defining loader");
    return klass;
}
struct VersionResource {
    std::string version, diagnostic;
    std::string observed;
    bool operator==(const VersionResource &) const = default;
};
VersionResource resourceVersion(JNIEnv *env, jobject loader) {
    EvidenceFrame frame(env);
    auto type = env->FindClass("java/lang/ClassLoader");
    auto open = type ? env->GetMethodID(type, "getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;") : nullptr;
    if (env->ExceptionCheck() || !open) return {{}, "version.json resource API unavailable; "+evidenceExceptionType(env)};
    auto name = env->NewStringUTF("version.json");
    auto input = name ? env->CallObjectMethod(loader, open, name) : nullptr;
    if (env->ExceptionCheck()) return {{}, "version.json resource lookup threw; "+evidenceExceptionType(env)};
    if (!input) return {{}, "version.json resource not visible to anchor loader"};
    auto inputType = env->FindClass("java/io/InputStream");
    auto read = inputType ? env->GetMethodID(inputType, "read", "([B)I") : nullptr;
    auto close = inputType ? env->GetMethodID(inputType, "close", "()V") : nullptr;
    if (evidenceException(env) || !read || !close) return {{}, "runtime version resource reader unavailable"};
    auto bytes = env->NewByteArray(4096);
    std::string content;
    bool valid = bytes && !evidenceException(env), eof = false;
    for (int reads = 0; valid && reads < 32; ++reads) {
        const auto size = env->CallIntMethod(input, read, bytes);
        if (evidenceException(env)) { valid = false; break; }
        if (size == -1) { eof = true; break; }
        if (size <= 0 || size > 4096 || content.size() + std::size_t(size) > 65536) { valid = false; break; }
        char buffer[4096]; env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte *>(buffer));
        if (evidenceException(env)) { valid = false; break; }
        content.append(buffer, std::size_t(size));
    }
    env->CallVoidMethod(input, close);
    if (evidenceException(env) || !valid || !eof) return {{}, "runtime version resource unreadable or exceeds 65536 bytes"};
    try {
        const auto json = Json::parse(content); const auto id = json.at("id").string();
        return semanticVersion(id) ? VersionResource{id, {}, id} : VersionResource{{}, "unsupported runtime version resource id",id.substr(0,64)};
    } catch (...) { return {{}, "malformed runtime version resource"}; }
}
VersionResource legacyRuntimeVersion(JNIEnv *env, jvmtiEnv *ti, jobject loader) {
    EvidenceFrame frame(env);
    std::string reason;
    auto klass = evidenceClass(env, ti, loader, "net.minecraft.realms.RealmsSharedConstants",&reason);
    if (!klass) return {{},reason};
    const auto field = env->GetStaticFieldID(klass, "VERSION_STRING", "Ljava/lang/String;");
    if (env->ExceptionCheck() || !field) return {{},"VERSION_STRING:Ljava/lang/String; unavailable; "+evidenceExceptionType(env)};
    auto value = static_cast<jstring>(env->GetStaticObjectField(klass, field));
    if (env->ExceptionCheck()) return {{},"VERSION_STRING read/initialization failed; "+evidenceExceptionType(env)};
    if(!value) return {{},"VERSION_STRING is null"};
    const auto text = evidenceString(env, value, 32);
    return semanticVersion(text) ? VersionResource{text,{},text} : VersionResource{{},"VERSION_STRING is not an exact semantic version",text};
}
// Bounded independent semantic witness for a modified legacy constants holder.
// No field name is inferred for GameBindings. Protocol/tick fields and the exact
// version value belong to the audited game class in the pinned anchor loader.
VersionResource legacyCandidateVersion(JNIEnv *env,jvmtiEnv *ti,jobject loader,const Json *candidates) {
    if(!candidates || std::none_of(candidates->array().begin(),candidates->array().end(),
        [](const Json& value){return value.string()=="1.8.9";})) return {{},"no audited legacy candidate requested"};
    EvidenceFrame frame(env); std::string reason;
    auto klass=evidenceClass(env,ti,loader,"net.minecraft.realms.RealmsSharedConstants",&reason);
    if(!klass)return {{},reason};
    const auto protocol=env->GetStaticFieldID(klass,"NETWORK_PROTOCOL_VERSION","I");
    if(env->ExceptionCheck() || !protocol)return {{},"legacy protocol discriminator unavailable; "+evidenceExceptionType(env)};
    const auto ticks=env->GetStaticFieldID(klass,"TICKS_PER_SECOND","I");
    if(env->ExceptionCheck() || !ticks)return {{},"legacy tick discriminator unavailable; "+evidenceExceptionType(env)};
    const auto p=env->GetStaticIntField(klass,protocol),t=env->GetStaticIntField(klass,ticks);
    if(evidenceException(env) || p!=47 || t!=20)return {{},"legacy protocol/tick discriminators do not match audited 1.8.9 structure"};
    jint count=0;Buffer<jfieldID> fields{ti};
    const auto result=ti->GetClassFields(klass,&count,&fields.data);countCall("GetClassFields:version-candidate");
    if(result!=JVMTI_ERROR_NONE || count>32)return {{},"legacy candidate field inventory unavailable or exceeds 32"};
    std::set<std::string> versions;
    for(int i=0;i<count;++i) {
        Buffer<char> name{ti},descriptor{ti};jint modifiers=0;
        require(ti->GetFieldName(klass,fields.data[i],&name.data,&descriptor.data,nullptr),"GetFieldName:version-candidate");
        require(ti->GetFieldModifiers(klass,fields.data[i],&modifiers),"GetFieldModifiers:version-candidate");
        if(!(modifiers&8) || !descriptor.data || std::string(descriptor.data)!="Ljava/lang/String;")continue;
        auto value=static_cast<jstring>(env->GetStaticObjectField(klass,fields.data[i]));
        if(env->ExceptionCheck())return {{},"legacy candidate static field read failed; "+evidenceExceptionType(env)};
        const auto text=evidenceString(env,value,32);if(value)env->DeleteLocalRef(value);
        if(semanticVersion(text))versions.insert(text);
    }
    if(versions.size()!=1 || *versions.begin()!="1.8.9")return {{},"legacy candidate has no unique exact 1.8.9 version value"};
    return {"1.8.9",{},"protocol=47;ticks=20;unique-version=1.8.9"};
}
std::string runtimeBrand(JNIEnv *env, jvmtiEnv *ti, jobject loader,std::string *observed=nullptr) {
    EvidenceFrame frame(env);
    auto klass = evidenceClass(env, ti, loader, "net.minecraft.client.ClientBrandRetriever");
    if (!klass) return {};
    const auto getter = env->GetStaticMethodID(klass, "getClientModName", "()Ljava/lang/String;");
    if (evidenceException(env) || !getter) return {};
    auto value = static_cast<jstring>(env->CallStaticObjectMethod(klass, getter));
    if (evidenceException(env)) return {};
    auto text = evidenceString(env, value, 64);
    if(observed)*observed=text;
    // Audited Forge 1.8.9 FMLCommonHandler.getModName(): Joiner.on(',')
    // joins the canonical fml + forge tokens. Do not accept arbitrary comma lists.
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char ch){return char(std::tolower(ch));});
    if (text=="fml,forge") return text;
    for (auto &ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return "modified";
        ch = char(std::tolower(c));
    }
    return text;
}
Json readRuntimeEvidence(JNIEnv *env, jvmtiEnv *ti, jobject loader,const Json *candidates=nullptr) {
    const auto resource = resourceVersion(env, loader);
    const auto legacy = legacyRuntimeVersion(env, ti, loader);
    std::string observedBrand,checkedBrand;
    const auto brand = runtimeBrand(env, ti, loader,&observedBrand);
    const auto structural=legacy.version.empty() && candidates?legacyCandidateVersion(env,ti,loader,candidates):VersionResource{};
    if (resource != resourceVersion(env, loader) || legacy != legacyRuntimeVersion(env, ti, loader) ||
        brand != runtimeBrand(env, ti, loader,&checkedBrand) || observedBrand!=checkedBrand)
        throw RuntimeChanged("runtime version/brand evidence changed during capture");
    if(candidates && legacy.version.empty() && structural!=legacyCandidateVersion(env,ti,loader,candidates))
        throw RuntimeChanged("candidate version evidence changed during capture");
    Json result = Json::Object{{"evidenceVersion", 1}, {"version", ""}, {"versionSource", ""}, {"brand", brand},
        {"brandObserved",observedBrand},
        {"brandSource", brand.empty() ? "" : "ClientBrandRetriever.getClientModName"}};
    bool resourceWitness=false;
    {
        EvidenceFrame frame(env);
        resourceWitness=evidenceClass(env,ti,loader,"net.minecraft.client.ClientBrandRetriever") ||
            evidenceClass(env,ti,loader,"net.minecraft.client.main.Main");
    }
    const auto attempt=[](const char *source,const VersionResource& value) {
        return Json::Object{{"source",source},{"status",value.version.empty()?"unavailable":"verified-value"},
            {"value",value.version},{"observed",value.observed},{"reason",value.diagnostic}};
    };
    Json::Array attempts{attempt("loader-version-json",resource),attempt("realms-shared-constants",legacy)};
    if(candidates)attempts.push_back(attempt("realms-semantic-candidate",structural));
    result["attempts"]=attempts;
    const auto independent=legacy.version.empty()?structural.version:legacy.version;
    if (!resource.version.empty() && !independent.empty() && resource.version != independent)
        result["diagnostic"] = "conflicting runtime version evidence";
    else if (!independent.empty()) {
        result["version"]=independent;
        result["versionSource"]=legacy.version.empty()?"realms-semantic-candidate":"realms-shared-constants";
        if(!resource.diagnostic.empty())result["diagnostic"]="unusable version.json ignored; "+resource.diagnostic;
    } else if (!resource.version.empty() && !resourceWitness)
        result["diagnostic"] = "version resource has no independent game witness in anchor defining loader";
    else if (!resource.version.empty()) {
        result["version"] = resource.version;result["versionSource"]="loader-version-json";
    } else result["diagnostic"] = "independent runtime version evidence unavailable; resource: "+resource.diagnostic+"; legacy: "+legacy.diagnostic+
        (candidates?"; candidate: "+structural.diagnostic:"");
    return result;
}
struct Capture {
    JNIEnv *env;
    jvmtiEnv *ti;
    std::vector<jobject> loaders;
    bool lite = false;
    Json *stats = nullptr;
    SnapshotWriter *stream = nullptr;
    const Json *selection = nullptr;
    const Json *liteScope = nullptr;
    bool bindingCheck = false;
    const Json *runtimeAnchors = nullptr;
    bool detectionOnly = false;
    const Json *runtimeLoader = nullptr;
    std::map<int, bool> loaderMatches;
    const Json *versionCandidates = nullptr;
    bool acceptsLoader(jclass klass) {
        if (!runtimeLoader) return true;
        const auto id=loaderId(klass);
        if (!id) return false;
        if (loaderMatches.contains(id)) return loaderMatches.at(id);
        auto loader=loaders.at(std::size_t(id-1));
        jint identity=0; require(ti->GetObjectHashCode(loader,&identity),"GetObjectHashCode:pinned-loader");
        auto type=env->GetObjectClass(loader); const auto typeName=signature(ti,type); env->DeleteLocalRef(type);
        const bool matches=runtimeLoader->at("instance").number()==double(static_cast<unsigned int>(identity)) && runtimeLoader->at("type").string()==typeName;
        loaderMatches.emplace(id,matches); return matches;
    }
    ~Capture() {
        for (auto loader : loaders)
            env->DeleteLocalRef(loader);
    }
    int loaderId(jclass klass) {
        jobject loader = nullptr;
        require(ti->GetClassLoader(klass, &loader), "GetClassLoader");
        if (!loader)
            return 0;
        for (std::size_t i = 0; i < loaders.size(); ++i)
            if (env->IsSameObject(loader, loaders[i])) {
                env->DeleteLocalRef(loader);
                return int(i + 1);
            }
        loaders.push_back(loader);
        return int(loaders.size());
    }
    Json classRef(jclass klass) {
        return Json::Object{{"name", signature(ti, klass)},
                            {"loader", klass ? loaderId(klass) : 0}};
    }
    Json members(jclass klass) {
        Json::Array fields, methods, interfaces;
        jint count = 0;
        Buffer<jfieldID> fieldIds{ti};
        require(ti->GetClassFields(klass, &count, &fieldIds.data), "GetClassFields");
        for (int i = 0; i < count; ++i) {
            Buffer<char> name{ti}, desc{ti};
            jint mods = 0;
            require(ti->GetFieldName(klass, fieldIds.data[i], &name.data, &desc.data, nullptr),
                    "GetFieldName");
            require(ti->GetFieldModifiers(klass, fieldIds.data[i], &mods), "GetFieldModifiers");
            fields.push_back(
                Json::Object{{"name", name.data}, {"descriptor", desc.data}, {"modifiers", mods}});
        }
        Buffer<jmethodID> methodIds{ti};
        require(ti->GetClassMethods(klass, &count, &methodIds.data), "GetClassMethods");
        for (int i = 0; i < count; ++i) {
            Buffer<char> name{ti}, desc{ti};
            jint mods = 0;
            require(ti->GetMethodName(methodIds.data[i], &name.data, &desc.data, nullptr),
                    "GetMethodName");
            require(ti->GetMethodModifiers(methodIds.data[i], &mods), "GetMethodModifiers");
            jboolean obsolete = JNI_FALSE;
            require(ti->IsMethodObsolete(methodIds.data[i], &obsolete), "IsMethodObsolete");
            if (obsolete)
                throw RuntimeChanged("class changed during capture");
            std::string code;
            if ((!lite || bindingCheck) && !(mods & (0x100 | 0x400))) {
                jint length = 0;
                Buffer<unsigned char> bytes{ti};
                require(ti->GetBytecodes(methodIds.data[i], &length, &bytes.data), "GetBytecodes");
                (*stats)["jvmtiBytecodeBytes"] = (*stats).at("jvmtiBytecodeBytes").number() + length;
                code = hex(bytes.data, length,
                           "method-bytecode:" + signature(ti, klass) + "." + name.data + desc.data);
            }
            Json method =
                Json::Object{{"name", name.data}, {"descriptor", desc.data}, {"modifiers", mods}};
            if (!lite || bindingCheck)
                method["bytecode"] = std::move(code);
            methods.push_back(std::move(method));
        }
        auto order = [](const Json &a, const Json &b) { return a.dump() < b.dump(); };
        std::sort(fields.begin(), fields.end(), order);
        if (bindingCheck) std::sort(methods.begin(), methods.end(), [](const Json &a, const Json &b) {
            return a.at("name").string() + a.at("descriptor").string() < b.at("name").string() + b.at("descriptor").string();
        });
        else std::sort(methods.begin(), methods.end(), order);
        Buffer<jclass> interfaceIds{ti};
        require(ti->GetImplementedInterfaces(klass, &count, &interfaceIds.data),
                "GetImplementedInterfaces");
        for (int i = 0; i < count; ++i) {
            interfaces.push_back(classRef(interfaceIds.data[i]));
            env->DeleteLocalRef(interfaceIds.data[i]);
        }
        std::sort(interfaces.begin(), interfaces.end(), order);
        jclass super = env->GetSuperclass(klass);
        auto superRef = classRef(super);
        if (super)
            env->DeleteLocalRef(super);
        jint cpCount = 0, cpBytes = 0, major = 0, minor = 0, mods = 0;
        Buffer<unsigned char> pool{ti};
        if (!lite || bindingCheck) {
            require(ti->GetConstantPool(klass, &cpCount, &cpBytes, &pool.data), "GetConstantPool");
            (*stats)["jvmtiConstantPoolBytes"] = (*stats).at("jvmtiConstantPoolBytes").number() + cpBytes;
        }
        if (!lite && !selection)
            require(ti->GetClassVersionNumbers(klass, &minor, &major), "GetClassVersionNumbers");
        require(ti->GetClassModifiers(klass, &mods), "GetClassModifiers");
        Json result = Json::Object{{"name", signature(ti, klass)},
                                   {"loader", loaderId(klass)},
                                   {"super", superRef},
                                   {"interfaces", interfaces},
                                   {"modifiers", mods},
                                   {"major", major},
                                   {"minor", minor},
                                   {"fields", fields},
                                   {"methods", methods}};
        if (lite) {
            result.object().erase("major");
            result.object().erase("minor");
        }
        if (!lite || bindingCheck) {
            result["constantPoolCount"] = cpCount;
            result["constantPool"] =
                hex(pool.data, cpBytes, "constant-pool:" + signature(ti, klass));
        }
        if (bindingCheck) {
            result["installedDigest"] = installedHash(result);
            result.object().erase("constantPool"); result.object().erase("constantPoolCount");
            for (auto &method : result["methods"].array()) method.object().erase("bytecode");
        }
        return result;
    }
    void warmup(const Json &requests) {
        jint count = 0;
        Buffer<jclass> classes{ti};
        if (env->EnsureLocalCapacity(65536)!=JNI_OK) throw std::runtime_error("JNI class-table local capacity unavailable");
        require(ti->GetLoadedClasses(&count, &classes.data), "warmup GetLoadedClasses");
        struct Locals {
            JNIEnv *env;
            jclass *values;
            int count;
            ~Locals() {
                for (int i = 0; i < count; ++i)
                    env->DeleteLocalRef(values[i]);
            }
        } locals{env, classes.data, count};
        std::set<std::string> attempted, warmed;
        for (int i = 0; i < count; ++i) {
            const auto name = signature(ti, classes.data[i]);
            if (std::none_of(requests.array().begin(), requests.array().end(),
                    [&](const Json &request) { return request.at("anchor").string()==name; })) continue;
            if (!acceptsLoader(classes.data[i])) continue;
            for (const auto &request : requests.array())
                if (name == request.at("anchor").string()) {
                    jobject loader = nullptr;
                    countCall("GetClassLoader:warmup");
                    if (ti->GetClassLoader(classes.data[i], &loader) == JVMTI_ERROR_NONE &&
                        loader) {
                        jclass type = env->FindClass("java/lang/Class");
                        auto load = type ? env->GetStaticMethodID(type, "forName",
                            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
                        if (env->ExceptionCheck())
                            env->ExceptionClear();
                        if (load)
                            for (const auto &binary : request.at("classes").array()) {
                                const auto key=std::to_string(loaderId(classes.data[i]))+":"+binary.string();
                                if (!attempted.insert(key).second) continue;
                                (*stats)["warmupAttemptCount"]=double(attempted.size());
                                jstring text = env->NewStringUTF(binary.string().c_str());
                                if (text) {
                                    auto loaded = env->CallStaticObjectMethod(type, load, text, JNI_FALSE, loader);
                                    if (env->ExceptionCheck())
                                        env->ExceptionClear();
                                    if (loaded) {
                                        warmed.insert(key); (*stats)["warmedClassCount"]=double(warmed.size());
                                        // Reflection links metadata without running the target's
                                        // initializer; loadClass/forName(false) may leave it unprepared.
                                        auto members = env->GetMethodID(type, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
                                        if (members && !env->ExceptionCheck()) {
                                            auto methods = env->CallObjectMethod(loaded, members);
                                            if (methods) env->DeleteLocalRef(methods);
                                        }
                                        if (env->ExceptionCheck()) env->ExceptionClear();
                                        env->DeleteLocalRef(loaded);
                                    }
                                    env->DeleteLocalRef(text);
                                }
                                if (env->ExceptionCheck())
                                    env->ExceptionClear();
                            }
                        if (type)
                            env->DeleteLocalRef(type);
                        env->DeleteLocalRef(loader);
                    }
                }
        }
    }
    Json run() {
        jint count = 0;
        Buffer<jclass> classes{ti};
        if (env->EnsureLocalCapacity(65536)!=JNI_OK) throw std::runtime_error("JNI class-table local capacity unavailable");
        require(ti->GetLoadedClasses(&count, &classes.data), "GetLoadedClasses");
        struct Locals {
            JNIEnv *env;
            jclass *values;
            int count;
            ~Locals() {
                for (int i = 0; i < count; ++i)
                    env->DeleteLocalRef(values[i]);
            }
        } locals{env, classes.data, count};
        (*stats)["loadedClassCount"] = count;
        if (count > 100000)
            throw SnapshotLimit("JVMTI inventory", "loaded-class-count", count, 100000);
        std::size_t captured = 0;
        std::set<std::string> liteNames, observed;
        Json::Array evidence;
        Json::Array runtimeEvidence;
        std::map<int, Json> loaderEvidence;
        const bool scoped=liteScope && liteScope->contains("liteClasses");
        if (scoped) for (const auto &name : liteScope->at("liteClasses").array()) {
            auto signature = name.string(); std::replace(signature.begin(), signature.end(), '.', '/');
            liteNames.insert("L" + signature + ";");
        }
        const auto contentNames = liteNames;
        // Resolve scope hierarchy before metadata capture; unrelated classes remain signature-only.
        if (scoped && !detectionOnly) for (int i = 0; i < count; ++i) if (liteNames.contains(signature(ti, classes.data[i])) && acceptsLoader(classes.data[i])) {
            jint status = 0;
            require(ti->GetClassStatus(classes.data[i], &status), "scope class status");
            if (!(status & JVMTI_CLASS_STATUS_PREPARED)) continue;
            jclass base = env->GetSuperclass(classes.data[i]);
            while (base) { liteNames.insert(signature(ti, base)); auto next = env->GetSuperclass(base); env->DeleteLocalRef(base); base = next; }
            jint n = 0; Buffer<jclass> interfaces{ti};
            require(ti->GetImplementedInterfaces(classes.data[i], &n, &interfaces.data), "scope interfaces");
            for (int j = 0; j < n; ++j) { liteNames.insert(signature(ti, interfaces.data[j])); env->DeleteLocalRef(interfaces.data[j]); }
        }
        std::map<std::string, std::vector<const Json *>> wanted;
        if (selection)
            for (const auto &item : selection->at("classes").array())
                wanted[item.at("name").string()].push_back(&item);
        for (int i = 0; i < count; ++i) {
            const auto name = signature(ti, classes.data[i]);
            if (name.empty() || name.front() != 'L') continue;
            if (selection && !wanted.contains(name)) continue;
            if (scoped && !liteNames.contains(name) && std::none_of(liteScope->at("classHints").array().begin(),
                    liteScope->at("classHints").array().end(), [&](const Json &hint) {
                        return hint.at("prefix").boolean() ? name.starts_with(hint.at("value").string()) : name==hint.at("value").string();
                    })) continue;
            if (!acceptsLoader(classes.data[i])) continue;
            if (liteScope) {
                for (const auto &hint : liteScope->at("classHints").array())
                    if ((hint.at("prefix").boolean() ? name.starts_with(hint.at("value").string()) : name == hint.at("value").string()) &&
                        observed.insert(hint.at("family").string() + ":" + std::to_string(loaderId(classes.data[i]))).second) {
                        evidence.push_back(Json::Object{{"family",hint.at("family")}, {"confidence",hint.at("confidence")}, {"loader",loaderId(classes.data[i])}});
                    }
                if (scoped && !liteNames.contains(name)) continue;
            }
            // Scope filters precede loader registration. Unrelated loaded classes
            // must not introduce unrequested loaders into a selected snapshot.
            if (!loaderId(classes.data[i])) continue;
            if (selection) {
                auto matches = wanted.find(name);
                if (matches == wanted.end())
                    continue;
                const auto loader = loaders.at(std::size_t(loaderId(classes.data[i]) - 1));
                jint identity = 0;
                require(ti->GetObjectHashCode(loader, &identity),
                        "GetObjectHashCode:selected-loader");
                jclass type = env->GetObjectClass(loader);
                auto typeName = signature(ti, type);
                env->DeleteLocalRef(type);
                bool requested = false;
                for (const auto *candidate : matches->second)
                    requested |= candidate->at("instance").number() ==
                                     double(static_cast<unsigned int>(identity)) &&
                                 candidate->at("type").string() == typeName;
                if (!requested)
                    continue;
            }
            if (runtimeAnchors && std::any_of(runtimeAnchors->array().begin(), runtimeAnchors->array().end(),
                    [&](const Json &anchor) { return anchor.string() == name; })) {
                const auto id = loaderId(classes.data[i]);
                if (!loaderEvidence.contains(id)) loaderEvidence.emplace(id, readRuntimeEvidence(env, ti, loaders.at(std::size_t(id - 1)),versionCandidates));
                auto receipt = loaderEvidence.at(id); receipt["anchor"] = name; receipt["loader"] = id;
                runtimeEvidence.push_back(std::move(receipt));
            }
            if (detectionOnly) {
                // Identity collection has no game API/member/hierarchy dependency.
                // Empty anchors locate only the independently observed defining loader.
                if (signature(ti, classes.data[i])!=name) throw RuntimeChanged("detection anchor changed during capture");
                const Json anchor=Json::Object{{"name",name},{"loader",loaderId(classes.data[i])},
                    {"super",Json::Object{{"name",""},{"loader",0}}},{"interfaces",Json::Array{}},
                    {"modifiers",0},{"fields",Json::Array{}},{"methods",Json::Array{}}};
                stream->object("class",captured++,anchor);
                (*stats)["capturedClassCount"]=double(captured);
                (*stats)["classMetadataBytes"]=(*stats).at("classMetadataBytes").number()+double(anchor.dump().size());
                (*stats)["totalBytes"]=double(stream->bytes);
                continue;
            }
            jint status = 0;
            require(ti->GetClassStatus(classes.data[i], &status), "GetClassStatus");
            if (!(status & JVMTI_CLASS_STATUS_PREPARED))
                continue;
            (*stats)["activeClass"] = name;
            const bool compact = bindingCheck;
            bindingCheck = compact && contentNames.contains(name);
            auto first = members(classes.data[i]);
            auto second = members(classes.data[i]);
            if (compact) {
                if (!bindingCheck) {
                    first["installedDigest"] = installedHash(first, false);
                    second["installedDigest"] = installedHash(second, false);
                }
                first["installedProofKind"] = second["installedProofKind"] = bindingCheck ? "required-content" : "hierarchy-metadata";
            }
            bindingCheck = compact;
            if (first != second)
                throw RuntimeChanged("unstable transformed class: " + name);
            auto metadata = first;
            std::size_t cp = 0, code = 0;
            if (!lite) {
                cp = first.at("constantPool").string().size() / 2;
                metadata.object().erase("constantPool");
                metadata.object().erase("constantPoolCount");
                for (auto &method : metadata["methods"].array()) {
                    code += method.at("bytecode").string().size() / 2;
                    method.object().erase("bytecode");
                }
            }
            (*stats)["fieldCount"]=(*stats).at("fieldCount").number()+double(first.at("fields").array().size());
            (*stats)["methodCount"]=(*stats).at("methodCount").number()+double(first.at("methods").array().size());
            const auto classBytes = first.dump().size();
            if (classBytes > (*stats).at("largestClassBytes").number()) {
                (*stats)["largestClass"] = name;
                (*stats)["largestClassBytes"] = double(classBytes);
            }
            (*stats)["classMetadataBytes"] =
                (*stats).at("classMetadataBytes").number() + double(metadata.dump().size());
            (*stats)["constantPoolBytes"] = (*stats).at("constantPoolBytes").number() + double(cp);
            (*stats)["bytecodeBytes"] = (*stats).at("bytecodeBytes").number() + double(code);
            stream->object("class", captured++, first);
            (*stats)["capturedClassCount"] = double(captured);
            (*stats)["totalBytes"] = double(stream->bytes);
        }
        if (selection && captured != selection->at("classes").array().size())
            throw RuntimeChanged(
                "selected classes disappeared, became unprepared, or changed defining loader");
        Json::Array loaderInfo;
        for (std::size_t i = 0; i < loaders.size(); ++i) {
            jclass type = env->GetObjectClass(loaders[i]);
            jint identity = 0;
            require(ti->GetObjectHashCode(loaders[i], &identity), "GetObjectHashCode:classloader");
            loaderInfo.push_back(
                Json::Object{{"id", int(i + 1)},
                             {"type", signature(ti, type)},
                             {"instance", double(static_cast<unsigned int>(identity))}});
            env->DeleteLocalRef(type);
        }
        return Json::Object{{"loaders", loaderInfo}, {"classEvidence", evidence}, {"runtimeEvidence", runtimeEvidence}};
    }
};
std::string decode(const char *options) {
    if (!options)
        throw std::runtime_error("missing probe request");
    std::string text(options);
    if (text.size() > 65536)
        throw SnapshotLimit("protocol", "probe-request-hex", text.size(), 65536);
    if (text.size() % 2)
        throw std::runtime_error("bad probe request: odd hex length");
    auto digit = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("bad request hex");
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += char(digit(text[i]) * 16 + digit(text[i + 1]));
    return out;
}
struct RequestReadError : std::runtime_error {
    DWORD code;
    explicit RequestReadError(DWORD value)
        : std::runtime_error("probe request file unavailable or invalid"), code(value) {}
};
Json readRequest(const char *options) {
    const auto envelope = Json::parse(decode(options));
    if (!envelope.contains("requestFile"))
        return envelope; // legacy inline requests
    const auto path = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t *>(envelope.at("requestFile").string().c_str())));
    if (!path.is_absolute())
        throw RequestReadError(ERROR_INVALID_NAME);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        throw RequestReadError(GetLastError());
    try {
        return Json::read(path, probeRequestBytes);
    } catch (...) {
        throw RequestReadError(ERROR_INVALID_DATA);
    }
}
std::string inlineRequest(const Json &request) {
    const auto text = request.dump();
    if (text.size() > probeRequestBytes)
        throw RequestReadError(ERROR_INVALID_DATA);
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(text.size() * 2);
    for (unsigned char ch : text) {
        encoded += digits[ch >> 4];
        encoded += digits[ch & 15];
    }
    return encoded;
}
} // namespace
extern "C" __declspec(dllexport) jint JNICALL Agent_OnAttach(JavaVM *vm, char *options, void *) {
    std::unique_lock lock(captureMutex, std::try_to_lock);
    jvmtiEnv *ti = nullptr;
    JNIEnv *env = nullptr;
    std::filesystem::path errorPath, temporary;
    Json stats = Json::Object{
        {"jvmtiCalls", 0}, {"jvmtiCallsByOperation", Json::Object{}},
        {"jvmtiBytecodeBytes", 0}, {"jvmtiConstantPoolBytes", 0},
        {"loadedClassCount", 0},
        {"capturedClassCount", 0}, {"fieldCount",0}, {"methodCount",0},
        {"warmupAttemptCount",0}, {"warmedClassCount",0},
        {"classMetadataBytes", 0},
        {"constantPoolBytes", 0},
        {"bytecodeBytes", 0},
        {"totalBytes", 0},
        {"configuredLimit", double(snapshotObjectBytes)},
        {"limitScope",
         "per serialized class/header/footer object; no aggregate snapshot file limit"},
        {"frameLimit", double(snapshotFrameBytes)},
        {"chunkBytes", double(snapshotChunkBytes)},
        {"jvmBufferLimit", 16 * 1024 * 1024},
        {"largestClass", ""},
        {"largestClassBytes", 0}};
    callStats = &stats;
    struct ResetStats { ~ResetStats() { callStats = nullptr; } } resetStats;
    auto failure = [&](Json detail) {
        if (!temporary.empty()) {
            std::error_code error;
            auto size = std::filesystem::file_size(temporary, error);
            if (!error)
                stats["totalBytes"] = double(size);
        }
        detail["stats"] = stats;
        if (!errorPath.empty()) {
            std::ofstream file(errorPath, std::ios::binary);
            file << detail.dump();
        }
        if (!temporary.empty()) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
        }
    };
    try {
        const auto captureStarted=GetTickCount64();
        const auto request = readRequest(options);
        const auto tier=request.contains("captureTier")?request.at("captureTier").string():"structural";
        const bool detection=tier=="detection";
        stats["captureTier"]=tier; stats["requestBytes"]=double(request.dump().size());
        stats["versionEvidenceStage"]=request.contains("versionCandidates")?"candidate":"initial";
        if(request.contains("versionCandidates")) {
            const auto& candidates=request.at("versionCandidates").array();
            if(candidates.empty() || candidates.size()>2 || !request.contains("runtimeLoader"))
                throw std::runtime_error("candidate evidence scope requires 1-2 versions and a pinned loader");
            for(const auto& candidate:candidates)if(candidate.string()!="1.8.9" && candidate.string()!="1.20.1")
                throw std::runtime_error("unaudited candidate version evidence request");
        }
        stats["requestedClassCount"]=request.contains("liteClasses")?double(request.at("liteClasses").array().size()):0.;
        stats["runtimeAnchorCount"]=request.contains("runtimeAnchors")?double(request.at("runtimeAnchors").array().size()):0.;
        stats["warmupClassCount"]=0;
        if (request.contains("warmup")) for (const auto &group:request.at("warmup").array())
            stats["warmupClassCount"]=stats.at("warmupClassCount").number()+double(group.at("classes").array().size());
        if (detection && (request.contains("warmup") || request.contains("bindingCheck") || request.at("mode").string()!="lite"))
            throw std::runtime_error("invalid detection capture scope");
        if (tier=="vanilla-validation" && (request.contains("bindingCheck") || request.at("mode").string()!="lite"))
            throw std::runtime_error("Vanilla validation must remain metadata-only");
        const auto path = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(request.at("output").string().c_str())));
        if (!path.is_absolute())
            throw std::runtime_error("absolute output required");
        errorPath = path.wstring() + L".error";
        progress(path, "capture-initializing");
        if (!lock.owns_lock())
            throw std::runtime_error("capture busy: another probe request owns the capture mutex");
        if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8) != JNI_OK)
            throw std::runtime_error("JNI unavailable");
        if (vm->GetEnv(reinterpret_cast<void **>(&ti), JVMTI_VERSION_1_2) != JNI_OK)
            throw std::runtime_error("JVMTI unavailable");
        const bool lite = request.contains("mode") && request.at("mode").string() == "lite";
        Json selected;
        if (request.contains("selectionPath")) {
            selected = Json::read(std::filesystem::path(std::u8string(
                reinterpret_cast<const char8_t *>(request.at("selectionPath").string().c_str()))));
            FILETIME created{}, ended{}, kernel{}, user{};
            if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user))
                throw std::runtime_error("selection process identity unavailable");
            const auto start =
                std::to_string((static_cast<unsigned long long>(created.dwHighDateTime) << 32) |
                               created.dwLowDateTime);
            if (selected.at("candidateVersion").integer() != 1 ||
                selected.at("pid").number() != GetCurrentProcessId() ||
                selected.at("processStart").string() != start)
                throw std::runtime_error("selection belongs to a different JVM instance");
            if (selected.at("classes").array().empty())
                throw std::runtime_error("empty detail selection");
        }
        const bool detail = request.contains("selectionPath");
        if (!lite || request.contains("bindingCheck")) {
            jvmtiCapabilities wanted{};
            wanted.can_get_bytecodes = 1;
            wanted.can_get_constant_pool = 1;
            require(ti->AddCapabilities(&wanted), "AddCapabilities:bytecodes/constantPool");
        }
        temporary = path.wstring() + L".tmp";
        SnapshotWriter stream(temporary);
        stream.object("header", 0,
                      Json::Object{{"snapshotVersion", 1},
                                   {"captureKind", lite ? "jvmti-metadata-double-read"
                                                        : "jvmti-installed-double-read"},
                                   {"detailLevel", lite     ? "lite"
                                                   : detail ? "selected"
                                                            : "full"}});
        Json snapshot;
        {
            Capture capture{env, ti, {}, lite, &stats, &stream, detail ? &selected : nullptr, &request, request.contains("bindingCheck"), request.contains("runtimeAnchors") ? &request.at("runtimeAnchors") : nullptr, detection, request.contains("runtimeLoader") ? &request.at("runtimeLoader") : nullptr, {}};
            capture.versionCandidates=request.contains("versionCandidates")?&request.at("versionCandidates"):nullptr;
            stats["compactBindingCheck"] = capture.bindingCheck;
            if (request.contains("warmup")) {
                progress(path, "authored-class-warmup");
                capture.warmup(request.at("warmup"));
            }
            progress(path, detail ? "selected-class-detail" : "class-index");
            snapshot = capture.run();
        }
        Json::Array evidence = snapshot.at("classEvidence").array();
        snapshot.object().erase("classEvidence");
        if (request.contains("detection"))
            for (const auto *property : {"java.home", "java.class.path", "sun.java.command"}) {
                Buffer<char> hint{ti};
                countCall("GetSystemProperty");
                if (ti->GetSystemProperty(property, &hint.data) != JVMTI_ERROR_NONE || !hint.data)
                    continue;
                std::string value(hint.data);
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                for (const auto &pattern : request.at("detection").array()) {
                    auto needle = pattern.at("value").string();
                    std::transform(needle.begin(), needle.end(), needle.begin(),
                                   [](unsigned char c) { return char(std::tolower(c)); });
                    if (!needle.empty() && value.find(needle) != std::string::npos)
                        evidence.push_back(Json::Object{{"family", pattern.at("family")},
                                                        {"confidence", pattern.at("confidence")}});
                }
            }
        if (detail) {
            snapshot["loaderBindings"] = selected.at("loaderBindings");
            Json::Array scope;
            for (const auto &item : selected.at("classes").array())
                scope.push_back(Json::Object{{"name", item.at("name")},
                                             {"loaderKey", item.at("loaderKey")},
                                             {"metadataDigest", item.at("metadataDigest")}});
            snapshot["captureScope"] = Json::Object{
                {"liteFingerprint", selected.at("liteFingerprint")}, {"classes", scope}};
            evidence = selected.at("launchEvidence").array();
        }
        snapshot["launchEvidence"] =
            evidence; // Never persist raw command lines, paths or property values.
        snapshot["captureTier"] = tier;
        stats["durationMs"]=double(GetTickCount64()-captureStarted);
        snapshot["snapshotVersion"] = 1;
        snapshot["captureKind"] = lite     ? "jvmti-metadata-double-read"
                                  : detail ? "jvmti-selected-detail-double-read"
                                           : "jvmti-installed-double-read";
        snapshot["requestId"] = request.at("requestId");
        snapshot["pid"] = double(GetCurrentProcessId());
        FILETIME created{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exit, &kernel, &user))
            throw std::runtime_error("process identity unavailable");
        snapshot["processStart"] =
            std::to_string((static_cast<unsigned long long>(created.dwHighDateTime) << 32) |
                           created.dwLowDateTime);
        snapshot["complete"] = true;
        // The probe owns a dedicated environment and installs no hooks or callbacks.
        countCall("DisposeEnvironment");
        ti->DisposeEnvironment();
        ti = nullptr;
        const auto footerIndex = std::size_t(stats.at("capturedClassCount").number());
        for (int pass = 0; pass < 8; ++pass) {
            snapshot["stats"] = stats;
            std::size_t size = 0;
            snapshotFrames("footer", footerIndex, snapshot,
                           [&](const std::string &line) { size += line.size(); });
            const auto total = stream.bytes + size;
            if (stats.at("totalBytes").number() == double(total))
                break;
            stats["totalBytes"] = double(total);
        }
        snapshot["stats"] = stats;
        stream.object("footer", footerIndex, snapshot);
        stream.finish();
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("snapshot publish failed");
        return JNI_OK;
    } catch (const SnapshotLimit &e) {
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        if (ti)
            ti->DisposeEnvironment();
        try {
            failure(e.json());
        } catch (...) {
        }
        return JNI_ERR;
    } catch (const RuntimeChanged &e) {
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        if (ti)
            ti->DisposeEnvironment();
        try {
            failure(Json::Object{
                {"reason", e.what()}, {"retryable", true}, {"failureType", "runtime-drift"}});
        } catch (...) {
        }
        return JNI_ERR;
    } catch (const std::exception &e) {
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        if (ti)
            ti->DisposeEnvironment();
        try {
            failure(Json::Object{{"reason", e.what()}});
        } catch (...) {
        }
        return JNI_ERR;
    } catch (...) {
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        if (ti)
            ti->DisposeEnvironment();
        return JNI_ERR;
    }
}

// Existing native loader fallback, with a copied request and no main Agent runtime.
// The loaded probe remains resident like any LoadLibrary-loaded JVMTI helper.
extern "C" __declspec(dllexport) DWORD WINAPI McOverlay_Start(LPVOID rawOptions) {
    try {
        const auto *options = static_cast<const char *>(rawOptions);
        if (!options)
            return 1;
        std::string copy(options);
        if (copy.size() > 65536)
            return 1;
        // Verify the remote process can publish diagnostics before acknowledging startup.
        // Otherwise an unwritable output directory masquerades as a capture timeout.
        const auto request = readRequest(copy.c_str());
        copy = inlineRequest(request); // worker owns the request even after Analyzer cancellation
        const auto output = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(request.at("output").string().c_str())));
        const auto statusPath = std::filesystem::path(output.wstring() + L".status");
        HANDLE status = CreateFileW(statusPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (status == INVALID_HANDLE_VALUE)
            return probeOutputOpenError | (GetLastError() & 0xffffU);
        const std::string started = Json(Json::Object{{"stage", "native-worker-starting"}}).dump();
        DWORD written = 0;
        const bool published =
            WriteFile(status, started.data(), DWORD(started.size()), &written, nullptr);
        const DWORD publishError = published ? ERROR_WRITE_FAULT : GetLastError();
        CloseHandle(status);
        if (!published || written != started.size())
            return probeOutputWriteError | (publishError & 0xffffU);
        using GetVms = jint(JNICALL *)(JavaVM **, jsize, jsize *);
        auto module = GetModuleHandleW(L"jvm.dll");
        auto get = module
                       ? reinterpret_cast<GetVms>(GetProcAddress(module, "JNI_GetCreatedJavaVMs"))
                       : nullptr;
        JavaVM *vm = nullptr;
        jsize count = 0;
        if (!get || get(&vm, 1, &count) != JNI_OK || count != 1 || !vm)
            return 1;
        std::thread([vm, copy = std::move(copy), output]() mutable {
            progress(output, "JNI-daemon-attach");
            JNIEnv *env = nullptr;
            bool attached = false;
            if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_8) != JNI_OK) {
                if (vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void **>(&env), nullptr) !=
                    JNI_OK) {
                    std::ofstream status(std::filesystem::path(output.wstring() + L".error"),
                                         std::ios::binary);
                    status << Json(Json::Object{{"stage", "AttachCurrentThreadAsDaemon"},
                                                {"reason", "JNI daemon thread attach failed"}})
                                  .dump();
                    return;
                }
                attached = true;
            }
            (void)Agent_OnAttach(vm, copy.data(), nullptr);
            if (attached)
                vm->DetachCurrentThread();
        }).detach();
        return 0;
    } catch (const RequestReadError &error) {
        return probeRequestReadError | (error.code & 0xffffU);
    } catch (...) {
        return 1;
    }
}
