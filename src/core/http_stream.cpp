// v1.31.0 A5b: HTTP streaming download + file SHA256. See http_stream.hpp.
#include "http_stream.hpp"
#include "exec_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace icmg::core {

namespace {

// Normalize backslashes -> forward slashes for shell consumption on Windows.
std::string shPath(std::string s) {
#ifdef _WIN32
    for (auto& c : s) if (c == '\\') c = '/';
#endif
    return s;
}

std::string toLowerHex(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

DownloadResult downloadToFile(const std::string& url,
                              const std::string& dest_path,
                              int timeout_s,
                              const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
    DownloadResult r;
    auto t0 = std::chrono::steady_clock::now();

    // Streaming approach: spawn curl async; poll dest file size for progress.
    // safeExecShell is synchronous — for progress polling we run curl via
    // popen-style detachment is unavailable here, so we accept a simpler
    // synchronous pattern: curl writes to dest, we report final size only
    // when `progress` is unset OR poll via background thread when set.
    //
    // To keep this self-contained without threading curl, use a sentinel
    // file approach: tell curl to write progress to a header-out file
    // via -D, then poll dest_path file_size in a side thread.

    std::string sh_dest = shPath(dest_path);
    std::string cmd =
        std::string(curlBin()) + " -sSL --fail --location --max-time " + std::to_string(timeout_s) +
        " --retry 2 --retry-delay 2 -o \"" + sh_dest + "\" -w \"%{http_code}\" \"" + url + "\"";

    // Side thread polls file size while main thread waits for curl.
    std::atomic<bool> done{false};
    std::atomic<bool> aborted{false};
    std::thread poller;
    if (progress) {
        poller = std::thread([&]() {
            while (!done.load(std::memory_order_acquire)) {
                std::error_code ec;
                std::uint64_t sz = 0;
                if (fs::exists(dest_path, ec))
                    sz = static_cast<std::uint64_t>(fs::file_size(dest_path, ec));
                if (!progress(sz, 0)) {
                    aborted.store(true, std::memory_order_release);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            // Final tick.
            std::error_code ec;
            if (fs::exists(dest_path, ec)) {
                std::uint64_t sz = static_cast<std::uint64_t>(fs::file_size(dest_path, ec));
                progress(sz, sz);
            }
        });
    }

    // Larger overall budget: curl --max-time governs the transfer; safeExecShell
    // wait must exceed it by ~5 s grace.
    auto res = safeExecShell(cmd, /*merge_stderr=*/true,
                             /*timeout_ms=*/static_cast<int>(timeout_s) * 1000 + 5000);
    done.store(true, std::memory_order_release);
    if (poller.joinable()) poller.join();

    auto t1 = std::chrono::steady_clock::now();
    r.wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (aborted.load(std::memory_order_acquire)) {
        std::error_code _e; fs::remove(dest_path, _e);
        r.error = "aborted by progress callback";
        return r;
    }
    if (res.exit_code != 0) {
        std::error_code _e; fs::remove(dest_path, _e);
        r.error = "curl exit=" + std::to_string(res.exit_code) +
                  " stderr=" + res.err.substr(0, 200);
        return r;
    }
    // Parse `-w "%{http_code}"` — last line of stdout.
    try { r.http_status = std::stoi(res.out); } catch (...) {}
    if (r.http_status >= 400) {
        std::error_code _e; fs::remove(dest_path, _e);
        r.error = "HTTP " + std::to_string(r.http_status);
        return r;
    }
    std::error_code ec;
    if (!fs::exists(dest_path, ec)) {
        r.error = "dest file missing after curl ok";
        return r;
    }
    r.bytes = static_cast<std::uint64_t>(fs::file_size(dest_path, ec));
    r.ok = true;
    return r;
}

std::string sha256OfFile(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return "";
    std::string sh_path = shPath(path);
#ifdef _WIN32
    // certutil -hashfile <path> SHA256 -> hash on line 2.
    std::string cmd = "certutil -hashfile \"" + sh_path + "\" SHA256";
    auto r = safeExecShell(cmd, true, 60000);
    if (r.exit_code != 0) return "";
    std::stringstream ss(r.out);
    std::string line;
    int idx = 0;
    while (std::getline(ss, line)) {
        if (idx == 1) {
            // Strip spaces.
            std::string h;
            for (char c : line) if (!std::isspace(static_cast<unsigned char>(c))) h.push_back(c);
            if (h.size() == 64) return toLowerHex(h);
            return "";
        }
        ++idx;
    }
    return "";
#else
    std::string cmd = "sha256sum \"" + sh_path + "\"";
    auto r = safeExecShell(cmd, true, 60000);
    if (r.exit_code != 0) return "";
    // Output: "<hex>  <path>\n"
    auto sp = r.out.find(' ');
    if (sp == std::string::npos || sp != 64) return "";
    return toLowerHex(r.out.substr(0, 64));
#endif
}

bool verifySha256(const std::string& path, const std::string& expected_hex) {
    if (expected_hex.empty()) return true;
    std::string got = sha256OfFile(path);
    if (got.empty()) return false;
    std::string want = toLowerHex(expected_hex);
    return got == want;
}

} // namespace icmg::core
