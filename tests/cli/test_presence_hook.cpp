// me-everywhere heartbeat hook template — baked into `icmg init`.
// Verifies the script body + the settings registration command are well-formed,
// identity-agnostic, and that registering it into a UserPromptSubmit array is
// idempotent (no duplicate on re-init).
#include "../test_main.hpp"
#include "../../src/cli/presence_hook.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <cctype>

using nlohmann::json;
using namespace icmg::cli;

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

TEST("presence_hook: script body has the essential pipeline") {
    std::string sh = PRESENCE_HEARTBEAT_SH;
    ASSERT_TRUE(!sh.empty());
    ASSERT_TRUE(sh.find("icmg presence sync") != std::string::npos);
    ASSERT_TRUE(sh.find("hookio get session_id") != std::string::npos);
    ASSERT_TRUE(sh.find("hookio emit UserPromptSubmit") != std::string::npos);
    // dependency guard so it no-ops cleanly without icmg on PATH.
    ASSERT_TRUE(sh.find("command -v icmg") != std::string::npos);
    // pid fallback when host gives no session_id.
    ASSERT_TRUE(sh.find("sess-$$") != std::string::npos);
}

TEST("presence_hook: identity-agnostic — no hardcoded persona/user names") {
    std::string s = lower(PRESENCE_HEARTBEAT_SH);
    // Embedded hooks must not leak a specific identity (clients differ).
    ASSERT_TRUE(s.find("acme") == std::string::npos);
    ASSERT_TRUE(s.find("alice") == std::string::npos);
    ASSERT_TRUE(s.find("bob")  == std::string::npos);
}

TEST("presence_hook: registration command guards on script presence") {
    std::string cmd = presenceHeartbeatHookCmd();
    ASSERT_TRUE(cmd.find("icmg-presence-heartbeat.sh") != std::string::npos);
    ASSERT_TRUE(cmd.find("[ -f ") != std::string::npos);   // file-exists guard
    ASSERT_TRUE(cmd.find("|| exit 0") != std::string::npos); // fast no-op
}

// Replicates the init UserPromptSubmit append + dedup so re-init never doubles it.
static void registerHeartbeat(json& ups) {
    const std::string cmd = presenceHeartbeatHookCmd();
    for (auto& block : ups)
        for (auto& h : block.value("hooks", json::array()))
            if (h.value("command", "") == cmd) return;  // already present
    ups.push_back({{"hooks", json::array({
        {{"type", "command"}, {"timeout", 5}, {"command", cmd}}
    })}});
}

TEST("presence_hook: register is idempotent across re-init") {
    json ups = json::array();
    registerHeartbeat(ups);
    registerHeartbeat(ups);  // second init must not duplicate

    int count = 0;
    for (auto& block : ups)
        for (auto& h : block.value("hooks", json::array()))
            if (h.value("command", "") == presenceHeartbeatHookCmd()) ++count;
    ASSERT_EQ(count, 1);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
