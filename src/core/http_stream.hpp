// v1.31.0 A5b: HTTP streaming download + file SHA256.
//
// Used by `icmg llm install` to fetch large GGUF models (400 MB - 2 GB).
// Streams directly to disk via system curl (`-o file`); never buffers
// the whole body in memory.
//
// SHA256 verification shells out to `certutil` (Windows) or `sha256sum`
// (Linux/macOS). No new third-party deps.
//
// Latency category: cold path only. Hot/warm paths must NEVER block on
// this — caller is expected to be a CLI command or background worker.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace icmg::core {

struct DownloadResult {
    bool        ok          = false;
    std::string error;
    std::uint64_t bytes     = 0;     // bytes written to dest
    double      wall_ms     = 0.0;
    int         http_status = 0;     // 0 = unknown / curl error
};

// Download `url` to `dest_path`. Existing file is overwritten. Parent
// directory must already exist.
// `timeout_s` = total transfer timeout in seconds (curl --max-time).
// `progress` (optional) fires every ~500 ms with (bytes_so_far, total_or_zero).
//   Return false from progress to abort.
DownloadResult downloadToFile(const std::string& url,
                              const std::string& dest_path,
                              int timeout_s = 600,
                              const std::function<bool(std::uint64_t bytes, std::uint64_t total)>& progress = {});

// Compute lowercase hex SHA256 of a file via system tool. Returns empty
// string on failure (file missing, hash tool missing, parse fail).
std::string sha256OfFile(const std::string& path);

// Verify file SHA256 matches the expected hex (case-insensitive).
// Empty expected -> returns true (verification skipped — caller's choice).
// Empty actual (compute failed) -> returns false.
bool verifySha256(const std::string& path, const std::string& expected_hex);

} // namespace icmg::core
