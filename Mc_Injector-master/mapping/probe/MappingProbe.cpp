#include "../Json.h"
#include "../SnapshotStream.h"
#include "../ProbeProtocol.h"
#include <jni.h>
#include <jvmti.h>
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <memory>
#include <thread>
#include <cctype>

using namespace mcoverlay::mapping;
namespace {
std::mutex captureMutex;
void progress(const std::filesystem::path &output, const char *stage) {
    std::ofstream file(std::filesystem::path(output.wstring() + L".status"), std::ios::binary);
    file << Json(Json::Object{{"stage", stage}}).dump();
}
void require(jvmtiError result, const char *operation) {
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
        if (data)
            ti->Deallocate(reinterpret_cast<unsigned char *>(data));
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
struct Capture {
    JNIEnv *env;
    jvmtiEnv *ti;
    std::vector<jobject> loaders;
    bool lite = false;
    Json *stats = nullptr;
    SnapshotWriter *stream = nullptr;
    const Json *selection = nullptr;
    const Json *liteScope = nullptr;
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
            if (!lite && !(mods & (0x100 | 0x400))) {
                jint length = 0;
                Buffer<unsigned char> bytes{ti};
                require(ti->GetBytecodes(methodIds.data[i], &length, &bytes.data), "GetBytecodes");
                code = hex(bytes.data, length,
                           "method-bytecode:" + signature(ti, klass) + "." + name.data + desc.data);
            }
            Json method =
                Json::Object{{"name", name.data}, {"descriptor", desc.data}, {"modifiers", mods}};
            if (!lite)
                method["bytecode"] = std::move(code);
            methods.push_back(std::move(method));
        }
        auto order = [](const Json &a, const Json &b) { return a.dump() < b.dump(); };
        std::sort(fields.begin(), fields.end(), order);
        std::sort(methods.begin(), methods.end(), order);
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
        if (!lite)
            require(ti->GetConstantPool(klass, &cpCount, &cpBytes, &pool.data), "GetConstantPool");
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
        if (!lite) {
            result["constantPoolCount"] = cpCount;
            result["constantPool"] =
                hex(pool.data, cpBytes, "constant-pool:" + signature(ti, klass));
        }
        return result;
    }
    void warmup(const Json &requests) {
        jint count = 0;
        Buffer<jclass> classes{ti};
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
        for (int i = 0; i < count; ++i) {
            const auto name = signature(ti, classes.data[i]);
            for (const auto &request : requests.array())
                if (name == request.at("anchor").string()) {
                    jobject loader = nullptr;
                    if (ti->GetClassLoader(classes.data[i], &loader) == JVMTI_ERROR_NONE &&
                        loader) {
                        jclass type = env->FindClass("java/lang/Class");
                        auto load = type ? env->GetStaticMethodID(type, "forName",
                            "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
                        if (env->ExceptionCheck())
                            env->ExceptionClear();
                        if (load)
                            for (const auto &binary : request.at("classes").array()) {
                                jstring text = env->NewStringUTF(binary.string().c_str());
                                if (text) {
                                    auto loaded = env->CallStaticObjectMethod(type, load, text, JNI_FALSE, loader);
                                    if (env->ExceptionCheck())
                                        env->ExceptionClear();
                                    if (loaded) {
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
        if (liteScope) for (const auto &name : liteScope->at("liteClasses").array()) {
            auto signature = name.string(); std::replace(signature.begin(), signature.end(), '.', '/');
            liteNames.insert("L" + signature + ";");
        }
        // Resolve scope hierarchy before metadata capture; unrelated classes remain signature-only.
        if (liteScope) for (int i = 0; i < count; ++i) if (liteNames.contains(signature(ti, classes.data[i]))) {
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
            bool marker = false;
            if (liteScope) {
                for (const auto &hint : liteScope->at("classHints").array())
                    if ((hint.at("prefix").boolean() ? name.starts_with(hint.at("value").string()) : name == hint.at("value").string()) &&
                        observed.insert(hint.at("family").string()).second) {
                        evidence.push_back(Json::Object{{"family",hint.at("family")}, {"confidence",hint.at("confidence")}});
                        marker = true;
                    }
                if (!liteNames.contains(name) && !marker && captured) continue;
            }
            // Scope filters precede loader registration. Unrelated loaded classes
            // must not introduce unrequested loaders into a selected snapshot.
            if (selection && !wanted.contains(name)) continue;
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
            jint status = 0;
            require(ti->GetClassStatus(classes.data[i], &status), "GetClassStatus");
            if (!(status & JVMTI_CLASS_STATUS_PREPARED))
                continue;
            (*stats)["activeClass"] = name;
            auto first = members(classes.data[i]);
            auto second = members(classes.data[i]);
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
        return Json::Object{{"loaders", loaderInfo}, {"classEvidence", evidence}};
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
        {"loadedClassCount", 0},
        {"capturedClassCount", 0},
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
        const auto request = readRequest(options);
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
        if (!lite) {
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
            Capture capture{env, ti, {}, lite, &stats, &stream, detail ? &selected : nullptr, request.contains("liteClasses") ? &request : nullptr};
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
