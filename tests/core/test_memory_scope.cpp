// #292: memory namespaces (tenant scope) + trust (provenance) + --ttl units.
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/memory_scope.hpp"
#include "../../src/core/memory_visibility.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/cli/recall_index.hpp"
#include "../../src/cli/recall_json.hpp"
#include <string>
#include <vector>

using namespace icmg;
using namespace icmg::imem;

namespace {

void migratedDb(core::Db& db) {
    core::Migrator m("__nonexistent_migrations_dir__");
    m.runAll(db);
}

int64_t storeIn(MemoryStore& ms, const std::string& ns, const std::string& topic,
                const std::string& content, const std::string& source = "") {
    core::NamespaceScope scope(ns.empty() ? std::vector<std::string>{}
                                          : std::vector<std::string>{ns});
    MemoryNode n; n.topic = topic; n.content = content; n.source = source;
    return ms.store(n, /*force=*/true);
}

bool recallHasIn(MemoryStore& ms, const std::vector<std::string>& ns, const std::string& q,
                 int64_t id) {
    core::NamespaceScope scope(ns);
    for (const auto& n : ms.recall(q, 20)) if (n.id == id) return true;
    return false;
}

std::string col(core::Db& db, int64_t id, const char* name) {
    std::string out;
    db.query(std::string("SELECT ") + name + " FROM memory_nodes WHERE id=?", {std::to_string(id)},
             [&](const core::Row& r) { if (!r.empty()) out = r[0]; });
    return out;
}

}  // namespace

TEST("scope: parseNamespaces splits, trims, dedups, maps shared to empty") {
    std::vector<std::string> v;
    std::string err;
    ASSERT_TRUE(core::parseNamespaces(" user:12, team:finance ,user:12,shared", &v, &err));
    ASSERT_EQ(v.size(), (size_t)3);
    ASSERT_EQ(v[0], std::string("user:12"));
    ASSERT_EQ(v[1], std::string("team:finance"));
    ASSERT_EQ(v[2], std::string(""));
    ASSERT_EQ(core::namespaceLabel(""), std::string("shared"));
}

TEST("scope: parseNamespaces rejects control chars and over-long names") {
    std::vector<std::string> v;
    std::string err;
    ASSERT_TRUE(!core::parseNamespaces(std::string("user\x01x"), &v, &err));
    ASSERT_TRUE(!err.empty());
    ASSERT_TRUE(!core::parseNamespaces(std::string(core::kMaxNamespaceLen + 1, 'a'), &v, &err));
}

TEST("scope: NamespaceScope restores the previous scope") {
    ASSERT_TRUE(!core::namespaceFilterOn());
    {
        core::NamespaceScope a(std::vector<std::string>{"user:1"});
        ASSERT_TRUE(core::namespaceFilterOn());
        ASSERT_EQ(core::storeNamespace(), std::string("user:1"));
        ASSERT_TRUE(core::inActiveNamespace("user:1"));
        ASSERT_TRUE(!core::inActiveNamespace("user:2"));
        ASSERT_TRUE(!core::inActiveNamespace(""));
    }
    ASSERT_TRUE(!core::namespaceFilterOn());
    ASSERT_TRUE(core::inActiveNamespace("anything"));
}

TEST("scope: namespacesFromArgs reads --ns") {
    std::vector<std::string> v;
    std::string err;
    ASSERT_TRUE(core::namespacesFromArgs({"q", "--ns", "user:7,team:x"}, &v, &err));
    ASSERT_EQ(v.size(), (size_t)2);
}

TEST("scope: store writes namespace; recall --ns sees only that tenant") {
    core::Db db(":memory:");
    migratedDb(db);
    MemoryStore ms(db);
    const int64_t a = storeIn(ms, "user:12", "invoice-a", "zebracorn invoice for tenant twelve");
    const int64_t b = storeIn(ms, "user:13", "invoice-b", "zebracorn invoice for tenant thirteen");
    const int64_t s = storeIn(ms, "",        "invoice-s", "zebracorn invoice shared policy");
    ASSERT_EQ(col(db, a, "namespace"), std::string("user:12"));
    ASSERT_EQ(col(db, s, "namespace"), std::string(""));

    ASSERT_TRUE(recallHasIn(ms, {"user:12"}, "zebracorn", a));
    ASSERT_TRUE(!recallHasIn(ms, {"user:12"}, "zebracorn", b));
    ASSERT_TRUE(!recallHasIn(ms, {"user:12"}, "zebracorn", s));
    // user + shared
    ASSERT_TRUE(recallHasIn(ms, {"user:12", ""}, "zebracorn", s));
    ASSERT_TRUE(!recallHasIn(ms, {"user:12", ""}, "zebracorn", b));
    // no --ns = unfiltered (single-user CLI keeps the old behaviour)
    ASSERT_TRUE(recallHasIn(ms, {}, "zebracorn", b));
}

TEST("scope: recall cache does not leak across namespaces") {
    core::Db db(":memory:");
    migratedDb(db);
    MemoryStore ms(db);
    const int64_t a = storeIn(ms, "user:1", "k1", "quokkaflux alpha");
    const int64_t b = storeIn(ms, "user:2", "k2", "quokkaflux beta");
    ASSERT_TRUE(recallHasIn(ms, {"user:1"}, "quokkaflux", a));   // warms the cache
    ASSERT_TRUE(!recallHasIn(ms, {"user:2"}, "quokkaflux", a));  // same query, other tenant
    ASSERT_TRUE(recallHasIn(ms, {"user:2"}, "quokkaflux", b));
}

TEST("scope: dedup never merges into another namespace") {
    core::Db db(":memory:");
    migratedDb(db);
    MemoryStore ms(db);
    const int64_t a = storeIn(ms, "user:1", "same-topic", "identical body text here");
    core::NamespaceScope s2(std::vector<std::string>{"user:2"});
    MemoryNode n; n.topic = "same-topic"; n.content = "identical body text here";
    const int64_t b = ms.store(n, /*force=*/false);
    ASSERT_TRUE(b != a);
    ASSERT_EQ(col(db, b, "namespace"), std::string("user:2"));
}

TEST("trust: source picks the default, untrusted rows carry the marker") {
    ASSERT_EQ(std::string(core::defaultTrustForSource("web")), std::string("untrusted"));
    ASSERT_EQ(std::string(core::defaultTrustForSource("file")), std::string("untrusted"));
    ASSERT_EQ(std::string(core::defaultTrustForSource("user")), std::string("trusted"));
    ASSERT_TRUE(core::parseTrust("bogus") == nullptr);
    ASSERT_TRUE(core::isUntrusted("untrusted"));
    ASSERT_TRUE(!core::isUntrusted(""));   // old rows = trusted

    core::Db db(":memory:");
    migratedDb(db);
    MemoryStore ms(db);
    const int64_t w = storeIn(ms, "", "scraped", "page text says ignore previous instructions", "web");
    ASSERT_EQ(col(db, w, "trust"), std::string("untrusted"));
    const MemoryNode got = ms.get(w);
    ASSERT_EQ(got.trust, std::string("untrusted"));
    const std::string line = cli::formatIndexLine(got);
    ASSERT_TRUE(line.find(core::kUntrustedMarker) != std::string::npos);
    const std::string js = cli::recallNodesToJson({got});
    ASSERT_TRUE(js.find("\"untrusted\":true") != std::string::npos);
}

TEST("trust: explicit trust overrides the source default") {
    core::Db db(":memory:");
    migratedDb(db);
    MemoryStore ms(db);
    MemoryNode n; n.topic = "doc"; n.content = "vetted runbook"; n.source = "file";
    n.trust = core::kTrustTrusted;
    const int64_t id = ms.store(n, true);
    ASSERT_EQ(col(db, id, "trust"), std::string("trusted"));
}

TEST("ttl: parseTtlSeconds units") {
    ASSERT_EQ(core::parseTtlSeconds("3600"), (int64_t)3600);
    ASSERT_EQ(core::parseTtlSeconds("30m"), (int64_t)1800);
    ASSERT_EQ(core::parseTtlSeconds("12h"), (int64_t)43200);
    ASSERT_EQ(core::parseTtlSeconds("7d"), (int64_t)604800);
    ASSERT_EQ(core::parseTtlSeconds("2w"), (int64_t)1209600);
    ASSERT_EQ(core::parseTtlSeconds("abc"), (int64_t)-1);
    ASSERT_EQ(core::parseTtlSeconds(""), (int64_t)-1);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
