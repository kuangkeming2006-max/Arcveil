#include "../../agent/bindings/MappingPack.h"
#include <cstdio>
#include <iomanip>
#include <sstream>
using namespace mcoverlay;
using namespace mcoverlay::bindings;
using mapping::Json;
int main(){
    int failures=0,checks=0;
    auto check=[&](bool b,const char* message){++checks;if(!b){++failures;std::printf("FAIL %s\n",message);}};
    const auto path=std::filesystem::path(MC_MAPPING_SOURCE_DIR)/"packs/default-v1.json";
    const auto document=Json::read(path);
    const auto pack=loadMappingPack(path);
    check(mappingPackJson(pack)==document,"lossless complete pack roundtrip");
    const auto golden=Json::read(std::filesystem::path(MC_MAPPING_TEST_DIR)/"legacy-parity.json");
    std::size_t dictionaries=0;
    for(const auto& p:pack.providers)for(const auto& d:p.dictionaries){
        ++dictionaries;
        Json actual=mappingDictionaryJson(d),symbols=Json::Object{};
        for(const auto& name:golden.at("legacyKeys").array())symbols[name.string()]=actual.at("symbols").at(name.string());
        check(symbols.object().size()==247,"all original schema fields covered");actual["symbols"]=std::move(symbols);
        std::uint64_t hash=14695981039346656037ULL;for(unsigned char c:actual.dump()){hash^=c;hash*=1099511628211ULL;}
        std::ostringstream text;text<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
        check(text.str()==golden.at("dictionaryDigests").at(d.id).string(),d.id.c_str());
        check(d.cameraMouseOverCandidates==std::array<std::string,3>{"objectMouseOver","field_71476_x","s"},"legacy alternative order preserved");
        check(!d.diggingPacketName.empty()&&!d.hitTypeSignature.empty(),"namespace-dependent resolver data is in pack");
    }
    check(dictionaries==4,"all four legacy dictionaries migrated");
    MappingRegistry registry(path);check(registry.healthy()&&registry.freeze(),"explicit pack loads and freezes");
    auto late=pack.providers.front().dictionaries.front();late.id="late-test";
    check(registry.registerDictionary(late)==MappingRegistrationResult::Frozen,"pack loading preserves freeze boundary");
    MappingRegistry missing(path.parent_path()/"does-not-exist.json");
    check(!missing.healthy()&&!missing.freeze(),"missing pack fails closed without compiled symbol fallback");
    const auto reject=[&](Json j,const char* why){bool rejected=false;try{(void)parseMappingPack(j);}catch(...){rejected=true;}check(rejected,why);};
    auto j=document;j["schemaVersion"]=2;reject(j,"unsupported schema rejected");
    j=document;j["packVersion"]=0;reject(j,"invalid pack version rejected");
    j=document;j["extra"]=true;reject(j,"unknown root key rejected");
    j=document;j["providers"].array()[0]["priority"]=-1;reject(j,"invalid priority rejected");
    j=document;j["providers"].array()[0]["dictionaries"].array()[0]["symbols"]["positionFields"]=Json::Array{"x","y"};reject(j,"short symbol arrays rejected");
    j=document;j["providers"].array()[0]["dictionaries"].array()[0]["symbols"].object().erase("getHealth");reject(j,"missing schema field rejected");
    j=document;j["providers"].array()[0]["dictionaries"].array()[0]["symbols"]["madeUpLogicalKey"]="x";reject(j,"unknown logical symbol rejected");
    j=document;j["providers"].array()[0]["dictionaries"].array()[0]["symbols"]["minecraftSignature"]="Lwrong;";reject(j,"name/signature mismatch rejected");
    j=document;j["providers"].array()[0]["dictionaries"].array().push_back(j["providers"].array()[0]["dictionaries"].array()[0]);reject(j,"duplicate dictionary rejected before registration");
    j=document;j["providers"].array()[0]["detection"]=Json::Array{Json::Object{{"match",3},{"value","x"},{"confidence",1}}};reject(j,"unknown detection enum rejected");
    for(const auto text:{"{\"x\":1,\"x\":2}","{\"x\":01}","[1,]","1e999","\"\\uD800\"","null garbage"}){bool rejected=false;try{(void)Json::parse(text);}catch(...){rejected=true;}check(rejected,"malformed JSON rejected");}
    check(Json::parse("\"\\u4f60\\u597d\"").string()=="你好","Unicode JSON roundtrip");
    std::printf("Mapping pack parity: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
