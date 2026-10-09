#include "Analyzer.h"
#include "../ProbeProtocol.h"
#include "CaptureClient.h"
#include "../SnapshotStream.h"
#include "Resolver.h"
#include "../../agent/bindings/MappingPack.h"
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QThread>
#include <QElapsedTimer>
#include <cstdio>
using namespace mcoverlay::mapping;
namespace {
void event(std::string type, Json data) {
    data["event"] = std::move(type);
    data["eventVersion"] = 1;
    const auto line = data.dump() + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}
Json readSnapshot(const std::filesystem::path &path) { return readSnapshotFile(path); }
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() > 1 && args[1] == "inspect-lite") {
        args[1] = "inspect";
        args.append("--lite");
    }
    try {
        if (args.size() == 2 && (args[1] == "--help" || args[1] == "help")) {
            event("help",
                  Json::Object{
                      {"usage",
                       "MappingAnalyzer inspect --pid PID --lite [--java JAVA] [--out "
                       "INDEX.jsonl]; inspect|select|inspect-detail|validate|resolve|diff; see docs/mapping/README.md"}});
            return 0;
        }
        if (args.size() < 2)
            throw std::runtime_error(
                "Usage: MappingAnalyzer inspect|select|inspect-detail|validate|resolve|diff [--snapshot file | --pid PID "
                "--java executable] --pack file --out file");
        std::map<QString, QString> options;
        for (int i = 2; i < args.size(); ++i) {
            if (!args[i].startsWith("--") || options.contains(args[i]))
                throw std::runtime_error("invalid/duplicate option");
            if (args[i] == "--lite" || args[i]=="--incremental" || args[i]=="--allow-empty" || args[i]=="--validation-scope" || args[i]=="--detect-only" || args[i]=="--identity-lite" || args[i]=="--required-only" || args[i]=="--binding-check") {
                options[args[i]] = "true";
                continue;
            }
            if (i + 1 >= args.size() || args[i + 1].startsWith("--"))
                throw std::runtime_error("missing option value");
            auto key = args[i];
            options[key] = args[++i];
        }
        if (options.contains("--lite") && args[1] != "inspect")
            throw std::runtime_error(
                "--lite is an inspect diagnostic flag; it never resolves mappings");
        if (args[1] == "inspect" && options.contains("--pid") && !options.contains("--out"))
            options["--out"] = QDir::current().absoluteFilePath(
                "snapshot-" + options.at("--pid") +
                (options.contains("--lite") ? "-lite.jsonl" : ".json"));
        const auto option = [&](const QString &key) {
            auto it = options.find(key);
            if (it == options.end() || it->second.isEmpty())
                throw std::runtime_error("missing option " + key.toStdString());
            return it->second;
        };
        const auto command = args[1];
        Json result;
        const auto contracts = [&]() {
            return Json::read(
                filePath(options.contains("--contracts")
                             ? option("--contracts")
                             : QCoreApplication::applicationDirPath() + "/contracts-v1.json"));
        };
        const Events events = [](const Json &value) {
            auto data = value;
            const auto type = data.at("event").string();
            data.object().erase("event");
            event(type, data);
        };
        if (command == "inspect" || command == "inspect-detail") {
            Json snapshot;
            if (options.contains("--pid")) {
                if (command == "inspect-detail") {
                    (void)option("--candidates");
                    const auto selection = Json::read(filePath(option("--candidates")));
                    if (options.contains("--reuse-snapshot") && selection.at("classes").array().empty()) {
                        result = readSnapshot(filePath(option("--reuse-snapshot")));
                        result["classes"] = Json::Array{};
                        result["loaderInstances"] = selection.at("loaderBindings");
                    } else result = captureLive(options, Json(), events);
                    if (options.contains("--reuse-snapshot"))
                        result = mergeDetailReuse(result, selection, readSnapshot(filePath(option("--reuse-snapshot"))));
                } else if (!options.contains("--lite") && options.contains("--pack")) {
                    auto liteOptions = options;
                    liteOptions["--lite"] = "true";
                    auto lite = captureLive(liteOptions, contracts(), events);
                    writeSnapshotFile(filePath(option("--out") + ".lite.jsonl"), lite);
                    Json reference;
                    const Json *source = nullptr;
                    if (options.contains("--reference")) {
                        reference = readSnapshot(filePath(option("--reference")));
                        source = &reference;
                    }
                    auto candidates = selectDetailCandidates(Json::read(filePath(option("--pack"))),
                                                             lite, source, contracts(), events);
                    const auto path = option("--out") + ".candidates.json";
                    writeJson(filePath(path), candidates);
                    auto detailOptions = options;
                    detailOptions.erase("--pack");
                    detailOptions["--candidates"] = path;
                    result = captureLive(detailOptions, Json(), events);
                } else
                    result = captureLive(options, options.contains("--pack") ? contracts() : Json(),
                                         events);
            } else
                result = readSnapshot(filePath(option("--snapshot")));
            if (options.contains("--out")) {
                if (options.contains("--lite") || (result.contains("detailLevel") &&
                                                   result.at("detailLevel").string() == "selected"))
                    writeSnapshotFile(filePath(option("--out")), result);
                else
                    writeJson(filePath(option("--out")), result);
            }
            event("fingerprint",
                  Json::Object{{"output", options.contains("--out")
                                              ? option("--out").toUtf8().toStdString()
                                              : ""},
                               {"fingerprint", result.at("fingerprint")},
                               {"classes", double(result.at("classes").array().size())},
                               {"launchEvidence", result.contains("launchEvidence") ? result.at("launchEvidence") : Json(Json::Array{})},
                               {"methods", result.at("methodCount")},
                               {"captureKind", result.at("captureKind")},
                               {"pid", result.at("pid")},
                               {"processStart", result.at("processStart")}});
        } else if (command == "identify") {
            result = mappingIdentity(Json::read(filePath(option("--pack"))),
                                     readSnapshot(filePath(option("--snapshot"))), contracts());
            writeJson(filePath(option("--out")), result);
            event("mapping-identity", result);
        } else if (command == "select") {
            auto lite = readSnapshot(filePath(option("--snapshot")));
            Json reference;
            const Json *source = nullptr;
            if (options.contains("--reference")) {
                reference = readSnapshot(filePath(option("--reference")));
                source = &reference;
            }
            result = selectDetailCandidates(Json::read(filePath(option("--pack"))), lite, source,
                                            contracts(), events, options.contains("--allow-empty"), options.contains("--validation-scope"));
            if (options.contains("--reuse-snapshot")) {
                result = partitionDetailReuse(result, lite, readSnapshot(filePath(option("--reuse-snapshot"))));
                event("DETAIL_REUSE", Json::Object{{"capturedClasses", double(result.at("classes").array().size())},
                    {"retainedClasses", double(result.at("reuseClasses").array().size())}});
            }
            result["identity"] = mappingIdentity(Json::read(filePath(option("--pack"))), lite, contracts());
            writeJson(filePath(option("--out")), result);
        } else if (command == "validate-cache") {
            result = validateCachedRuntime(Json::read(filePath(option("--pack"))),
                readSnapshot(filePath(option("--snapshot"))), contracts(),
                option("--binding-identity").toStdString(), events);
            writeJson(filePath(option("--out")), result);
            event("validation", result);
            if (!result.at("valid").boolean()) return 3;
        } else if (command == "validate") {
            const auto pack = Json::read(filePath(option("--pack")));
            result = options.contains("--snapshot")
                         ? validateRuntime(pack, readSnapshot(filePath(option("--snapshot"))),
                                           contracts(), events)
                         : validatePack(pack);
            if (options.contains("--snapshot") && result.at("valid").boolean())
                result["identity"] = mappingIdentity(result.at("pack"), readSnapshot(filePath(option("--snapshot"))), contracts());
            if (options.contains("--out"))
                writeJson(filePath(option("--out")), result);
            event("validation", result);
            if (!result.at("valid").boolean())
                return 3;
        } else if (command == "resolve") {
            const auto pack = Json::read(filePath(option("--pack")));
            const auto target = readSnapshot(filePath(option("--snapshot")));
            Json reference;
            const Json *source = nullptr;
            if (options.contains("--reference")) {
                reference = readSnapshot(filePath(option("--reference")));
                source = &reference;
            }
            Json state;const Json* prior=nullptr;
            if(options.contains("--state")){state=Json::read(filePath(option("--state")));prior=&state;}
            result = resolveMappings(pack, source, target, contracts(), events, prior, options.contains("--incremental"));
            writeJson(filePath(option("--out")), result);
            event("candidate", Json::Object{{"complete", result.at("complete")},
                                            {"fingerprint", result.at("fingerprint")}});
            if (!result.at("complete").boolean())
                return 4;
            if (options.contains("--write-pack") && options.contains("--incremental"))
                throw std::runtime_error("incremental results are provisional; independent final validation required");
            if (options.contains("--write-pack"))
                writeJson(filePath(option("--write-pack")), result.at("pack"));
        } else if (command == "diff") {
            result = diffPacks(Json::read(filePath(option("--before"))),
                               Json::read(filePath(option("--after"))));
            if (options.contains("--out"))
                writeJson(filePath(option("--out")), result);
            event("diff", result);
        } else
            throw std::runtime_error("unsupported command");
        event("complete", Json::Object{{"success", true}, {"command", command.toStdString()}});
        return 0;
    } catch (const RuntimeChanged &e) {
        event("failure",Json::Object{{"reason",e.what()},{"retryable",true},{"failureType","runtime-drift"}});
        return 5;
    } catch (const SnapshotLimit &e) {
        event("failure", e.json());
        return 1;
    } catch (const std::exception &e) {
        event("failure", Json::Object{{"reason", e.what()}});
        return 1;
    }
}
