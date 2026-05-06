#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace icmg::core {

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_path,
              uint64_t max_bytes     = 1024 * 1024,  // 1 MB per file
              int      max_files     = 5);

    void log(LogLevel level, const std::string& msg);
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }
    void info (const std::string& msg) { log(LogLevel::INFO,  msg); }
    void warn (const std::string& msg) { log(LogLevel::WARN,  msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }

private:
    Logger() = default;
    std::string   base_path_;
    uint64_t      max_bytes_  = 1024 * 1024;
    int           max_files_  = 5;
    std::ofstream file_;
    std::mutex    mu_;
    uint64_t      current_bytes_ = 0;

    void rotate();
    void openFile();
    std::string levelStr(LogLevel l);
    std::string timestamp();
};

} // namespace icmg::core
