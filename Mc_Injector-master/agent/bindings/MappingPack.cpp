#include "MappingPack.h"
#include <algorithm>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mcoverlay::bindings {
namespace {
using mapping::Json;
void keys(const Json& j,std::initializer_list<std::string_view> expected) {
    if(j.object().size()!=expected.size())throw std::runtime_error("unexpected/missing mapping pack key");
    for(auto key:expected)(void)j.at(key);
}
std::string bounded(const Json& j,std::size_t maximum=512) {
    auto s=j.string();if(s.size()>maximum||s.find('\0')!=std::string::npos)throw std::runtime_error("mapping string limit/NUL");return s;
}
bool identifier(std::string_view s){return !s.empty()&&s.size()<=64&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-';});}
ClientFamily family(const Json& j) {const auto& s=j.string();if(s=="Vanilla")return ClientFamily::Vanilla;if(s=="Forge")return ClientFamily::Forge;if(s=="Lunar")return ClientFamily::Lunar;throw std::runtime_error("unknown mapping family");}
std::vector<DetectionPattern> detection(const Json& j) {
    if(j.array().size()>24)throw std::runtime_error("detection capacity");
    std::vector<DetectionPattern> result;
    for(const auto& v:j.array()) {
        keys(v,{"match","value","confidence"});int match=v.at("match").integer(),confidence=v.at("confidence").integer();
        auto value=bounded(v.at("value"));
        if(match<0||match>2||confidence<=0||confidence>65535||value.empty()||(match!=2&&value.front()!='L'))throw std::runtime_error("invalid detection pattern");
        result.push_back({static_cast<DetectionMatch>(match),std::move(value),static_cast<std::uint16_t>(confidence)});
    }return result;
}
Json detectionJson(const std::vector<DetectionPattern>& d){Json::Array out;for(const auto& p:d)out.push_back(Json::Object{{"match",int(p.match)},{"value",p.value},{"confidence",int(p.confidence)}});return out;}
MappingDictionary dictionary(const Json& document) {
    keys(document,{"id","label","family","detection","symbols"});
    MappingDictionary d;d.id=bounded(document.at("id"),64);d.label=bounded(document.at("label"),128);d.family=family(document.at("family"));d.detection=detection(document.at("detection"));
    const auto& symbols=document.at("symbols");std::size_t count=0;
#define MC_MAPPING_STRING(name) d.name=bounded(symbols.at(#name));++count;
#define MC_MAPPING_ARRAY(name,extent) {const auto& a=symbols.at(#name).array();if(a.size()!=extent)throw std::runtime_error("wrong symbol array length: " #name);for(std::size_t i=0;i<extent;++i)d.name[i]=bounded(a[i]);++count;}
#include "MappingSymbols.inc"
#undef MC_MAPPING_STRING
#undef MC_MAPPING_ARRAY
    if(symbols.object().size()!=count)throw std::runtime_error("unknown logical symbol key");
    std::string error;if(!d.validate(&error))throw std::runtime_error(error);
    return d;
}
}
MappingPack parseMappingPack(const mapping::Json& document) {
    keys(document,{"schemaVersion","packVersion","id","gameVersion","source","providers"});
    MappingPack pack;pack.schemaVersion=document.at("schemaVersion").integer();pack.packVersion=document.at("packVersion").integer();
    pack.id=bounded(document.at("id"),64);pack.gameVersion=bounded(document.at("gameVersion"),64);pack.source=bounded(document.at("source"),1024);
    if(pack.schemaVersion!=1||pack.packVersion<1||!identifier(pack.id)||pack.gameVersion.empty())throw std::runtime_error("unsupported/invalid mapping pack version");
    const auto& entries=document.at("providers").array();if(entries.empty()||entries.size()>32)throw std::runtime_error("mapping provider capacity");
    std::set<std::string> ids;
    for(const auto& entry:entries) {
        keys(entry,{"id","family","priority","detection","dictionaries"});MappingPackProvider p;
        p.id=bounded(entry.at("id"),64);p.family=family(entry.at("family"));p.priority=entry.at("priority").integer();p.detection=detection(entry.at("detection"));
        if(!identifier(p.id)||p.priority<0||p.priority>10000||!ids.insert(p.id).second)throw std::runtime_error("invalid/duplicate provider metadata");
        const auto& dictionaries=entry.at("dictionaries").array();if(dictionaries.empty()||dictionaries.size()>16)throw std::runtime_error("dictionary capacity");
        for(const auto& item:dictionaries){auto d=dictionary(item);if(d.family!=p.family||!ids.insert(d.id).second)throw std::runtime_error("duplicate/family-mismatched dictionary");p.dictionaries.push_back(std::move(d));}
        pack.providers.push_back(std::move(p));
    }return pack;
}
MappingPack loadMappingPack(const std::filesystem::path& path){return parseMappingPack(mapping::Json::read(path));}
mapping::Json mappingDictionaryJson(const MappingDictionary& d) {
    Json symbols=Json::Object{};
#define MC_MAPPING_STRING(name) symbols[#name]=d.name;
#define MC_MAPPING_ARRAY(name,extent) {Json::Array a;for(const auto& v:d.name)a.emplace_back(v);symbols[#name]=std::move(a);}
#include "MappingSymbols.inc"
#undef MC_MAPPING_STRING
#undef MC_MAPPING_ARRAY
    return Json::Object{{"id",d.id},{"label",d.label},{"family",clientFamilyName(d.family)},{"detection",detectionJson(d.detection)},{"symbols",std::move(symbols)}};
}
mapping::Json mappingPackJson(const MappingPack& pack) {
    Json::Array providers;for(const auto& p:pack.providers){Json::Array dicts;for(const auto& d:p.dictionaries)dicts.push_back(mappingDictionaryJson(d));providers.push_back(Json::Object{{"id",p.id},{"family",clientFamilyName(p.family)},{"priority",p.priority},{"detection",detectionJson(p.detection)},{"dictionaries",std::move(dicts)}});}
    return Json::Object{{"schemaVersion",pack.schemaVersion},{"packVersion",pack.packVersion},{"id",pack.id},{"gameVersion",pack.gameVersion},{"source",pack.source},{"providers",std::move(providers)}};
}
std::filesystem::path defaultMappingPackPath() {
#ifdef _WIN32
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&defaultMappingPackPath),&module))throw std::runtime_error("mapping module unavailable");
    std::wstring path(32768,L'\0');DWORD size=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!size||size>=path.size())throw std::runtime_error("mapping module path unavailable");
    path.resize(size);
    return std::filesystem::path(path).parent_path()/"mappings"/"default-v1.json";
#else
    throw std::runtime_error("explicit mapping pack path required on this platform");
#endif
}
}
