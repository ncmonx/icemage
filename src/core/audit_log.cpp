// Phase 75: HMAC-chained append-only audit log (impl).
//
// NOTE: chain uses FNV-1a 64-bit + secret-prefix mixing — fast, suitable for
// accidental-tamper detection. NOT cryptographically HMAC-secure (no
// pre-image resistance against motivated attacker). For real HMAC, swap in
// SHA-256 via OpenSSL link. Documented in audit_log.hpp.

#include "audit_log.hpp"
#include "path_utils.hpp"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace icmg::core {

AuditLog::AuditLog(const std::string& log_path, const std::string& key_path)
    : log_path_(log_path)
    , key_path_(key_path)
{
    if (key_path_.empty()) {
        key_path_ = icmgGlobalDir() + "/secret.key";
    }
    ensureKey();
    fs::create_directories(fs::path(log_path_).parent_path());
}

void AuditLog::ensureKey() {
    if (fs::exists(key_path_)) {
        std::ifstream f(key_path_, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        key_ = ss.str();
        return;
    }
    // Generate 32 random bytes (sufficient for FNV-1a key mixing).
    fs::create_directories(fs::path(key_path_).parent_path());
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::string buf;
    buf.resize(32);
    for (int i = 0; i < 4; ++i) {
        uint64_t v = dis(gen);
        std::memcpy(buf.data() + i * 8, &v, 8);
    }
    std::ofstream f(key_path_, std::ios::binary);
    f.write(buf.data(), (std::streamsize)buf.size());
    key_ = buf;
#ifndef _WIN32
    // Best-effort 0600 on POSIX.
    std::error_code ec;
    fs::permissions(key_path_, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
#endif
}

std::string AuditLog::hexEncode(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
    }
    return out;
}

// FNV-1a 64-bit keyed mix. Not crypto-secure; collision-resistant for our
// "did anyone edit the file" need.
std::string AuditLog::hmac_sha256(const std::string& key,
                                  const std::string& msg) {
    auto fnv = [](const std::string& s, uint64_t init) {
        uint64_t h = init;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return h;
    };
    // Two parallel streams for 128 bits of effective state.
    uint64_t h1 = fnv(key, 14695981039346656037ULL);
    uint64_t h2 = fnv(key, 1099511628211ULL);
    h1 = fnv(msg, h1);
    h2 = fnv(msg + std::string(key.rbegin(), key.rend()), h2);
    char raw[16];
    std::memcpy(raw, &h1, 8);
    std::memcpy(raw + 8, &h2, 8);
    return hexEncode(std::string(raw, 16));
}

std::string AuditLog::nowIso() {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream o;
    o << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return o.str();
}

static std::string sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == '|') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

std::string AuditLog::append(const std::string& actor,
                             const std::string& event,
                             const std::string& payload) {
    // Find prev_hmac from last line.
    std::string prev_hmac = "0000000000000000";
    if (fs::exists(log_path_)) {
        std::ifstream f(log_path_);
        std::string line, last;
        while (std::getline(f, line)) if (!line.empty()) last = line;
        if (!last.empty()) {
            // Format: ts | actor | event | payload | prev | hmac
            auto pos = last.rfind('|');
            if (pos != std::string::npos)
                prev_hmac = last.substr(pos + 1);
            // Strip leading whitespace.
            size_t s = prev_hmac.find_first_not_of(" \t");
            if (s != std::string::npos) prev_hmac = prev_hmac.substr(s);
        }
    }
    std::string ts = nowIso();
    std::string a = sanitize(actor);
    std::string e = sanitize(event);
    std::string p = sanitize(payload);
    std::string row_payload = ts + "|" + a + "|" + e + "|" + p + "|" + prev_hmac;
    std::string hmac = hmac_sha256(key_, row_payload);
    std::ofstream f(log_path_, std::ios::app);
    if (!f) return {};
    f << row_payload << "|" << hmac << "\n";
    return hmac;
}

std::vector<AuditLog::Entry> AuditLog::read(int limit) {
    std::vector<Entry> out;
    if (!fs::exists(log_path_)) return out;
    std::ifstream f(log_path_);
    std::string line;
    int64_t n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.empty()) continue;
        Entry e;
        e.line_no = n;
        // Split on '|' (6 fields).
        size_t p = 0;
        std::string parts[6];
        for (int i = 0; i < 6; ++i) {
            size_t q = line.find('|', p);
            if (q == std::string::npos) {
                parts[i] = line.substr(p);
                p = line.size();
                break;
            }
            parts[i] = line.substr(p, q - p);
            p = q + 1;
        }
        e.ts        = parts[0];
        e.actor     = parts[1];
        e.event     = parts[2];
        e.payload   = parts[3];
        e.prev_hmac = parts[4];
        e.hmac      = parts[5];
        out.push_back(std::move(e));
    }
    if (limit > 0 && (int)out.size() > limit)
        out.erase(out.begin(), out.end() - limit);
    return out;
}

std::vector<AuditLog::Entry> AuditLog::tail(int n) {
    return read(n);
}

int AuditLog::verify(std::vector<int64_t>* bad_rows) {
    auto rows = read();
    int bad = 0;
    std::string prev = "0000000000000000";
    for (auto& r : rows) {
        std::string row_payload = r.ts + "|" + r.actor + "|" + r.event
                                + "|" + r.payload + "|" + prev;
        std::string expect = hmac_sha256(key_, row_payload);
        if (expect != r.hmac || r.prev_hmac != prev) {
            ++bad;
            if (bad_rows) bad_rows->push_back(r.line_no);
        }
        prev = r.hmac;
    }
    return bad;
}

} // namespace icmg::core
