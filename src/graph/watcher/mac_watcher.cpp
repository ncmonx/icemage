#if defined(__APPLE__)
#include "mac_watcher.hpp"
#include <filesystem>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace icmg::graph {

MacWatcher::MacWatcher() = default;
MacWatcher::~MacWatcher() { stop(); }

bool MacWatcher::addDir(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_EVTONLY);
    if (fd < 0) return false;

    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND, 0,
           (void*)path.c_str());
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    watched_fds_.push_back(fd);
    return true;
}

bool MacWatcher::start(const std::string& dir, Callback cb) {
    if (running_.load()) return false;

    kq_ = kqueue();
    if (kq_ < 0) return false;

    if (pipe(wake_pipe_) < 0) { close(kq_); kq_ = -1; return false; }

    // Add root dir and all subdirs
    std::error_code ec;
    addDir(dir);
    for (auto& entry : fs::recursive_directory_iterator(dir,
             fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec)) addDir(entry.path().string());
    }

    // Add wake pipe read end
    struct kevent wake_ev;
    EV_SET(&wake_ev, wake_pipe_[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(kq_, &wake_ev, 1, nullptr, 0, nullptr);

    running_.store(true);
    thread_ = std::thread([this, dir, cb]() { watchLoop(dir, cb); });
    return true;
}

void MacWatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (wake_pipe_[1] >= 0) write(wake_pipe_[1], "x", 1);
    if (thread_.joinable()) thread_.join();
    for (int fd : watched_fds_) close(fd);
    watched_fds_.clear();
    if (kq_ >= 0) { close(kq_); kq_ = -1; }
    if (wake_pipe_[0] >= 0) close(wake_pipe_[0]);
    if (wake_pipe_[1] >= 0) close(wake_pipe_[1]);
    wake_pipe_[0] = wake_pipe_[1] = -1;
}

void MacWatcher::watchLoop(const std::string& /*dir*/, Callback cb) {
    struct kevent events[32];
    struct timespec timeout = {0, 500000000}; // 500ms

    while (running_.load()) {
        int n = kevent(kq_, nullptr, 0, events, 32, &timeout);
        if (n <= 0) continue;

        for (int i = 0; i < n; ++i) {
            auto& ev = events[i];
            // Wake signal
            if ((int)ev.ident == wake_pipe_[0]) return;

            std::string path(reinterpret_cast<const char*>(ev.udata));
            WatchEvent we;
            if      (ev.fflags & NOTE_DELETE) we = WatchEvent::Deleted;
            else if (ev.fflags & NOTE_RENAME) we = WatchEvent::Modified;
            else                              we = WatchEvent::Modified;
            cb(path, we);
        }
    }
}

} // namespace icmg::graph
#endif // __APPLE__
