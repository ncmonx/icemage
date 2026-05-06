#pragma once
#include <string>
#include <stdexcept>
#include <cstdint>

namespace icmg::graph {

struct DaemonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Daemon {
public:
    /// Start watcher daemon for root directory.
    /// Forks/detaches process, writes PID to pidPath.
    static void start(const std::string& root, const std::string& dbPath,
                      const std::string& pidPath);

    /// Send SIGTERM (Unix) or TerminateProcess (Windows) using PID file.
    static void stop(const std::string& pidPath);

    /// Returns true if PID file exists and process is alive.
    static bool isRunning(const std::string& pidPath);

    /// Read PID from file. Returns 0 on error.
    static int64_t readPid(const std::string& pidPath);

private:
    static bool isProcessAlive(int64_t pid);
    static bool isIcmgProcess(int64_t pid);
    static void writePid(const std::string& pidPath, int64_t pid);
    static void runWatchLoop(const std::string& root, const std::string& dbPath);
};

} // namespace icmg::graph
