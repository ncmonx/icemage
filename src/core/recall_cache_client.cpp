#include "recall_cache_client.hpp"
#include "json_safe.hpp"
#include "../daemon/rule_daemon_client.hpp"
#include <nlohmann/json.hpp>

namespace icmg { namespace core {

std::optional<std::string> rcacheDaemonGet(const std::string& key) {
    std::string out;
    // callHook returns false on daemon-down; writes the "emit" field into out.
    if (icmg::daemon::RuleDaemonClient::callHook("RCACHE_GET", key, &out) && !out.empty())
        return out;
    return std::nullopt;   // miss or daemon down -> caller computes locally
}

void rcacheDaemonPut(const std::string& key, const std::string& value) {
    nlohmann::json j; j["key"] = key; j["value"] = value;
    (void)icmg::daemon::RuleDaemonClient::callHook("RCACHE_PUT", safeDump(j), nullptr);
}

void rcacheDaemonFlush() {
    (void)icmg::daemon::RuleDaemonClient::callHook("RCACHE_FLUSH", "", nullptr);
}

}} // namespace
