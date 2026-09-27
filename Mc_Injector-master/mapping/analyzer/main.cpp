#include "Analyzer.h"
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
void event(std::string type,Json data){data["event"]=std::move(type);data["eventVersion"]=1;const auto line=data.dump()+"\n";std::fwrite(line.data(),1,line.size(),stdout);std::fflush(stdout);}
Json readSnapshot(const std::filesystem::path& path){auto j=Json::read(path,64U*1024U*1024U);return inspectSnapshot(std::move(j));}
}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    try {
        if(args.size()==2 && (args[1]=="--help" || args[1]=="help")){ event("help",Json::Object{{"usage","MappingAnalyzer inspect|validate|resolve|diff; see docs/mapping/README.md for arguments"}});return 0;}
        if(args.size()<2)throw std::runtime_error("Usage: MappingAnalyzer inspect|validate|resolve|diff [--snapshot file | --pid PID --java executable] --pack file --out file");
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
                Json requestDocument=Json::Object{{"output",qtPath(rawPath).toUtf8().toStdString()},{"requestId",requestId.toStdString()}};
                if(options.contains("--pack")){
                    const auto authored=Json::read(filePath(option("--pack")));(void)mcoverlay::bindings::parseMappingPack(authored);const auto schema=contracts();Json::Array warmup,detection;
                    for(const auto&p:authored.at("providers").array())for(const auto&d:p.at("dictionaries").array()){
                        Json::Array names;for(const auto&[key,spec]:schema.at("symbols").object())if(spec.at("kind").string()=="class"&&!d.at("symbols").at(key).string().empty())names.push_back(d.at("symbols").at(key));
                        warmup.push_back(Json::Object{{"anchor",d.at("symbols").at("minecraftSignature")},{"classes",names}});
                        for(const auto&pattern:d.at("detection").array())if(pattern.at("match").integer()==2)detection.push_back(Json::Object{{"family",d.at("family")},{"value",pattern.at("value")},{"confidence",pattern.at("confidence")}});
                    }
                    requestDocument["warmup"]=warmup;requestDocument["detection"]=detection;
                }
                const auto request=requestDocument.dump();
                const auto directory=QCoreApplication::applicationDirPath();
                const auto helper=options.contains("--helper")?option("--helper"):directory+"/McOverlayAttachHelper.jar";
                const auto probe=options.contains("--probe")?option("--probe"):directory+"/MappingProbe.dll";
                event("capture",Json::Object{{"pid",double(pid)},{"phase","started"}});
                const auto requestHex=QString::fromLatin1(QByteArray::fromStdString(request).toHex());
                QStringList arguments;
                if(options.contains("--tools-jar"))arguments={"-cp",helper+";"+option("--tools-jar"),"com.mcoverlay.attach.AttachHelper"};
                else arguments={"--add-modules","jdk.attach","-jar",helper};
                arguments<<QString::number(pid)<<probe<<requestHex;
                QProcess process;process.setProgram(option("--java"));process.setArguments(arguments);
                process.start();bool success=process.waitForStarted(5000)&&process.waitForFinished(60000)&&process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0;
                if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(3000);}
                if(!success&&options.contains("--native-loader")){
                    event("capture",Json::Object{{"phase","native-fallback"},{"reason","standard JVM Attach unavailable"}});
                    QProcess native;native.start(option("--native-loader"),{QString::number(pid),probe,requestHex});
                    success=native.waitForStarted(5000)&&native.waitForFinished(25000)&&native.exitStatus()==QProcess::NormalExit&&native.exitCode()==0;
                    if(native.state()!=QProcess::NotRunning){native.kill();native.waitForFinished(3000);}
                    if(!success)throw std::runtime_error("native probe failed: "+native.readAllStandardError().toStdString());
                    QElapsedTimer deadline;deadline.start();
                    while(!QFile::exists(qtPath(rawPath))&&!QFile::exists(qtPath(rawPath)+".error")&&deadline.elapsed()<60000)QThread::msleep(50);
                }
                if(!success){QFile::remove(qtPath(rawPath));throw std::runtime_error("probe attach failed: "+process.readAllStandardError().toStdString());}
                if(QFile::exists(qtPath(rawPath)+".error")){auto failure=Json::read(std::filesystem::path(rawPath+L".error"));QFile::remove(qtPath(rawPath)+".error");throw std::runtime_error(failure.at("reason").string());}
                try {snapshot=Json::read(rawPath,64U*1024U*1024U);QFile::remove(qtPath(rawPath));}catch(...){QFile::remove(qtPath(rawPath));throw;}
                if(snapshot.at("requestId").string()!=requestId.toStdString()||snapshot.at("pid").number()!=pid)throw std::runtime_error("stale/mismatched probe response");
                result=inspectSnapshot(std::move(snapshot));
            }else result=readSnapshot(filePath(option("--snapshot")));
            if(options.contains("--out"))writeJson(filePath(option("--out")),result);
            event("fingerprint",Json::Object{{"fingerprint",result.at("fingerprint")},{"classes",double(result.at("classes").array().size())},{"methods",result.at("methodCount")},{"captureKind",result.at("captureKind")},{"pid",result.at("pid")},{"processStart",result.at("processStart")}});
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
