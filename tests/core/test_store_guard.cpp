// #292: secret / PII filter on every memory + knowledge write.
#include "../test_main.hpp"
#include "../../src/core/store_guard.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/hook_bus.hpp"
#include <string>
#include <vector>

using namespace icmg;

namespace {

constexpr const char* kAwsKey = "AKIAIOSFODNN7EXAMPLE";

bool hasType(const std::vector<core::SecretMatch>& v, const std::string& t) {
    for (const auto& m : v) if (m.type == t) return true;
    return false;
}

struct UserPatternsGuard {
    explicit UserPatternsGuard(const std::vector<std::string>& l) { core::setUserSecretPatternsForTest(l); }
    ~UserPatternsGuard() { core::setUserSecretPatternsForTest({}); }
};

}  // namespace

TEST("guard: parseSecretMode accepts the four modes, case-insensitive") {
    bool ok = false;
    ASSERT_TRUE(core::parseSecretMode("REJECT", &ok) == core::SecretMode::Reject && ok);
    ASSERT_TRUE(core::parseSecretMode("mask", &ok) == core::SecretMode::Mask && ok);
    ASSERT_TRUE(core::parseSecretMode("warn", &ok) == core::SecretMode::Warn && ok);
    ASSERT_TRUE(core::parseSecretMode("off", &ok) == core::SecretMode::Off && ok);
    (void)core::parseSecretMode("loud", &ok);
    ASSERT_TRUE(!ok);
}

TEST("guard: built-in and store-only patterns are detected") {
    ASSERT_TRUE(hasType(core::scanForStore(std::string("key ") + kAwsKey), "AWS_ACCESS_KEY"));
    ASSERT_TRUE(hasType(core::scanForStore("db password=Hunter2Secret!"), "PASSWORD"));
    ASSERT_TRUE(hasType(core::scanForStore("postgres://app:s3cr3tpw@db:5432/x"), "URL_CREDENTIALS"));
    ASSERT_TRUE(hasType(core::scanForStore("ssn 123-45-6789"), "SSN"));
    ASSERT_TRUE(hasType(core::scanForStore("card 4111 1111 1111 1111"), "CARD_NUMBER"));
}

TEST("guard: placeholders and Luhn-invalid numbers are not flagged") {
    ASSERT_TRUE(!hasType(core::scanForStore("password=<your-password>"), "PASSWORD"));
    ASSERT_TRUE(!hasType(core::scanForStore("password = ${DB_PASS}"), "PASSWORD"));
    ASSERT_TRUE(!hasType(core::scanForStore("order 4111 1111 1111 1112"), "CARD_NUMBER"));
    ASSERT_TRUE(core::scanForStore("plain note about the build cache").empty());
}

TEST("guard: mask replaces the match, reject throws, warn keeps text") {
    std::string a = std::string("token ") + kAwsKey;
    auto types = core::guardFields({&a}, core::SecretMode::Mask, "store");
    ASSERT_TRUE(!types.empty());
    ASSERT_TRUE(a.find(kAwsKey) == std::string::npos);
    ASSERT_TRUE(a.find("<REDACTED:") != std::string::npos);

    std::string b = std::string("token ") + kAwsKey;
    bool threw = false;
    try { core::guardFields({&b}, core::SecretMode::Reject, "store"); }
    catch (const core::SecretRejectedError&) { threw = true; }
    ASSERT_TRUE(threw);

    std::string c = std::string("token ") + kAwsKey;
    core::guardFields({&c}, core::SecretMode::Warn, "store");
    ASSERT_TRUE(c.find(kAwsKey) != std::string::npos);

    std::string d = std::string("token ") + kAwsKey;
    ASSERT_TRUE(core::guardFields({&d}, core::SecretMode::Off, "store").empty());
}

TEST("guard: user regex file rules (NAME=regex, comments)") {
    UserPatternsGuard g({"# national id", "NIK=\\b\\d{16}\\b", "", "EMP-[0-9]{5}"});
    auto m = core::scanForStore("nik 3201234567890123 and EMP-12345");
    ASSERT_TRUE(hasType(m, "NIK"));
    ASSERT_TRUE(m.size() >= 2);
}

TEST("guard: MemoryStore::store masks by default env") {
    core::Db db(":memory:");
    core::Migrator mig("__nonexistent_migrations_dir__");
    mig.runAll(db);
    imem::MemoryStore ms(db);
    imem::MemoryNode n;
    n.topic = "creds";
    n.content = std::string("deploy uses ") + kAwsKey;
    const int64_t id = ms.store(n, true);
    if (core::activeSecretMode() == core::SecretMode::Mask) {
        ASSERT_TRUE(ms.get(id).content.find(kAwsKey) == std::string::npos);
    }
}

// #294: a secret found in one field must not survive in a sibling field where
// the pattern context is lost (auto keywords: "password,sup3rrahasia,...").
// GitHub-token shaped probe, split so repo secret scanners do not flag the test.
const std::string kGhProbe = std::string("gh") + "p_" + "abcdefghijklmnopqrstuvwxyz0123456789";

TEST("guard: mask scrubs a secret's tokens from sibling fields (keywords)") {
    std::string content = "tes filter: password=Sup3rRahasia! token " + kGhProbe;
    std::string kw = "tes,filter,password,sup3rrahasia,token,ghp,abcdefghijklmnopqrstuvwxyz0123456789";
    core::guardFields({&content, &kw}, core::SecretMode::Mask, "store");
    ASSERT_TRUE(content.find("Sup3rRahasia") == std::string::npos);
    ASSERT_TRUE(kw.find("sup3rrahasia") == std::string::npos);
    ASSERT_TRUE(kw.find("abcdefghijklmnopqrstuvwxyz0123456789") == std::string::npos);
    ASSERT_TRUE(kw.find("filter") != std::string::npos);   // clean keywords kept
    ASSERT_TRUE(kw.find("password") != std::string::npos);
}

TEST("guard: keywordSourceText drops secret spans before keyword derivation") {
    const std::string raw = "tes filter: password=Sup3rRahasia! token " + kGhProbe;
    const std::string src = core::keywordSourceText(raw);
    ASSERT_TRUE(src.find("Sup3rRahasia") == std::string::npos);
    ASSERT_TRUE(src.find("abcdefghijklmnopqrstuvwxyz") == std::string::npos);
    ASSERT_TRUE(src.find("filter") != std::string::npos);
}

TEST("guard: MemoryStore keywords and update() never keep the secret") {
    core::Db db(":memory:");
    core::Migrator mig("__nonexistent_migrations_dir__");
    mig.runAll(db);
    imem::MemoryStore ms(db);
    if (core::activeSecretMode() != core::SecretMode::Mask) return;
    imem::MemoryNode n;
    n.topic = "probe";
    n.content = "tes filter: password=Sup3rRahasia! deploy";
    n.keywords = "tes,filter,password,sup3rrahasia";
    const int64_t id = ms.store(n, true);
    auto got = ms.get(id);
    ASSERT_TRUE(got.keywords.find("sup3rrahasia") == std::string::npos);
    ms.update(id, "rotated: password=An0therSecret9 ok", "rotated,an0thersecret9");
    got = ms.get(id);
    ASSERT_TRUE(got.content.find("An0therSecret9") == std::string::npos);
    ASSERT_TRUE(got.keywords.find("an0thersecret9") == std::string::npos);
}

// #294: PRE_STORE hooks derive side data (abbreviation auto-learn wrote
// "password -> <secret>" into the abbreviations table) -- they must only
// ever see the masked text.
TEST("guard: PRE_STORE hooks never see the raw secret") {
    if (core::activeSecretMode() != core::SecretMode::Mask) return;
    static bool armed = false;
    static std::string seen;
    static bool subscribed = false;
    if (!subscribed) {
        core::HookBus::instance().subscribe(core::HookEvent::PRE_STORE,
            [](core::HookContext& ctx) { if (armed) seen += ctx.get<std::string>("content", ""); }, 1);
        subscribed = true;
    }
    core::Db db(":memory:");
    core::Migrator mig("__nonexistent_migrations_dir__");
    mig.runAll(db);
    imem::MemoryStore ms(db);
    imem::MemoryNode n;
    n.topic = "probe-hook";
    n.content = "hook probe: password=Hk0okSecret77 end";
    armed = true;
    seen.clear();
    ms.store(n, true);
    armed = false;
    ASSERT_TRUE(!seen.empty());
    ASSERT_TRUE(seen.find("Hk0okSecret77") == std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
