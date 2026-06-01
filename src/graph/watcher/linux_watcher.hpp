#pragma once
#if defined(__linux__)

#include "base_watcher.hpp"
#include <atomic>
#include <thread>
#include <unordered_map>

namespace icmg::graph {

class LinuxWatcher : public BaseWatcher {
public:
    LinuxWatcher();
    ~LinuxWatcher() override;

    bool start(const std::string& dir, Callback cb) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    std::atomic<bool>                    running_{false};
    std::thread                          thread_;
    int                                  ifd_ = -1;   // inotify fd
    int                                  pipe_[2] = {-1, -1}; // wake pipe
    std::unordered_map<int, std::string> wd2path_;    // watch descriptor → path

    bool addWatch(const std::string& path);
    void watchLoop(Callback cb);
};

} // namespace icmg::graph
#endif // __linux__
