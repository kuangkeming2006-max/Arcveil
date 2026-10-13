#include "Resolver.h"
#include "../InstalledProof.h"
#include "../ProbeProtocol.h"
#include "../../agent/bindings/MappingPack.h"
#include "../../agent/bindings/VersionAdapter.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
namespace mcoverlay::mapping {
const Json& contractsForPack(const Json& pack, const Json& contracts) {
    if (contracts.at("contractVersion").integer()==1) return contracts;
    if (contracts.at("contractVersion").integer()!=2) throw std::runtime_error("unsupported contract bundle version");
    const auto version=pack.at("gameVersion").string();
    if (!bindings::selectVersionAdapter(bindings::MinecraftVersion::parse(version))) return contracts;
    if (!contracts.at("versions").contains(version)) throw std::runtime_error("missing API binding contract for Minecraft " + version);
    return contracts.at("versions").at(version);
}
namespace {
bool supportedApi(const Json& pack) {
    return bindings::selectVersionAdapter(bindings::MinecraftVersion::parse(pack.at("gameVersion").string())) != nullptr;
}
Json unsupportedApi(const Json& pack) {
    return Json::Object{{"valid",false},{"injectionReady",false},{"complete",false},
        {"requiredComplete",false},{"unsupportedApi",true},{"reason","unsupported Minecraft API version: no verified Version Adapter"},
        {"minecraftVersion",pack.at("gameVersion")}};
}
std::string sig(std::string value){std::replace(value.begin(),value.end(),'.','/');return value.empty()?"":"L"+value+";";}
std::string binary(std::string value){if(value.size()<3||value.front()!='L'||value.back()!=';')throw std::runtime_error("not a class signature");value=value.substr(1,value.size()-2);std::replace(value.begin(),value.end(),'/','.');return value;}
std::string render(const std::string& expression,const Json& symbols){std::string out;for(std::size_t i=0;i<expression.size();++i){if(expression[i]=='{'){auto end=expression.find('}',i);if(end==std::string::npos)throw std::runtime_error("invalid descriptor template");out+=symbols.at(expression.substr(i+1,end-i-1)).string();i=end;}else out+=expression[i];}return out;}
std::vector<std::string> values(const Json& value){try{return {value.string()};}catch(const std::bad_variant_access&){std::vector<std::string> out;for(const auto&v:value.array())out.push_back(v.string());return out;}}
void contractCheck(const Json& contracts,const Json& symbols){if(contracts.at("contractVersion").integer()!=1||contracts.at("symbols").object().size()!=symbols.object().size())throw std::runtime_error("logical contract/schema mismatch");for(const auto&[key,v]:symbols.object())(void)contracts.at("symbols").at(key);}
struct Method {const Json* json=nullptr;CodeShape code;};
struct Class {const Json* json=nullptr;std::vector<Method> methods;std::string structure, metadataStructure;bool supported=true;};
struct Model {
    Json snapshot;std::vector<Class> classes;
    mutable std::map<const Json*,std::vector<std::string>> usageIndex;mutable bool usagesReady=false;
    std::map<std::string,std::vector<const Class*>> nameIndex;
    explicit Model(const Json& input,bool code=false):snapshot(inspectSnapshot(input)){
        for(const auto& c:snapshot.at("classes").array()){
            Class klass;klass.json=&c;Json::Array fs,ms,metadataMethods;
            for(const auto& f:c.at("fields").array())fs.emplace_back(std::to_string(f.at("modifiers").integer()&0xd8)+descriptorShape(f.at("descriptor").string()));
            const auto codes = code ? normalizeClassBytecode(c) : std::vector<CodeShape>{};
            std::size_t methodIndex = 0;
            for(const auto& m:c.at("methods").array()){
                Method method{&m,{}};if(code)method.code=codes[methodIndex++];klass.supported &= method.code.supported;
                metadataMethods.emplace_back(std::to_string(m.at("modifiers").integer()&0x5f8)+descriptorShape(m.at("descriptor").string())+":");
                ms.emplace_back(std::to_string(m.at("modifiers").integer()&0x5f8)+descriptorShape(m.at("descriptor").string())+":"+(code?(method.code.supported?method.code.fingerprint:"unsupported"):""));klass.methods.push_back(std::move(method));
            }
            auto order=[](const Json&a,const Json&b){return a.string()<b.string();};std::sort(fs.begin(),fs.end(),order);std::sort(ms.begin(),ms.end(),order);std::sort(metadataMethods.begin(),metadataMethods.end(),order);
            Json::Array interfaces;for(const auto&i:c.at("interfaces").array())interfaces.emplace_back(descriptorShape(i.at("name").string()));std::sort(interfaces.begin(),interfaces.end(),order);
            klass.metadataStructure=sha256(Json(Json::Object{{"fields",fs},{"methods",metadataMethods},{"modifiers",c.at("modifiers").integer()&0x7610},{"super",descriptorShape(c.at("super").at("name").string())},{"interfaces",interfaces}}).dump());
            klass.structure=sha256(Json(Json::Object{{"fields",fs},{"methods",ms},{"modifiers",c.at("modifiers").integer()&0x7610},{"super",descriptorShape(c.at("super").at("name").string())},{"interfaces",interfaces}}).dump());classes.push_back(std::move(klass));
        }
        for(const auto&c:classes)nameIndex[c.json->at("name").string()].push_back(&c);
    }
    const Class* find(const std::string& name,const std::string& loader="")const{
        const Class* result=nullptr;auto it=nameIndex.find(name);if(it==nameIndex.end())return nullptr;for(const auto*c:it->second)if(loader.empty()||c->json->at("loaderKey").string()==loader){if(result)return nullptr;result=c;}return result;
    }
    const Class* related(const Json& ref)const{return ref.at("name").string().empty()?nullptr:find(ref.at("name").string(),ref.at("loaderKey").string());}
    const Json* member(const Class* klass,const std::string& kind,const std::string& name,const std::string& descriptor,bool isStatic,int depth=0)const{
        if(!klass||depth>64)return nullptr;
        for(const auto&m:klass->json->at(kind=="field"?"fields":"methods").array())if(m.at("name").string()==name&&m.at("descriptor").string()==descriptor&&bool(m.at("modifiers").integer()&8)==isStatic)return &m;
        if(name=="<init>")return nullptr;
        if(auto m=member(related(klass->json->at("super")),kind,name,descriptor,isStatic,depth+1))return m;
        for(const auto&i:klass->json->at("interfaces").array())if(auto m=member(related(i),kind,name,descriptor,isStatic,depth+1))return m;
        return nullptr;
    }
    std::vector<std::string> classReferences(const Class* target)const{
        std::vector<std::string> result;
        const auto& name=target->json->at("name").string();
        const auto contains=[&](const std::string& descriptor){for(std::size_t pos=0;(pos=descriptor.find('L',pos))!=std::string::npos;){auto end=descriptor.find(';',pos);if(end==std::string::npos)return false;if(descriptor.substr(pos,end-pos+1)==name)return true;pos=end+1;}return false;};
        for(const auto&c:classes){
            if(c.json->at("loaderKey")!=target->json->at("loaderKey"))continue;
            if(c.json->at("super").at("name").string()==name)result.push_back(c.structure+":super");
            for(const auto&i:c.json->at("interfaces").array())if(i.at("name").string()==name)result.push_back(c.structure+":interface");
            for(const auto* kind:{"fields","methods"})for(const auto&m:c.json->at(kind).array())if(contains(m.at("descriptor").string()))result.push_back(c.structure+":"+kind+":"+descriptorShape(m.at("descriptor").string()));
            for(const auto&m:c.methods)for(const auto&r:m.code.references)if(r.owner==name)result.push_back(c.structure+":"+m.code.fingerprint+":"+std::to_string(r.position)+":"+std::to_string(r.opcode));
        }
        std::sort(result.begin(),result.end());return result;
    }
    const Class* declaring(const Json* m,const std::string& kind)const{for(const auto&c:classes)for(const auto&v:c.json->at(kind=="field"?"fields":"methods").array())if(&v==m)return &c;return nullptr;}
    const Method* method(const Json* json)const{for(const auto&c:classes)for(const auto&m:c.methods)if(m.json==json)return &m;return nullptr;}
    std::vector<std::string> usages(const Class* owner,const Json& member)const{
        if(!usagesReady){
            for(const auto&c:classes)for(const auto&m:c.methods)if(m.code.supported)for(const auto&r:m.code.references){
                auto refOwner=find(r.owner,c.json->at("loaderKey").string());
                auto resolved=this->member(refOwner,r.opcode<=0xb5?"field":"method",r.name,r.descriptor,r.opcode==0xb2||r.opcode==0xb3||r.opcode==0xb8);
                if(resolved)usageIndex[resolved].push_back(c.structure+":"+m.code.fingerprint+":"+std::to_string(r.position)+":"+std::to_string(r.opcode));
            }
            for(auto&[key,list]:usageIndex)std::sort(list.begin(),list.end());usagesReady=true;
        }
        (void)owner;return usageIndex[&member];
    }
};

// Loader content keys can change when unrelated classes load. Runtime instance identity
// remains stable; use it for retention evidence and relevant-set comparison only.
std::string stableLoader(const Json& snapshot,const std::string& key) {
    if(key=="bootstrap") return key;
    if(snapshot.contains("loaderInstances")) for(const auto& item:snapshot.at("loaderInstances").array())
        if(item.at("loaderKey").string()==key) return item.at("type").string()+":"+item.at("instance").dump();
    return key; // Offline snapshots without runtime identity cannot prove loader continuity.
}
Json stableClass(Json klass,const Json& snapshot) {
    klass["loaderKey"]=stableLoader(snapshot,klass.at("loaderKey").string());
    klass["super"]["loaderKey"]=stableLoader(snapshot,klass.at("super").at("loaderKey").string());
    for(auto& item:klass["interfaces"].array()) item["loaderKey"]=stableLoader(snapshot,item.at("loaderKey").string());
    return klass;
}
std::set<std::string> requiredClasses(const Json &contracts, const Json &symbols) {
    std::set<std::string> result{"minecraftName"};
    const auto add = [&](const Json &spec, const std::string &key) {
        if (spec.at("kind").string() == "class") result.insert(key);
        if (spec.contains("owner")) result.insert(spec.at("owner").string());
        if (spec.contains("descriptor")) {
            const auto text = spec.at("descriptor").string();
            for (std::size_t i = 0; (i = text.find('{', i)) != std::string::npos;) {
                const auto end = text.find('}', i); if (end == std::string::npos) break;
                const auto value = symbols.at(text.substr(i + 1, end - i - 1)).string();
                for (const auto &[name, candidate] : contracts.at("symbols").object())
                    if (candidate.at("kind").string() == "class" && sig(symbols.at(name).string()) == value) result.insert(name);
                i = end + 1;
            }
        }
    };
    for (const auto &[key, spec] : contracts.at("symbols").object()) if (spec.at("required").boolean()) add(spec, key);
    for (const auto &group : contracts.at("requiredAlternatives").array())
        for (const auto &key : group.array()) add(contracts.at("symbols").at(key.string()), key.string());
    for (const auto &ctor : contracts.at("constructors").array()) if (ctor.at("required").boolean()) {
        auto spec = ctor; spec["kind"] = "method"; add(spec, "");
    }
    return result;
}
Json symbolEvent(const std::string& key,const Json& value,bool ok,const std::string& reason,double confidence=1.0){return Json::Object{{"event","symbol"},{"symbol",key},{"mapping",value},{"confidence",ok?confidence:0.0},{"accepted",ok},{"evidence",Json::Array{reason}},{"reason",ok?"":reason}};}
struct RuntimeScope {
    std::string version, loader, anchor;
    bool verified=false, ambiguous=false;
};
RuntimeScope runtimeScope(const Model& model) {
    RuntimeScope result;
    if (!model.snapshot.contains("runtimeEvidence")) return result;
    std::set<std::pair<std::string,std::string>> scopes;
    for (const auto& e:model.snapshot.at("runtimeEvidence").array()) {
        if (e.at("evidenceVersion").integer()!=1 || e.at("version").string().empty() ||
            e.at("versionSource").string().empty()) continue;
        const auto* anchor=model.find(e.at("anchor").string(),e.at("loaderKey").string());
        if (!anchor) continue; // Evidence cannot authorize another defining loader.
        scopes.emplace(e.at("loaderKey").string(),e.at("version").string());
        result.anchor=e.at("anchor").string();
    }
    result.ambiguous=scopes.size()>1;
    if (scopes.size()==1) {
        result.loader=scopes.begin()->first; result.version=scopes.begin()->second; result.verified=true;
    }
    return result;
}
const Class* dictionaryAnchor(const Json& dictionary,const Model& model) {
    const auto scope=runtimeScope(model);
    if (scope.ambiguous) return nullptr;
    return model.find(sig(dictionary.at("symbols").at("minecraftName").string()),scope.verified?scope.loader:"");
}
bool familyCompatible(const std::string& family,const Json& dictionary) {
    const auto& candidate=dictionary.at("family").string();
    // A Vanilla dictionary may describe shared symbols, never shared identity.
    if (family=="Unknown") return candidate=="Vanilla";
    return candidate==family || (family!="Vanilla" && candidate=="Vanilla") ||
        (family=="Badlion" && candidate=="Forge");
}
Json dictionaryValidation(const Json& dictionary,const Model& model,const Json& contracts,const Events& events){
    const auto& symbols=dictionary.at("symbols");contractCheck(contracts,symbols);
    const auto* anchor=dictionaryAnchor(dictionary,model);
    const std::string loader=anchor?anchor->json->at("loaderKey").string():"";
    bool valid=anchor!=nullptr;Json::Array results;std::map<std::string,bool> matched;
    for(const auto&[key,value]:symbols.object()){
        const auto& spec=contracts.at("symbols").at(key);const auto kind=spec.at("kind").string();bool ok=false;std::string reason;Json runtimeValue=value;
        if(kind=="class"){ok=anchor&&model.find(sig(value.string()),loader);reason=ok?"loaded class in anchor defining loader":"class missing or loader ambiguous";}
        else if(kind=="descriptor"){ok=true;reason="explicit pack descriptor; checked at member lookup";}
        else if(kind=="reserved"){ok=true;reason="reserved authored value; not consumed by current Agent";}
        else {
            const auto* owner=anchor?model.find(sig(symbols.at(spec.at("owner").string()).string()),loader):nullptr;
            auto names=values(value);bool any=false,all=true;const auto descriptor=render(spec.at("descriptor").string(),symbols);
            Json::Array foundNames;for(const auto&name:names){bool found=!name.empty()&&model.member(owner,kind,name,descriptor,spec.at("static").boolean());any|=found;all&=found;if(found)foundNames.emplace_back(name);}
            runtimeValue=foundNames;
            ok=spec.contains("alternatives")?any:all;reason=ok?"owner + exact descriptor + static/instance + inherited lookup":"live member missing (owner/descriptor/modifiers)";
        }
        matched[key]=ok;if(spec.at("required").boolean()&&!ok)valid=false;
        auto e=symbolEvent(key,value,ok,reason);e["runtimeMapping"]=runtimeValue;
        e["required"]=spec.at("required");
        if(kind=="field" || kind=="method") {
            e["owner"]=symbols.at(spec.at("owner").string());
            e["descriptor"]=render(spec.at("descriptor").string(),symbols);
            e["static"]=spec.at("static");
        }
        results.push_back(e);if(events)events(e);
    }
    for(const auto&group:contracts.at("requiredAlternatives").array()){bool found=false;for(const auto&key:group.array())found|=matched.at(key.string());if(!found)valid=false;}
    for(const auto& ctor:contracts.at("constructors").array())if(ctor.at("required").boolean()){
        const auto* owner=anchor?model.find(sig(symbols.at(ctor.at("owner").string()).string()),loader):nullptr;
        if(!model.member(owner,"method","<init>",render(ctor.at("descriptor").string(),symbols),false))valid=false;
    }
    // These modern relationships are API structure, not member-name guesses.
    // Metadata must establish the same inherited lookup path used by the Adapter.
    if (symbols.contains("entityPlayerName")) {
        for (const auto& pair:std::vector<std::pair<std::string,std::string>>{
                 {"playerName","entityPlayerName"},{"entityPlayerName","livingName"},
                 {"livingName","entityName"},{"worldClientName","worldName"}}) {
            const auto* child=anchor?model.find(sig(symbols.at(pair.first).string()),loader):nullptr;
            const auto* parent=anchor?model.find(sig(symbols.at(pair.second).string()),loader):nullptr;
            bool reaches=false;
            int depth=0;
            for (auto* c=child; c && depth++<64; c=model.related(c->json->at("super"))) {
                if (c==parent) { reaches=true; break; }
                // Model member lookups are bounded too; prevent malformed cyclic metadata.
                if (c!=child && c->json->at("super").at("name")==child->json->at("name")) break;
            }
            valid &= reaches;
            results.push_back(symbolEvent(pair.first+".hierarchy",sig(symbols.at(pair.second).string()),reaches,
                reaches?"verified Adapter inheritance in defining loader":"required Adapter inheritance missing"));
        }
    }
    return Json::Object{{"valid",valid},{"dictionary",dictionary.at("id")},{"loaderKey",loader},{"symbols",results}};
}
Json retargetPack(Json pack, const std::string &family) {
    if (family == "Unknown") return pack;
    for (auto &p : pack["providers"].array()) {
        p["family"] = family;
        for (auto &d : p["dictionaries"].array()) {
            d["family"] = family;
            if (family == "Badlion") d["detection"] = Json::Array{
                Json::Object{{"match", 1}, {"value", "Lnet/badlion/"}, {"confidence", 250}},
                Json::Object{{"match", 1}, {"value", "Lcom/badlion/"}, {"confidence", 250}},
                Json::Object{{"match", 2}, {"value", "badlion"}, {"confidence", 250}}};
        }
    }
    return pack;
}
Json selectedPack(const Json& pack,const Json& provider,const Json& dictionary){auto result=pack;auto p=provider;p["dictionaries"]=Json::Array{dictionary};result["providers"]=Json::Array{p};return result;}
std::string detectedFamily(const Json& pack,const Model& model){
    bindings::ClientEnvironment environment;
    const auto parsed=bindings::parseMappingPack(pack);
    // Family evidence is independent of which versions/families a pack supports.
    if (model.snapshot.contains("launchEvidence")) for (const auto& evidence:model.snapshot.at("launchEvidence").array())
        for (auto family:{bindings::ClientFamily::Forge,bindings::ClientFamily::Lunar,bindings::ClientFamily::Badlion})
            if (evidence.at("family").string()==bindings::clientFamilyName(family)) environment.addEvidence(family,evidence.at("confidence").integer());
    for(const auto&p:parsed.providers)for(const auto&d:p.dictionaries)if(d.family!=bindings::ClientFamily::Vanilla)for(const auto&pattern:d.detection)for(const auto&c:model.classes){const auto&name=c.json->at("name").string();if((pattern.match==bindings::DetectionMatch::ExactClassSignature&&name==pattern.value)||(pattern.match==bindings::DetectionMatch::ClassSignaturePrefix&&name.starts_with(pattern.value)))environment.addEvidence(d.family,pattern.confidence);}
    for (const auto &c : model.classes) if (c.json->at("name").string().starts_with("Lnet/badlion/") ||
        c.json->at("name").string().starts_with("Lcom/badlion/")) environment.addEvidence(bindings::ClientFamily::Badlion, 250);
    const auto scope=runtimeScope(model);
    if (scope.ambiguous) return "Unknown";
    std::set<std::string> brands;
    if (model.snapshot.contains("runtimeEvidence")) for (const auto& e:model.snapshot.at("runtimeEvidence").array()) {
        if (!scope.verified || e.at("loaderKey").string()!=scope.loader ||
            !model.find(e.at("anchor").string(),scope.loader) || e.at("brandSource").string().empty()) continue;
        auto brand=e.at("brand").string();
        std::transform(brand.begin(),brand.end(),brand.begin(),[](unsigned char c){return char(std::tolower(c));});
        if (!brand.empty()) brands.insert(brand);
    }
    if (brands.size()>1) return "Unknown";
    if (brands.size()==1) {
        const auto& brand=*brands.begin();
        if (brand=="forge") environment.addEvidence(bindings::ClientFamily::Forge,250);
        else if (brand=="lunar" || brand=="lunarclient") environment.addEvidence(bindings::ClientFamily::Lunar,250);
        else if (brand=="badlion") environment.addEvidence(bindings::ClientFamily::Badlion,250);
        else if (brand=="vanilla" && environment.family()==bindings::ClientFamily::Unknown)
            return "Vanilla";
        else if (brand!="vanilla") return "Unknown";
    }
    return bindings::clientFamilyName(environment.family());
}
}
Json identifyRuntime(const Json::Array& catalog,const Json& snapshot,const Json& contractBundle) {
    Model model(snapshot);
    const auto scope=runtimeScope(model);
    if (scope.ambiguous) throw std::runtime_error("ambiguous independent runtime versions or defining loaders");
    Json::Array candidates;
    struct Candidate {const Json* item;const Json* dict;const Class* anchor;int matched;};
    std::vector<Candidate> compatible;
    std::set<std::string> candidateScopes;
    std::string family="Unknown";
    // Family is independent of whether this catalog contains a usable dictionary.
    if (!catalog.empty()) family=detectedFamily(catalog.front().at("pack"),model);
    for (const auto& item:catalog) {
        const auto& pack=item.at("pack");
        if (!supportedApi(pack)) continue;
        if (scope.verified && pack.at("gameVersion").string()!=scope.version) continue;
        const auto& contracts=contractsForPack(pack,contractBundle);
        for (const auto& p:pack.at("providers").array()) for (const auto& d:p.at("dictionaries").array()) {
            if (!familyCompatible(family,d)) continue;
            const auto* anchor=dictionaryAnchor(d,model);
            if (!anchor) continue;
            const auto validation=dictionaryValidation(d,model,contracts,{});
            int matched=0,total=0; bool player=false,world=false,singleton=false;
            for (const auto& row:validation.at("symbols").array()) {
                const auto key=row.at("symbol").string();
                if (!contracts.at("symbols").contains(key)) continue;
                const auto& spec=contracts.at("symbols").at(key);
                const bool accepted=row.at("accepted").boolean();
                if (spec.at("required").boolean()) {++total;if(accepted)++matched;}
                if(key=="playerField") player=accepted;
                if(key=="worldField") world=accepted;
                if(key=="getMinecraft" || key=="minecraftInstanceFields") singleton |= accepted;
            }
            // Critical API metadata excludes unrelated classes with coincident obfuscated names.
            const bool critical=player && world && singleton;
            candidates.push_back(Json::Object{{"version",pack.at("gameVersion")},
                {"dictionary",d.at("id")},{"requiredMatched",matched},{"requiredTotal",total},
                {"criticalApiMatched",critical},{"anchor",anchor->json->at("name")},
                {"loaderKey",anchor->json->at("loaderKey")}});
            // Independent exact version narrows this to the authored API contract.
            // A mismatched member must reach authored diagnostics/fallback, not block
            // a trusted modified client before its recovery layers can run.
            if (!critical && !scope.verified) continue;
            compatible.push_back({&item,&d,anchor,matched});
            candidateScopes.insert(pack.at("gameVersion").string()+":"+anchor->json->at("loaderKey").string());
        }
    }
    Json result=Json::Object{{"family",family},{"minecraftVersion",scope.version},{"versionVerified",scope.verified},
        {"selectedPack",""},{"mappingNamespace","Unknown"},{"runtimeBinding",Json()},
        {"candidates",candidates},{"detectionReason",scope.verified?"independent version evidence in defining loader":"independent runtime version evidence unavailable; API candidate is diagnostic only"}};
    if(scope.verified) {
        Json binding=Json::Object{{"anchor",scope.anchor},{"loaderKey",scope.loader}};
        if(model.snapshot.contains("loaderInstances")) for(const auto& loader:model.snapshot.at("loaderInstances").array())
            if(loader.at("loaderKey").string()==scope.loader) {
                binding["type"]=loader.at("type");binding["instance"]=loader.at("instance");
            }
        result["runtimeBinding"]=binding;
    }
    if (candidateScopes.size()>1) {
        result["detectionReason"]="multiple compatible API scopes; no unique defining loader";
        return result;
    }
    if (compatible.empty()) {
        if (scope.verified) result["detectionReason"]="runtime version identified; no compatible authored Mapping/API support";
        // A renamed modified client still needs to reach Cache/Authored/Resolver
        // recovery. Version evidence chooses only the exact API contract here;
        // this grants neither member acceptance nor a verified dictionary.
        if(scope.verified && family!="Unknown" && family!="Vanilla")
            for(const auto& item:catalog) if(item.at("pack").at("gameVersion").string()==scope.version)
                for(const auto& p:item.at("pack").at("providers").array())
                    if(p.at("family").string()==family) {
                        result["selectedPack"]=item.at("path");
                        result["detectionReason"]="independent version/family identified; authored names require recovery";
                        return result;
                    }
        return result;
    }
    // Dedicated family precedes Vanilla data reuse; provider priorities cannot change identity.
    std::stable_sort(compatible.begin(),compatible.end(),[&](const Candidate& a,const Candidate& b){
        const bool exactA=a.dict->at("family").string()==family,exactB=b.dict->at("family").string()==family;
        return exactA!=exactB?exactA:a.matched>b.matched;
    });
    const auto& selected=compatible.front();
    if (!scope.verified) result["minecraftVersion"]=selected.item->at("pack").at("gameVersion");
    result["selectedPack"]=selected.item->at("path");
    result["mappingNamespace"]=selected.dict->contains("mappingNamespace")?selected.dict->at("mappingNamespace"):Json("Unknown");
    Json binding=Json::Object{{"anchor",selected.anchor->json->at("name")},{"loaderKey",selected.anchor->json->at("loaderKey")}};
    if (model.snapshot.contains("loaderInstances")) for(const auto& loader:model.snapshot.at("loaderInstances").array())
        if(loader.at("loaderKey")==selected.anchor->json->at("loaderKey")) {
            binding["type"]=loader.at("type");binding["instance"]=loader.at("instance");
        }
    result["runtimeBinding"]=binding;
    return result;
}
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contractBundle, const Events &events, bool allowEmpty, bool validationOnly, const Json *state) {
    const auto& contracts=contractsForPack(pack,contractBundle);
    (void)bindings::parseMappingPack(pack);
    Model live(lite);
    if (!lite.contains("detailLevel") || lite.at("detailLevel").string() != "lite")
        throw std::runtime_error("candidate selection requires lite index");
    (void)state;
    std::set<const Class *> selected;
    std::map<const Class *, std::string> reasons;
    // Authored exact symbols are scope requests, never automatically accepted mappings.
    for (const auto &provider : pack.at("providers").array())
        for (const auto &dict : provider.at("dictionaries").array()) {
            const auto &symbols = dict.at("symbols");
            contractCheck(contracts, symbols);
            const auto *anchor = dictionaryAnchor(dict,live);
            if (!anchor)
                continue;
            const auto required = requiredClasses(contracts, symbols);
            for (const auto &[key, spec] : contracts.at("symbols").object())
                if (spec.at("kind").string() == "class" && (!validationOnly || required.contains(key))) {
                    if (const auto *c = live.find(sig(symbols.at(key).string()),
                                                  anchor->json->at("loaderKey").string())) {
                        selected.insert(c);
                        reasons[c] = "authored pack scope; requires independent live validation";
                    }
                }
        }
    if (reference && !validationOnly) {
        Model old(*reference);
        std::map<std::string, std::vector<const Class *>> shapes;
        for (const auto &c : live.classes)
            shapes[c.structure].push_back(&c);
        for (const auto &c : old.classes)
            for (const auto *match : shapes[c.structure]) {
                selected.insert(match);
                reasons[match] = "hierarchy/descriptor/access/member structure candidate; "
                                 "nameWeight=0; not yet accepted";
            }
    }
    // Include inherited declarations/interfaces, but never blindly capture the whole JVM.
    bool changed = true;
    while (changed) {
        changed = false;
        auto current = selected;
        for (const auto *c : current) {
            auto add = [&](const Json &ref) {
                if (const auto *base = live.related(ref))
                    if (selected.insert(base).second) {
                        reasons[base] = "candidate hierarchy dependency";
                        changed = true;
                    }
            };
            add(c->json->at("super"));
            for (const auto &i : c->json->at("interfaces").array())
                add(i);
        }
    }
    if (selected.empty() && !allowEmpty)
        throw std::runtime_error("no detail candidates: authored anchors unavailable and no "
                                 "compatible verified structural reference");
    std::map<std::string, Json> loaders;
    for (const auto &item : lite.at("loaderInstances").array())
        loaders[item.at("loaderKey").string()] = item;
    Json::Array classes, relevant;
    for (const auto *c : selected) {
        const auto &value = *c->json;
        relevant.push_back(classMetadata(stableClass(value,lite)));
        const auto &loader = loaders.at(value.at("loaderKey").string());
        classes.push_back(Json::Object{{"name", value.at("name")},
                                       {"loaderKey", value.at("loaderKey")},
                                       {"instance", loader.at("instance")},
                                       {"type", loader.at("type")},
                                       {"metadataDigest", sha256(classMetadata(value).dump())},
                                       {"evidence", reasons.at(c)}});
    }
    std::sort(classes.begin(), classes.end(),
              [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
    std::sort(relevant.begin(),relevant.end(),[](const Json&a,const Json&b){return a.dump()<b.dump();});
    if (events)
        events(Json::Object{{"event", "selection"},
                            {"totalClasses", double(lite.at("classes").array().size())},
                            {"candidateClasses", double(classes.size())},
                            {"referenceAvailable", reference != nullptr}});
    return Json::Object{{"candidateVersion", 1},
                        {"relevantFingerprint",sha256(Json(relevant).dump())},
                        {"pid", lite.at("pid")},
                        {"processStart", lite.at("processStart")},
                        {"liteFingerprint", lite.at("fingerprint")},
                        {"classes", classes},
                        {"loaderBindings", lite.at("loaderInstances")},
                        {"launchEvidence", lite.contains("launchEvidence")
                                               ? lite.at("launchEvidence")
                                               : Json(Json::Array{})}};
}
Json partitionDetailReuse(Json selection, const Json &lite, const Json &prior) {
    if (lite.at("pid") != prior.at("pid") || lite.at("processStart") != prior.at("processStart"))
        throw std::runtime_error("detail reuse cannot cross transaction process identity");
    Json::Array capture, reused;
    for (const auto &item : selection.at("classes").array()) {
        const Json *current = nullptr, *saved = nullptr;
        for (const auto &c : lite.at("classes").array())
            if (c.at("name") == item.at("name") && c.at("loaderKey") == item.at("loaderKey")) current = &c;
        if (current) for (const auto &c : prior.at("classes").array())
            if (c.at("name") == current->at("name") &&
                stableClass(classMetadata(c), prior) == stableClass(classMetadata(*current), lite)) saved = &c;
        if (saved) reused.push_back(item); else capture.push_back(item);
    }
    selection["classes"] = capture; selection["reuseClasses"] = reused;
    return selection;
}
Json mergeDetailReuse(Json detail, const Json &selection, const Json &prior) {
    if (detail.at("pid") != prior.at("pid") || detail.at("processStart") != prior.at("processStart"))
        throw std::runtime_error("detail reuse crossed process identity");
    // Only metadata-checked, same-instance detail captured in this transaction is reusable.
    std::map<std::string, std::string> rebound;
    for (const auto &old : prior.at("loaderInstances").array())
        for (const auto &now : selection.at("loaderBindings").array())
            if (old.at("type") == now.at("type") && old.at("instance") == now.at("instance"))
                rebound[old.at("loaderKey").string()] = now.at("loaderKey").string();
    rebound["bootstrap"] = "bootstrap";
    for (const auto &item : selection.at("reuseClasses").array()) {
        bool found = false;
        for (const auto &old : prior.at("classes").array()) {
            const auto key = old.at("loaderKey").string();
            if (old.at("name") != item.at("name") || !rebound.contains(key) || rebound.at(key) != item.at("loaderKey").string()) continue;
            auto klass = old;
            const auto rebind = [&](Json &ref) {
                const auto loader = ref.at("loaderKey").string();
                if (!rebound.contains(loader)) throw RuntimeChanged("reuse defining loader disappeared");
                ref["loaderKey"] = rebound.at(loader);
            };
            rebind(klass); rebind(klass["super"]);
            for (auto &i : klass["interfaces"].array()) rebind(i);
            if (sha256(classMetadata(klass).dump()) != item.at("metadataDigest").string())
                throw RuntimeChanged("reuse class metadata changed");
            detail["classes"].array().push_back(klass); found = true; break;
        }
        if (!found) throw RuntimeChanged("reuse class identity unavailable");
    }
    detail["loaderInstances"] = selection.at("loaderBindings");
    detail["captureScope"] = selection;
    return refreshSnapshotFingerprint(std::move(detail));
}
Json requiredClassNames(const Json &contracts, const Json &symbols) {
    Json::Array names;
    for (const auto &key : requiredClasses(contracts, symbols))
        if (contracts.at("symbols").at(key).at("kind").string() == "class" && !symbols.at(key).string().empty())
            names.push_back(symbols.at(key));
    return names;
}
Json mappingIdentity(const Json &pack, const Json &snapshot, const Json &contractBundle, bool bindingOnly) {
    const auto& contracts=contractsForPack(pack,contractBundle);
    Model model(snapshot);
    const auto family = detectedFamily(pack, model);
    const auto scope = runtimeScope(model);
    std::set<const Class *> selected;
    std::set<std::string> contentRequired;
    bool complete = false;
    for (const auto &provider : pack.at("providers").array())
        for (const auto &dict : provider.at("dictionaries").array()) {
            if (!familyCompatible(family,dict)) continue;
            const auto &symbols = dict.at("symbols");
            contractCheck(contracts, symbols);
            const auto *anchor = dictionaryAnchor(dict,model);
            if (!anchor) continue;
            bool ready = true;
            const auto required = requiredClasses(contracts, symbols);
            for (const auto &[key, spec] : contracts.at("symbols").object())
                if (spec.at("kind").string() == "class" && required.contains(key) && !symbols.at(key).string().empty()) {
                    const auto *c = model.find(sig(symbols.at(key).string()), anchor->json->at("loaderKey").string());
                    if (c) { selected.insert(c); contentRequired.insert(c->json->at("name").string()); }
                    else if (spec.at("required").boolean()) ready = false;
                }
            complete |= ready;
        }
    bool changed = true;
    while (changed) {
        changed = false;
        const auto current = selected;
        for (const auto *c : current) {
            const auto add = [&](const Json &ref) {
                if (const auto *base = model.related(ref)) changed |= selected.insert(base).second;
            };
            add(c->json->at("super"));
            for (const auto &i : c->json->at("interfaces").array()) add(i);
        }
    }
    const auto stable = [&](Json klass) {
        const auto loader = [&](const Json &key) -> Json {
            if (snapshot.contains("loaderInstances"))
                for (const auto &item : snapshot.at("loaderInstances").array())
                    if (item.at("loaderKey") == key) return item.at("type");
            return key.string() == "bootstrap" ? Json("bootstrap") : Json("defining-loader");
        };
        klass["loaderKey"] = loader(klass.at("loaderKey"));
        klass["super"]["loaderKey"] = loader(klass.at("super").at("loaderKey"));
        for (auto &item : klass["interfaces"].array()) item["loaderKey"] = loader(item.at("loaderKey"));
        return classMetadata(klass);
    };
    Json::Array metadata, detail, bindings;
    std::map<std::string, Json::Array> loaderGroups;
    for (const auto *c : selected) loaderGroups[c->json->at("loaderKey").string()].push_back(c->json->at("name"));
    std::map<std::string, std::string> loaderRoles;
    for (auto &[key, names] : loaderGroups) {
        std::sort(names.begin(), names.end(), [](const Json &a, const Json &b) { return a.string() < b.string(); });
        loaderRoles[key] = "required-loader:" + sha256(Json(names).dump());
    }
    const auto role = [&](const Json &key) {
        const auto name = key.string();
        return name == "bootstrap" ? name : loaderRoles.contains(name) ? loaderRoles.at(name) : "uncaptured:" + name;
    };
    bool bindingsComplete = complete;
    const bool lite = snapshot.contains("detailLevel") && snapshot.at("detailLevel").string() == "lite";
    for (const auto *c : selected) {
        auto klass = bindingOnly ? Json() : stable(*c->json);
        if (!bindingOnly) metadata.push_back(klass);
        const auto &source = *c->json;
        const bool content = contentRequired.contains(source.at("name").string());
        const auto installed = source.contains("installedDigest") ? source.at("installedDigest").string()
            : !lite ? sha256(content ? installedClassMaterial(source) : installedHierarchyMaterial(source)) : std::string{};
        if (source.contains("installedDigest")) bindingsComplete &= source.contains("installedProofKind") &&
            source.at("installedProofKind").string() == (content ? "required-content" : "hierarchy-metadata");
        bindingsComplete &= installed.size() == 64;
        Json::Array interfaces;
        for (const auto &i : source.at("interfaces").array()) interfaces.push_back(Json::Object{{"name", i.at("name")}, {"loader", role(i.at("loaderKey"))}});
        std::sort(interfaces.begin(), interfaces.end(), [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
        bindings.push_back(Json::Object{{"name", source.at("name")}, {"loader", role(source.at("loaderKey"))},
            {"superLoader", role(source.at("super").at("loaderKey"))}, {"interfaces", interfaces},
            {"proofKind", content ? "required-content" : "hierarchy-metadata"}, {"digest", installed}});
        if (!lite && !bindingOnly) {
            Json::Array methods;
            const auto codes = normalizeClassBytecode(*c->json);
            std::size_t methodIndex = 0;
            for (const auto &method : c->json->at("methods").array()) {
                const auto &code = codes[methodIndex++];
                methods.push_back(Json::Object{{"name", method.at("name")},
                    {"descriptor", method.at("descriptor")}, {"code", code.supported ? code.fingerprint :
                        sha256(method.at("bytecode").string() + c->json->at("constantPool").string())},
                    {"supported", code.supported}});
            }
            std::sort(methods.begin(), methods.end(), [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
            klass["normalizedMethods"] = methods;
            detail.push_back(klass);
        }
    }
    auto sort = [](Json::Array &items) { std::sort(items.begin(), items.end(), [](const Json &a, const Json &b) { return a.dump() < b.dump(); }); };
    sort(metadata); sort(detail); sort(bindings);
    Json runtimeBinding;
    for (const auto &p : pack.at("providers").array()) for (const auto &d : p.at("dictionaries").array()) {
        if (!familyCompatible(family,d) || family=="Unknown") continue;
        const auto *anchor = dictionaryAnchor(d,model);
        if (!anchor) continue;
        if (snapshot.contains("loaderInstances")) for (const auto &loader : snapshot.at("loaderInstances").array())
            if (loader.at("loaderKey") == anchor->json->at("loaderKey"))
                runtimeBinding = Json::Object{{"anchor", anchor->json->at("name")}, {"loaderKey",anchor->json->at("loaderKey")}, {"type", loader.at("type")}, {"instance", loader.at("instance")}};
    }
    const auto version = pack.at("gameVersion");
    const auto digest = [&](const Json::Array &classes, const char *domain) {
        return sha256(Json(Json::Object{{"identityVersion", 1}, {"domain", domain},
            {"family", family}, {"minecraftVersion", version}, {"contracts", sha256(contracts.dump())},
            {"analyzerRevision", 5}, {"schemaVersion", pack.at("schemaVersion")}, {"classes", classes}}).dump());
    };
    return Json::Object{{"family", family}, {"minecraftVersion", scope.version},
        {"versionVerified",scope.verified && !scope.ambiguous && scope.version==version.string()},
        {"apiCandidateVersion",version},
        {"metadataIdentity", complete && !bindingOnly ? digest(metadata, "metadata") : ""},
        {"mappingIdentity", complete && !lite && !bindingOnly ? digest(detail, "installed-structure") : ""},
        {"bindingIdentity", bindingsComplete ? digest(bindings, "installed-binding-v3") : ""},
        {"bindingClasses", bindings},
        {"runtimeBinding", runtimeBinding},
        {"identityComplete", complete}, {"identityClasses", double(selected.size())}};
}
Json validateAuthoredRuntime(const Json& pack,const Json& snapshot,const Json& contractBundle,const Events& events) {
    (void)bindings::parseMappingPack(pack);
    if (!supportedApi(pack)) return unsupportedApi(pack);
    const auto& contracts=contractsForPack(pack,contractBundle);
    Model model(snapshot);
    const auto scope=runtimeScope(model);
    const auto family=detectedFamily(pack,model);
    Json::Array attempts;
    Json accepted;
    for(const auto& p:pack.at("providers").array()) for(const auto& d:p.at("dictionaries").array()) {
        if(d.at("family").string()!="Vanilla") continue;
        auto validation=dictionaryValidation(d,model,contracts,{});
        attempts.push_back(validation);
        if (!validation.at("valid").boolean()) continue;
        auto selected=selectedPack(pack,p,d);
        auto identity=mappingIdentity(selected,model.snapshot,contractBundle);
        // Separate proof domain: authored exact metadata is not installed bytecode proof
        // and must never be promoted as a structural resolver/cache reference.
        const bool ready=scope.verified && scope.version==pack.at("gameVersion").string() &&
            family=="Vanilla" && identity.at("runtimeBinding").contains("instance");
        identity["mappingIdentity"]=sha256(Json(Json::Object{{"domain","authored-metadata-v1"},
            {"pack",sha256(selected.dump())},{"metadata",identity.at("metadataIdentity")},
            {"version",scope.version},{"family",family},{"binding",identity.at("runtimeBinding")}}).dump());
        identity["bindingIdentity"]="";
        identity["validationMode"]="authored-metadata";
        validation["injectionReady"]=ready;
        validation["validationMode"]="authored-metadata";
        validation["level"]="authored-exact-members-and-loader";
        validation["identity"]=identity;
        validation["pack"]=selected;
        validation["selectedPack"]=selected;
        validation["fingerprint"]=model.snapshot.at("fingerprint");
        validation["reason"]=ready?"":"independent matching version and positive Vanilla identity required";
        if(events)for(const auto& row:validation.at("symbols").array())events(row);
        accepted=validation;
        if(ready) return accepted;
    }
    if (accepted!=Json()) return accepted;
    if(events) for(const auto& attempt:attempts) for(const auto& row:attempt.at("symbols").array()) events(row);
    return Json::Object{{"valid",false},{"injectionReady",false},{"validationMode","authored-metadata"},
        {"reason","Vanilla authored Mapping required descriptor, hierarchy or defining loader mismatch"},
        {"minecraftVersion",scope.version},{"family",family},{"attempts",attempts}};
}
Json validateCachedRuntime(const Json &pack, const Json &snapshot, const Json &contractBundle,
                           const std::string &bindingIdentity,
                           const Events &events) {
    const auto& contracts=contractsForPack(pack,contractBundle);
    (void)bindings::parseMappingPack(pack);
    if (!supportedApi(pack)) return unsupportedApi(pack);
    const auto identity = mappingIdentity(pack, snapshot, contracts, true);
    // bindingIdentity covers metadata/content and the defining-loader partition.
    // Diagnostic metadataIdentity retains original session-specific method names.
    if (bindingIdentity.empty() ||
        identity.at("bindingIdentity").string() != bindingIdentity ||
        !identity.at("runtimeBinding").contains("instance"))
        return Json::Object{{"valid", false}, {"injectionReady", false}, {"reason", "cached-required-structure-or-loader-changed"}};
    Model model(snapshot);
    const auto family = detectedFamily(pack, model);
    const auto scope=runtimeScope(model);
    if (scope.verified && scope.version!=pack.at("gameVersion").string()) return unsupportedApi(pack);
    if (pack.at("gameVersion").string()=="1.20.1" && family!="Vanilla")
        return Json::Object{{"valid",false},{"injectionReady",false},{"reason","Unsupported: Minecraft 1.20.1 requires a verified Vanilla Mapping; modified-client Mapping unavailable"}};
    for (const auto &p : pack.at("providers").array()) for (const auto &d : p.at("dictionaries").array()) {
        if (!familyCompatible(family,d) || family=="Unknown") continue;
        auto result = dictionaryValidation(d, model, contracts, {});
        if (!result.at("valid").boolean()) continue;
        if (events) for (const auto &e : result.at("symbols").array()) events(e);
        result["injectionReady"] = true;
        result["level"] = "cached-required-members-and-installed-digests";
        result["fingerprint"] = snapshot.at("fingerprint");
        result["pack"] = pack;
        result["identity"] = identity;
        return result;
    }
    return Json::Object{{"valid", false}, {"injectionReady", false}, {"reason", "cached-required-member-validation-failed"}};
}
Json validateRuntime(const Json& pack,const Json& snapshot,const Json& contractBundle,const Events& events){
    const auto& contracts=contractsForPack(pack,contractBundle);
    if(snapshot.contains("detailLevel")&&snapshot.at("detailLevel").string()=="lite")throw std::runtime_error("lite metadata is diagnostic only; inspect-detail validation is required before mapping/injection");
    (void)bindings::parseMappingPack(pack);if(!supportedApi(pack))return unsupportedApi(pack);Model model(snapshot);Json::Array attempts;
    const auto family=detectedFamily(pack,model);auto providers=pack.at("providers").array();std::stable_sort(providers.begin(),providers.end(),[](const Json&a,const Json&b){return a.at("priority").integer()>b.at("priority").integer();});
    const auto scope=runtimeScope(model);
    if (scope.ambiguous || (scope.verified && scope.version!=pack.at("gameVersion").string()))
        return Json::Object{{"valid",false},{"injectionReady",false},{"reason","runtime version/loader evidence is incompatible with selected Mapping"}};
    if (pack.at("gameVersion").string()=="1.20.1" && family!="Vanilla")
        return Json::Object{{"valid",false},{"injectionReady",false},{"reason","Unsupported: Minecraft 1.20.1 requires a verified Vanilla Mapping; modified-client Mapping unavailable"}};
    for(const auto&p:providers)for(const auto&d:p.at("dictionaries").array()){
        if(!familyCompatible(family,d))continue;
        auto result=dictionaryValidation(d,model,contracts,{});attempts.push_back(result);
        if(result.at("valid").boolean()){
            if(events)for(const auto&e:result.at("symbols").array())events(e);
            const auto scope=runtimeScope(model);
            result["injectionReady"]=family!="Unknown" && !scope.ambiguous && (!scope.verified || scope.version==pack.at("gameVersion").string());result["level"]="live-members";result["fingerprint"]=model.snapshot.at("fingerprint");result["pack"]=retargetPack(selectedPack(pack,p,d),family);result["packDigest"]=sha256(result.at("pack").dump());return result;
        }
    }
    return Json::Object{{"valid",false},{"injectionReady",false},{"level","live-members"},{"reason","no family-compatible dictionary passed required live bindings"},{"attempts",attempts},{"fingerprint",model.snapshot.at("fingerprint")}};
}
Json resolveMappings(const Json& pack,const Json* reference,const Json& target,const Json& contractBundle,const Events& events,const Json* state,bool incremental){
    const auto& contracts=contractsForPack(pack,contractBundle);
    if((target.contains("detailLevel")&&target.at("detailLevel").string()=="lite")||(reference&&reference->contains("detailLevel")&&reference->at("detailLevel").string()=="lite"))throw std::runtime_error("lite metadata cannot enter normalized bytecode/call-graph matching; inspect-detail required");
    (void)bindings::parseMappingPack(pack);
    if(!supportedApi(pack)){auto rejected=unsupportedApi(pack);rejected["fingerprint"]=target.at("fingerprint");return rejected;}
    Model live(target,true);Json::Array attempts;
    std::unique_ptr<Model> baseline;if(reference)baseline=std::make_unique<Model>(*reference,true);
    Json best; int bestScore=-1;
    const bool stateBound=state && state->contains("stateVersion") && state->at("stateVersion").integer()==1 &&
        state->at("packDigest").string()==sha256(pack.dump()) && state->at("contractDigest").string()==sha256(contracts.dump()) &&
        state->at("pid")==target.at("pid") && state->at("processStart")==target.at("processStart") && baseline &&
        state->at("referenceFingerprint")==baseline->snapshot.at("fingerprint");
    for(const auto&p:pack.at("providers").array())for(const auto&original:p.at("dictionaries").array()){
        if(!familyCompatible(detectedFamily(pack,live),original)) continue;
        if(stateBound && state->at("dictionary")!=original.at("id")) continue;
        auto dict=original;
        // Structural remapping may produce a mixed/transformed namespace.
        // Preserve legacy schema-1 as Unknown; never mislabel recovered names SRG/MCP.
        if(pack.at("schemaVersion").integer()==2)dict["mappingNamespace"]="Custom";
        auto& symbols=dict["symbols"];contractCheck(contracts,symbols);
        bool complete=bool(baseline);Json::Array results;std::map<const Class*,const Class*> classes;std::map<std::string,std::string> names;
        // Preserve the concrete rejection rather than presenting every unavailable
        // correspondence as a confidence or ambiguity problem. These diagnostics
        // never change eligibility, thresholds, or the final live-validation gate.
        std::map<const Class*,std::string> classFailures;
        if(baseline){
            std::map<std::string,std::vector<const Class*>> structures;
            for(const auto&c:live.classes)if(c.supported)structures[c.structure].push_back(&c);
            for(const auto& c:baseline->classes){
                if(!c.supported){classFailures[&c]="installed method uses unsupported dynamic bootstrap evidence; bootstrap attributes unavailable";continue;}
                const auto& matches=structures[c.structure];std::vector<const Class*> eligible;
                const auto references=baseline->classReferences(&c);
                const bool hasBody=std::any_of(c.methods.begin(),c.methods.end(),[](const Method&m){return m.json->at("name").string()!="<init>"&&m.json->at("name").string()!="<clinit>"&&m.json->at("bytecode").string().size()>8;});
                if(!hasBody&&references.size()<2){classFailures[&c]="bytecode-less class lacks independent incoming reference evidence";continue;}
                for(const auto* candidate:matches)if(references==live.classReferences(candidate))eligible.push_back(candidate);
                if(eligible.size()==1)classes[&c]=eligible.front();
                else if(matches.empty())classFailures[&c]="no supported target class has identical normalized installed structure";
                else if(eligible.empty())classFailures[&c]="normalized class structure exists but incoming reference evidence differs";
                else classFailures[&c]="multiple target classes have identical normalized structure and incoming reference evidence";
            }
            // Reject all sides of a collision before modifying the map.
            bool changed=true;
            while(changed){std::set<const Class*> rejected;
                for(const auto&[from,to]:classes){
                    if(baseline->find(from->json->at("name").string())!=from||live.find(to->json->at("name").string())!=to){rejected.insert(from);classFailures[from]="class name is ambiguous across defining loaders";}
                    for(const auto&entry:classes)if(entry.first!=from&&entry.second==to){rejected.insert(from);classFailures[from]="multiple reference classes correspond to the same target class";}
                    const auto* oldSuper=baseline->related(from->json->at("super"));const auto* newSuper=live.related(to->json->at("super"));
                    if(oldSuper&&(!classes.contains(oldSuper)||classes.at(oldSuper)!=newSuper)){rejected.insert(from);classFailures[from]="superclass lacks a verified unique structural correspondence: "+oldSuper->json->at("name").string();}
                    for(const auto&iface:from->json->at("interfaces").array())if(const auto* old=baseline->related(iface)){
                        bool found=false;if(classes.contains(old))for(const auto&targetIface:to->json->at("interfaces").array())found|=live.related(targetIface)==classes.at(old);
                        if(!found){rejected.insert(from);classFailures[from]="interface lacks a verified unique structural correspondence: "+old->json->at("name").string();}
                    }
                }
                changed=!rejected.empty();for(auto* key:rejected)classes.erase(key);
            }
            for(const auto&[from,to]:classes)names[from->json->at("name").string()]=to->json->at("name").string();
        }
        const auto rewrite=[&](std::string descriptor,bool& ok){std::string out;for(std::size_t i=0;i<descriptor.size();++i){if(descriptor[i]=='L'){auto end=descriptor.find(';',i);if(end==std::string::npos){ok=false;return descriptor;}auto name=descriptor.substr(i,end-i+1);if(names.contains(name))out+=names.at(name);else {out+=name;if(!name.starts_with("Ljava/")&&!name.starts_with("Ljavax/")&&!name.starts_with("Lcom/mojang/")&&!name.starts_with("Lorg/lwjgl/"))ok=false;}i=end;}else out+=descriptor[i];}return out;};

        // A retained binding needs the same unique class correspondence, runtime owner,
        // descriptor/modifiers, installed bytes and incoming references as its original proof.
        const auto proof=[&](const std::string& key,const Json& value)->std::string {
            if(!baseline) return "";
            const auto authored=values(original.at("symbols").at(key));
            if(std::all_of(authored.begin(),authored.end(),[](const auto& v){return v.empty();}))return sha256("authored-omission:"+key);
            const auto& spec=contracts.at("symbols").at(key);const auto kind=spec.at("kind").string();
            std::set<std::string> dependencies;
            const auto scan=[&](const std::string& descriptor) {
                for(std::size_t at=0;(at=descriptor.find('L',at))!=std::string::npos;){
                    auto end=descriptor.find(';',at);if(end==std::string::npos)break;
                    const auto name=descriptor.substr(at,end-at+1);
                    if(baseline->find(name)) dependencies.insert(name);at=end+1;
                }
            };
            if(kind=="class") dependencies.insert(sig(original.at("symbols").at(key).string()));
            else if(kind=="descriptor") scan(original.at("symbols").at(key).string());
            else if(spec.contains("owner")) {
                dependencies.insert(sig(original.at("symbols").at(spec.at("owner").string()).string()));
                scan(render(spec.at("descriptor").string(),original.at("symbols")));
            }
            Json::Array evidence;
            for(const auto& name:dependencies) {
                const auto* old=baseline->find(name);if(!old || !classes.contains(old))return "";
                const auto* current=classes.at(old);
                Json::Array refs, methods;for(const auto& ref:live.classReferences(current))refs.emplace_back(ref);
                for(const auto& method:current->methods)methods.emplace_back(Json::Object{
                    {"name",method.json->at("name")},{"descriptor",method.json->at("descriptor")},
                    {"fingerprint",method.code.fingerprint},{"supported",method.code.supported}});
                std::sort(methods.begin(),methods.end(),[](const Json&a,const Json&b){return a.dump()<b.dump();});
                evidence.emplace_back(Json::Object{{"class",stableClass(classMetadata(*current->json),live.snapshot)},
                    {"normalizedMethods",methods},{"references",refs}});
            }
            return sha256(Json(Json::Object{{"mapping",value},{"evidence",evidence}}).dump());
        };
        Json::Object accepted;Json::Array unresolved;Json::Object unresolvedReasons;
        if(stateBound) for(const auto&[key,saved]:state->at("accepted").object()) symbols[key]=saved.at("mapping");
        const auto retainedValidation=stateBound?dictionaryValidation(dict,live,contracts,{}):Json();
        std::map<std::string,bool> retainedValid;
        if(stateBound) for(const auto& e:retainedValidation.at("symbols").array())retainedValid[e.at("symbol").string()]=e.at("accepted").boolean();
        for(const auto&[key,oldValue]:original.at("symbols").object()){
            if(incremental && stateBound && state->at("accepted").contains(key)) {
                const auto& saved=state->at("accepted").at(key);const auto digest=proof(key,saved.at("mapping"));
                const auto oldNames=values(oldValue);
                const bool omission=std::all_of(oldNames.begin(),oldNames.end(),[](const auto& v){return v.empty();});
                if(!digest.empty() && (retainedValid[key] || omission) && saved.at("bindingProof").string()==digest) {
                    accepted[key]=saved;results.push_back(saved);
                    if(events){auto e=saved;e["event"]="symbol-revalidated";e["provisional"]=true;events(e);}
                    continue;
                }
                if(events)events(Json::Object{{"event","symbol-invalidated"},{"symbol",key},{"reason","binding evidence or unique correspondence changed"}});
            }

            if(events)events(Json::Object{{"event","symbol-started"},{"symbol",key},{"dictionary",original.at("id")}});
            const auto&spec=contracts.at("symbols").at(key);const auto kind=spec.at("kind").string();bool ok=bool(baseline);std::string reason="reference snapshot unavailable";Json value=oldValue;
            if(baseline){
                reason="no unique structural match above confidence/margin threshold";
                if(kind=="class") {const auto* old=baseline->find(sig(oldValue.string()));ok=old&&classes.contains(old);if(ok){value=binary(classes.at(old)->json->at("name").string());reason="unique class hierarchy + descriptor/field structure + normalized method fingerprints";}else reason=old&&classFailures.contains(old)?classFailures.at(old):"reference class missing or ambiguous across defining loaders";}
                else if(kind=="descriptor"){value=rewrite(oldValue.string(),ok);if(ok)reason="descriptor derived entirely from verified class correspondences";}
                else if(kind=="reserved"){ok=true;reason="retained explicitly authored reserved value; no runtime consumer";}
                else {
                    const auto& oldSymbols=original.at("symbols");const auto* owner=baseline->find(sig(oldSymbols.at(spec.at("owner").string()).string()));
                    const auto desc=render(spec.at("descriptor").string(),oldSymbols);auto oldNames=values(oldValue);Json::Array replacements;bool any=false,all=true;
                    for(const auto&name:oldNames){
                        if(name.empty()){replacements.emplace_back("");continue;}
                        auto member=baseline->member(owner,kind,name,desc,spec.at("static").boolean());auto declaring=member?baseline->declaring(member,kind):nullptr;
                        std::vector<const Json*> matches;
                        if(declaring&&classes.contains(declaring)){
                            const auto* mapped=classes.at(declaring);const auto oldUsages=baseline->usages(declaring,*member);
                            for(const auto&candidate:mapped->json->at(kind=="field"?"fields":"methods").array()){
                                if(descriptorShape(candidate.at("descriptor").string())!=descriptorShape(desc)||(candidate.at("modifiers").integer()&0x5f8)!=(member->at("modifiers").integer()&0x5f8))continue;
                                bool evidence=false;
                                if(kind=="method"){const auto* a=baseline->method(member);const auto*b=live.method(&candidate);evidence=a&&b&&a->code.supported&&b->code.supported&&(a->code.fingerprint!="no-code"||!oldUsages.empty())&&a->code.fingerprint==b->code.fingerprint;}
                                else evidence=!oldUsages.empty();
                                if(evidence&&oldUsages==live.usages(mapped,candidate))matches.push_back(&candidate);
                            }
                        }else if(declaring&&classFailures.contains(declaring))reason="declaring class rejected: "+classFailures.at(declaring);
                        else if(owner&&classFailures.contains(owner))reason="owner class rejected: "+classFailures.at(owner);
                        if(matches.size()==1){replacements.push_back(matches.front()->at("name"));any=true;}else {replacements.emplace_back("");all=false;}
                    }
                    const bool alternatives=spec.contains("alternatives");ok=alternatives?any:all;
                    if(spec.contains("extent")){value=replacements;}else value=replacements.empty()?Json(""):replacements.front();
                    if(ok)reason="unique owner/hierarchy + descriptor/modifiers + normalized bytecode or field usage + call/reference graph";
                }
            }
            // Empty optional values are intentional authored omissions, not guesses.
            auto oldNames=values(oldValue);if(std::all_of(oldNames.begin(),oldNames.end(),[](const auto&n){return n.empty();})){ok=true;value=oldValue;reason="explicit optional omission retained";}
            if(ok)symbols[key]=value;else complete=false;
            auto e=symbolEvent(key,ok?value:Json(nullptr),ok,reason,0.99);e["threshold"]=0.98;e["margin"]=ok?1.0:0.0;e["nameWeight"]=0.0;if(ok && spec.contains("owner") && baseline){const auto* owner=baseline->find(sig(original.at("symbols").at(spec.at("owner").string()).string()));if(owner && classes.contains(owner))e["runtimeOwner"]=binary(classes.at(owner)->json->at("name").string());}if(incremental) {
                if(ok) {e["bindingProof"]=proof(key,value);e["provisional"]=true;accepted[key]=e;}
                else {
                    unresolved.emplace_back(key);
                    bool missing = !baseline;
                    if (baseline) {
                        const auto ownerKey = kind == "class" ? key : (spec.contains("owner") ? spec.at("owner").string() : "minecraftName");
                        const auto *old = baseline->find(sig(original.at("symbols").at(ownerKey).string()));
                        if (!old) missing = true;
                        else missing = std::none_of(live.classes.begin(), live.classes.end(),
                            [&](const Class &c) { return c.metadataStructure == old->metadataStructure; });
                    }
                    unresolvedReasons[key] = missing ? "missing-runtime-class" : "stable-ambiguity";
                }
            }
            results.push_back(e);if(events){events(e);events(Json::Object{{"event","progress"},{"phase","symbols"},{"completed",int(results.size())},{"total",int(original.at("symbols").object().size())}});}
        }

        if(incremental) {
            bool requiredComplete=true;int requiredMatched=0;
            for(const auto&[key,spec]:contracts.at("symbols").object())if(spec.at("required").boolean()){
                if(!accepted.contains(key))requiredComplete=false;else ++requiredMatched;
            }
            for(const auto& group:contracts.at("requiredAlternatives").array()) {
                bool found=false;for(const auto& key:group.array())found|=accepted.contains(key.string());
                requiredComplete &= found;
            }
            // Unresolved optional symbols are disabled in the draft, never copied as guessed names.
            for(const auto& key:unresolved) {
                const auto& old=original.at("symbols").at(key.string());
                try {Json::Array empty;for(const auto& v:old.array()){(void)v;empty.emplace_back("");}symbols[key.string()]=empty;}
                catch(const std::bad_variant_access&){symbols[key.string()]="";}
            }
            const int score=requiredMatched*10000+int(accepted.size());
            if(score>bestScore) {
                bestScore=score;
                auto draft=retargetPack(selectedPack(pack,p,dict), detectedFamily(pack,live));draft["packVersion"]=pack.at("packVersion").integer()+1;
                best=Json::Object{{"candidateVersion",1},{"stateVersion",1},{"complete",requiredComplete},
                    {"requiredComplete",requiredComplete},{"fingerprint",live.snapshot.at("fingerprint")},
                    {"packDigest",sha256(pack.dump())},{"contractDigest",sha256(contracts.dump())},
                    {"pid",target.at("pid")},{"processStart",target.at("processStart")},
                    {"referenceFingerprint",baseline?baseline->snapshot.at("fingerprint"):Json("")},
                    {"dictionary",original.at("id")},{"accepted",accepted},{"unresolved",unresolved},{"unresolvedReasons",unresolvedReasons},
                    {"symbols",results},{"pack",draft}};
            }
            continue;
        }
        Json attempt=Json::Object{{"dictionary",dict.at("id")},{"symbols",results},{"complete",complete}};
        if(complete){auto candidatePack=selectedPack(pack,p,dict);candidatePack["packVersion"]=pack.at("packVersion").integer()+1;auto validation=validateRuntime(candidatePack,target,contracts,{});bool allMappedValid=validation.at("valid").boolean();
            if(allMappedValid)for(const auto&result:validation.at("symbols").array()){
                const auto authored=values(original.at("symbols").at(result.at("symbol").string()));
                if(!result.at("accepted").boolean()&&std::any_of(authored.begin(),authored.end(),[](const auto&v){return !v.empty();}))allMappedValid=false;
            }
            if(allMappedValid)return Json::Object{{"candidateVersion",1},{"complete",true},{"fingerprint",live.snapshot.at("fingerprint")},{"referenceFingerprint",baseline->snapshot.at("fingerprint")},{"threshold",0.98},{"symbols",results},{"pack",candidatePack},{"validation",validation}};attempt["complete"]=false;attempt["reason"]="post-resolution live validation failed";}
        attempts.push_back(attempt);
    }
    if(incremental) return best;
    return Json::Object{{"candidateVersion",1},{"complete",false},{"fingerprint",live.snapshot.at("fingerprint")},{"threshold",0.98},{"reason",reference?"insufficient or ambiguous structural evidence":"a verified reference snapshot is required; names alone cannot authorize mappings"},{"attempts",attempts}};
}
}
