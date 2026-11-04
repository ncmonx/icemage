// 2026-11-03 TDD: BM25 word tokenizer, default (ICMG_BM25_TOKENIZER=legacy reverts).
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/core/migrator.hpp"
#include "../../src/imem/bm25_tokenize.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/scorer.hpp"
#include <algorithm>
#include <cstdlib>

using namespace icmg::imem;

namespace {
void setEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}
bool has(const std::vector<std::string>& v, const std::string& t) {
    return std::find(v.begin(), v.end(), t) != v.end();
}
}  // namespace

TEST("bm25-tokenize: splits on punctuation inside tokens and lowercases") {
    auto t = wordTokenize("I'd love the Corgi's new trick: roll-dance, 2x!");
    ASSERT_TRUE(has(t, "corgi"));
    ASSERT_TRUE(has(t, "roll"));
    ASSERT_TRUE(has(t, "dance"));
    ASSERT_TRUE(has(t, "trick"));
    ASSERT_TRUE(has(t, "2x"));
    ASSERT_FALSE(has(t, "corgi's"));
    ASSERT_FALSE(has(t, "roll-dance"));
}

TEST("bm25-tokenize: drops stopwords and single-char tokens") {
    auto t = wordTokenize("What is the name of my dog");
    ASSERT_FALSE(has(t, "what"));
    ASSERT_FALSE(has(t, "the"));
    ASSERT_FALSE(has(t, "my"));
    ASSERT_TRUE(has(t, "name"));
    ASSERT_TRUE(has(t, "dog"));
    ASSERT_TRUE(isBm25Stopword("the"));
    ASSERT_FALSE(isBm25Stopword("corgi"));
}

TEST("bm25-tokenize: keeps UTF-8 words whole") {
    auto t = wordTokenize("kucing café");
    ASSERT_TRUE(has(t, "kucing"));
    ASSERT_TRUE(has(t, "caf\xC3\xA9"));
}

TEST("bm25-tokenize: word tokenizer is the default, legacy only via env") {
    setEnv(kBm25TokenizerEnv, nullptr);
    ASSERT_TRUE(bm25WordTokenizerFromEnv());
    icmg::core::Db db(":memory:");
    icmg::core::Migrator mig("__nonexistent_migrations_dir__");  // embedded path
    mig.runAll(db);
    MemoryStore store(db);
    MemoryNode a; a.topic = "pets"; a.content = "Biscuit's favourite trick is roll-dance";
    store.store(a, /*force=*/true);
    Scorer::instance().invalidate();
    // default (word): "roll" matches inside "roll-dance"
    auto r = store.recallAsOf("roll", 4000000000LL, 5);
    ASSERT_FALSE(r.empty());
    ASSERT_CONTAINS(r[0].content, "roll-dance");

    setEnv(kBm25TokenizerEnv, kBm25TokenizerLegacy);
    ASSERT_FALSE(bm25WordTokenizerFromEnv());
    Scorer::instance().invalidate();
    // legacy: "roll-dance" is one token, so "roll" does not match
    bool legacyEmpty = store.recallAsOf("roll", 4000000000LL, 5).empty();
    setEnv(kBm25TokenizerEnv, nullptr);
    Scorer::instance().invalidate();
    ASSERT_TRUE(legacyEmpty);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
