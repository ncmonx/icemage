// v1.68 S2: icmg-server IPC token auth — implementation.

#include "server_token.hpp"
#include "path_utils.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
// BCryptGenRandom is self-contained (no _CRT_RAND_S macro that a precompiled
// header would defeat by including <stdlib.h> first). It is the system CSPRNG.
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#  ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#    define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#  endif
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace icmg::core {

std::string serverTokenPath() {
    fs::path dir = icmgGlobalDir();
    return (dir / "server.token").string();
}

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Fill 'buf' with 'n' cryptographically-secure random bytes.
// Windows: rand_s (CRT wrapper over the system CSPRNG / RtlGenRandom).
// POSIX:   /dev/urandom. Returns false if the OS RNG is unavailable —
// the caller must NOT fall back to a weak PRNG for a credential.
bool genRandomBytes(unsigned char* buf, size_t n) {
#if defined(_WIN32)
    NTSTATUS st = BCryptGenRandom(nullptr, buf, (ULONG)n,
                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return st == 0;   // STATUS_SUCCESS
#else
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t got = 0;
    bool ok = true;
    while (got < n) {
        ssize_t r = ::read(fd, buf + got, n - got);
        if (r <= 0) { ok = false; break; }
        got += (size_t)r;
    }
    ::close(fd);
    return ok;
#endif
}

std::string genHex32() {
    unsigned char raw[16];
    if (!genRandomBytes(raw, sizeof(raw))) return "";   // fail closed
    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char b : raw) {
        out.push_back(hexd[(b >> 4) & 0xF]);
        out.push_back(hexd[b & 0xF]);
    }
    return out;
}

// Write the token to 'path' with owner-only permissions, created atomically
// so the credential is never momentarily world-readable. Returns true on
// success. On POSIX uses O_CREAT|O_EXCL|0600; on Windows the file lands in the
// user-profile dir whose inherited DACL already restricts to the owner.
bool writeTokenSecurely(const std::string& path, const std::string& tok) {
#if !defined(_WIN32)
    int fd = ::open(path.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_TRUNC,
                    S_IRUSR | S_IWUSR);            // 0600, never world-readable
    if (fd < 0) {
        // Another process created it first (race) — accept the existing token.
        return fs::exists(path);
    }
    ssize_t w = ::write(fd, tok.data(), tok.size());
    ::close(fd);
    return w == (ssize_t)tok.size();
#else
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << tok;
    return (bool)f;
#endif
}

} // namespace

std::string readServerToken() {
    std::ifstream f(serverTokenPath(), std::ios::binary);
    if (!f) return "";
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return trim(s);
}

std::string loadOrCreateServerToken() {
    std::string existing = readServerToken();
    if (!existing.empty()) return existing;

    std::error_code ec;
    fs::create_directories(icmgGlobalDir(), ec);

    std::string tok = genHex32();
    if (tok.empty()) return "";                    // RNG unavailable — fail closed
    if (!writeTokenSecurely(serverTokenPath(), tok)) {
        // Creation lost a race with another process; re-read the winner.
        return readServerToken();
    }
    return tok;
}

bool tokenMatches(const std::string& expected, const std::string& got) {
    if (expected.empty()) return false;           // fail closed
    if (expected.size() != got.size()) return false;
    // full-scan compare (no early-out on first mismatch)
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        diff |= static_cast<unsigned char>(expected[i] ^ got[i]);
    }
    return diff == 0;
}

} // namespace icmg::core
