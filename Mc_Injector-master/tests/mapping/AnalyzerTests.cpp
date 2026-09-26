#include "../../mapping/analyzer/Analyzer.h"
#include <cstdio>
using namespace mcoverlay::mapping;
int main(int argc,char**argv){
    if(argc!=3)return 2;int checks=0,failures=0;
    auto check=[&](bool v){++checks;if(!v)++failures;};
    const auto raw=Json::read(filePath(QString::fromLocal8Bit(argv[1])),64U*1024U*1024U);
    check(inspectSnapshot(raw)==raw);
    auto tampered=raw;tampered["classes"].array()[0]["modifiers"]=999;
    try{inspectSnapshot(tampered);check(false);}catch(...){check(true);}
    tampered=raw;tampered["complete"]=false;try{inspectSnapshot(tampered);check(false);}catch(...){check(true);}
    const auto pack=Json::read(filePath(QString::fromLocal8Bit(argv[2])));
    check(validatePack(pack).at("valid").boolean());check(!validatePack(pack).at("injectionReady").boolean());
    check(!diffPacks(pack,pack).at("changed").boolean());
    auto changed=pack;changed["providers"].array()[0]["dictionaries"].array()[0]["symbols"]["getHealth"]="renamed";
    check(diffPacks(pack,changed).at("changes").array().size()==1);
    changed=pack;changed["packVersion"]=2;check(diffPacks(pack,changed).at("changed").boolean());
    std::printf("Analyzer inventory: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
