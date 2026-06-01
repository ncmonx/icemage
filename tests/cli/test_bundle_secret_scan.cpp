// T6 (v1.4.0): test that bundle/pack cmd wires core::secret_scanner correctly.
// Tests exercise the lib directly (no full pack pipeline needed).
// NOTE: "secret" strings below are fake test values — not real credentials.
#include "../test_main.hpp"
#include "../../src/core/secret_scanner.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/registry.hpp"
#include <string>

using namespace icmg::core;

// Build fake-but-pattern-matching strings at runtime to avoid static analysis.

// sk-ant-[a-zA-Z0-9\-_]{40,}
static std::string fakeAntKey() {
    std::string s = "sk-ant-api03-";
    for (int i = 0; i < 40; ++i) s += (char)('A' + (i % 26));
    return s;
}

// ---- Test 1: scan returns matches for sk-ant key ----------------------------

TEST("bundle_secret_scan: scan returns matches for sk-ant key") {
    std::string key = fakeAntKey();
    std::string text = "ANTHROPIC_API_KEY=" + key;
    auto matches = scanSecrets(text);
    bool found = false;
    for (auto& m : matches) {
        if (m.type == "ANTHROPIC_KEY") found = true;
    }
    ASSERT_TRUE(found);
    // Must be at least 1 match
    ASSERT_TRUE((int)matches.size() >= 1);
}

// ---- Test 2: redact replaces match with placeholder ------------------------

TEST("bundle_secret_scan: redact replaces sk-ant key with placeholder") {
    std::string key = fakeAntKey();
    std::string text = "key=" + key + " end";
    auto matches = scanSecrets(text);
    // Must detect something
    ASSERT_TRUE((int)matches.size() >= 1);
    std::string redacted = redactSecrets(text, matches);
    ASSERT_CONTAINS(redacted, "<REDACTED:ANTHROPIC_KEY>");
    // Original key prefix must be gone
    ASSERT_NOT_CONTAINS(redacted, std::string("sk-ant-"));
}

// ---- Test 3: redact preserves non-secret content ---------------------------

TEST("bundle_secret_scan: redact preserves non-secret content") {
    std::string text = "hello world";
    auto matches = scanSecrets(text);
    ASSERT_EQ((int)matches.size(), 0);
    std::string redacted = redactSecrets(text, matches);
    ASSERT_EQ(redacted, text);
}

// ---- Test 4: pack cmd is registered in the command registry ----------------

TEST("bundle_secret_scan: pack cmd is registered") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("pack");
    ASSERT_TRUE(cmd != nullptr);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
