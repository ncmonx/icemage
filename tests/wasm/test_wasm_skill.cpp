// 2026-06-07: WASM skill manifest + capability model (pure helpers).
#include "../test_main.hpp"
#include "../../src/wasm/wasm_skill.hpp"
#include "../../src/wasm/wasm_registry.hpp"
using namespace icmg::wasm;

TEST("wasm_skill: parse valid manifest") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"strip","kind":"tkil-filter","match":"acme",
        "wasm":"skills/strip.wasm","abi":"filter-v1","capabilities":[],"sha256":"ab12"})", err);
    ASSERT_TRUE(s.has_value());
    ASSERT_EQ(s->name, std::string("strip"));
    ASSERT_EQ(s->match, std::string("acme"));
    ASSERT_EQ(s->abi, std::string("filter-v1"));
    ASSERT_EQ(s->sha256, std::string("ab12"));
    ASSERT_TRUE(s->caps.empty());
}

TEST("wasm_skill: missing required field -> nullopt + err") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"x","kind":"tkil-filter"})", err);
    ASSERT_FALSE(s.has_value());
    ASSERT_TRUE(!err.empty());
}

TEST("wasm_skill: unknown abi rejected") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"x","kind":"tkil-filter","match":"a",
        "wasm":"x.wasm","abi":"filter-v999","sha256":"00"})", err);
    ASSERT_FALSE(s.has_value());
    ASSERT_CONTAINS(err, "abi");
}

TEST("wasm_skill: bad json -> nullopt") {
    std::string err;
    ASSERT_FALSE(parseSkillManifest("{not json", err).has_value());
}

TEST("wasm_skill: grantedCaps = declared INTERSECT allowlist") {
    ASSERT_EQ(grantedCaps({"read_memory","read_graph"}, {"read_memory"}).size(), (size_t)1);
    ASSERT_EQ(grantedCaps({"read_memory"}, {}).size(), (size_t)0);            // empty allowlist denies all
    ASSERT_EQ(grantedCaps({"evil_cap"}, {"read_memory"}).size(), (size_t)0);  // unknown dropped
    ASSERT_EQ(grantedCaps({}, {"read_memory"}).size(), (size_t)0);
}

TEST("wasm_registry: matchesCommand substring") {
    ASSERT_TRUE(matchesCommand("acme", "acme-tool --x"));
    ASSERT_TRUE(matchesCommand("acme-tool", "run acme-tool now"));
    ASSERT_FALSE(matchesCommand("acme", "other-tool"));
    ASSERT_FALSE(matchesCommand("", "anything"));
}
