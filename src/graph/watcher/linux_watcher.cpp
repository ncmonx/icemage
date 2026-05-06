#if defined(__linux__)
#include "linux_watcher.hpp"
#include <filesystem>
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

namespace fs = std::filesystem;

namespace icmg::graph {

LinuxWatcher::LinuxWatcher() = default;

LinuxWatcher::~LinuxWatcher() {
    stop();
}

bool LinuxWatcher::addWatch(const std::string& path) {
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE |
                    IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE;
    int wd = inotify_add_watch(ifd_, path.c_str(), mask);
    if (wd < 0) return false;
    wd2path_[wd] = path;
    return true;
}

bool LinuxWatcher::start(const std::string& dir, Callback cb) {
    if (running_.load()) return false;

    ifd_ = inotify_init1(IN_NONBLOCK);
    if (ifd_ < 0) return false;

    if (pipe(pipe_) < 0) { close(ifd_); ifd_ = -1; return false; }

    // Add root + all subdirs (A4: skip_permission_denied, no follow_directory_symlink)
    std::error_code ec;
    addWatch(dir);
    for (auto& entry : fs::recursive_directory_iterator(dir,
             fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec)) addWatch(entry.path().string());
    }

    running_.store(true);
    thread_ = std::thread([this, cb]() { watchLoop(cb); });
    return true;
}

void LinuxWatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);
    // Signal watchLoop to exit
    if (pipe_[1] >= 0) { write(pipe_[1], "x", 1); }
    if (thread_.joinable()) thread_.join();
    if (ifd_ >= 0) { close(ifd_); ifd_ = -1; }
    if (pipe_[0] >= 0) { close(pipe_[0]); pipe_[0] = -1; }
    if (pipe_[1] >= 0) { close(pipe_[1]); pipe_[1] = -1; }
    wd2path_.clear();
}

void LinuxWatcher::watchLoop(Callback cb) {
    constexpr size_t BUF = 4096;
    char buf[BUF];

    struct pollfd fds[2];
    fds[0] = {ifd_,    POLLIN, 0};
    fds[1] = {pipe_[0], POLLIN, 0};

    while (running_.load()) {
        int ret = poll(fds, 2, 500);
        if (ret <= 0) continue;
        if (fds[1].revents & POLLIN) break;  // stop signal

        ssize_t len = read(ifd_, buf, BUF);
        if (len <= 0) continue;

        ssize_t i = 0;
        while (i < len) {
            auto* ev = reinterpret_cast<inotify_event*>(buf + i);
            i += sizeof(inotify_event) + ev->len;

            if (ev->wd < 0) continue;
            auto it = wd2path_.find(ev->wd);
            if (it == wd2path_.end()) continue;

            std::string fullpath = it->second;
            if (ev->len > 0 && ev->name[0] != '\0') fullpath += "/" + std::string(ev->name);

            WatchEvent we;
            if      (ev->mask & (IN_CREATE | IN_MOVED_TO))    we = WatchEvent::Created;
            else if (ev->mask & (IN_DELETE | IN_MOVED_FROM))  we = WatchEvent::Deleted;
            else                                               we = WatchEvent::Modified;

            cb(fullpath, we);

            // If new directory, add watch
            if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR)) {
                addWatch(fullpath);
            }
        }
    }
}

} // namespace icmg::graph
#endif // __linux__
