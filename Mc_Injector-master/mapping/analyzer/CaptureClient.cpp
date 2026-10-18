#include "CaptureClient.h"
#include "Bytecode.h"
#include "Resolver.h"
#include "../ProbeProtocol.h"
#include <QRegularExpression>
#include <QTemporaryFile>
#include "../SnapshotStream.h"
#include "../../agent/bindings/MappingPack.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <QUuid>
#include <windows.h>
#include <set>
#include <tuple>
namespace mcoverlay::mapping {
Json readSnapshotFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::string first(256, '\0');
    file.read(first.data(), 256);
    if (first.find("\"snapshotStreamVersion\"") != std::string::npos)
        return inspectSnapshot(readSnapshotStream(path));
    try {
        return inspectSnapshot(Json::read(path, 64U * 1024U * 1024U));
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("legacy snapshot JSON reader (file limit=67108864 "
                                             "bytes; use streamed snapshot): ") +
                                 e.what());
    }
}
void writeSnapshotFile(const std::filesystem::path &path, const Json &snapshot) {
    auto temporary = path;
    temporary += L".tmp";
    try {
        SnapshotWriter stream(temporary);
        auto header = snapshot;
        header.object().erase("classes");
        header.object().erase("complete");
        stream.object("header", 0, header);
        std::size_t index = 0;
        for (const auto &c : snapshot.at("classes").array())
            stream.object("class", index++, c);
        stream.object("footer", index, Json::Object{{"complete", true}});
        stream.finish();
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("snapshot stream publish failed");
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
namespace {
QString targetJava(quint32 pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[32768];
    DWORD length = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!ok)
        return {};
    auto dir = QFileInfo(QString::fromWCharArray(path, int(length))).absolutePath();
    const auto java = dir + "/java.exe";
    return QFileInfo::exists(java) ? java : QString{};
}
Json processRun(const QString &program, const QStringList &arguments, int timeout) {
    QElapsedTimer elapsed;
    elapsed.start();
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    bool started = process.waitForStarted(5000),
         finished = started && process.waitForFinished(timeout);
    const auto error = process.errorString();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    return Json::Object{
        {"durationMs", double(elapsed.elapsed())},
        {"started", started},
        {"timedOut", started && !finished},
        {"exitCode", started ? Json(process.exitCode()) : Json(nullptr)},
        {"exitStatus", process.exitStatus() == QProcess::NormalExit ? "normal" : "crash"},
        {"success",
         finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0},
        {"stderr", QString::fromLocal8Bit(process.readAllStandardError()).toUtf8().toStdString()},
        {"stdout", QString::fromLocal8Bit(process.readAllStandardOutput()).toUtf8().toStdString()},
        {"reason", !started || !finished ? error.toStdString() : ""},
        {"program", program.toUtf8().toStdString()}};
}
} // namespace
Json captureRequestStats(const Json &request) {
    Json::Object sections;
    for (const auto &[key, value] : request.object()) sections[key] = double(value.dump().size());
    std::set<std::string> warmed;
    if (request.contains("warmup")) for (const auto &group : request.at("warmup").array())
        for (const auto &name : group.at("classes").array()) warmed.insert(group.at("anchor").string()+":"+name.string());
    auto count = [&](const char *key) { return request.contains(key) ? double(request.at(key).array().size()) : 0.; };
    return Json::Object{{"captureTier",request.at("captureTier")}, {"requestBytes",double(request.dump().size())},
        {"configuredRequestBytes",double(probeRequestBytes)}, {"requestedClassCount",count("liteClasses")},
        {"runtimeAnchorCount",count("runtimeAnchors")}, {"classHintCount",count("classHints")},
        {"warmupClassCount",double(warmed.size())}, {"sections",sections},
        {"identityCatalogUsed",false}, {"scope",request.contains("liteClasses") ? "selected" : "structural-inventory"}};
}
Json planCaptureRequest(const std::map<QString, QString> &options, const Json &contracts,
                        const std::string &requestId) {
    auto option = [&](const QString &name, const QString &fallback = QString{}) {
        auto it=options.find(name); return it==options.end()?fallback:it->second;
    };
    const auto tier=option("--capture-tier",options.contains("--detect-only")?"detection":"structural").toStdString();
    if (tier!="detection" && tier!="vanilla-validation" && tier!="modded-validation" && tier!="structural")
        throw std::runtime_error("unsupported capture tier: "+tier);
    const bool detection=tier=="detection", validation=tier=="vanilla-validation" || tier=="modded-validation";
    if (detection && options.contains("--binding-check")) throw std::runtime_error("detection cannot request installed-code proof");
    if (tier=="vanilla-validation" && options.contains("--binding-check")) throw std::runtime_error("Vanilla validation is metadata-only");
    Json request=Json::Object{{"requestId",requestId},{"captureTier",tier},
        {"mode",(detection || validation || options.contains("--lite"))?"lite":"full"}};
    if (options.contains("--binding-check")) request["bindingCheck"]=true;
    if (options.contains("--candidates")) {
        if (tier!="structural") throw std::runtime_error("detail candidates require structural capture tier");
        request["selectionPath"]=QFileInfo(option("--candidates")).absoluteFilePath().toUtf8().toStdString();
    }
    if (options.contains("--runtime-anchor")) request["runtimeAnchor"]=option("--runtime-anchor").toStdString();
    if (options.contains("--runtime-loader-type") || options.contains("--runtime-loader-instance")) {
        bool valid=false; const auto instance=option("--runtime-loader-instance").toULongLong(&valid);
        if (!valid || instance>0xffffffffULL || option("--runtime-loader-type").isEmpty())
            throw std::runtime_error("invalid runtime loader receipt");
        request["runtimeLoader"]=Json::Object{{"type",option("--runtime-loader-type").toStdString()},{"instance",double(instance)}};
    }
    if(options.contains("--evidence-candidates")) {
        if(!request.contains("runtimeLoader"))throw std::runtime_error("candidate evidence requires a pinned defining loader");
        std::set<std::string> versions;
        for(const auto& value:option("--evidence-candidates").split(',',Qt::SkipEmptyParts)) {
            if(value!="1.8.9" && value!="1.20.1")throw std::runtime_error("candidate evidence version has not been audited");
            versions.insert(value.toStdString());
        }
        if(versions.empty() || versions.size()>2)throw std::runtime_error("invalid bounded candidate evidence request");
        Json::Array candidates;for(const auto& version:versions)candidates.push_back(version);
        request["versionCandidates"]=candidates;
    }
    // These are supported installed API anchors, not version evidence. Detection never
    // reads old Cache packs or expands their game API symbols/launch patterns.
    std::set<std::string> anchors, names;
    // The standalone two-stage command omits --pack on its selected detail pass.
    // Observe evidence only if an audited anchor is already in that exact scope;
    // this does not add a detail candidate or warm any extra game class.
    if (detection || (tier=="structural" && !options.contains("--pack") && !options.contains("--runtime-anchor")))
        anchors={"Lnet/minecraft/client/main/Main;","Lnet/minecraft/client/Minecraft;","Lave;","Lenn;"};
    std::map<std::string,std::set<std::string>> warmups;
    Json::Array hints, detectionHints;
    for (const auto &[family,prefix,confidence] : std::vector<std::tuple<std::string,std::string,int>>{
        {"Forge","Lnet/minecraftforge/",200},{"Forge","Lcpw/mods/fml/",180},
        {"Lunar","Lcom/lunarclient/",250},{"Lunar","Lcom/moonsworth/",250},
        {"Badlion","Lnet/badlion/",250},{"Badlion","Lcom/badlion/",250}})
        hints.push_back(Json::Object{{"family",family},{"value",prefix},{"prefix",true},{"confidence",confidence}});
    for (const auto &[family,value,confidence] : std::vector<std::tuple<std::string,std::string,int>>{
        {"Forge","forge",190},{"Forge","launchwrapper",90},
        {"Lunar",".lunarclient",260},{"Lunar","lunar",240},{"Badlion","badlion",250}})
        detectionHints.push_back(Json::Object{{"family",family},{"value",value},{"confidence",confidence}});
    if (!detection && options.contains("--pack")) {
        const auto pack=Json::read(filePath(option("--pack")));
        (void)bindings::parseMappingPack(pack);
        const auto &activeContracts=contractsForPack(pack,contracts);
        for (const auto &provider:pack.at("providers").array()) for (const auto &dict:provider.at("dictionaries").array()) {
            const auto anchor=dict.at("symbols").at("minecraftSignature").string();
            if (anchor.empty()) continue;
            anchors.insert(anchor);
            Json::Array selected;
            if (validation || options.contains("--required-only")) selected=requiredClassNames(activeContracts,dict.at("symbols")).array();
            else for (const auto &[key,spec]:activeContracts.at("symbols").object())
                if (spec.at("kind").string()=="class" && !dict.at("symbols").at(key).string().empty())
                    selected.push_back(dict.at("symbols").at(key));
            for (const auto &name:selected) if (!name.string().empty()) {
                names.insert(name.string()); warmups[anchor].insert(name.string());
            }
        }
    } else if (validation) throw std::runtime_error("validation capture requires exactly scoped --pack");
    if (request.contains("runtimeAnchor")) anchors.insert(request.at("runtimeAnchor").string());
    Json::Array runtimeAnchors, liteClasses, warmup;
    for (const auto &anchor:anchors) {
        runtimeAnchors.push_back(anchor);
        if (anchor.size()>2 && anchor.front()=='L' && anchor.back()==';') {
            auto binary=anchor.substr(1,anchor.size()-2); std::replace(binary.begin(),binary.end(),'/','.');
            if (detection) names.insert(binary);
        }
    }
    for (const auto &name:names) liteClasses.push_back(name);
    for (const auto &[anchor,classes]:warmups) {
        Json::Array values; for (const auto &name:classes) values.push_back(name);
        warmup.push_back(Json::Object{{"anchor",anchor},{"classes",values}});
    }
    request["runtimeAnchors"]=runtimeAnchors; request["classHints"]=hints; request["detection"]=detectionHints;
    if (detection || validation || options.contains("--identity-lite")) request["liteClasses"]=liteClasses;
    if (!detection && !options.contains("--detect-only") && !warmup.empty()) request["warmup"]=warmup;
    return request;
}
Json captureLive(const std::map<QString, QString> &options, const Json &contracts,
                 const Events &events) {
    auto option = [&](const QString &name, const QString &fallback = QString{}) {
        auto it = options.find(name);
        return it == options.end() ? fallback : it->second;
    };
    bool valid = false;
    const auto pid = option("--pid").toUInt(&valid);
    if (!valid || !pid)
        throw std::runtime_error("invalid PID");
    const auto directory = QCoreApplication::applicationDirPath();
    const auto java = option("--java", targetJava(pid));
    const auto helper = option("--helper", directory + "/McOverlayAttachHelper.jar");
    const auto probe = option("--probe", directory + "/MappingProbe-v17.dll");
    const auto nativeLoader = option("--native-loader", directory + "/McOverlayNativeLoader.exe");
    const auto output = filePath(QFileInfo(option("--out")).absoluteFilePath());
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    Json request = planCaptureRequest(options, contracts, requestId);
    Json paths =
        Json::Object{{"event", "CAPTURE_PATH"},
                     {"standardAttach", Json::Object{{"status", "pending"},
                                                     {"javaRuntime", java.toUtf8().toStdString()}}},
                     {"fallback", Json::Object{{"status", "not-attempted"}}}};
    const bool nativePreferred = option("--transport") == "native";
    if (nativePreferred) paths["standardAttach"] = Json::Object{{"status", "skipped"}, {"reason", "pid/processStart transport memo"}};
    for (int attempt = nativePreferred ? 1 : 0; attempt < 2; ++attempt) {
        const auto rawPath = output.wstring() + L"." +
                             QString::fromStdString(requestId).toStdWString() +
                             (attempt ? L".fallback.capture" : L".standard.capture");
        const auto errorPath = std::filesystem::path(rawPath + L".error");
        request["output"] = qtPath(rawPath).toUtf8().toStdString();
        const auto requestBytes = QByteArray::fromStdString(request.dump());
        auto requestStats=captureRequestStats(request); requestStats["event"]="CAPTURE_REQUEST";
        requestStats["transport"]=attempt ? "NativeLoader" : "StandardAttach"; events(requestStats);
        if (std::size_t(requestBytes.size()) > probeRequestBytes)
            throw SnapshotLimit("file", "probe-request-json", requestBytes.size(),
                                probeRequestBytes);
        QTemporaryFile requestFile(QDir::tempPath() + "/Arcveil-mapping-XXXXXX.json");
        if (!requestFile.open() || requestFile.write(requestBytes) != requestBytes.size() ||
            !requestFile.flush())
            throw std::runtime_error("cannot publish probe request file");
        const auto envelope =
            Json(Json::Object{{"requestFile", requestFile.fileName().toUtf8().toStdString()}})
                .dump();
        const auto encoded = QString::fromLatin1(QByteArray::fromStdString(envelope).toHex());
        if (encoded.size() > 65536)
            throw SnapshotLimit("protocol", "probe-request-hex", encoded.size(), 65536);
        QStringList args;
        if (options.contains("--tools-jar"))
            args = {"-cp", helper + ";" + option("--tools-jar"),
                    "com.mcoverlay.attach.AttachHelper"};
        else
            args = {"--add-modules", "jdk.attach", "-jar", helper};
        args << QString::number(pid) << probe << encoded;
        Json diagnostic =
            processRun(attempt ? nativeLoader : java,
                       attempt ? QStringList{QString::number(pid), probe, encoded} : args,
                       attempt ? 25000 : 60000);
        diagnostic["javaRuntime"] = java.toUtf8().toStdString();
        events(Json::Object{{"event", "CAPTURE_HELPER"}, {"transport", attempt ? "NativeLoader" : "StandardAttach"},
                            {"durationMs", diagnostic.at("durationMs")}, {"success", diagnostic.at("success")}});
        if (!attempt && diagnostic.at("exitCode") == Json(10))
            events(Json::Object{{"event", "CAPTURE_TRANSPORT_SELECTED"}, {"transport", "NativeLoader"},
                {"pid", double(pid)}, {"reason", "standard-attach-explicitly-unsupported"}});
        if (attempt) {
            const auto match = QRegularExpression("McOverlay_Start returned ([0-9]+)")
                                   .match(QString::fromStdString(diagnostic.at("stderr").string()));
            if (match.hasMatch()) {
                const auto code = match.captured(1).toUInt();
                const auto stage = code & 0xffff0000U;
                if (stage == probeOutputOpenError || stage == probeOutputWriteError ||
                    stage == probeRequestReadError) {
                    const auto error = code & 0xffffU;
                    wchar_t message[1024]{};
                    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, error, 0, message, 1024, nullptr);
                    diagnostic["captureFailure"] = Json::Object{
                        {"stage", stage == probeOutputOpenError    ? "output-file-open"
                                  : stage == probeOutputWriteError ? "output-file-write"
                                                                   : "request-file-read"},
                        {"win32Error", double(error)},
                        {"reason",
                         QString::fromWCharArray(message).trimmed().toUtf8().toStdString()}};
                }
            }
        }
        if (attempt && diagnostic.at("success").boolean()) {
            QElapsedTimer deadline;
            deadline.start();
            while (!QFile::exists(qtPath(rawPath)) && !QFile::exists(qtPath(errorPath)) &&
                   deadline.elapsed() < 60000)
                QThread::msleep(50);
        }
        const auto key = attempt ? "fallback" : "standardAttach";
        const auto statusPath = std::filesystem::path(rawPath + L".status");
        if (QFile::exists(qtPath(statusPath)))
            try {
                diagnostic["lastProbeStatus"] = Json::read(statusPath);
            } catch (...) {
            }
        try {
            if (QFile::exists(qtPath(errorPath))) {
                auto failure = Json::read(errorPath);
                diagnostic["captureFailure"] = failure;
                if (failure.contains("stats")) {
                    auto stats = failure.at("stats");
                    stats["event"] = "SNAPSHOT_STATS";
                    stats["capturePath"] = key;
                    events(stats);
                }
                if(failure.contains("retryable") && failure.at("retryable").boolean()) throw RuntimeChanged(failure.at("reason").string());
                throw std::runtime_error(failure.at("reason").string());
            }
            if (!diagnostic.at("success").boolean())
                throw std::runtime_error("helper failed: exit=" + diagnostic.at("exitCode").dump() +
                                         "; " + diagnostic.at("reason").string() +
                                         "; stderr=" + diagnostic.at("stderr").string() +
                                         "; stdout=" + diagnostic.at("stdout").string());
            if (!QFile::exists(qtPath(rawPath)))
                throw std::runtime_error(
                    attempt ? "fallback capture timeout: no result/error file"
                            : "Attach helper exited successfully without capture output");
            auto snapshot = readSnapshotStream(rawPath);
            diagnostic["statisticsAvailable"] = true;
            if (snapshot.at("requestId").string() != requestId ||
                snapshot.at("pid").number() != pid)
                throw std::runtime_error("stale/mismatched probe response");
            diagnostic["status"] = "captured";
            events(Json::Object{{"event", "CAPTURE_TRANSPORT_SELECTED"},
                {"transport", attempt ? "NativeLoader" : "StandardAttach"}, {"pid", double(pid)},
                {"processStart", snapshot.at("processStart")},
                {"reason", nativePreferred ? "remembered-pid-processStart" : (attempt ? "standard-attach-unavailable" : "standard-attach-success")}});
            paths[key] = diagnostic;
            events(paths);
            auto stats = snapshot.at("stats");
            stats["event"] = "SNAPSHOT_STATS";
            stats["capturePath"] = key;
            events(stats);
            const auto tier=request.at("captureTier").string();
            if((tier=="vanilla-validation" || tier=="modded-validation") && snapshot.at("complete").boolean() && snapshot.at("classes").array().empty())
                throw CaptureScopeUnavailable(pid,snapshot.at("processStart").string());
            if (options.contains("--candidates"))
                for (auto &klass : snapshot["classes"].array()) {
                    Json::Array graph;
                    for (const auto &method : klass.at("methods").array()) {
                        Json entry = Json::Object{{"method", method.at("name")},
                                                  {"descriptor", method.at("descriptor")}};
                        try {
                            const auto shape = normalizeBytecode(klass, method);
                            Json::Array refs;
                            for (const auto &ref : shape.references)
                                refs.push_back(Json::Object{{"owner", ref.owner},
                                                            {"name", ref.name},
                                                            {"descriptor", ref.descriptor},
                                                            {"opcode", ref.opcode},
                                                            {"position", ref.position}});
                            entry["complete"] = shape.supported;
                            entry["references"] = refs;
                        } catch (const std::exception &e) {
                            entry["complete"] = false;
                            entry["reason"] = e.what();
                        }
                        graph.push_back(std::move(entry));
                    }
                    klass["crossReferences"] = graph;
                }
            auto normalized = inspectSnapshot(std::move(snapshot));
            if (options.contains("--candidates")) {
                const auto selection = Json::read(filePath(option("--candidates")));
                std::map<std::string, std::string> expected;
                for (const auto &item : selection.at("classes").array()) {
                    if (!expected
                             .emplace(item.at("loaderKey").string() + item.at("name").string(),
                                      item.at("metadataDigest").string())
                             .second)
                        throw std::runtime_error("duplicate detail candidate");
                }
                if (expected.size() != normalized.at("classes").array().size())
                    throw RuntimeChanged("detail selection count changed");
                for (const auto &klass : normalized.at("classes").array()) {
                    const auto id = klass.at("loaderKey").string() + klass.at("name").string();
                    if (!expected.contains(id) ||
                        sha256(classMetadata(klass).dump()) != expected.at(id))
                        throw RuntimeChanged(
                            "class metadata changed between lite and detail: " +
                            klass.at("name").string());
                }
            }
            QFile::remove(qtPath(rawPath));
            QFile::remove(qtPath(statusPath));
            return normalized;
        } catch (const CaptureScopeUnavailable &e) {
            diagnostic["status"]="candidate-scope-unavailable";diagnostic["reason"]=e.what();paths[key]=diagnostic;events(paths);
            QFile::remove(qtPath(rawPath));QFile::remove(qtPath(statusPath));
            throw;
        } catch (const RuntimeChanged &e) {
            diagnostic["status"]="changed";diagnostic["reason"]=e.what();paths[key]=diagnostic;events(paths);
            throw;
        } catch (const SnapshotLimit &e) {
            diagnostic["captureFailure"] = e.json();
            diagnostic["reason"] = e.what();
        } catch (const std::exception &e) {
            diagnostic["reason"] = e.what();
        }
        diagnostic["status"] = "failed";
        paths[key] = diagnostic;
        events(paths);
        if (!diagnostic.contains("statisticsAvailable") &&
            (!diagnostic.contains("captureFailure") ||
             !diagnostic.at("captureFailure").contains("stats")))
            events(Json::Object{{"event", "SNAPSHOT_STATS"},
                                {"capturePath", key},
                                {"available", false},
                                {"loadedClassCount", Json()},
                                {"capturedClassCount", Json()},
                                {"classMetadataBytes", Json()},
                                {"constantPoolBytes", Json()},
                                {"bytecodeBytes", Json()},
                                {"totalBytes", Json()},
                                {"configuredLimit", double(snapshotObjectBytes)},
                                {"limitScope", "per object; no aggregate file limit"},
                                {"largestClass", Json()},
                                {"largestClassBytes", Json()},
                                {"reason", "probe did not return capture statistics"}});
        // Keep failed raw snapshots for normalization/transport diagnosis.
        QFile::remove(qtPath(errorPath));
        QFile::remove(qtPath(statusPath));
    }
    throw std::runtime_error(
        "standard Attach failed: " + paths.at("standardAttach").at("reason").string() +
        " | fallback capture failed: " + paths.at("fallback").at("reason").string());
}
} // namespace mcoverlay::mapping
