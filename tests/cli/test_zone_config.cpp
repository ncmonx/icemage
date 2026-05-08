// Phase 31 T5 — zone config resolver order: zone.<X>.<key> > <key> > default.
#include "../test_main.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <set>

using nlohmann::json;

static std::string resolve(const json& j, const std::string& key,
                            const std::string& zone, const std::string& def) {
    if (!zone.empty()) {
        std::string z = "zone." + zone + "." + key;
        if (j.contains(z) && j[z].is_string()) return j[z].get<std::string>();
    }
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

TEST("zone config: zone-scoped beats global") {
    json j = {
        {"agent.command", "claude --print"},
        {"zone.api.agent.command", "ollama run mistral"}
    };
    ASSERT_EQ(resolve(j, "agent.command", "api", ""), std::string("ollama run mistral"));
}

TEST("zone config: missing zone falls back to global") {
    json j = {
        {"agent.command", "claude --print"},
        {"zone.api.agent.command", "ollama run mistral"}
    };
    ASSERT_EQ(resolve(j, "agent.command", "ui", ""), std::string("claude --print"));
}

TEST("zone config: missing both falls to default") {
    json j;
    ASSERT_EQ(resolve(j, "agent.command", "api", "fallback"), std::string("fallback"));
}

TEST("zone config: dotted key with zone prefix detection") {
    json j = {
        {"zone.api.agent.command", "x"},
        {"zone.api.agent.timeout", "60"},
        {"zone.ui.agent.command",  "y"},
        {"verbose",                "true"}
    };
    std::set<std::string> zones;
    for (auto& [k, v] : j.items()) {
        if (k.rfind("zone.", 0) != 0) continue;
        std::string rest = k.substr(5);
        auto dot = rest.find('.');
        if (dot != std::string::npos) zones.insert(rest.substr(0, dot));
    }
    ASSERT_EQ((int)zones.size(), 2);
    ASSERT_TRUE(zones.count("api") == 1);
    ASSERT_TRUE(zones.count("ui") == 1);
}

int main() {
    std::cout << "=== zone config tests ===\n";
    return icmg::test::run_all();
}
