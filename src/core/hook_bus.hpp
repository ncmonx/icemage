#pragma once
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <any>

namespace icmg::core {

enum class HookEvent {
    PRE_STORE,
    POST_STORE,
    PRE_RECALL,
    POST_RECALL,
    PRE_GRAPH_SCAN,
    POST_GRAPH_SCAN,
    PRE_SP_ADD,
    POST_SP_ADD,
    PRE_RUN_CMD,
    POST_RUN_CMD,
};

// Flexible context passed to hooks: string key → any value
struct HookContext {
    std::unordered_map<std::string, std::any> data;

    template<typename T>
    T get(const std::string& key, T def = T{}) const {
        auto it = data.find(key);
        if (it == data.end()) return def;
        try { return std::any_cast<T>(it->second); }
        catch (...) { return def; }
    }

    template<typename T>
    void set(const std::string& key, T val) {
        data[key] = std::move(val);
    }

    bool cancelled = false; // hook can set this to abort operation
};

using HookFn = std::function<void(HookContext&)>;

class HookBus {
public:
    static HookBus& instance();

    // Register handler for event at given priority (lower = earlier).
    void subscribe(HookEvent event, HookFn fn, int priority = 0);

    // Emit event: calls handlers in stable priority order.
    void emit(HookEvent event, HookContext& ctx);

private:
    struct Entry {
        int     priority;
        HookFn  fn;
    };
    std::unordered_map<int, std::vector<Entry>> handlers_; // event int → sorted entries
};

} // namespace icmg::core

// ICMG_REGISTER_HOOK(HookEvent::PRE_STORE, myHandler, 10)
#define ICMG_REGISTER_HOOK(event, fn, priority) \
    static bool _reg_hook_##fn = []() { \
        ::icmg::core::HookBus::instance().subscribe(event, fn, priority); \
        return true; \
    }()
