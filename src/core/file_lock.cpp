#include "file_lock.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <signal.h>
#endif

namespace fs = std::filesystem;

namespace icmg::core {

namespace {

int currentPid() {
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

bool pidAlive(int pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return kill(pid, 0) == 0;
#endif
}

} // namespace

FileLock::FileLock(const std::string& path, int wait_ms)
    : path_(path), lock_path_(path + ".lock") {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(wait_ms);
    while (true) {
        // Try create exclusive.
        std::ofstream f;
        std::error_code ec;
        if (!fs::exists(lock_path_, ec)) {
            f.open(lock_path_, std::ios::out);
            if (f) {
                f << currentPid() << "\n";
                f.close();
                acquired_ = true;
                return;
            }
        }

        // Lock exists — check staleness.
        std::ifstream in(lock_path_);
        int holder_pid = 0;
        if (in) in >> holder_pid;
        if (holder_pid > 0 && !pidAlive(holder_pid)) {
            // Stale — remove and retry.
            fs::remove(lock_path_, ec);
            continue;
        }
        if (holder_pid == currentPid()) {
            // Re-entrant — already ours.
            acquired_ = true;
            return;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            err_ = "resource locked by PID " + std::to_string(holder_pid)
                 + " (" + path_ + ")";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

FileLock::~FileLock() {
    if (acquired_) {
        std::error_code ec;
        int holder_pid = 0;
        // Scope the ifstream — Windows refuses fs::remove while handle open.
        {
            std::ifstream in(lock_path_);
            if (in) in >> holder_pid;
        }
        if (holder_pid == currentPid()) {
            fs::remove(lock_path_, ec);
        }
    }
}

} // namespace icmg::core
