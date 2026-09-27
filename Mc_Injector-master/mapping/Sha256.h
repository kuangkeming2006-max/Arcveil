#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <wincrypt.h>
#include <windows.h>
namespace mcoverlay::mapping {
inline std::string windowsSha256(std::string_view bytes) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("SHA256 provider unavailable");
    struct Release {
        HCRYPTPROV provider;
        HCRYPTHASH *hash;
        ~Release() {
            if (*hash)
                CryptDestroyHash(*hash);
            CryptReleaseContext(provider, 0);
        }
    } release{provider, &hash};
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) ||
        !CryptHashData(hash, reinterpret_cast<const BYTE *>(bytes.data()),
                       static_cast<DWORD>(bytes.size()), 0))
        throw std::runtime_error("SHA256 failed");
    BYTE digest[32];
    DWORD length = 32;
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) || length != 32)
        throw std::runtime_error("SHA256 result unavailable");
    std::string out;
    constexpr char hex[] = "0123456789abcdef";
    for (BYTE b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
} // namespace mcoverlay::mapping
