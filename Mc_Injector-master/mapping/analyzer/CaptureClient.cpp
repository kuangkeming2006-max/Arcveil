#include "CaptureClient.h"
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
    const auto probe = option("--probe", directory + "/MappingProbe.dll");
    const auto nativeLoader = option("--native-loader", directory + "/McOverlayNativeLoader.exe");
    const auto output = filePath(QFileInfo(option("--out")).absoluteFilePath());
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    Json request = Json::Object{{"requestId", requestId},
                                {"mode", options.contains("--lite") ? "lite" : "full"}};
    if (options.contains("--pack")) {
        const auto authored = Json::read(filePath(option("--pack")));
        (void)bindings::parseMappingPack(authored);
        Json::Array warmup, detection;
        for (const auto &p : authored.at("providers").array())
            for (const auto &d : p.at("dictionaries").array()) {
                Json::Array names;
                for (const auto &[key, spec] : contracts.at("symbols").object())
                    if (spec.at("kind").string() == "class" &&
                        !d.at("symbols").at(key).string().empty())
                        names.push_back(d.at("symbols").at(key));
                warmup.push_back(Json::Object{{"anchor", d.at("symbols").at("minecraftSignature")},
                                              {"classes", names}});
                for (const auto &pattern : d.at("detection").array())
                    if (pattern.at("match").integer() == 2)
                        detection.push_back(Json::Object{{"family", d.at("family")},
                                                         {"value", pattern.at("value")},
                                                         {"confidence", pattern.at("confidence")}});
            }
        request["warmup"] = warmup;
        request["detection"] = detection;
    }
    Json paths =
        Json::Object{{"event", "CAPTURE_PATH"},
                     {"standardAttach", Json::Object{{"status", "pending"},
                                                     {"javaRuntime", java.toUtf8().toStdString()}}},
                     {"fallback", Json::Object{{"status", "not-attempted"}}}};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto rawPath = output.wstring() + L"." +
                             QString::fromStdString(requestId).toStdWString() +
                             (attempt ? L".fallback.capture" : L".standard.capture");
        const auto errorPath = std::filesystem::path(rawPath + L".error");
        request["output"] = qtPath(rawPath).toUtf8().toStdString();
        const auto encoded = QString::fromLatin1(QByteArray::fromStdString(request.dump()).toHex());
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
        if (attempt && diagnostic.at("success").boolean()) {
            QElapsedTimer deadline;
            deadline.start();
            while (!QFile::exists(qtPath(rawPath)) && !QFile::exists(qtPath(errorPath)) &&
                   deadline.elapsed() < 60000)
                QThread::msleep(50);
        }
        const auto key = attempt ? "fallback" : "standardAttach";
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
                throw std::runtime_error(failure.at("reason").string());
            }
            if (!diagnostic.at("success").boolean())
                throw std::runtime_error("helper failed: exit=" + diagnostic.at("exitCode").dump() +
                                         "; " + diagnostic.at("reason").string() +
                                         "; stderr=" + diagnostic.at("stderr").string());
            if (!QFile::exists(qtPath(rawPath)))
                throw std::runtime_error(
                    attempt ? "fallback capture timeout: no result/error file"
                            : "Attach helper exited successfully without capture output");
            auto snapshot = readSnapshotStream(rawPath);
            if (snapshot.at("requestId").string() != requestId ||
                snapshot.at("pid").number() != pid)
                throw std::runtime_error("stale/mismatched probe response");
            diagnostic["status"] = "captured";
            paths[key] = diagnostic;
            events(paths);
            auto stats = snapshot.at("stats");
            stats["event"] = "SNAPSHOT_STATS";
            stats["capturePath"] = key;
            events(stats);
            QFile::remove(qtPath(rawPath));
            return inspectSnapshot(std::move(snapshot));
        } catch (const SnapshotLimit &e) {
            diagnostic["captureFailure"] = e.json();
            diagnostic["reason"] = e.what();
        } catch (const std::exception &e) {
            diagnostic["reason"] = e.what();
        }
        diagnostic["status"] = "failed";
        paths[key] = diagnostic;
        events(paths);
        if (!diagnostic.contains("captureFailure"))
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
        QFile::remove(qtPath(rawPath));
        QFile::remove(qtPath(errorPath));
    }
    throw std::runtime_error(
        "standard Attach failed: " + paths.at("standardAttach").at("reason").string() +
        " | fallback capture failed: " + paths.at("fallback").at("reason").string());
}
} // namespace mcoverlay::mapping
