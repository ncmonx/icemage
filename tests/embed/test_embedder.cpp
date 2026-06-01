// Phase 24: unit tests for embedder math + EmbedStore.
// Exercises pack/unpack, cosine, fnv1a64, BLOB round-trip via raw sqlite.
#include "../test_main.hpp"
#include "../../src/embed/embedder.hpp"
#include "../../src/embed/embed_store.hpp"
#include "../../src/core/db.hpp"
#include <cmath>

using namespace icmg::embed;

TEST("embed: pack + unpack round-trip preserves values") {
    std::vector<float> v = {0.5f, -1.0f, 0.0f, 3.14159f, -0.001f};
    auto blob = packVec(v);
    ASSERT_EQ(blob.size(), v.size() * sizeof(float));
    auto back = unpackVec(blob, (int)v.size());
    ASSERT_EQ(back.size(), v.size());
    for (size_t i = 0; i < v.size(); ++i) ASSERT_TRUE(back[i] == v[i]);
}

TEST("embed: unpack returns zeroed vec on undersized blob") {
    std::vector<uint8_t> blob(4, 0xAB); // only 4 bytes
    auto v = unpackVec(blob, 8);        // ask for 8 floats = 32 bytes
    ASSERT_EQ(v.size(), (size_t)8);
    for (float f : v) ASSERT_TRUE(f == 0.0f);
}

TEST("embed: cosine identical vectors = 1.0") {
    std::vector<float> a = {1, 2, 3, 4};
    ASSERT_TRUE(std::abs(cosine(a, a) - 1.0f) < 1e-5f);
}

TEST("embed: cosine orthogonal vectors = 0") {
    std::vector<float> a = {1, 0, 0};
    std::vector<float> b = {0, 1, 0};
    ASSERT_TRUE(std::abs(cosine(a, b)) < 1e-5f);
}

TEST("embed: cosine opposite vectors = -1") {
    std::vector<float> a = {1, 2, 3};
    std::vector<float> b = {-1, -2, -3};
    ASSERT_TRUE(std::abs(cosine(a, b) + 1.0f) < 1e-5f);
}

TEST("embed: cosine empty/mismatched returns 0") {
    std::vector<float> a = {1, 2};
    std::vector<float> b = {1, 2, 3};
    ASSERT_TRUE(cosine(a, b) == 0.0f);
    std::vector<float> empty;
    ASSERT_TRUE(cosine(empty, a) == 0.0f);
}

TEST("embed: cosine zero-norm returns 0 (no NaN)") {
    std::vector<float> z(4, 0.0f);
    std::vector<float> a = {1, 1, 1, 1};
    float c = cosine(z, a);
    ASSERT_TRUE(c == 0.0f);
    ASSERT_TRUE(!std::isnan(c));
}

TEST("embed: fnv1a64 deterministic + sensitive to input") {
    auto h1 = fnv1a64("hello");
    auto h2 = fnv1a64("hello");
    auto h3 = fnv1a64("world");
    ASSERT_EQ(h1, h2);
    ASSERT_TRUE(h1 != h3);
    ASSERT_EQ(h1.size(), (size_t)16);   // 64-bit hex
}

TEST("embed_store: put + get BLOB round-trip via sqlite") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE embeddings(node_id INTEGER NOT NULL, kind TEXT NOT NULL,"
           "vec BLOB NOT NULL, dim INTEGER NOT NULL, model TEXT NOT NULL DEFAULT '',"
           "body_hash TEXT NOT NULL DEFAULT '',"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           "PRIMARY KEY(node_id, kind))");
    EmbedStore es(db);

    std::vector<float> v = {0.1f, 0.2f, 0.3f, 0.4f};
    es.put(42, "memory", v, "test-model", "abc123");

    auto got = es.get(42, "memory", 4);
    ASSERT_EQ(got.size(), v.size());
    for (size_t i = 0; i < v.size(); ++i) ASSERT_TRUE(std::abs(got[i] - v[i]) < 1e-6f);
}

TEST("embed_store: fresh() detects unchanged body_hash") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE embeddings(node_id INTEGER NOT NULL, kind TEXT NOT NULL,"
           "vec BLOB NOT NULL, dim INTEGER NOT NULL, model TEXT NOT NULL DEFAULT '',"
           "body_hash TEXT NOT NULL DEFAULT '',"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           "PRIMARY KEY(node_id, kind))");
    EmbedStore es(db);
    es.put(1, "memory", {1.0f, 2.0f}, "m", "hash-A");
    ASSERT_TRUE(es.fresh(1, "memory", "hash-A"));
    ASSERT_FALSE(es.fresh(1, "memory", "hash-B"));
    ASSERT_FALSE(es.fresh(99, "memory", "hash-A"));
}

TEST("embed_store: put overwrites on conflict (same PK)") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE embeddings(node_id INTEGER NOT NULL, kind TEXT NOT NULL,"
           "vec BLOB NOT NULL, dim INTEGER NOT NULL, model TEXT NOT NULL DEFAULT '',"
           "body_hash TEXT NOT NULL DEFAULT '',"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           "PRIMARY KEY(node_id, kind))");
    EmbedStore es(db);
    es.put(7, "graph", {1.0f, 2.0f, 3.0f}, "m1", "h1");
    es.put(7, "graph", {9.0f, 8.0f, 7.0f}, "m2", "h2");
    auto got = es.get(7, "graph", 3);
    ASSERT_TRUE(std::abs(got[0] - 9.0f) < 1e-6f);
    ASSERT_TRUE(es.fresh(7, "graph", "h2"));
    ASSERT_FALSE(es.fresh(7, "graph", "h1"));
}

TEST("embed_store: getMany returns matching ids only") {
    icmg::core::Db db(":memory:");
    db.run("CREATE TABLE embeddings(node_id INTEGER NOT NULL, kind TEXT NOT NULL,"
           "vec BLOB NOT NULL, dim INTEGER NOT NULL, model TEXT NOT NULL DEFAULT '',"
           "body_hash TEXT NOT NULL DEFAULT '',"
           "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
           "PRIMARY KEY(node_id, kind))");
    EmbedStore es(db);
    es.put(1, "memory", {1.0f, 0.0f}, "m", "");
    es.put(2, "memory", {0.0f, 1.0f}, "m", "");
    es.put(3, "memory", {1.0f, 1.0f}, "m", "");
    auto rows = es.getMany("memory", {1, 3, 999}, 2);
    ASSERT_EQ(rows.size(), (size_t)2);   // 999 missing
    ASSERT_EQ(es.count("memory"), 3);
    ASSERT_EQ(es.count("graph"), 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
