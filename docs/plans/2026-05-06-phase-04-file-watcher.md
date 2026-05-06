# Phase 04: File Watcher (Cross-platform Daemon)

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Background daemon yang watch directory changes dan auto-trigger graph re-scan untuk file yang berubah.
**Architecture:** Platform-specific watcher (inotify/kqueue/ReadDirectoryChangesW) wrapped di unified interface. Daemon write PID ke .icmg/watcher.pid.
**Tech Stack:** C++17, POSIX (Linux/macOS), Win32 API (Windows)
**Assumptions:** Phase 03 selesai. Hanya support local filesystem (bukan network drives).

---

### Task 1: Watcher interface

**Files:**
- Create: `src/graph/watcher/base_watcher.hpp`

```cpp
class BaseWatcher {
public:
    virtual ~BaseWatcher() = default;
    using Callback = std::function<void(const std::string& path, WatchEvent event)>;
    enum class WatchEvent { Created, Modified, Deleted };

    virtual bool start(const std::string& dir, Callback cb) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};
```

---

### Task 2: Windows watcher (ReadDirectoryChangesW)

**Files:**
- Create: `src/graph/watcher/win_watcher.hpp`
- Create: `src/graph/watcher/win_watcher.cpp`

Wrap `ReadDirectoryChangesW` dengan buffer 64KB, watch subtree = TRUE.
Detect: FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE

Guard dengan `#if defined(_WIN32)`.

---

### Task 3: Linux watcher (inotify)

**Files:**
- Create: `src/graph/watcher/linux_watcher.hpp`
- Create: `src/graph/watcher/linux_watcher.cpp`

Pakai inotify_init + inotify_add_watch per directory (recursive via std::filesystem::recursive_directory_iterator).
Events: IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO

Guard dengan `#elif defined(__linux__)`.

---

### Task 4: macOS watcher (kqueue)

**Files:**
- Create: `src/graph/watcher/mac_watcher.hpp`
- Create: `src/graph/watcher/mac_watcher.cpp`

Pakai kqueue + kevent dengan EVFILT_VNODE.
NOTE_WRITE | NOTE_DELETE | NOTE_RENAME

Guard dengan `#elif defined(__APPLE__)`.

---

### Task 5: Watcher factory + daemon runner

**Files:**
- Create: `src/graph/watcher/watcher_factory.hpp`
- Create: `src/graph/daemon.hpp`
- Create: `src/graph/daemon.cpp`

**watcher_factory.hpp:**
```cpp
std::unique_ptr<BaseWatcher> createWatcher();
// Returns WinWatcher / LinuxWatcher / MacWatcher based on platform
```

**daemon.cpp:**
- Fork process (Unix) atau CreateProcess (Windows) untuk background
- Write PID ke `.icmg/watcher.pid`
- On file change: call Scanner::scan untuk file yang berubah saja (incremental)
- Debounce: 500ms delay sebelum re-scan (batch rapid changes)

---

### Task 6: CLI commands: graph watch + stop

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

```
icmg graph watch <path>    # start daemon
icmg graph stop            # kill daemon via PID file
icmg graph watch-status    # show apakah daemon running
```

**watch output:**
```
Watching: /path/to/project (recursive)
PID: 12345 saved to .icmg/watcher.pid
Press Ctrl+C to stop, or run: icmg graph stop
```

**Verify:**
```bash
./build/icmg graph watch src/ &
touch src/new_test_file.cpp
sleep 2
./build/icmg graph context src/new_test_file.cpp
./build/icmg graph stop
```
Expected: new_test_file.cpp muncul di graph setelah dibuat.

---

### Task 7: Commit

```bash
git add src/graph/watcher/ src/graph/daemon.hpp src/graph/daemon.cpp
git commit -m "feat: phase-04 cross-platform file watcher daemon"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — PID file atomic create + verification**
```cpp
// Buka dengan O_CREAT|O_EXCL — gagal jika file sudah ada
int fd = open(pidPath.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0600);
if (fd < 0) {
    // Daemon mungkin sudah jalan — cek apakah PID masih hidup
    pid_t existing = readPid(pidPath);
    if (isProcessAlive(existing)) {
        throw DaemonError("Watcher already running (PID " + existing + ")");
    }
    // Stale PID — overwrite
    unlink(pidPath.c_str());
    fd = open(pidPath.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0600);
}
```

**A2 — PID verification sebelum kill**
```cpp
void stopDaemon(const std::string& pidPath) {
    pid_t pid = readPid(pidPath);
    if (!isProcessAlive(pid)) {
        fs::remove(pidPath);  // clean stale
        return;
    }
    // Verify proses adalah icmg (bukan PID reuse)
    if (!isIcmgProcess(pid)) {
        throw DaemonError("PID " + pid + " is not icmg — refusing to kill");
    }
    kill(pid, SIGTERM);
}

bool isIcmgProcess(pid_t pid) {
    // Linux: read /proc/<pid>/comm atau /proc/<pid>/exe
    // macOS: sysctl KERN_PROC_PATHNAME
    // Windows: OpenProcess + QueryFullProcessImageName
}
```

**A3 — Thread-safe debounce**
```cpp
class Debouncer {
    std::mutex mu_;
    std::condition_variable cv_;
    std::thread timer_;
    std::set<std::string> pending_;
    bool running_ = true;

public:
    void trigger(const std::string& path) {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.insert(path);
        cv_.notify_one();
    }

    void run(std::function<void(const std::set<std::string>&)> cb) {
        while (running_) {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(500));
            if (!pending_.empty()) {
                auto batch = std::move(pending_);
                lock.unlock();
                cb(batch);
            }
        }
    }
};
```

### HIGH Fixes

**A4 — Symlink handling documented**
Scanner pakai `recursive_directory_iterator` dengan symlink option:
```cpp
// Explicit: tidak follow symlinks by default
// Document sebagai non-goal: "symlinks ke direktori eksternal tidak di-watch"
// Tapi symlinks ke FILE di dalam project: ikuti (follow_directory_symlink)
auto opts = fs::directory_options::skip_permission_denied;
// NOT: follow_directory_symlink (mencegah loop)
```

**A5 — Auto-backup harian**
Tambahkan ke daemon loop:
```cpp
void maybeDailyBackup() {
    auto lastBackup = readLastBackupTime();
    if (hoursAgo(lastBackup) >= 24) {
        auto backupPath = icmgDir + "/backups/data." + dateStr() + ".db";
        fs::copy_file(dbPath, backupPath);
        pruneBackups(maxKeep=7);
        writeLastBackupTime(now());
    }
}
```
```
icmg backup               # manual trigger
icmg restore --from .icmg/backups/data.20260430.db
icmg backup list
```

**A6 — Global daemon coordination**
Sebelum start daemon per-project, check apakah ada daemon lain aktif di project yang sama (bukan global):
Dokumentasikan: satu daemon per project root. Multiple projects = multiple daemons, masing-masing di PID file project sendiri.
