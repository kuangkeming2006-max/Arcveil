#include "Bytecode.h"
#include <algorithm>
#include <set>
namespace mcoverlay::mapping {
namespace {
std::vector<unsigned char> unhex(std::string_view text){
    if(text.size()%2)throw std::runtime_error("odd bytecode/constant-pool hex");
    auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("invalid bytecode hex");};
    std::vector<unsigned char> out;out.reserve(text.size()/2);for(std::size_t i=0;i<text.size();i+=2)out.push_back(static_cast<unsigned char>(digit(text[i])*16+digit(text[i+1])));return out;
}
struct Reader {const std::vector<unsigned char>& b;std::size_t pos=0;
    unsigned u1(){if(pos>=b.size())throw std::runtime_error("truncated JVM data");return b[pos++];}
    unsigned u2(){auto a=u1();return (a<<8)|u1();}
    std::uint32_t u4(){auto a=u2();return (a<<16)|u2();}
    std::string bytes(std::size_t n){if(n>b.size()-pos)throw std::runtime_error("truncated constant");std::string out(reinterpret_cast<const char*>(b.data()+pos),n);pos+=n;return out;}
};
struct Entry {int tag=0;unsigned a=0,b=0;std::string data;};
struct Pool {
    std::vector<Entry> entries;
    explicit Pool(const Json& c){const auto bytes=unhex(c.at("constantPool").string());Reader r{bytes};int n=c.at("constantPoolCount").integer();if(n<1||n>65535)throw std::runtime_error("constant pool count");entries.resize(std::size_t(n));
        for(int i=1;i<n;++i){auto&e=entries[std::size_t(i)];e.tag=int(r.u1());switch(e.tag){
            case 1:e.data=r.bytes(r.u2());break;
            case 3:case 4:e.data=r.bytes(4);break;
            case 5:case 6:e.data=r.bytes(8);if(++i>=n)throw std::runtime_error("wide constant overflow");break;
            case 7:case 8:case 16:case 19:case 20:e.a=r.u2();break;
            case 9:case 10:case 11:case 12:case 17:case 18:e.a=r.u2();e.b=r.u2();break;
            case 15:e.a=r.u1();e.b=r.u2();break;
            default:throw std::runtime_error("unsupported constant pool tag");
        }}if(r.pos!=bytes.size())throw std::runtime_error("constant pool trailing bytes");
    }
    const Entry& at(unsigned i)const{if(i==0||i>=entries.size()||!entries[i].tag)throw std::runtime_error("invalid constant index");return entries[i];}
    std::string utf(unsigned i)const{const auto&e=at(i);if(e.tag!=1)throw std::runtime_error("expected UTF8 constant");return e.data;}
    std::string className(unsigned i)const{const auto&e=at(i);if(e.tag!=7)throw std::runtime_error("expected class constant");auto s=utf(e.a);return s.starts_with("[")?s:"L"+s+";";}
    MemberReference reference(unsigned i)const{const auto&e=at(i);if(e.tag!=9&&e.tag!=10&&e.tag!=11)throw std::runtime_error("expected member reference");const auto& nt=at(e.b);if(nt.tag!=12)throw std::runtime_error("expected name/type");return {className(e.a),utf(nt.a),utf(nt.b),0,0};}
    std::string shape(unsigned i,int depth=0)const{
        if(depth>8)throw std::runtime_error("constant cycle");const auto&e=at(i);switch(e.tag){
        case 3:case 4:case 5:case 6:return std::to_string(e.tag)+":"+sha256(e.data);
        case 7:return "class:"+descriptorShape(className(i));
        case 8:return "string:"+sha256(utf(e.a));
        case 9:case 10:case 11:{const auto ref=reference(i);return std::to_string(e.tag)+":"+descriptorShape(ref.owner)+":"+descriptorShape(ref.descriptor);}
        case 15:return "handle:"+std::to_string(e.a)+":"+shape(e.b,depth+1);
        case 16:return "methodtype:"+descriptorShape(utf(e.a));
        case 17:case 18:throw std::runtime_error("bootstrap attributes unavailable for dynamic constant");
        default:throw std::runtime_error("unsupported bytecode constant");}
    }
};
}
std::string descriptorShape(std::string_view desc){
    std::string out;for(std::size_t i=0;i<desc.size();++i){if(desc[i]=='L'){auto end=desc.find(';',i);if(end==std::string_view::npos)throw std::runtime_error("malformed descriptor");auto name=desc.substr(i,end-i+1);out+=name.starts_with("Ljava/")||name.starts_with("Ljavax/")?std::string(name):"L*;";i=end;}else out+=desc[i];}return out;
}
CodeShape normalizeBytecode(const Json& klass,const Json& method){
    CodeShape shape;const auto bytes=unhex(method.at("bytecode").string());
    if(bytes.empty()){shape.fingerprint="no-code";return shape;}
    Pool pool(klass);Reader r{bytes};Json::Array tokens;std::map<int,int> offsets;std::vector<std::pair<std::size_t,int>> branches;
    while(r.pos<bytes.size()){
        const int start=int(r.pos);offsets[start]=int(tokens.size());unsigned op=r.u1();Json token=Json::Object{{"op",int(op)}};
        if(op==0x12||op==0x13||op==0x14||(op>=0xb2&&op<=0xb9)||op==0xba||op==0xbb||op==0xbd||op==0xc0||op==0xc1||op==0xc5){
            unsigned index=op==0x12?r.u1():r.u2();
            if(op==0xba||pool.at(index).tag==17){shape.supported=false;return shape;}
            token["constant"]=pool.shape(index);if(op==0x13)token["op"]=0x12;
            if(op>=0xb2&&op<=0xb9){auto ref=pool.reference(index);ref.opcode=int(op);ref.position=int(tokens.size());shape.references.push_back(std::move(ref));}
            if(op==0xb9){token["count"]=int(r.u1());if(r.u1()!=0)throw std::runtime_error("invalid invokeinterface");}
            if(op==0xc5)token["dimensions"]=int(r.u1());
        }else if((op>=0x99&&op<=0xa8)||op==0xc6||op==0xc7||op==0xc8||op==0xc9){
            int delta=op==0xc8||op==0xc9?static_cast<std::int32_t>(r.u4()):static_cast<std::int16_t>(r.u2());
            branches.emplace_back(tokens.size(),start+delta);
        }else if(op==0xaa||op==0xab){
            while(r.pos%4)r.u1();Json::Array destinations,keys;destinations.emplace_back(start+static_cast<std::int32_t>(r.u4()));
            if(op==0xaa){auto low=static_cast<std::int32_t>(r.u4()),high=static_cast<std::int32_t>(r.u4());if(high<low||std::int64_t(high)-low>65535)throw std::runtime_error("invalid tableswitch");for(std::int64_t k=low;k<=high;++k){keys.emplace_back(int(k));destinations.emplace_back(start+static_cast<std::int32_t>(r.u4()));}}
            else {auto n=static_cast<std::int32_t>(r.u4());if(n<0||n>65535)throw std::runtime_error("invalid lookupswitch");for(int k=0;k<n;++k){keys.emplace_back(static_cast<std::int32_t>(r.u4()));destinations.emplace_back(start+static_cast<std::int32_t>(r.u4()));}}
            token["keys"]=keys;token["targets"]=destinations;
        }else if(op==0xc4){auto widened=r.u1();if(widened!=0x84&&widened!=0xa9&&!(widened>=0x15&&widened<=0x19)&&!(widened>=0x36&&widened<=0x3a))throw std::runtime_error("invalid wide opcode");token["wide"]=int(widened);token["local"]=int(r.u2());if(widened==0x84)token["increment"]=int(r.u2());
        }else if(op==0x11||op==0x84){token["operand"]=int(r.u2());}
        else if(op==0x10||(op>=0x15&&op<=0x19)||(op>=0x36&&op<=0x3a)||op==0xa9||op==0xbc){token["operand"]=int(r.u1());}
        else if(op>0xc9)throw std::runtime_error("reserved bytecode opcode");
        tokens.push_back(std::move(token));
    }
    for(const auto&[i,target]:branches){if(!offsets.contains(target))throw std::runtime_error("invalid branch boundary");tokens[i]["target"]=offsets.at(target);}
    for(auto&t:tokens)if(t.contains("targets"))for(auto&target:t["targets"].array()){if(!offsets.contains(target.integer()))throw std::runtime_error("invalid switch boundary");target=offsets.at(target.integer());}
    shape.fingerprint=sha256(Json(tokens).dump());return shape;
}
}
