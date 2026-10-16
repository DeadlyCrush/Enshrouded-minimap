#pragma once
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// Hash the on-disk executable, not already-patched memory. A build ID or a
// generic prologue alone cannot authorize writes into a foreign client.
inline bool ClientExecutableMatchesSha256(const wchar_t* file, const char* expected)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::ifstream input(file, std::ios::binary);
    if (!input || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    DWORD objectSize = 0, resultSize = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0) >= 0;
    std::vector<unsigned char> object(objectSize), buffer(65536);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    while (ok && input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto count = input.gcount();
        if (count > 0) ok = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) >= 0;
    }
    unsigned char digest[32]{};
    ok = ok && !input.bad() && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok || !expected || std::strlen(expected) != 64) return false;
    static const char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 32; ++i)
        if (expected[i * 2] != hex[digest[i] >> 4] || expected[i * 2 + 1] != hex[digest[i] & 15])
            return false;
    return true;
}
