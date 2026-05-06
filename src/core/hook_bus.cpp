#include "hook_bus.hpp"
#include <algorithm>

namespace icmg::core {

HookBus& HookBus::instance() {
    static HookBus inst;
    return inst;
}

void HookBus::subscribe(HookEvent event, HookFn fn, int priority) {
    auto& vec = handlers_[static_cast<int>(event)];
    vec.push_back({priority, std::move(fn)});
    // stable_sort: deterministic order for equal priorities
    std::stable_sort(vec.begin(), vec.end(),
        [](const Entry& a, const Entry& b) { return a.priority < b.priority; });
}

void HookBus::emit(HookEvent event, HookContext& ctx) {
    auto it = handlers_.find(static_cast<int>(event));
    if (it == handlers_.end()) return;
    for (auto& entry : it->second) {
        if (ctx.cancelled) break;
        entry.fn(ctx);
    }
}

} // namespace icmg::core
