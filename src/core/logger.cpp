#include "logger.hpp"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
namespace icmg::core {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& log_path, uint64_t max_bytes, int max_files) {
    std::lock_guard<std::mutex> lk(mu_);
    base_path_  = log_path;
    max_bytes_  = max_bytes;
    max_files_  = max_files;
    openFile();
}

void Logger::openFile() {
    if (base_path_.empty()) return;
    fs::create_directories(fs::path(base_path_).parent_path());
    file_.open(base_path_, std::ios::app);
    current_bytes_ = fs::exists(base_path_)
        ? (uint64_t)fs::file_size(base_path_) : 0;
}

void Logger::rotate() {
    file_.close();

    // Shift: icmg.log.4 → delete, icmg.log.3 → .4, ... icmg.log → .1
    for (int i = max_files_ - 1; i >= 1; --i) {
        std::string from = base_path_ + "." + std::to_string(i);
        std::string to   = base_path_ + "." + std::to_string(i + 1);
        if (fs::exists(from)) {
            if (i == max_files_ - 1) fs::remove(from);
            else fs::rename(from, to);
        }
    }
    if (fs::exists(base_path_)) fs::rename(base_path_, base_path_ + ".1");
    openFile();
}

void Logger::log(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    if (base_path_.empty()) return;
    if (!file_.is_open()) return;

    std::string line = timestamp() + " [" + levelStr(level) + "] " + msg + "\n";
    file_ << line;
    file_.flush();
    current_bytes_ += line.size();

    if (current_bytes_ >= max_bytes_) rotate();
}

std::string Logger::levelStr(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace icmg::core
