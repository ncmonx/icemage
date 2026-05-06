#pragma once
#if defined(_WIN32)

#include "base_watcher.hpp"
#include <atomic>
#include <thread>
#include <string>

namespace icmg::graph {

class WinWatcher : public BaseWatcher {
public:
    WinWatcher();
    ~WinWatcher() override;

    bool start(const std::string& dir, Callback cb) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    std::atomic<bool>  running_{false};
    std::thread        thread_;
    void*              dir_handle_ = nullptr;  // HANDLE
    void               watchLoop(const std::string& dir, Callback cb);
};

} // namespace icmg::graph

#endif // _WIN32
