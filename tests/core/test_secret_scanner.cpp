// Test T15a: secret_scanner — detect and redact common secrets in text.
// NOTE: "secret" values below are intentionally fake/test strings used to
// verify detection logic; they are not real credentials.
#include "../test_main.hpp"
#include "../../src/core/secret_scanner.hpp"
#include <string>

using namespace icmg::core;

// Build fake-but-pattern-matching strings at runtime to avoid static analysis.
// AKIA[0-9A-Z]{16} → 20 chars total
static std::string fakeAwsKey() {
    // AKIA + 16 uppercase/digits
    return std::string("AKIA") + "IOSFODNN7EXAMPLE";
}

// sk-ant-[a-zA-Z0-9-_]{40,}
static std::string fakeAntKey() {
    std::string s = "sk-ant-api03-";
    for (int i = 0; i < 40; ++i) s += (char)('A' + (i % 26));
    return s;
}

// github_pat_ + 82 alphanums
static std::string fakeGhPat() {
    std::string s = "github_pat_";
    for (int i = 0; i < 82; ++i) s += (char)('a' + (i % 26));
    return s;
}

// Slack token: xoxb-<10+ chars>
static std::string fakeSlackToken() {
    return std::string("xoxb-") + "1234567890" + "-" + "ABCDEFGHIJKLMNO";
}

// ---- Test 1: empty text → no matches ----------------------------------------

TEST("secret_scanner: empty text yields no matches") {
    auto matches = scanSecrets("");
    ASSERT_EQ((int)matches.size(), 0);
}

// ---- Test 2: AWS access key → 1 match with type AWS_ACCESS_KEY --------------

TEST("secret_scanner: AKIA key detected") {
    std::string text = "export AWS_ACCESS_KEY_ID=" + fakeAwsKey();
    auto matches = scanSecrets(text);
    ASSERT_EQ((int)matches.size(), 1);
    ASSERT_EQ(matches[0].type, std::string("AWS_ACCESS_KEY"));
}

// ---- Test 3: redactSecrets strips key and inserts placeholder ---------------

TEST("secret_scanner: redact replaces AKIA key with placeholder") {
    std::string key = fakeAwsKey();
    std::string text = "key=" + key + " end";
    auto matches = scanSecrets(text);
    ASSERT_EQ((int)matches.size(), 1);
    std::string redacted = redactSecrets(text, matches);
    ASSERT_CONTAINS(redacted, "<REDACTED:AWS_ACCESS_KEY>");
    ASSERT_NOT_CONTAINS(redacted, key);
}

// ---- Test 4: Anthropic key detected -----------------------------------------

TEST("secret_scanner: anthropic key detected") {
    std::string text = "key=" + fakeAntKey();
    auto matches = scanSecrets(text);
    bool found = false;
    for (auto& m : matches) if (m.type == "ANTHROPIC_KEY") found = true;
    ASSERT_TRUE(found);
}

// ---- Test 5a: multiple different secrets in same text → ≥2 matches ----------

TEST("secret_scanner: multiple different secrets in same text") {
    std::string text = "aws=" + fakeAwsKey() + " ant=" + fakeAntKey();
    auto matches = scanSecrets(text);
    ASSERT_TRUE((int)matches.size() >= 2);
}

// ---- Test 5b: false-positive guard — sk-ant-x too short → no anthropic ------

TEST("secret_scanner: sk-ant-x too short yields no anthropic match") {
    std::string text = "sk-ant-x";
    auto matches = scanSecrets(text);
    bool found_ant = false;
    for (auto& m : matches) if (m.type == "ANTHROPIC_KEY") found_ant = true;
    ASSERT_FALSE(found_ant);
}

// ---- Extra: GitHub PAT detection --------------------------------------------

TEST("secret_scanner: github PAT detected") {
    std::string text = "token=" + fakeGhPat();
    auto matches = scanSecrets(text);
    bool found = false;
    for (auto& m : matches) if (m.type == "GITHUB_TOKEN") found = true;
    ASSERT_TRUE(found);
}

// ---- Extra: Slack token detection -------------------------------------------

TEST("secret_scanner: slack token detected") {
    std::string text = "slack=" + fakeSlackToken();
    auto matches = scanSecrets(text);
    bool found = false;
    for (auto& m : matches) if (m.type == "SLACK_TOKEN") found = true;
    ASSERT_TRUE(found);
}

// ---- Extra: redact multiple secrets -----------------------------------------

TEST("secret_scanner: redact removes all detected secrets") {
    std::string ak = fakeAwsKey();
    std::string antk = fakeAntKey();
    std::string text = "key=" + ak + " token=" + antk;
    auto matches = scanSecrets(text);
    std::string redacted = redactSecrets(text, matches);
    ASSERT_NOT_CONTAINS(redacted, ak);
    ASSERT_NOT_CONTAINS(redacted, antk);
}

int main() { return icmg::test::run_all(); }
