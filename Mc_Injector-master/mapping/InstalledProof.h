#pragma once
#include "Json.h"
#include <algorithm>
#include <set>
#include <functional>
#include <optional>
namespace mcoverlay::mapping {
inline std::string installedHierarchyMaterial(const Json &klass) {
    auto fields = klass.at("fields").array(), methods = klass.at("methods").array();
    for (auto &m : methods) {
        m.object().erase("bytecode");
        auto name = m.at("name").string();
        if ((m.at("modifiers").integer() & 0x1000) && name.size() > 16 && name.starts_with("md") &&
            name.substr(8, 8) == "$lambda$" && std::all_of(name.begin() + 2, name.begin() + 8,
                [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
            name.replace(2, 6, "000000"); m["name"] = name;
        }
    }
    const auto order = [](const Json &a, const Json &b) { return a.dump() < b.dump(); };
    std::sort(fields.begin(), fields.end(), order); std::sort(methods.begin(), methods.end(), order);
    Json::Array interfaces;
    for (const auto &i : klass.at("interfaces").array()) interfaces.push_back(i.at("name"));
    std::sort(interfaces.begin(), interfaces.end(), order);
    return Json(Json::Object{{"proofVersion", 2}, {"name", klass.at("name")}, {"super", klass.at("super").at("name")},
        {"interfaces", interfaces}, {"modifiers", klass.at("modifiers")}, {"fields", fields}, {"methods", methods}}).dump();
}
inline bool mixinUuid(const std::string &s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (s[i] != '-') return false; }
        else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}
struct InstalledPool {
    std::string hex, session;
};
inline std::optional<std::set<unsigned>> installedCodeReferences(const Json &method) {
    const auto &hex = method.at("bytecode").string();
    std::vector<unsigned char> code;
    for (std::size_t p = 0; p < hex.size(); p += 2) {
        unsigned b = 0; if (p + 2 > hex.size()) return std::nullopt;
        auto parsed = std::from_chars(hex.data() + p, hex.data() + p + 2, b, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + p + 2) return std::nullopt;
        code.push_back(static_cast<unsigned char>(b));
    }
    std::size_t p = 0; std::set<unsigned> references;
    const auto u4 = [&]() -> std::int64_t {
        if (code.size() - p < 4) throw std::runtime_error("truncated switch");
        std::uint32_t value = 0; for (int i = 0; i < 4; ++i) value = (value << 8) | code[p++];
        return static_cast<std::int32_t>(value);
    };
    try { while (p < code.size()) {
        const auto op = code[p++]; std::size_t operands = 0;
        if (op == 0x12 || op == 0x13 || op == 0x14 || (op >= 0xb2 && op <= 0xba) ||
            op == 0xbb || op == 0xbd || op == 0xc0 || op == 0xc1 || op == 0xc5) {
            const auto width = op == 0x12 ? 1U : 2U;
            if (code.size() - p < width) return std::nullopt;
            references.insert(width == 1 ? unsigned(code[p]) : (unsigned(code[p]) << 8) | code[p + 1]);
            operands = op == 0xb9 || op == 0xba ? 4 : op == 0xc5 ? 3 : width;
        } else if (op == 0xaa || op == 0xab) {
            while (p % 4) { if (p == code.size()) return std::nullopt; ++p; }
            (void)u4();
            if (op == 0xaa) { const auto low = u4(), high = u4(); if (high < low || high - low > 65535) return std::nullopt; operands = std::size_t(high - low + 1) * 4; }
            else { const auto pairs = u4(); if (pairs < 0 || pairs > 65535) return std::nullopt; operands = std::size_t(pairs) * 8; }
        } else if (op == 0xc4) {
            if (p == code.size()) return std::nullopt; const auto widened = code[p++];
            if (widened != 0x84 && widened != 0xa9 && !(widened >= 0x15 && widened <= 0x19) && !(widened >= 0x36 && widened <= 0x3a)) return std::nullopt;
            operands = widened == 0x84 ? 4 : 2;
        } else if (op == 0xc8 || op == 0xc9) operands = 4;
        else if (op == 0x11 || op == 0x84 || (op >= 0x99 && op <= 0xa8) || op == 0xc6 || op == 0xc7) operands = 2;
        else if (op == 0x10 || (op >= 0x15 && op <= 0x19) || (op >= 0x36 && op <= 0x3a) || op == 0xa9 || op == 0xbc) operands = 1;
        else if (op > 0xc9) return std::nullopt;
        if (operands > code.size() - p) return std::nullopt;
        p += operands;
    } } catch (...) { return std::nullopt; }
    return references;
}
inline bool installedCodeUsesPool(const Json &method) {
    const auto references = installedCodeReferences(method);
    return !references || !references->empty();
}
inline InstalledPool installedPool(const Json &klass) {
    auto hex = klass.at("constantPool").string();
    std::vector<unsigned char> bytes;
    for (std::size_t p = 0; p < hex.size(); p += 2) {
        unsigned value = 0;
        if (p + 2 > hex.size()) throw std::runtime_error("invalid installed pool hex");
        auto parsed = std::from_chars(hex.data() + p, hex.data() + p + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + p + 2) throw std::runtime_error("invalid installed pool hex");
        bytes.push_back(static_cast<unsigned char>(value));
    }
    struct Utf8 { unsigned index; std::size_t offset; std::string value; };
    std::vector<Utf8> strings;
    std::set<unsigned> literalStrings;
    const auto count = unsigned(klass.at("constantPoolCount").integer());
    if (count > 65536) throw std::runtime_error("installed pool count limit");
    std::vector<Json> entries(count);
    std::size_t p = 0;
    auto bound = [&](std::size_t n) { if (n > bytes.size() - p) throw std::runtime_error("truncated installed pool"); };
    for (unsigned i = 1; i < count; ++i) {
        bound(1); const auto tag = bytes[p++];
        const auto index = i;
        entries[index] = Json::Object{{"tag", int(tag)}, {"references", Json::Array{}}, {"value", ""}};
        if (tag == 1) {
            bound(2); const auto length = (unsigned(bytes[p]) << 8) | bytes[p + 1]; p += 2; bound(length);
            strings.push_back({i, p, std::string(reinterpret_cast<const char *>(bytes.data() + p), length)}); p += length;
        } else {
            unsigned length = 0;
            switch (tag) {
            case 3: case 4: case 9: case 10: case 11: case 12: case 17: case 18: length = 4; break;
            case 5: case 6: length = 8; ++i; break;
            case 7: case 8: case 16: case 19: case 20: length = 2; break;
            case 15: length = 3; break;
            default: throw std::runtime_error("unsupported installed pool tag");
            }
            bound(length);
            if (tag == 8) literalStrings.insert((unsigned(bytes[p]) << 8) | bytes[p + 1]);
            auto &refs = entries[index]["references"].array();
            const auto ref = [&](unsigned at) { refs.push_back(int((unsigned(bytes[p + at]) << 8) | bytes[p + at + 1])); };
            if (tag == 7 || tag == 8 || tag == 16 || tag == 19 || tag == 20) ref(0);
            else if (tag == 9 || tag == 10 || tag == 11 || tag == 12) { ref(0); ref(2); }
            else if (tag == 17 || tag == 18) { ref(2); entries[index]["value"] = hex.substr(p * 2, 4); }
            else if (tag == 15) { ref(1); entries[index]["value"] = hex.substr(p * 2, 2); }
            else entries[index]["value"] = hex.substr(p * 2, length * 2);
            p += length;
        }
    }
    if (p != bytes.size()) throw std::runtime_error("trailing installed pool bytes");
    bool merged = false, sessionKey = false;
    std::set<std::string> sessions;
    for (const auto &s : strings) {
        merged |= s.value == "Lorg/spongepowered/asm/mixin/transformer/meta/MixinMerged;";
        sessionKey |= s.value == "sessionId";
        if (mixinUuid(s.value) && !literalStrings.contains(s.index)) sessions.insert(s.value);
    }
    // Mixin's annotation-only session UUID changes every JVM launch. Never
    // normalize a CONSTANT_String literal or an unassociated/random UUID.
    const auto session = merged && sessionKey && sessions.size() == 1 ? *sessions.begin() : std::string{};
    std::set<std::string> synthetic;
    if (!session.empty()) for (const auto &m : klass.at("methods").array())
        if ((m.at("modifiers").integer() & 0x1000) &&
            m.at("name").string().starts_with("md" + session.substr(30) + "$lambda$")) synthetic.insert(m.at("name").string());
    for (const auto &s : strings) {
        auto value = s.value;
        if (!literalStrings.contains(s.index)) {
            if (!session.empty() && value == session) value = "00000000-0000-0000-0000-000000000000";
            else if (synthetic.contains(value)) value.replace(2, 6, "000000");
        }
        entries[s.index]["value"] = value;
        for (std::size_t j = 0; j < value.size(); ++j) {
            constexpr char digits[] = "0123456789abcdef";
            const auto b = static_cast<unsigned char>(value[j]);
            hex[(s.offset + j) * 2] = digits[b >> 4]; hex[(s.offset + j) * 2 + 1] = digits[b & 15];
        }
    }
    // JVMTI can append generated overpass constants in a different order after
    // restart. Keep every pool value plus the index/value of every instruction
    // reference: moving an unused entry is harmless, changing a used one is not.
    // Unknown/truncated instructions conservatively retain the indexed raw pool.
    std::set<unsigned> codeReferences; bool decoded = true;
    for (const auto &method : klass.at("methods").array()) {
        const auto references = installedCodeReferences(method);
        if (!references) { decoded = false; break; }
        codeReferences.insert(references->begin(), references->end());
    }
    if (decoded) {
        std::vector<std::string> memo(count);
        std::function<const std::string &(unsigned, unsigned)> canonical = [&](unsigned index, unsigned depth) -> const std::string & {
            if (index == 0 || index >= count || entries[index] == Json() || depth > 32)
                throw std::runtime_error("invalid installed pool reference");
            if (!memo[index].empty()) return memo[index];
            const auto &value = entries[index]; std::string refs = "[";
            for (const auto &ref : value.at("references").array()) {
                if (refs.size() != 1) refs += ',';
                refs += canonical(unsigned(ref.integer()), depth + 1);
            }
            refs += ']';
            memo[index] = "{\"references\":" + refs + ",\"tag\":" + value.at("tag").dump() + ",\"value\":" + value.at("value").dump() + "}";
            return memo[index];
        };
        // Serialize once before sorting. Serializing nested pool references on
        // each comparison made the compact live check unnecessarily expensive.
        std::vector<std::string> pool;
        for (unsigned i = 1; i < count; ++i) if (entries[i] != Json()) pool.push_back(canonical(i, 0));
        std::sort(pool.begin(), pool.end());
        std::string ordered = "[";
        for (const auto &entry : pool) { if (ordered.size() != 1) ordered += ','; ordered += entry; }
        ordered += ']';
        std::string used = "[";
        for (const auto index : codeReferences) {
            if (used.size() != 1) used += ',';
            used += "{\"entry\":" + canonical(index, 0) + ",\"index\":" + std::to_string(index) + "}";
        }
        used += ']';
        hex = "installed-pool-v3:{\"entries\":" + ordered + ",\"instructionReferences\":" + used + "}";
    }
    return {std::move(hex), session};
}
// Portable installed-content evidence. Never includes PID or loader instance.
// Hash inside the JVM on the fast path; do not transport bytecode/CP buffers.
inline std::string installedClassMaterial(const Json &klass) {
    const auto pool = installedPool(klass);
    Json::Array methods;
    for (const auto &m : klass.at("methods").array()) {
        auto name = m.at("name").string();
        if (!pool.session.empty() && (m.at("modifiers").integer() & 0x1000) &&
            name.starts_with("md" + pool.session.substr(30) + "$lambda$")) name.replace(2, 6, "000000");
        methods.push_back(Json::Object{{"name", name}, {"descriptor", m.at("descriptor")}, {"modifiers", m.at("modifiers")},
                                      {"bytecode", m.at("bytecode")}});
    }
    std::sort(methods.begin(), methods.end(), [](const Json &a, const Json &b) {
        return a.at("name").string() + a.at("descriptor").string() <
               b.at("name").string() + b.at("descriptor").string();
    });
    auto fields = klass.at("fields").array();
    std::sort(fields.begin(), fields.end(), [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
    Json::Array interfaces;
    for (const auto &i : klass.at("interfaces").array()) interfaces.push_back(i.at("name"));
    std::sort(interfaces.begin(), interfaces.end(), [](const Json &a, const Json &b) { return a.string() < b.string(); });
    return Json(Json::Object{{"proofVersion", 3}, {"constantPool", pool.hex}, {"name", klass.at("name")},
        {"super", klass.at("super").at("name")}, {"interfaces", interfaces}, {"modifiers", klass.at("modifiers")},
        {"fields", fields}, {"methods", methods}}).dump();
}
}
