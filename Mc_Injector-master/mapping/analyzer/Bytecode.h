#pragma once
#include "Analyzer.h"
#include <map>
namespace mcoverlay::mapping {
struct MemberReference {std::string owner,name,descriptor;int opcode=0,position=0;};
struct CodeShape {std::string fingerprint;std::vector<MemberReference> references;bool supported=true;};
std::string descriptorShape(std::string_view descriptor);
CodeShape normalizeBytecode(const Json& klass,const Json& method);
}
