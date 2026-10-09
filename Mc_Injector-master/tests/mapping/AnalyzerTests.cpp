#include "../../mapping/analyzer/Analyzer.h"
#include "../../mapping/analyzer/CaptureClient.h"
#include "../../mapping/InstalledProof.h"
#include <cstdio>
using namespace mcoverlay::mapping;
int main(int argc,char**argv){
    if(argc!=3)return 2;int checks=0,failures=0;
    auto check=[&](bool v){++checks;if(!v)++failures;};
    const auto mixin = [](std::string uuid, bool literal = false, bool merged = true, bool synthetic = true) {
        std::string pool;
        const auto utf8 = [&](const std::string &s) {
            pool += char(1); pool += char(s.size() >> 8); pool += char(s.size()); pool += s;
        };
        utf8(merged ? "Lorg/spongepowered/asm/mixin/transformer/meta/MixinMerged;" : "OtherAnnotation");
        utf8("sessionId"); utf8(uuid);
        const auto name = "md" + uuid.substr(30) + "$lambda$work$0$0"; utf8(name);
        if (literal) { pool += char(8); pool += char(0); pool += char(3); }
        return Json(Json::Object{{"name", "LProof;"}, {"super", Json::Object{{"name", "Ljava/lang/Object;"}}},
            {"interfaces", Json::Array{}}, {"fields", Json::Array{}}, {"modifiers", 1},
            {"constantPoolCount", literal ? 6 : 5}, {"constantPool", QString::fromLatin1(QByteArray(pool.data(), pool.size()).toHex()).toStdString()},
            {"methods", Json::Array{Json::Object{{"name", name}, {"descriptor", "()V"}, {"modifiers", synthetic ? 4098 : 2}, {"bytecode", "b1"}}}}});
    };
    const std::string one = "c7ebddb4-26b1-4e7e-8246-88839602d7c0", two = "a48e2553-3b0b-4512-bebc-643729188523";
    check(installedClassMaterial(mixin(one)) == installedClassMaterial(mixin(two)));
    check(installedHierarchyMaterial(mixin(one)) == installedHierarchyMaterial(mixin(two)));
    check(installedHierarchyMaterial(mixin(one, false, true, false)) != installedHierarchyMaterial(mixin(two, false, true, false)));
    check(installedClassMaterial(mixin(one, true)) != installedClassMaterial(mixin(two, true)));
    check(installedClassMaterial(mixin(one, false, false)) != installedClassMaterial(mixin(two, false, false)));
    check(installedClassMaterial(mixin(one, false, true, false)) != installedClassMaterial(mixin(two, false, true, false)));
    auto changedCode = mixin(one); changedCode["methods"].array()[0]["bytecode"] = "00b1";
    check(installedClassMaterial(changedCode) != installedClassMaterial(mixin(one)));
    auto changedField = mixin(one); changedField["fields"].array().push_back(Json::Object{{"name", "newField"}, {"descriptor", "I"}, {"modifiers", 1}});
    check(installedClassMaterial(changedField) != installedClassMaterial(mixin(one)));
    auto brokenPool = mixin(one); brokenPool["constantPool"] = "01";
    try { installedClassMaterial(brokenPool); check(false); } catch (...) { check(true); }
    auto reordered = mixin(one), reorderedOther = reordered;
    reordered["constantPool"] = "0100014101000142080001080002";
    reorderedOther["constantPool"] = "0100014201000141080001080002";
    check(installedClassMaterial(reordered) == installedClassMaterial(reorderedOther));
    reordered["methods"].array()[0]["bytecode"] = "1203b0";
    reorderedOther["methods"].array()[0]["bytecode"] = "1203b0";
    check(installedClassMaterial(reordered) != installedClassMaterial(reorderedOther));
    reordered["constantPool"] = "010001410100014201000143080001";
    reorderedOther["constantPool"] = "010001410100014301000142080001";
    reordered["methods"].array()[0]["bytecode"] = "1204b0";
    reorderedOther["methods"].array()[0]["bytecode"] = "1204b0";
    check(installedClassMaterial(reordered) == installedClassMaterial(reorderedOther));
    reorderedOther["constantPool"] = "010001420100014301000141080001";
    check(installedClassMaterial(reordered) != installedClassMaterial(reorderedOther));
    check(!installedCodeUsesPool(Json::Object{{"bytecode", "11b200ac"}}));
    check(installedCodeUsesPool(Json::Object{{"bytecode", "b20001ac"}}));
    check(!installedCodeReferences(Json::Object{{"bytecode", "aa000000"}}).has_value());
    const auto raw=readSnapshotFile(filePath(QString::fromLocal8Bit(argv[1])));
    check(inspectSnapshot(raw)==raw);
    auto tampered=raw;tampered["classes"].array()[0]["modifiers"]=999;
    try{inspectSnapshot(tampered);check(false);}catch(...){check(true);}
    tampered=raw;tampered["complete"]=false;try{inspectSnapshot(tampered);check(false);}catch(...){check(true);}
    const auto pack=Json::read(filePath(QString::fromLocal8Bit(argv[2])));
    check(validatePack(pack).at("valid").boolean());check(!validatePack(pack).at("injectionReady").boolean());
    check(!diffPacks(pack,pack).at("changed").boolean());
    auto changed=pack;changed["providers"].array()[0]["dictionaries"].array()[0]["symbols"]["getHealth"]="renamed";
    check(diffPacks(pack,changed).at("changes").array().size()==1);
    changed=pack;changed["packVersion"]=pack.at("packVersion").integer()+1;check(diffPacks(pack,changed).at("changed").boolean());
    std::printf("Analyzer inventory: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
