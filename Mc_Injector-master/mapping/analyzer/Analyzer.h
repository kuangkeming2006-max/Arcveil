#pragma once
#include "../Json.h"
#include <QString>
#include <functional>
namespace mcoverlay::mapping {
using Events=std::function<void(const Json&)>;
std::filesystem::path filePath(const QString& path);
QString qtPath(const std::filesystem::path& path);
void writeJson(const std::filesystem::path& path,const Json& value);
std::string sha256(std::string_view value);
Json inspectSnapshot(Json snapshot);
Json validatePack(const Json& pack);
Json diffPacks(const Json& before,const Json& after);
}
