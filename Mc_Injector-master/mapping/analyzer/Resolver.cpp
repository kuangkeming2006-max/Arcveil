#include "Resolver.h"
#include "../../agent/bindings/MappingPack.h"
#include <algorithm>
#include <set>
#include <sstream>
namespace mcoverlay::mapping {
namespace {
std::string sig(std::string value){std::replace(value.begin(),value.end(),'.','/');return value.empty()?"":"L"+value+";";}
std::string binary(std::string value){if(value.size()<3||value.front()!='L'||value.back()!=';')throw std::runtime_error("not a class signature");value=value.substr(1,value.size()-2);std::replace(value.begin(),value.end(),'/','.');return value;}
std::string render(const std::string& expression,const Json& symbols){std::string out;for(std::size_t i=0;i<expression.size();++i){if(expression[i]=='{'){auto end=expression.find('}',i);if(end==std::string::npos)throw std::runtime_error("invalid descriptor template");out+=symbols.at(expression.substr(i+1,end-i-1)).string();i=end;}else out+=expression[i];}return out;}
std::vector<std::string> values(const Json& value){try{return {value.string()};}catch(const std::bad_variant_access&){std::vector<std::string> out;for(const auto&v:value.array())out.push_back(v.string());return out;}}
void contractCheck(const Json& contracts,const Json& symbols){if(contracts.at("contractVersion").integer()!=1||contracts.at("symbols").object().size()!=symbols.object().size())throw std::runtime_error("logical contract/schema mismatch");for(const auto&[key,v]:symbols.object())(void)contracts.at("symbols").at(key);}
struct Method {const Json* json=nullptr;CodeShape code;};
struct Class {const Json* json=nullptr;std::vector<Method> methods;std::string structure;bool supported=true;};
struct Model {
    Json snapshot;std::vector<Class> classes;
    mutable std::map<const Json*,std::vector<std::string>> usageIndex;mutable bool usagesReady=false;
    std::map<std::string,std::vector<const Class*>> nameIndex;
    explicit Model(const Json& input,bool code=false):snapshot(inspectSnapshot(input)){
        for(const auto& c:snapshot.at("classes").array()){
            Class klass;klass.json=&c;Json::Array fs,ms;
            for(const auto& f:c.at("fields").array())fs.emplace_back(std::to_string(f.at("modifiers").integer()&0xd8)+descriptorShape(f.at("descriptor").string()));
            for(const auto& m:c.at("methods").array()){
                Method method{&m,{}};if(code)method.code=normalizeBytecode(c,m);klass.supported &= method.code.supported;
                ms.emplace_back(std::to_string(m.at("modifiers").integer()&0x5f8)+descriptorShape(m.at("descriptor").string())+":"+(code?(method.code.supported?method.code.fingerprint:"unsupported"):""));klass.methods.push_back(std::move(method));
            }
            auto order=[](const Json&a,const Json&b){return a.string()<b.string();};std::sort(fs.begin(),fs.end(),order);std::sort(ms.begin(),ms.end(),order);
            Json::Array interfaces;for(const auto&i:c.at("interfaces").array())interfaces.emplace_back(descriptorShape(i.at("name").string()));std::sort(interfaces.begin(),interfaces.end(),order);
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
Json symbolEvent(const std::string& key,const Json& value,bool ok,const std::string& reason,double confidence=1.0){return Json::Object{{"event","symbol"},{"symbol",key},{"mapping",value},{"confidence",ok?confidence:0.0},{"accepted",ok},{"evidence",Json::Array{reason}},{"reason",ok?"":reason}};}
Json dictionaryValidation(const Json& dictionary,const Model& model,const Json& contracts,const Events& events){
    const auto& symbols=dictionary.at("symbols");contractCheck(contracts,symbols);
    const auto* anchor=model.find(sig(symbols.at("minecraftName").string()));
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
        auto e=symbolEvent(key,value,ok,reason);e["runtimeMapping"]=runtimeValue;results.push_back(e);if(events)events(e);
    }
    for(const auto&group:contracts.at("requiredAlternatives").array()){bool found=false;for(const auto&key:group.array())found|=matched.at(key.string());if(!found)valid=false;}
    for(const auto& ctor:contracts.at("constructors").array())if(ctor.at("required").boolean()){
        const auto* owner=anchor?model.find(sig(symbols.at(ctor.at("owner").string()).string()),loader):nullptr;
        if(!model.member(owner,"method","<init>",render(ctor.at("descriptor").string(),symbols),false))valid=false;
    }
    return Json::Object{{"valid",valid},{"dictionary",dictionary.at("id")},{"loaderKey",loader},{"symbols",results}};
}
Json selectedPack(const Json& pack,const Json& provider,const Json& dictionary){auto result=pack;auto p=provider;p["dictionaries"]=Json::Array{dictionary};result["providers"]=Json::Array{p};return result;}
std::string detectedFamily(const Json& pack,const Model& model){
    bindings::ClientEnvironment environment;
    const auto parsed=bindings::parseMappingPack(pack);
    if(model.snapshot.contains("launchEvidence"))for(const auto& evidence:model.snapshot.at("launchEvidence").array())for(const auto&p:parsed.providers)if(evidence.at("family").string()==bindings::clientFamilyName(p.family))environment.addEvidence(p.family,static_cast<std::uint16_t>(evidence.at("confidence").integer()));
    for(const auto&p:parsed.providers)for(const auto&d:p.dictionaries)for(const auto&pattern:d.detection)for(const auto&c:model.classes){const auto&name=c.json->at("name").string();if((pattern.match==bindings::DetectionMatch::ExactClassSignature&&name==pattern.value)||(pattern.match==bindings::DetectionMatch::ClassSignaturePrefix&&name.starts_with(pattern.value)))environment.addEvidence(d.family,pattern.confidence);}
    return bindings::clientFamilyName(environment.family());
}
}
Json selectDetailCandidates(const Json &pack, const Json &lite, const Json *reference,
                            const Json &contracts, const Events &events) {
    (void)bindings::parseMappingPack(pack);
    Model live(lite);
    if (!lite.contains("detailLevel") || lite.at("detailLevel").string() != "lite")
        throw std::runtime_error("candidate selection requires lite index");
    std::set<const Class *> selected;
    std::map<const Class *, std::string> reasons;
    // Authored exact symbols are scope requests, never automatically accepted mappings.
    for (const auto &provider : pack.at("providers").array())
        for (const auto &dict : provider.at("dictionaries").array()) {
            const auto &symbols = dict.at("symbols");
            contractCheck(contracts, symbols);
            const auto *anchor = live.find(sig(symbols.at("minecraftName").string()));
            if (!anchor)
                continue;
            for (const auto &[key, spec] : contracts.at("symbols").object())
                if (spec.at("kind").string() == "class") {
                    if (const auto *c = live.find(sig(symbols.at(key).string()),
                                                  anchor->json->at("loaderKey").string())) {
                        selected.insert(c);
                        reasons[c] = "authored pack scope; requires independent live validation";
                    }
                }
        }
    if (reference) {
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
    if (selected.empty())
        throw std::runtime_error("no detail candidates: authored anchors unavailable and no "
                                 "compatible verified structural reference");
    std::map<std::string, Json> loaders;
    for (const auto &item : lite.at("loaderInstances").array())
        loaders[item.at("loaderKey").string()] = item;
    Json::Array classes;
    for (const auto *c : selected) {
        const auto &value = *c->json;
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
    if (events)
        events(Json::Object{{"event", "selection"},
                            {"totalClasses", double(lite.at("classes").array().size())},
                            {"candidateClasses", double(classes.size())},
                            {"referenceAvailable", reference != nullptr}});
    return Json::Object{{"candidateVersion", 1},
                        {"pid", lite.at("pid")},
                        {"processStart", lite.at("processStart")},
                        {"liteFingerprint", lite.at("fingerprint")},
                        {"classes", classes},
                        {"loaderBindings", lite.at("loaderInstances")},
                        {"launchEvidence", lite.contains("launchEvidence")
                                               ? lite.at("launchEvidence")
                                               : Json(Json::Array{})}};
}
Json validateRuntime(const Json& pack,const Json& snapshot,const Json& contracts,const Events& events){
    if(snapshot.contains("detailLevel")&&snapshot.at("detailLevel").string()=="lite")throw std::runtime_error("lite metadata is diagnostic only; inspect-detail validation is required before mapping/injection");
    (void)bindings::parseMappingPack(pack);Model model(snapshot);Json::Array attempts;
    const auto family=detectedFamily(pack,model);auto providers=pack.at("providers").array();std::stable_sort(providers.begin(),providers.end(),[](const Json&a,const Json&b){return a.at("priority").integer()>b.at("priority").integer();});
    for(const auto&p:providers)for(const auto&d:p.at("dictionaries").array()){
        if(family!="Unknown"&&d.at("family").string()!=family)continue;
        auto result=dictionaryValidation(d,model,contracts,{});attempts.push_back(result);
        if(result.at("valid").boolean()){
            if(events)for(const auto&e:result.at("symbols").array())events(e);
            result["injectionReady"]=true;result["level"]="live-members";result["fingerprint"]=model.snapshot.at("fingerprint");result["pack"]=selectedPack(pack,p,d);result["packDigest"]=sha256(result.at("pack").dump());return result;
        }
    }
    return Json::Object{{"valid",false},{"injectionReady",false},{"level","live-members"},{"reason","no family-compatible dictionary passed required live bindings"},{"attempts",attempts},{"fingerprint",model.snapshot.at("fingerprint")}};
}
Json resolveMappings(const Json& pack,const Json* reference,const Json& target,const Json& contracts,const Events& events){
    if((target.contains("detailLevel")&&target.at("detailLevel").string()=="lite")||(reference&&reference->contains("detailLevel")&&reference->at("detailLevel").string()=="lite"))throw std::runtime_error("lite metadata cannot enter normalized bytecode/call-graph matching; inspect-detail required");
    (void)bindings::parseMappingPack(pack);Model live(target,true);Json::Array attempts;
    std::unique_ptr<Model> baseline;if(reference)baseline=std::make_unique<Model>(*reference,true);
    for(const auto&p:pack.at("providers").array())for(const auto&original:p.at("dictionaries").array()){
        auto dict=original;auto& symbols=dict["symbols"];contractCheck(contracts,symbols);
        bool complete=bool(baseline);Json::Array results;std::map<const Class*,const Class*> classes;std::map<std::string,std::string> names;
        if(baseline){
            std::map<std::string,std::vector<const Class*>> structures;
            for(const auto&c:live.classes)if(c.supported)structures[c.structure].push_back(&c);
            for(const auto& c:baseline->classes){
                if(!c.supported)continue;
                const auto& matches=structures[c.structure];std::vector<const Class*> eligible;
                const auto references=baseline->classReferences(&c);
                const bool hasBody=std::any_of(c.methods.begin(),c.methods.end(),[](const Method&m){return m.json->at("name").string()!="<init>"&&m.json->at("name").string()!="<clinit>"&&m.json->at("bytecode").string().size()>8;});
                if(!hasBody&&references.size()<2)continue;
                for(const auto* candidate:matches)if(references==live.classReferences(candidate))eligible.push_back(candidate);
                if(eligible.size()==1)classes[&c]=eligible.front();
            }
            // Reject all sides of a collision before modifying the map.
            bool changed=true;
            while(changed){std::set<const Class*> rejected;
                for(const auto&[from,to]:classes){
                    if(baseline->find(from->json->at("name").string())!=from||live.find(to->json->at("name").string())!=to)rejected.insert(from);
                    for(const auto&entry:classes)if(entry.first!=from&&entry.second==to)rejected.insert(from);
                    const auto* oldSuper=baseline->related(from->json->at("super"));const auto* newSuper=live.related(to->json->at("super"));
                    if(oldSuper&&(!classes.contains(oldSuper)||classes.at(oldSuper)!=newSuper))rejected.insert(from);
                    for(const auto&iface:from->json->at("interfaces").array())if(const auto* old=baseline->related(iface)){
                        bool found=false;if(classes.contains(old))for(const auto&targetIface:to->json->at("interfaces").array())found|=live.related(targetIface)==classes.at(old);
                        if(!found)rejected.insert(from);
                    }
                }
                changed=!rejected.empty();for(auto* key:rejected)classes.erase(key);
            }
            for(const auto&[from,to]:classes)names[from->json->at("name").string()]=to->json->at("name").string();
        }
        const auto rewrite=[&](std::string descriptor,bool& ok){std::string out;for(std::size_t i=0;i<descriptor.size();++i){if(descriptor[i]=='L'){auto end=descriptor.find(';',i);if(end==std::string::npos){ok=false;return descriptor;}auto name=descriptor.substr(i,end-i+1);if(names.contains(name))out+=names.at(name);else {out+=name;if(!name.starts_with("Ljava/")&&!name.starts_with("Ljavax/")&&!name.starts_with("Lcom/mojang/")&&!name.starts_with("Lorg/lwjgl/"))ok=false;}i=end;}else out+=descriptor[i];}return out;};
        for(const auto&[key,oldValue]:original.at("symbols").object()){
            if(events)events(Json::Object{{"event","symbol-started"},{"symbol",key},{"dictionary",original.at("id")}});
            const auto&spec=contracts.at("symbols").at(key);const auto kind=spec.at("kind").string();bool ok=bool(baseline);std::string reason="reference snapshot unavailable";Json value=oldValue;
            if(baseline){
                reason="no unique structural match above confidence/margin threshold";
                if(kind=="class") {const auto* old=baseline->find(sig(oldValue.string()));ok=old&&classes.contains(old);if(ok){value=binary(classes.at(old)->json->at("name").string());reason="unique class hierarchy + descriptor/field structure + normalized method fingerprints";}}
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
                        }
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
            auto e=symbolEvent(key,ok?value:Json(nullptr),ok,reason,0.99);e["threshold"]=0.98;e["margin"]=ok?1.0:0.0;e["nameWeight"]=0.0;results.push_back(e);if(events){events(e);events(Json::Object{{"event","progress"},{"phase","symbols"},{"completed",int(results.size())},{"total",int(original.at("symbols").object().size())}});}
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
    return Json::Object{{"candidateVersion",1},{"complete",false},{"fingerprint",live.snapshot.at("fingerprint")},{"threshold",0.98},{"reason",reference?"insufficient or ambiguous structural evidence":"a verified reference snapshot is required; names alone cannot authorize mappings"},{"attempts",attempts}};
}
}
