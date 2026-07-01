// 2026-07-01: extractor-v1 ABI — WASM-backed language extractor manifest +
// output mapper (PURE helpers, no wasmtime). Phase 1 of WASM extractor modules.
#include "../test_main.hpp"
#include "../../src/wasm/wasm_extractor.hpp"
#include "../../src/graph/extractor/base_extractor.hpp"
using namespace icmg::wasm;

static const char* kValid = R"({
  "name":"zig-extractor","kind":"extractor","abi":"extractor-v1",
  "language":"zig","extensions":[".zig"],
  "wasm":"skills/zig_ext.wasm","sha256":"deadbeef",
  "priority":10,"min_icmg":"2.12.0"
})";

TEST("wasm_extractor: parse valid manifest") {
    std::string err;
    auto e = parseExtractorManifest(kValid, err);
    ASSERT_TRUE(e.has_value());
    ASSERT_EQ(e->name, std::string("zig-extractor"));
    ASSERT_EQ(e->language, std::string("zig"));
    ASSERT_EQ(e->abi, std::string("extractor-v1"));
    ASSERT_EQ(e->sha256, std::string("deadbeef"));
    ASSERT_EQ(e->priority, 10);
    ASSERT_EQ(e->minIcmg, std::string("2.12.0"));
    ASSERT_EQ(e->extensions.size(), (size_t)1);
    ASSERT_EQ(e->extensions[0], std::string(".zig"));
}

TEST("wasm_extractor: unknown abi rejected") {
    std::string err;
    auto e = parseExtractorManifest(R"({"name":"x","kind":"extractor","abi":"extractor-v2",
        "language":"zig","wasm":"x.wasm","sha256":"00"})", err);
    ASSERT_FALSE(e.has_value());
    ASSERT_CONTAINS(err, "abi");
}

TEST("wasm_extractor: missing language -> nullopt + err") {
    std::string err;
    auto e = parseExtractorManifest(R"({"name":"x","kind":"extractor","abi":"extractor-v1",
        "wasm":"x.wasm","sha256":"00"})", err);
    ASSERT_FALSE(e.has_value());
    ASSERT_CONTAINS(err, "language");
}

TEST("wasm_extractor: priority defaults to 0 when absent") {
    std::string err;
    auto e = parseExtractorManifest(R"({"name":"x","kind":"extractor","abi":"extractor-v1",
        "language":"zig","extensions":[".zig"],"wasm":"x.wasm","sha256":"00"})", err);
    ASSERT_TRUE(e.has_value());
    ASSERT_EQ(e->priority, 0);
}

TEST("wasm_extractor: bad json -> nullopt") {
    std::string err;
    ASSERT_FALSE(parseExtractorManifest("{not json", err).has_value());
}

TEST("wasm_extractor: semver floor gate") {
    ASSERT_TRUE(icmgVersionAtLeast("2.12.0", "2.12.0"));   // equal
    ASSERT_TRUE(icmgVersionAtLeast("2.13.0", "2.12.0"));   // newer minor
    ASSERT_TRUE(icmgVersionAtLeast("3.0.0",  "2.99.9"));   // newer major
    ASSERT_FALSE(icmgVersionAtLeast("2.11.9","2.12.0"));   // older
    ASSERT_TRUE(icmgVersionAtLeast("2.12.0", ""));         // empty floor = no constraint
}

TEST("wasm_extractor: output maps to ExtractResult") {
    icmg::graph::ExtractResult r;
    std::string err;
    bool ok = parseExtractorOutput(R"({
        "context":"a zig module",
        "imports":["std","fmt"],
        "namespaces":["main"],
        "classes":["Foo"],
        "functions":["doThing","init"]
    })", r, err);
    ASSERT_TRUE(ok);
    ASSERT_EQ(r.context, std::string("a zig module"));
    ASSERT_EQ(r.imports.size(), (size_t)2);
    ASSERT_EQ(r.namespaces.size(), (size_t)1);
    ASSERT_EQ(r.classes.size(), (size_t)1);
    ASSERT_EQ(r.functions.size(), (size_t)2);
    ASSERT_EQ(r.functions[0], std::string("doThing"));
}

TEST("wasm_extractor: output missing fields -> empty defaults, no crash") {
    icmg::graph::ExtractResult r;
    std::string err;
    bool ok = parseExtractorOutput(R"({"imports":["x"]})", r, err);
    ASSERT_TRUE(ok);
    ASSERT_EQ(r.imports.size(), (size_t)1);
    ASSERT_TRUE(r.classes.empty());
    ASSERT_TRUE(r.context.empty());
}

TEST("wasm_extractor: malformed output -> false + err, no crash") {
    icmg::graph::ExtractResult r;
    std::string err;
    ASSERT_FALSE(parseExtractorOutput("{broken", r, err));
    ASSERT_TRUE(!err.empty());
}

TEST("wasm_extractor: non-string array entries skipped defensively") {
    icmg::graph::ExtractResult r;
    std::string err;
    bool ok = parseExtractorOutput(R"({"imports":["ok",42,null,"also"]})", r, err);
    ASSERT_TRUE(ok);
    ASSERT_EQ(r.imports.size(), (size_t)2);  // 42 + null dropped
}
