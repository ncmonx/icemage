// 2026-06-07: WASM filter-v1 runtime (integration). Embeds WAT + writes temp
// files so the test is cwd-independent (mono binary crashes from project cwd).
#include "../test_main.hpp"
#include "../../src/wasm/wasm_runtime.hpp"
#include "../../src/tkil/filters/wasm_filter.hpp"
#include <chrono>
#include <fstream>
#include <filesystem>
using namespace icmg::wasm;
namespace fs = std::filesystem;

static const char* UPPER_WAT = R"WAT(
(module
  (memory (export "memory") 2)
  (global $bump (mut i32) (i32.const 1024))
  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))
  (func (export "icmg_filter") (param $ptr i32) (param $len i32) (result i64)
    (local $i i32) (local $c i32)
    (block $done (loop $loop
      (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $ptr) (local.get $i))))
      (if (i32.and (i32.ge_u (local.get $c) (i32.const 97)) (i32.le_u (local.get $c) (i32.const 122)))
        (then (i32.store8 (i32.add (local.get $ptr) (local.get $i)) (i32.sub (local.get $c) (i32.const 32)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop)))
    (i64.or (i64.shl (i64.extend_i32_u (local.get $ptr)) (i64.const 32))
            (i64.extend_i32_u (local.get $len)))))
)WAT";

static const char* SPIN_WAT = R"WAT(
(module
  (memory (export "memory") 1)
  (global $bump (mut i32) (i32.const 1024))
  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))
  (func (export "icmg_filter") (param $ptr i32) (param $len i32) (result i64)
    (loop $l (br $l)) (i64.const 0)))
)WAT";

static std::string writeTemp(const std::string& name, const char* content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary) << content;
    return p.string();
}

TEST("wasm_runtime: availability detectable") {
    std::string err;
    bool avail = wasmRuntimeAvailable(err);
    ASSERT_TRUE(avail || !err.empty());
}

TEST("wasm_runtime: uppercase filter transforms input") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;   // skip if runtime absent
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=writeTemp("icmg-up.wat", UPPER_WAT);
    std::string out, rerr;
    bool ok = runWasmFilter(s, "abc123xyz", WasmLimits{}, out, rerr);
    ASSERT_TRUE(ok);
    ASSERT_EQ(out, std::string("ABC123XYZ"));
}

TEST("wasm_runtime: oversized output capped") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=writeTemp("icmg-up.wat", UPPER_WAT);
    WasmLimits lim; lim.maxOutBytes = 4;
    std::string out, rerr;
    runWasmFilter(s, "abcdefgh", lim, out, rerr);
    ASSERT_TRUE(out.size() <= 4);
}

TEST("wasm_runtime: missing file -> fail, no crash") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="x"; s.abi="filter-v1"; s.wasmPath="Z:/nope/does-not-exist.wasm";
    std::string out, rerr;
    ASSERT_FALSE(runWasmFilter(s, "x", WasmLimits{}, out, rerr));
}

TEST("wasm_runtime: infinite loop aborts within timeout") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="spin"; s.abi="filter-v1"; s.wasmPath=writeTemp("icmg-spin.wat", SPIN_WAT);
    WasmLimits lim; lim.timeoutMs = 100; lim.fuel = 5'000'000;
    std::string out, rerr;
    bool ok = runWasmFilter(s, "x", lim, out, rerr);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(!rerr.empty());
}

TEST("wasm_runtime: sha256 mismatch refused") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1";
    s.wasmPath = writeTemp("icmg-up-sha.wat", UPPER_WAT);
    s.sha256 = "deadbeef";   // wrong on purpose
    std::string out, rerr;
    ASSERT_FALSE(runWasmFilter(s, "abc", WasmLimits{}, out, rerr));
    ASSERT_CONTAINS(rerr, "sha");
}

TEST("wasm_runtime: cached module 1000 calls under budget") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=writeTemp("icmg-up.wat", UPPER_WAT);
    std::string out, rerr;
    runWasmFilter(s, "warmup", WasmLimits{}, out, rerr);   // compile + cache
    auto t0 = std::chrono::steady_clock::now();
    for (int i=0;i<1000;i++) runWasmFilter(s, "abcabcabc", WasmLimits{}, out, rerr);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now()-t0).count();
    ASSERT_TRUE(ms < 2000);   // avg <2ms/call or WASM unsuitable for hot path
}

TEST("wasm_filter: adapts skill to FilterResult (uppercases)") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=writeTemp("icmg-up.wat", UPPER_WAT);
    icmg::tkil::WasmFilter f(s);
    auto r = f.filter("hello world", "acme-tool");
    ASSERT_EQ(r.output, std::string("HELLO WORLD"));
    ASSERT_EQ(f.name(), std::string("wasm:up"));
}

TEST("wasm_filter: fail-open passthrough on bad module") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="bad"; s.abi="filter-v1"; s.wasmPath="Z:/nope.wasm";
    icmg::tkil::WasmFilter f(s);
    auto r = f.filter("untouched", "x");
    ASSERT_EQ(r.output, std::string("untouched"));   // raw passes through
}
