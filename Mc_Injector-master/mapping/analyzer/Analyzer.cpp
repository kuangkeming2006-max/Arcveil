#include "Analyzer.h"
#include "../../agent/bindings/MappingPack.h"
#include <QCryptographicHash>
#include <QSaveFile>
#include <algorithm>
#include <set>

namespace mcoverlay::mapping {
std::filesystem::path filePath(const QString& path){return std::filesystem::path(path.toStdWString());}
QString qtPath(const std::filesystem::path& path){return QString::fromStdWString(path.wstring());}
void writeJson(const std::filesystem::path& path,const Json& value){
    QSaveFile file(qtPath(path));if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("cannot open output");
    const auto bytes=value.dump();if(file.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size())||!file.commit())throw std::runtime_error("atomic output failed");
}
std::string sha256(std::string_view value){return QCryptographicHash::hash(QByteArrayView(value.data(),qsizetype(value.size())),QCryptographicHash::Sha256).toHex().toStdString();}
Json inspectSnapshot(Json snapshot){
    const bool lite=snapshot.contains("detailLevel")&&snapshot.at("detailLevel").string()=="lite";
    if(snapshot.at("snapshotVersion").integer()!=1||!snapshot.at("complete").boolean()||snapshot.at("captureKind").string()!=(lite?"jvmti-metadata-double-read":"jvmti-installed-double-read"))throw std::runtime_error("incomplete/unsupported live snapshot");
    if(snapshot.contains("normalizedVersion")) {
        if(snapshot.at("normalizedVersion").integer()!=1)throw std::runtime_error("unsupported normalized snapshot");
        const auto digest=sha256(Json(Json::Object{{"snapshotVersion",1},{"classes",snapshot.at("classes")},{"launchEvidence",snapshot.contains("launchEvidence")?snapshot.at("launchEvidence"):Json(Json::Array{})}}).dump());
        if(snapshot.at("fingerprint").string()!=(lite?sha256("lite-v1:"+digest):digest))throw std::runtime_error("snapshot fingerprint mismatch");
        if(snapshot.at("classes").array().empty())throw std::runtime_error("empty snapshot");
        return snapshot;
    }
    auto& classes=snapshot["classes"].array();if(classes.empty()||classes.size()>100000)throw std::runtime_error("empty/oversize snapshot");
    std::map<int,std::string> loaderKeys;std::set<std::string> groups;
    for(const auto& loader:snapshot.at("loaders").array()){
        const int id=loader.at("id").integer();Json::Array names;
        for(const auto& c:classes)if(c.at("loader").integer()==id)names.push_back(c.at("name"));
        std::sort(names.begin(),names.end(),[](const Json&a,const Json&b){return a.string()<b.string();});
        const auto key=sha256(Json(Json::Object{{"type",loader.at("type")},{"classes",names}}).dump());
        if(!groups.insert(key).second)throw std::runtime_error("indistinguishable defining loaders; explicit scope required");
        loaderKeys[id]=key;
    }
    loaderKeys[0]="bootstrap";
    const auto convert=[&](Json& ref){const int id=ref.at("loader").integer();if(!loaderKeys.contains(id))throw std::runtime_error("unknown defining loader");ref["loaderKey"]=loaderKeys.at(id);ref.object().erase("loader");};
    std::set<std::string> ids;std::size_t methods=0,fields=0;
    for(auto& c:classes){
        convert(c);convert(c["super"]);for(auto& ref:c["interfaces"].array())convert(ref);
        const auto id=c.at("loaderKey").string()+c.at("name").string();if(!ids.insert(id).second)throw std::runtime_error("duplicate class identity");
        methods+=c.at("methods").array().size();fields+=c.at("fields").array().size();
        if(!lite){(void)c.at("constantPoolCount").integer();(void)c.at("constantPool").string();}
        else if(c.contains("constantPool"))throw std::runtime_error("lite snapshot contains detailed data");
    }
    std::sort(classes.begin(),classes.end(),[](const Json&a,const Json&b){return a.at("loaderKey").string()+a.at("name").string()<b.at("loaderKey").string()+b.at("name").string();});
    // Runtime identity is tracked separately: the content fingerprint can be reused
    // across sessions, but no snapshot from another process authorizes injection.
    const auto fingerprint=sha256(Json(Json::Object{{"snapshotVersion",1},{"classes",classes},{"launchEvidence",snapshot.contains("launchEvidence")?snapshot.at("launchEvidence"):Json(Json::Array{})}}).dump());
    snapshot["fingerprint"]=lite?sha256("lite-v1:"+fingerprint):fingerprint;snapshot["methodCount"]=double(methods);snapshot["fieldCount"]=double(fields);
    snapshot["normalizedVersion"]=1;snapshot.object().erase("loaders");return snapshot;
}
Json validatePack(const Json& pack){const auto parsed=bindings::parseMappingPack(pack);int count=0;for(const auto&p:parsed.providers)count+=int(p.dictionaries.size());return Json::Object{{"valid",true},{"level","schema"},{"injectionReady",false},{"packId",parsed.id},{"dictionaries",count}};}
Json diffPacks(const Json& before,const Json& after){
    (void)bindings::parseMappingPack(before);(void)bindings::parseMappingPack(after);
    std::map<std::string,Json> a,b;
    for(const auto&p:before.at("providers").array())for(const auto&d:p.at("dictionaries").array())a.emplace(d.at("id").string(),d);
    for(const auto&p:after.at("providers").array())for(const auto&d:p.at("dictionaries").array())b.emplace(d.at("id").string(),d);
    Json::Array changes;std::set<std::string> ids;for(const auto&[id,d]:a)ids.insert(id);for(const auto&[id,d]:b)ids.insert(id);
    for(const auto&id:ids){if(!a.contains(id)||!b.contains(id)){changes.push_back(Json::Object{{"dictionary",id},{"kind",a.contains(id)?"removed":"added"}});continue;}
        for(const auto&[key,value]:a.at(id).at("symbols").object())if(value!=b.at(id).at("symbols").at(key))changes.push_back(Json::Object{{"dictionary",id},{"symbol",key},{"before",value},{"after",b.at(id).at("symbols").at(key)}});
        for(const auto* key:{"label","family","detection"})if(a.at(id).at(key)!=b.at(id).at(key))changes.push_back(Json::Object{{"dictionary",id},{"metadata",key},{"before",a.at(id).at(key)},{"after",b.at(id).at(key)}});
    }
    return Json::Object{{"changed",before!=after},{"changes",changes},{"beforeDigest",sha256(before.dump())},{"afterDigest",sha256(after.dump())}};
}
}
