#pragma once
#include "base_watcher.hpp"
#include <memory>

namespace icmg::graph {

/// Returns the platform-appropriate watcher.
std::unique_ptr<BaseWatcher> createWatcher();

} // namespace icmg::graph
