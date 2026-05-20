// v1.13.0: exec_server wire protocol.
//
// Length-prefixed JSON frames bidirectional over named pipe / unix socket.
// Client sends one request frame, server streams multiple response frames
// until done=true, then closes.
//
// Frame format:
//   [4-byte little-endian uint32 length] + [JSON UTF-8 bytes]
//
// Request:
//   { "op":"exec", "argv":["context","foo.cpp"], "cwd":"D:\\proj",
//     "env":{}, "stdin":"" }
//
// Streaming response frames:
//   { "out":"<chunk>" }       — stdout chunk (up to 4KB)
//   { "err":"<chunk>" }       — stderr chunk
//   { "done":true, "exit":0 } — terminal frame
//
// Pipe name (per-user):
//   Windows: \\.\pipe\icmg-exec-<USERNAME>
//   POSIX:   ~/.icmg/exec.sock

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace icmg::core::exec_proto {

#ifdef _WIN32
inline std::string pipeName() {
    // Per-user pipe naming. Multi-user safety (v1.20.2): if $USERNAME is
    // empty (e.g., detached SYSTEM service context), use a stable salted
    // fallback instead of an unsalted shared pipe — prevents cross-user
    // collision in shared installs.
    const char* user = std::getenv("USERNAME");
    if (user && *user) {
        return std::string("\\\\.\\pipe\\icmg-exec-") + user;
    }
    // Fallback uses USERPROFILE-hash so each user-session still gets unique
    // pipe even if USERNAME absent.
    const char* prof = std::getenv("USERPROFILE");
    if (prof && *prof) {
        size_t h = 0;
        for (const char* p = prof; *p; ++p) h = h * 131 + (unsigned char)*p;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "\\\\.\\pipe\\icmg-exec-h%zx", h);
        return std::string(buf);
    }
    return "\\\\.\\pipe\\icmg-exec";
}
#else
inline std::string pipeName() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.icmg/exec.sock";
}
#endif

// Pack JSON payload as length-prefixed wire bytes.
inline std::string frame(const std::string& json) {
    uint32_t n = (uint32_t)json.size();
    std::string out;
    out.reserve(4 + json.size());
    out.push_back((char)(n & 0xff));
    out.push_back((char)((n >> 8) & 0xff));
    out.push_back((char)((n >> 16) & 0xff));
    out.push_back((char)((n >> 24) & 0xff));
    out += json;
    return out;
}

// Parse 4-byte LE length prefix. Returns 0 on insufficient bytes.
inline uint32_t parseLength(const char* buf, size_t len) {
    if (len < 4) return 0;
    return ((uint32_t)(unsigned char)buf[0])
         | ((uint32_t)(unsigned char)buf[1] << 8)
         | ((uint32_t)(unsigned char)buf[2] << 16)
         | ((uint32_t)(unsigned char)buf[3] << 24);
}

} // namespace icmg::core::exec_proto
