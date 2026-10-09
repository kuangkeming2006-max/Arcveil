// Real Analyzer algorithms with an offline, process-bound capture transport.
// This fixture never fabricates validation/resolve receipts.
#define main productionAnalyzerMain
#include "../../mapping/analyzer/main.cpp"
#undef main
#include <QThread>
#include <windows.h>
int main(int argc, char **argv) {
    QStringList args;
    for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    QFile log(qEnvironmentVariable("ARCVEIL_TRANSACTION_LOG"));
    if (log.open(QIODevice::Append)) log.write(args.value(1).toUtf8() + '\n');
    log.close();
    if (args.value(1) != "inspect" && args.value(1) != "inspect-detail")
        return productionAnalyzerMain(argc, argv);
    QCoreApplication app(argc, argv);
    const auto get = [&](const QString &key) { return args.value(args.indexOf(key) + 1); };
    try {
        if (qEnvironmentVariable("ARCVEIL_TRANSACTION_SLOW") == args[1]) QThread::msleep(2000);
        auto raw = Json::read(filePath(qEnvironmentVariable("ARCVEIL_TRANSACTION_RAW")));
        const auto pid = get("--pid").toUInt();
        auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        FILETIME created{}, exit{}, kernel{}, user{};
        if (!handle || !GetProcessTimes(handle, &created, &exit, &kernel, &user)) return 1;
        CloseHandle(handle);
        raw["pid"] = double(pid);
        raw["processStart"] = QString::number((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime).toStdString();
        raw["requestId"] = "fixture-live-capture";
        raw["loaders"].array()[0]["instance"] = qEnvironmentVariableIntValue("ARCVEIL_TRANSACTION_LOADER");
        const auto family = qEnvironmentVariable("ARCVEIL_TRANSACTION_FAMILY", "Lunar").toStdString();
        raw["launchEvidence"] = Json::Array{Json::Object{{"family", family}, {"confidence", 250}}};
        if (qEnvironmentVariableIntValue("ARCVEIL_TRANSACTION_EXTRA")) {
            auto unrelated = raw.at("classes").array()[0];
            unrelated["name"] = "LUnrelatedLoadedClass;"; unrelated["fields"] = Json::Array{};
            unrelated["methods"] = Json::Array{}; raw["classes"].array().push_back(unrelated);
            std::reverse(raw["classes"].array().begin(), raw["classes"].array().end());
        }
        Json selection;
        if (args[1] == "inspect") {
            raw["detailLevel"] = "lite"; raw["captureKind"] = "jvmti-metadata-double-read";
            for (auto &c : raw["classes"].array()) {
                for (auto key : {"constantPool", "constantPoolCount", "major", "minor"}) c.object().erase(key);
                for (auto &m : c["methods"].array()) m.object().erase("bytecode");
            }
        } else {
            selection = Json::read(filePath(get("--candidates")));
            Json::Array selected;
            for (const auto &c : raw.at("classes").array())
                for (const auto &item : selection.at("classes").array()) if (c.at("name") == item.at("name")) selected.push_back(c);
            raw["classes"] = selected; raw["detailLevel"] = "selected";
            raw["captureKind"] = "jvmti-selected-detail-double-read";
            raw["loaderBindings"] = selection.at("loaderBindings"); raw["captureScope"] = selection;
        }
        Json result;
        if (raw.at("classes").array().empty() && args.contains("--reuse-snapshot")) {
            result = readSnapshot(filePath(get("--reuse-snapshot")));
            result["classes"] = Json::Array{};
        } else result = inspectSnapshot(raw);
        if (args.contains("--reuse-snapshot")) result = mergeDetailReuse(result, selection, readSnapshot(filePath(get("--reuse-snapshot"))));
        writeSnapshotFile(filePath(get("--out")), result);
        event("fingerprint", Json::Object{{"fingerprint", result.at("fingerprint")},
            {"pid", result.at("pid")}, {"processStart", result.at("processStart")},
            {"classes", double(result.at("classes").array().size())}, {"launchEvidence", result.at("launchEvidence")}});
        event("complete", Json::Object{{"success", true}});
        return 0;
    } catch (const std::exception &e) { event("failure", Json::Object{{"reason", e.what()}}); return 1; }
}
