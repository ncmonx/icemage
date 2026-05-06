#include "watcher_factory.hpp"

#if defined(_WIN32)
#  include "win_watcher.hpp"
#elif defined(__linux__)
#  include "linux_watcher.hpp"
#elif defined(__APPLE__)
#  include "mac_watcher.hpp"
#endif

namespace icmg::graph {

std::unique_ptr<BaseWatcher> createWatcher() {
#if defined(_WIN32)
    return std::make_unique<WinWatcher>();
#elif defined(__linux__)
    return std::make_unique<LinuxWatcher>();
#elif defined(__APPLE__)
    return std::make_unique<MacWatcher>();
#else
    return nullptr;  // unsupported platform
#endif
}

} // namespace icmg::graph
