// v1.27.0 (Phase 1.2): impl extracted from port_cmd.cpp anon namespace.

#include "port_artifact.hpp"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace icmg::cli::port_artifact {

std::string fnv128hex(const std::string& msg) {
    auto fnv = [](const std::string& s, uint64_t init) {
        uint64_t h = init;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return h;
    };
    uint64_t h1 = fnv(msg, 14695981039346656037ULL);
    uint64_t h2 = fnv(msg + std::string(msg.rbegin(), msg.rend()),
                       1099511628211ULL);
    char raw[16];
    std::memcpy(raw, &h1, 8);
    std::memcpy(raw + 8, &h2, 8);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char c : std::string(raw, 16)) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
    }
    return out;
}

ArtifactParse parseArtifact(const std::string& blob) {
    ArtifactParse out;
    auto first_nl = blob.find('\n');
    if (first_nl == std::string::npos
        || blob.substr(0, first_nl) != "ICMG-PORT v1") {
        out.error = "bad magic / wrong version";
        return out;
    }
    auto body_start = blob.find("\n---\n");
    if (body_start == std::string::npos) {
        out.error = "missing payload separator";
        return out;
    }
    std::string header = blob.substr(0, body_start);
    out.payload = blob.substr(body_start + 5);
    std::stringstream hss(header);
    std::string line;
    while (std::getline(hss, line)) {
        auto colon = line.find(": ");
        if (colon == std::string::npos) continue;
        std::string k = line.substr(0, colon);
        std::string v = line.substr(colon + 2);
        if (k == "FILES") { try { out.files = std::stoi(v); } catch (...) {} }
        else if (k == "RAW") { try { out.raw_bytes = std::stoll(v); } catch (...) {} }
        else if (k == "HASH") out.hash = v;
    }
    std::string actual = fnv128hex(out.payload);
    if (actual != out.hash) {
        out.error = "hash mismatch — artifact corrupted (expected "
                  + out.hash + ", got " + actual + ")";
        return out;
    }
    out.ok = true;
    return out;
}

std::string serializeArtifact(const std::string& /*name*/,
                               int files,
                               long long raw_bytes,
                               const std::string& payload) {
    // BUNDLE line intentionally omitted — wire format frozen at v1.24.0
    // for backward compat. `name` arg reserved for future v2 header.
    std::string hash = fnv128hex(payload);
    std::ostringstream out;
    out << "ICMG-PORT v1\n"
        << "FILES: " << files << "\n"
        << "RAW: " << raw_bytes << "\n"
        << "HASH: " << hash << "\n"
        << "---\n"
        << payload;
    return out.str();
}

}  // namespace icmg::cli::port_artifact
