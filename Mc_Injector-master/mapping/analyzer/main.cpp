#include "Analyzer.h"
#include "Resolver.h"
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <cstdio>
using namespace mcoverlay::mapping;
namespace {
void event(std::string type,Json data){data["event"]=std::move(type);data["eventVersion"]=1;const auto line=data.dump()+"\n";std::fwrite(line.data(),1,line.size(),stdout);std::fflush(stdout);}
Json readSnapshot(const std::filesystem::path& path){auto j=Json::read(path,64U*1024U*1024U);return inspectSnapshot(std::move(j));}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    try {
        if(args.size()<2)throw std::runtime_error("Usage: MappingAnalyzer inspect|validate|diff [--snapshot file | --pid PID --java executable] --pack file --out file");
        std::map<QString,QString> options;
        for(int i=2;i<args.size();i+=2){if(i+1>=args.size()||!args[i].startsWith("--")||options.contains(args[i]))throw std::runtime_error("invalid/duplicate option");options[args[i]]=args[i+1];}
        const auto option=[&](const QString& key){auto it=options.find(key);if(it==options.end()||it->second.isEmpty())throw std::runtime_error("missing option "+key.toStdString());return it->second;};
        const auto command=args[1];Json result;
        const auto contracts=[&](){return Json::read(filePath(options.contains("--contracts")?option("--contracts"):QCoreApplication::applicationDirPath()+"/contracts-v1.json"));};
        const Events events=[](const Json& value){auto data=value;const auto type=data.at("event").string();data.object().erase("event");event(type,data);};
        if(command=="inspect"){
            Json snapshot;
            if(options.contains("--pid")){
                bool valid=false;const auto pid=option("--pid").toUInt(&valid);if(!valid||!pid)throw std::runtime_error("invalid PID");
                const auto output=filePath(option("--out"));const auto requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
                const auto rawPath=output.wstring()+L"."+requestId.toStdWString()+L".capture";
                const auto request=Json(Json::Object{{"output",qtPath(rawPath).toUtf8().toStdString()},{"requestId",requestId.toStdString()}}).dump();
                const auto directory=QCoreApplication::applicationDirPath();
                const auto helper=options.contains("--helper")?option("--helper"):directory+"/McOverlayAttachHelper.jar";
                const auto probe=options.contains("--probe")?option("--probe"):directory+"/MappingProbe.dll";
                event("capture",Json::Object{{"pid",double(pid)},{"phase","started"}});
                QProcess process;process.setProgram(option("--java"));process.setArguments({"--add-modules","jdk.attach","-jar",helper,QString::number(pid),probe,QString::fromLatin1(QByteArray::fromStdString(request).toHex())});
                process.start();if(!process.waitForStarted(5000))throw std::runtime_error("cannot start JVM attach helper");
                if(!process.waitForFinished(60000)){process.kill();process.waitForFinished(3000);throw std::runtime_error("probe capture timed out");}
                if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){QFile::remove(qtPath(rawPath));throw std::runtime_error("probe attach failed: "+process.readAllStandardError().toStdString());}
                try {snapshot=Json::read(rawPath,64U*1024U*1024U);QFile::remove(qtPath(rawPath));}catch(...){QFile::remove(qtPath(rawPath));throw;}
                if(snapshot.at("requestId").string()!=requestId.toStdString()||snapshot.at("pid").number()!=pid)throw std::runtime_error("stale/mismatched probe response");
                result=inspectSnapshot(std::move(snapshot));
            }else result=readSnapshot(filePath(option("--snapshot")));
            if(options.contains("--out"))writeJson(filePath(option("--out")),result);
            event("fingerprint",Json::Object{{"fingerprint",result.at("fingerprint")},{"classes",double(result.at("classes").array().size())},{"methods",result.at("methodCount")},{"captureKind",result.at("captureKind")}});
        }else if(command=="validate"){
            const auto pack=Json::read(filePath(option("--pack")));
            result=options.contains("--snapshot")?validateRuntime(pack,readSnapshot(filePath(option("--snapshot"))),contracts(),events):validatePack(pack);if(options.contains("--out"))writeJson(filePath(option("--out")),result);event("validation",result);if(!result.at("valid").boolean())return 3;
        }else if(command=="resolve"){
            const auto pack=Json::read(filePath(option("--pack")));const auto target=readSnapshot(filePath(option("--snapshot")));
            Json reference;const Json* source=nullptr;if(options.contains("--reference")){reference=readSnapshot(filePath(option("--reference")));source=&reference;}
            result=resolveMappings(pack,source,target,contracts(),events);
            writeJson(filePath(option("--out")),result);
            event("candidate",Json::Object{{"complete",result.at("complete")},{"fingerprint",result.at("fingerprint")}});
            if(!result.at("complete").boolean())return 4;
            if(options.contains("--write-pack"))writeJson(filePath(option("--write-pack")),result.at("pack"));
        }else if(command=="diff"){
            result=diffPacks(Json::read(filePath(option("--before"))),Json::read(filePath(option("--after"))));if(options.contains("--out"))writeJson(filePath(option("--out")),result);event("diff",result);
        }else throw std::runtime_error("unsupported command");
        event("complete",Json::Object{{"success",true},{"command",command.toStdString()}});return 0;
    }catch(const std::exception&e){event("failure",Json::Object{{"reason",e.what()}});return 1;}
}
