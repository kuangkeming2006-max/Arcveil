#include "../../src/MappingService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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
        service.m_probe=build+"/tools/MappingProbe-v13.dll";
        service.m_contracts=source+"/mapping/contracts-v2.json";
        service.m_defaultPack=service.m_legacyPack=source+"/mapping/packs/default-v2.json";
        service.m_modernPack=source+"/mapping/packs/vanilla-1.20.1.json";
        int ready=0,failed=0,promoted=0;
        QObject::connect(&service,&MappingService::ready,[&](const auto&,const auto&){++ready;});
        QObject::connect(&service,&MappingService::failed,[&](const QString& reason){++failed;std::printf("SERVICE_FAILURE %s\n",qPrintable(reason));});
        QObject::connect(&service,&MappingService::eventReceived,[&](const QJsonObject& e){if(e.value("event")=="CACHE_PROMOTE")++promoted;});
        const auto attach=[&] {
            service.prepare(quint32(jvm.processId()),java,build+"/tools/McOverlayAttachHelper.jar",true,{},{});
            QElapsedTimer time;time.start();
            while(service.busy() && time.elapsed()<45000) QCoreApplication::processEvents(QEventLoop::AllEvents,10);
            if(service.busy()) {std::printf("TIMEOUT %s\n",qPrintable(service.status()));service.cancel();return false;}
            return true;
        };
        check(cache.isValid(),"isolated cache available");
        check(attach() && ready==1 && !failed,"cold 1.20.1 service preflight ready");
        check(service.m_minecraftVersion=="1.20.1" && service.m_family=="Vanilla","catalog selects modern API and Vanilla independently");
        check(!service.m_runtimeBinding.value("instance").isUndefined(),"fresh defining loader pin returned");
        check(promoted==1,"verified modern cache proof promoted");
        check(attach() && ready==2 && !failed,"repeat service preflight revalidates modern cache");
        check(service.m_cachePath,"modern cache candidate accepted after fresh live check");
        service.cancel();jvm.terminate();if(!jvm.waitForFinished(5000)){jvm.kill();jvm.waitForFinished();}
        check(jvm.readAllStandardError().isEmpty(),"no checked JNI warnings in owned Java 17 JVM");
        std::printf("Modern MappingService: %d checks, %d failures (owned Java 17, synthetic game)\n",checks,failures);
        return failures?1:0;
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(argc!=5)return 2;
    return MappingServiceTests::run(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),
        QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
}
