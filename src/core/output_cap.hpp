#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstdint>

namespace icmg::core {

// Truncate text to N bytes; spill rest to /tmp/icmg-spill-<hash>.txt.
// Returns the truncated string (with footer); writes spill_path on overflow.
inline std::string capOutput(const std::string& full, size_t cap, std::string& spill_path) {
    spill_path.clear();
    if (full.size() <= cap) return full;

    // Hash for spill filename — keeps repeat-call output stable
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : full) { h ^= c; h *= 1099511628211ULL; }
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);

    // v1.61 bug-fix (Bug 1): the spill path must NEVER crash the command.
    // On Windows std::filesystem::temp_directory_path() can throw a
    // filesystem_error whose message is the wrapped Win32 text "The
    // specified module could not be found" (error 126) when the temp dir
    // env is broken on a host. Previously uncaught -> any over-cap output
    // (e.g. `icmg context --lines A-B` / `--max-bytes N` producing > cap
    // bytes) terminated the process, while small plain output that never
    // spilled worked. Degrade gracefully to in-memory truncation (no spill)
    // on ANY failure.
    namespace fs = std::filesystem;
    try {
        std::error_code ec;
        fs::path base = fs::temp_directory_path(ec);
        if (!ec && !base.empty()) {
            fs::path tmp = base / (std::string("icmg-spill-") + hex + ".txt");
            std::ofstream f(tmp, std::ios::binary);
            if (f) { f << full; f.close(); spill_path = tmp.string(); }
        }
    } catch (...) {
        spill_path.clear();   // spill unavailable — truncate in memory only
    }

    // Keep head + small tail so tool callers don't lose footer info
    size_t head = cap > 256 ? cap - 256 : cap;
    std::string out = full.substr(0, head);
    out += "\n... [truncated, " + std::to_string(full.size() - cap) + " bytes spilled to "
         + (spill_path.empty() ? "<spill failed>" : spill_path) + "]\n";
    if (full.size() > 256) out += full.substr(full.size() - 256);
    return out;
}

} // namespace icmg::core
