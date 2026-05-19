// v1.13.0: log rotation impl.
#include "log_rotate.hpp"
#include "path_utils.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::core::log_rotate {

namespace {

std::string today() {
    auto t = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return std::string(buf);
}

std::string now_time() {
    auto t = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H%M%S", &tm_buf);
    return std::string(buf);
}

} // namespace

void rotate(const std::string& path, size_t max_bytes, int retention_days) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    auto sz = fs::file_size(path, ec);
    if (ec) return;
    if (sz <= max_bytes) {
        // Just retention sweep.
    } else {
        // Rename.
        fs::path src = path;
        fs::path dst = src.string() + "." + today();
        if (fs::exists(dst, ec)) {
            dst = src.string() + "." + today() + "." + now_time();
        }
        fs::rename(src, dst, ec);
        if (ec) ec.clear();
    }

    // Retention: delete rotated files older than N days.
    fs::path dir = fs::path(path).parent_path();
    std::string base = fs::path(path).filename().string();
    if (!fs::exists(dir, ec)) return;
    auto cutoff = std::chrono::system_clock::now()
                - std::chrono::hours(24 * retention_days);
    std::regex pat(base + R"(\.\d{4}-\d{2}-\d{2}(\.\d{6})?)");
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) { ec.clear(); continue; }
        std::string name = e.path().filename().string();
        if (!std::regex_match(name, pat)) continue;
        auto wt = fs::last_write_time(e, ec);
        if (ec) { ec.clear(); continue; }
        // Convert file_time_type to system_clock for comparison.
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            wt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        if (sctp < cutoff) {
            fs::remove(e.path(), ec);
            if (ec) ec.clear();
        }
    }
}

void rotateIcmgLogs() {
    fs::path gd = icmgGlobalDir();
    size_t cap = 5 * 1024 * 1024;  // 5 MB
    int retention = 7;             // days
    for (auto* name : {"service.out.log", "service.err.log",
                       "cron.log", "popup-killer.log",
                       "strict-denials.jsonl", "bfs-queries.jsonl"}) {
        rotate((gd / name).string(), cap, retention);
    }
}

} // namespace icmg::core::log_rotate
