#include "../../src/MappingService.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

// Owns its Java 17 process; exercises the production service, Probe and Analyzer.
// Only Minecraft objects are synthetic. No external PID or production cache.
struct MappingServiceTests {
    static int run(const QString& build,const QString& java,const QString& fixture,const QString& source) {
        int checks=0,failures=0;
        const auto check=[&](bool ok,const char* label) {++checks;if (!ok) {++failures;std::printf("FAIL %s\n",label);}};
        QProcess jvm;
        jvm.start(java,{"-Xcheck:jni","-cp",fixture,"FixtureBoot"});
        if (!jvm.waitForStarted() || !jvm.waitForReadyRead(10000) || jvm.readAllStandardOutput().trimmed()!="FIXTURE_READY") {
            jvm.kill();jvm.waitForFinished();return 2;
        }
        QTemporaryDir cache;
        MappingService service;
        service.m_root=cache.path()+"/cache";
        service.m_tools=build+"/tools";
        service.m_analyzer=build+"/MappingAnalyzer.exe";
        service.m_probe=build+"/tools/MappingProbe-v17.dll";
        service.m_contracts=source+"/mapping/contracts-v2.json";
        service.m_defaultPack=service.m_legacyPack=source+"/mapping/packs/default-v2.json";
        service.m_modernPack=source+"/mapping/packs/vanilla-1.20.1.json";
        int ready=0,failed=0,promoted=0,autoCalls=0,windows=0,cycle=0;
        QJsonArray requests,snapshots,stages,cycles;
        QObject::connect(&service,&MappingService::ready,[&](const auto&,const auto&){++ready;});
        QObject::connect(&service,&MappingService::failed,[&](const QString& reason){++failed;std::printf("SERVICE_FAILURE %s\n",qPrintable(reason));});
        QObject::connect(&service,&MappingService::eventReceived,[&](const QJsonObject& e){
            if(e.value("event")=="CACHE_PROMOTE")++promoted;
            if(e.value("event")=="AUTO_RESOLVE")++autoCalls;
            if(e.value("event")=="mapping-fallback")++windows;
            auto metric=e;
            metric.insert("cycle",cycle);
            if(e.value("event")=="CAPTURE_REQUEST") requests.append(metric);
            if(e.value("event")=="SNAPSHOT_STATS" && !e.value("capturedClassCount").isNull()) snapshots.append(metric);
            if(e.value("event")=="STAGE_PERFORMANCE") stages.append(metric);
        });
        const auto attach=[&] {
            ++cycle;
            QElapsedTimer time;time.start();
            service.prepare(quint32(jvm.processId()),java,build+"/tools/McOverlayAttachHelper.jar",true,{},{});
            while(service.busy() && time.elapsed()<45000) QCoreApplication::processEvents(QEventLoop::AllEvents,10);
            if(service.busy()) {std::printf("TIMEOUT %s\n",qPrintable(service.status()));service.cancel();return false;}
            cycles.append(QJsonObject{{"cycle",cycle},{"durationMs",double(time.elapsed())},
                {"mappingVerified",ready==cycle && !failed},{"agentReady","not exercised by service test"},
                {"rendererReady","not exercised by service test"}});
            return true;
        };
        check(cache.isValid(),"isolated cache available");
        check(attach() && ready==1 && !failed,"cold 1.20.1 service preflight ready");
        check(service.m_minecraftVersion=="1.20.1" && service.m_family=="Vanilla","catalog selects modern API and Vanilla independently");
        check(!service.m_runtimeBinding.value("instance").isUndefined(),"fresh defining loader pin returned");
        check(promoted==0 && service.m_transaction->detailCaptureCalls==0,
              "Vanilla fast path captures metadata only and never promotes an installed-code Cache proof");
        check(service.m_transaction->verifiedRuntimeContext.value("versionVerified").toBool() &&
              service.m_transaction->verifiedRuntimeContext.value("validationMode")=="authored-metadata" &&
              service.m_transaction->verifiedRuntimeContext.value("mappingNamespace")=="Notch" &&
              service.m_transaction->verifiedRuntimeContext.value("transactionId")==service.transactionId(),
              "current transaction publishes independently verified runtime context");
        check(attach() && ready==2 && !failed,"repeat service preflight independently revalidates authored modern mapping");
        check(!service.m_cachePath && autoCalls==0 && windows==0,"Vanilla success has no automatic resolver or Mapping window");

        int detection=0,vanilla=0;
        bool scoped=true,bounded=true,metadataOnly=true,detectionMinimal=true,statsComplete=true;
        bool selectedVanilla=true;
        QJsonObject tierSummaries;
        for(const auto& item:requests) {
            const auto request=item.toObject();
            const auto tier=request.value("captureTier").toString();
            scoped &= tier=="detection" || tier=="vanilla-validation";
            bounded &= request.value("requestBytes").toDouble()>0 &&
                request.value("requestBytes").toDouble()<32768 &&
                request.value("configuredRequestBytes").toInt()==32768;
            scoped &= !request.value("identityCatalogUsed").toBool(true) && request.value("scope")=="selected";
            if(tier=="detection") detectionMinimal &= request.value("warmupClassCount").toInt()==0 &&
                request.value("requestedClassCount").toInt()==request.value("runtimeAnchorCount").toInt();
            if(tier=="vanilla-validation") selectedVanilla &= request.value("runtimeAnchorCount").toInt()==1 &&
                request.value("requestedClassCount").toInt()>0 && request.value("requestedClassCount").toInt()<32;
        }
        for(const auto& item:snapshots) {
            const auto stat=item.toObject();
            const auto tier=stat.value("captureTier").toString();
            detection += tier=="detection";
            vanilla += tier=="vanilla-validation";
            metadataOnly &= stat.value("bytecodeBytes").toDouble()==0 && stat.value("constantPoolBytes").toDouble()==0 &&
                stat.value("jvmtiBytecodeBytes").toDouble()==0 && stat.value("jvmtiConstantPoolBytes").toDouble()==0;
            if(tier=="detection") detectionMinimal &= stat.value("fieldCount").toInt()==0 &&
                stat.value("methodCount").toInt()==0 && stat.value("warmupAttemptCount").toInt()==0;
            statsComplete &= stat.contains("durationMs") && stat.value("durationMs").toDouble()>=0 &&
                stat.value("capturedClassCount").toInt()>0 && stat.value("requestBytes").toDouble()>0;
            auto summary=tierSummaries.value(tier).toObject();
            summary.insert("captures",summary.value("captures").toInt()+1);
            summary.insert("maxRequestBytes",qMax(summary.value("maxRequestBytes").toDouble(),stat.value("requestBytes").toDouble()));
            summary.insert("capturedClassCountTotal",summary.value("capturedClassCountTotal").toInt()+stat.value("capturedClassCount").toInt());
            summary.insert("probeDurationMsTotal",summary.value("probeDurationMsTotal").toDouble()+stat.value("durationMs").toDouble());
            summary.insert("bytecodeBytesTotal",summary.value("bytecodeBytesTotal").toDouble()+stat.value("bytecodeBytes").toDouble());
            summary.insert("constantPoolBytesTotal",summary.value("constantPoolBytesTotal").toDouble()+stat.value("constantPoolBytes").toDouble());
            tierSummaries.insert(tier,summary);
        }
        check(!requests.isEmpty() && !snapshots.isEmpty(),"production capture emits request and snapshot diagnostics");
        check(detection==2 && vanilla==4,"each transaction uses one Detection and two independent Vanilla Validation snapshots");
        check(scoped,"Vanilla production service requests no Structural Snapshot or historical identity catalog");
        check(bounded,"all production request bodies remain below unchanged 32768-byte limit");
        check(detectionMinimal,"Detection Snapshot has only independent anchor evidence without members, warmup or installed code");
        check(selectedVanilla,"Vanilla Validation requests only the selected modern dictionary scope");
        check(metadataOnly,"Detection and Vanilla Validation capture zero bytecode and constant-pool bytes");
        check(statsComplete,"each successful production snapshot reports request size, class count and Probe duration");
        service.cancel();jvm.terminate();if(!jvm.waitForFinished(5000)){jvm.kill();jvm.waitForFinished();}
        check(jvm.readAllStandardError().isEmpty(),"no checked JNI warnings in owned Java 17 JVM");
        const QJsonObject metrics{{"evidenceClass","owned Java 17 JVM with synthetic Minecraft objects"},
            {"realMinecraftTest",false},{"automaticResolverCalls",autoCalls},{"mappingWindowTriggers",windows},
            {"mappingVerifiedTransactions",ready},{"agentReady","not exercised by service test"},
            {"rendererReady","not exercised by service test"},{"requests",requests},{"snapshots",snapshots},
            {"stages",stages},{"cycles",cycles},{"tierSummaries",tierSummaries}};
        QFile metricsFile(QDir(build).filePath("tier-metrics-modern.json"));
        check(metricsFile.open(QIODevice::WriteOnly) && metricsFile.write(QJsonDocument(metrics).toJson())>0,
              "synthetic production-service tier measurements saved separately from game acceptance");
        std::printf("Modern MappingService: %d checks, %d failures (owned Java 17, synthetic game)\n",checks,failures);
        return failures?1:0;
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(argc!=5)return 2;
    return MappingServiceTests::run(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),
        QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
}
