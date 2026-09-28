#include "../src/MediaSessionService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <windows.h>
struct MediaSessionServiceShutdownTest {
    static int run() {
        auto* service=new MediaSessionService;
        bool destroying=false,received=false;int lateSignals=0;
        QObject::connect(service,&MediaSessionService::stateChanged,
            [&](bool available,bool,const QString&,const QString&,const QString&,const QString&,qint64,qint64){
                if(destroying)++lateSignals;else received|=available;
            });
        service->m_bridge.setProgram(QCoreApplication::applicationFilePath());
        service->m_bridge.setArguments({"--bridge-fixture"});
        service->m_bridge.start();
        if(!service->m_bridge.waitForStarted(5000)){delete service;return 1;}
        const auto pid=service->m_bridge.processId();
        QElapsedTimer wait;wait.start();
        while(!received&&wait.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(5);}
        destroying=true;
        delete service; // Child ignores QUIT, exercising the kill/finished path.
        HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));
        const bool ended=!child||WaitForSingleObject(child,0)==WAIT_OBJECT_0;
        if(child)CloseHandle(child);
        std::printf("Media shutdown: received=%d lateSignals=%d childExited=%d\n",received,lateSignals,ended);
        return received&&lateSignals==0&&ended?0:1;
    }
};
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--bridge-fixture")){
        std::printf("STATE\t1\tVGVzdCB0aXRsZQ==\tVGVzdCBhcnRpc3Q=\tVGVzdA==\t0\t1000\t\t0\n");
        std::fflush(stdout);QThread::sleep(20);return 0;
    }
    return MediaSessionServiceShutdownTest::run();
}
