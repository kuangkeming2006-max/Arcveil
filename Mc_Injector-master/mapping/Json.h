#pragma once
// Bounded JSON interchange shared by the non-Qt Agent and offline tools.
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mcoverlay::mapping {
class Json final {
public:
    using Array=std::vector<Json>;
    using Object=std::map<std::string,Json,std::less<>>;
    using Value=std::variant<std::nullptr_t,bool,double,std::string,Array,Object>;
    Json():m_value(nullptr){}
    Json(std::nullptr_t):m_value(nullptr){}
    Json(bool v):m_value(v){}
    Json(double v):m_value(v){if(!std::isfinite(v))throw std::runtime_error("nonfinite JSON number");}
    Json(int v):m_value(double(v)){}
    Json(std::string v):m_value(std::move(v)){}
    Json(const char* v):m_value(std::string(v)){}
    Json(Array v):m_value(std::move(v)){}
    Json(Object v):m_value(std::move(v)){}
    const std::string& string()const{return std::get<std::string>(m_value);}
    double number()const{return std::get<double>(m_value);}
    int integer()const {const auto n=number();if(n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max()||std::trunc(n)!=n)throw std::runtime_error("expected bounded integer");return int(n);}
    bool boolean()const{return std::get<bool>(m_value);}
    const Array& array()const{return std::get<Array>(m_value);}
    Array& array(){return std::get<Array>(m_value);}
    const Object& object()const{return std::get<Object>(m_value);}
    Object& object(){return std::get<Object>(m_value);}
    bool contains(std::string_view key)const{return object().contains(key);}
    const Json& at(std::string_view key)const {const auto& o=object();auto i=o.find(key);if(i==o.end())throw std::runtime_error("missing JSON key: "+std::string(key));return i->second;}
    Json& operator[](std::string key){if(std::holds_alternative<std::nullptr_t>(m_value))m_value=Object{};return object()[std::move(key)];}
    bool operator==(const Json&)const=default;
    std::string dump()const {std::string out;append(out);return out;}
    static Json parse(std::string_view text){if(text.size()>64U*1024U*1024U)throw std::runtime_error("JSON size limit");Parser p{text};auto v=p.value(0);p.space();if(p.pos!=text.size())p.fail("trailing JSON input");return v;}
    static Json read(const std::filesystem::path& path,std::size_t maxBytes=2U*1024U*1024U){
        std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("cannot open JSON file");
        file.seekg(0,std::ios::end);const auto length=file.tellg();
        if(length<0||static_cast<std::uint64_t>(length)>maxBytes)throw std::runtime_error("JSON file size limit");
        std::string text(static_cast<std::size_t>(length),'\0');file.seekg(0);
        if(!file.read(text.data(),static_cast<std::streamsize>(text.size())))throw std::runtime_error("short JSON read");
        return parse(text);
    }
private:
    Value m_value;
    static void quote(std::string& out,std::string_view value){
        out+='"';constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}out+='"';
    }
    void append(std::string& out)const {
        if(std::holds_alternative<std::nullptr_t>(m_value)){out+="null";return;}
        if(auto v=std::get_if<bool>(&m_value)){out+=*v?"true":"false";return;}
        if(auto v=std::get_if<double>(&m_value)){char buf[64];auto r=std::to_chars(buf,buf+sizeof(buf),*v);if(r.ec!=std::errc{})throw std::runtime_error("JSON number encoding");out.append(buf,r.ptr);return;}
        if(auto v=std::get_if<std::string>(&m_value)){quote(out,*v);return;}
        if(auto v=std::get_if<Array>(&m_value)){out+='[';bool first=true;for(const auto& x:*v){if(!first)out+=',';first=false;x.append(out);}out+=']';return;}
        out+='{';bool first=true;for(const auto& [k,v]:object()){if(!first)out+=',';first=false;quote(out,k);out+=':';v.append(out);}out+='}';
    }
    struct Parser {
        std::string_view text;std::size_t pos=0,nodes=0;
        [[noreturn]] void fail(const char* why)const {throw std::runtime_error(std::string(why)+" at byte "+std::to_string(pos));}
        void space(){while(pos<text.size()&&(text[pos]==' '||text[pos]=='\t'||text[pos]=='\r'||text[pos]=='\n'))++pos;}
        bool take(char c){space();if(pos<text.size()&&text[pos]==c){++pos;return true;}return false;}
        void need(char c){if(!take(c))fail("unexpected JSON token");}
        unsigned hex4(){unsigned n=0;for(int i=0;i<4;++i){if(pos>=text.size())fail("short Unicode escape");char c=text[pos++];unsigned v=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):c>='A'&&c<='F'?unsigned(c-'A'+10):99;if(v>15)fail("invalid Unicode escape");n=n*16+v;}return n;}
        static void utf8(std::string& s,unsigned cp){if(cp<0x80)s+=char(cp);else if(cp<0x800){s+=char(0xc0|(cp>>6));s+=char(0x80|(cp&63));}else if(cp<0x10000){s+=char(0xe0|(cp>>12));s+=char(0x80|((cp>>6)&63));s+=char(0x80|(cp&63));}else{s+=char(0xf0|(cp>>18));s+=char(0x80|((cp>>12)&63));s+=char(0x80|((cp>>6)&63));s+=char(0x80|(cp&63));}}
        std::string string(){need('"');std::string out;while(pos<text.size()){
            const auto c=static_cast<unsigned char>(text[pos++]);if(c=='"')return out;if(c<32)fail("unescaped control character");
            if(c>=0x80){unsigned cp=0;int following=0;unsigned minimum=0;
                if(c>=0xc2&&c<=0xdf){cp=c&31;following=1;minimum=0x80;}else if(c>=0xe0&&c<=0xef){cp=c&15;following=2;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){cp=c&7;following=3;minimum=0x10000;}else fail("invalid UTF-8");
                for(int i=0;i<following;++i){if(pos>=text.size())fail("short UTF-8");auto b=static_cast<unsigned char>(text[pos++]);if((b&0xc0)!=0x80)fail("invalid UTF-8 continuation");cp=(cp<<6)|(b&63);}if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))fail("invalid Unicode scalar");utf8(out,cp);continue;}
            if(c!='\\'){out+=char(c);continue;}if(pos>=text.size())fail("short escape");
            switch(text[pos++]){case '"':out+='"';break;case '\\':out+='\\';break;case '/':out+='/';break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{unsigned cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(pos+2>text.size()||text[pos++]!='\\'||text[pos++]!='u')fail("missing low surrogate");unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail("invalid low surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)fail("orphan low surrogate");utf8(out,cp);break;}default:fail("invalid escape");}
        }fail("unterminated string");}
        Json value(unsigned depth){if(depth>48||++nodes>2000000)fail("JSON complexity limit");space();if(pos>=text.size())fail("missing value");char c=text[pos];
            if(c=='"')return Json(string());
            if(c=='{'){++pos;Object o;if(take('}'))return o;do{auto key=string();need(':');auto v=value(depth+1);if(!o.emplace(std::move(key),std::move(v)).second)fail("duplicate object key");}while(take(','));need('}');return o;}
            if(c=='['){++pos;Array a;if(take(']'))return a;do{a.push_back(value(depth+1));}while(take(','));need(']');return a;}
            if(text.substr(pos,4)=="null"){pos+=4;return Json();}if(text.substr(pos,4)=="true"){pos+=4;return Json(true);}if(text.substr(pos,5)=="false"){pos+=5;return Json(false);}
            const auto start=pos;if(c=='-')++pos;
            if(pos>=text.size())fail("invalid number");
            if(text[pos]=='0')++pos;else{if(text[pos]<'1'||text[pos]>'9')fail("invalid number");while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9')++pos;}
            if(pos<text.size()&&text[pos]=='.'){++pos;const auto before=pos;while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9')++pos;if(pos==before)fail("missing fractional digits");}
            if(pos<text.size()&&(text[pos]=='e'||text[pos]=='E')){++pos;if(pos<text.size()&&(text[pos]=='+'||text[pos]=='-'))++pos;const auto before=pos;while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9')++pos;if(pos==before)fail("missing exponent digits");}
            double n=0;auto r=std::from_chars(text.data()+start,text.data()+pos,n);if(r.ec!=std::errc{}||r.ptr!=text.data()+pos||!std::isfinite(n))fail("invalid numeric range");return Json(n);
        }
    };
};
}
