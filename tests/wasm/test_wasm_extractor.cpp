// 2026-07-01: extractor-v1 ABI — WASM-backed language extractor manifest +
// output mapper (PURE helpers, no wasmtime). Phase 1 of WASM extractor modules.
#include "../test_main.hpp"
#include "../../src/wasm/wasm_extractor.hpp"
#include "../../src/wasm/wasm_extractor_adapter.hpp"
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

// ── Phase 4: priority arbitration + adapter ──────────────────────────────────

static WasmExtractor mk(const std::string& lang, int prio) {
    WasmExtractor e; e.name=lang+"-ext"; e.abi="extractor-v1"; e.language=lang;
    e.priority=prio; e.extensions={"."+lang};
    return e;
}

TEST("wasm_extractor: fills gap when no builtin exists") {
    ASSERT_TRUE(wasmExtractorWins(mk("zig",0), /*builtinExists=*/false));
    ASSERT_TRUE(wasmExtractorWins(mk("zig",5), /*builtinExists=*/false));
}

TEST("wasm_extractor: priority 0 defers to existing builtin") {
    ASSERT_FALSE(wasmExtractorWins(mk("kotlin",0), /*builtinExists=*/true, 0));
}

TEST("wasm_extractor: higher priority overrides builtin") {
    ASSERT_TRUE(wasmExtractorWins(mk("kotlin",10), /*builtinExists=*/true, 0));
    ASSERT_FALSE(wasmExtractorWins(mk("kotlin",3), /*builtinExists=*/true, 5));
    ASSERT_TRUE(wasmExtractorWins(mk("kotlin",6), /*builtinExists=*/true, 5));
}

TEST("wasm_extractor: select highest-priority among same language") {
    std::vector<WasmExtractor> all = { mk("zig",1), mk("rust",9), mk("zig",7), mk("zig",3) };
    int idx = selectWasmExtractor(all, "zig");
    ASSERT_TRUE(idx >= 0);
    ASSERT_EQ(all[idx].priority, 7);   // the zig with prio 7 wins
}

TEST("wasm_extractor: select returns -1 for unknown language") {
    std::vector<WasmExtractor> all = { mk("zig",1) };
    ASSERT_EQ(selectWasmExtractor(all, "haskell"), -1);
}

TEST("wasm_extractor: adapter exposes extensions from manifest") {
    WasmExtractorAdapter ad(mk("zig",0));
    auto exts = ad.extensions();
    ASSERT_EQ(exts.size(), (size_t)1);
    ASSERT_EQ(exts[0], std::string(".zig"));
    ASSERT_EQ(ad.def().language, std::string("zig"));
}

TEST("wasm_extractor: adapter fail-open on missing module -> empty result") {
    WasmExtractor e = mk("zig",0);
    e.wasmPath = "Z:/nope/extract.wasm";
    WasmExtractorAdapter ad(e);
    auto r = ad.extract("foo.zig", "const std = @import(\"std\");");
    ASSERT_TRUE(r.imports.empty());   // no crash, empty degrade
    ASSERT_TRUE(r.functions.empty());
}

TEST("wasm_extractor: wasmLangForExtension resolves ext -> language") {
    std::vector<WasmExtractor> all = { mk("zig",0), mk("rust",0) };
    ASSERT_EQ(wasmLangForExtension(all, ".zig"), std::string("zig"));
    ASSERT_EQ(wasmLangForExtension(all, ".rust"), std::string("rust"));
    ASSERT_EQ(wasmLangForExtension(all, ".unknown"), std::string(""));
}

TEST("wasm_extractor: wasmLangForExtension highest priority wins shared ext") {
    WasmExtractor lo = mk("langA",1); lo.extensions = {".x"};
    WasmExtractor hi = mk("langB",9); hi.extensions = {".x"};
    std::vector<WasmExtractor> all = { lo, hi };
    ASSERT_EQ(wasmLangForExtension(all, ".x"), std::string("langB"));  // prio 9 wins
}

TEST("wasm_extractor: pickWasmExtractor honours arbitration") {
    std::vector<WasmExtractor> all = { mk("zig",0), mk("kotlin",10) };
    // zig: no builtin -> fills gap -> picked
    ASSERT_TRUE(pickWasmExtractor(all, "zig", /*builtinExists=*/false) >= 0);
    // kotlin prio 10 > builtin 0 -> overrides -> picked
    ASSERT_TRUE(pickWasmExtractor(all, "kotlin", /*builtinExists=*/true, 0) >= 0);
    // unknown language -> -1
    ASSERT_EQ(pickWasmExtractor(all, "haskell", false), -1);
    // zig prio 0 but builtin exists -> defer -> -1
    ASSERT_EQ(pickWasmExtractor(all, "zig", /*builtinExists=*/true, 0), -1);
}
