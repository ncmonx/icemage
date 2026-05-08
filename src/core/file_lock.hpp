// Phase 47 T7: advisory lockfile for cross-process write coordination.
//
// SQLite WAL handles single-DB concurrency via busy_timeout retry. This adds an
// extra safety layer for icmg cmds that touch files outside the DB (e.g.
// memoir export overwriting a .md file at same time, or two `icmg update
// --apply` racing on .new staging).
//
// Pattern: RAII guard. Acquire on construction, release on destruction.
// Stale lock auto-cleanup if PID no longer exists.
#pragma once
#include <string>

namespace icmg::core {

class FileLock {
public:
    // path = full path of resource being protected (lock created at path + ".lock")
    // wait_ms = 0 means try-once, >0 polls.
    explicit FileLock(const std::string& path, int wait_ms = 5000);
    ~FileLock();

    bool acquired() const { return acquired_; }
    const std::string& errorMsg() const { return err_; }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    std::string path_;
    std::string lock_path_;
    bool acquired_ = false;
    std::string err_;
};

} // namespace icmg::core
