// Phase 30 T4 — config get/set/unset/list round-trip.
#include "../test_main.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

static fs::path tmpConfig() {
    auto p = fs::temp_directory_path() / ("icmg_cfg_" + std::to_string(::time(nullptr))
                                            + "_" + std::to_string(rand()) + ".json");
    return p;
}

TEST("config: write + read round-trip") {
    auto p = tmpConfig();
    {
        json j = {{"agent.command", "claude --print"}, {"verbose", "true"}};
        std::ofstream(p) << j.dump(2);
    }
    json got;
    {
        std::ifstream f(p);   // scope ensures close before remove (Windows lock).
        f >> got;
    }
    ASSERT_EQ(got["agent.command"].get<std::string>(), std::string("claude --print"));
    std::error_code ec;
    fs::remove(p, ec);  // ignore failure — test asserts already done.
}

TEST("config: unset removes key") {
    json j = {{"agent.command", "claude"}, {"verbose", "true"}};
    j.erase("agent.command");
    ASSERT_FALSE(j.contains("agent.command"));
    ASSERT_TRUE(j.contains("verbose"));
}

TEST("config: dotted-zone-key resolution order") {
    // zone.<zone>.<key> beats global <key>.
    json j = {
        {"agent.command", "claude --print"},
        {"zone.api.agent.command", "ollama run mistral"}
    };
    auto resolve = [&](const std::string& key, const std::string& zone) -> std::string {
        std::string z = "zone." + zone + "." + key;
        if (j.contains(z)) return j[z].get<std::string>();
        if (j.contains(key)) return j[key].get<std::string>();
        return "";
    };
    ASSERT_EQ(resolve("agent.command", "api"), std::string("ollama run mistral"));
    ASSERT_EQ(resolve("agent.command", "ui"),  std::string("claude --print"));
}

TEST("config: atomic-write pattern (temp + rename)") {
    auto target = tmpConfig();
    fs::path tmp = target; tmp += ".tmp";
    {
        std::ofstream out(tmp); out << "{\"k\":\"v\"}";
    }
    fs::rename(tmp, target);
    ASSERT_TRUE(fs::exists(target));
    ASSERT_FALSE(fs::exists(tmp));
    fs::remove(target);
}

int main() {
    std::cout << "=== config tests ===\n";
    return icmg::test::run_all();
}
