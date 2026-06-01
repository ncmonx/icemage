#pragma once
#if defined(__APPLE__)

#include "base_watcher.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <string>

namespace icmg::graph {

class MacWatcher : public BaseWatcher {
public:
    MacWatcher();
    ~MacWatcher() override;

    bool start(const std::string& dir, Callback cb) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    std::atomic<bool>    running_{false};
    std::thread          thread_;
    int                  kq_      = -1;
    std::vector<int>     watched_fds_;
    int                  wake_pipe_[2] = {-1, -1};

    void watchLoop(const std::string& dir, Callback cb);
    bool addDir(const std::string& path);
};

} // namespace icmg::graph
#endif // __APPLE__
