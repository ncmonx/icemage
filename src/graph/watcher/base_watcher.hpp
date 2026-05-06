#pragma once
#include <functional>
#include <string>

namespace icmg::graph {

enum class WatchEvent { Created, Modified, Deleted };

class BaseWatcher {
public:
    virtual ~BaseWatcher() = default;
    using Callback = std::function<void(const std::string& path, WatchEvent event)>;

    virtual bool start(const std::string& dir, Callback cb) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

} // namespace icmg::graph
