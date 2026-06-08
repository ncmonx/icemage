#include "wasm_runtime.hpp"
#include "wasmtime_dyn.hpp"
#include "../core/http_stream.hpp"   // icmg::core::sha256OfFile
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace icmg::wasm {
namespace {

WasmtimeApi& api() {
    static std::string err;
    static WasmtimeApi a = loadWasmtime(err);
    return a;
}

std::mutex g_mtx;                                   // guards engine + cache
wasm_engine_t* g_engine = nullptr;
std::unordered_map<std::string, wasmtime_module_t*> g_modCache;

// Take ownership of a wasmtime_error_t*, return its message, free it.
std::string takeErr(const WasmtimeApi& a, wasmtime_error_t* e) {
    if (!e) return "";
    wasm_byte_vec_t m; m.size = 0; m.data = nullptr;
    a.error_message(e, &m);
    std::string s(m.data ? m.data : "", m.size);
    if (a.byte_vec_delete) a.byte_vec_delete(&m);
    a.error_delete(e);
    return s.empty() ? "wasmtime error" : s;
}

// Read file; if path ends .wat, compile to wasm bytes via the DLL.
bool loadModuleBytes(const WasmtimeApi& a, const std::string& path,
                     std::string& bytes, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open " + path; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string raw = ss.str();
    bool isWat = path.size() >= 4 && path.compare(path.size() - 4, 4, ".wat") == 0;
    if (!isWat) { bytes = std::move(raw); return true; }
    wasm_byte_vec_t outv; outv.size = 0; outv.data = nullptr;
    if (auto* e = a.wat2wasm(raw.data(), raw.size(), &outv)) {
        err = "wat2wasm: " + takeErr(a, e); return false;
    }
    bytes.assign(outv.data, outv.size);
    a.byte_vec_delete(&outv);
    return true;
}

// Get-or-compile module (cache by path). Caller holds g_mtx.
wasmtime_module_t* getModule(const WasmtimeApi& a, const WasmSkill& s, std::string& err) {
    auto it = g_modCache.find(s.wasmPath);
    if (it != g_modCache.end()) return it->second;
    if (!s.sha256.empty()) {
        std::string actual = icmg::core::sha256OfFile(s.wasmPath);
        if (actual != s.sha256) {
            err = "sha256 mismatch (pinned " + s.sha256 + ", got " + actual + ")";
            return nullptr;
        }
    }
    std::string bytes;
    if (!loadModuleBytes(a, s.wasmPath, bytes, err)) return nullptr;
    wasmtime_module_t* mod = nullptr;
    if (auto* e = a.module_new(g_engine, (const uint8_t*)bytes.data(), bytes.size(), &mod)) {
        err = "module_new: " + takeErr(a, e); return nullptr;
    }
    g_modCache[s.wasmPath] = mod;
    return mod;
}

} // namespace

bool wasmRuntimeAvailable(std::string& err) {
    const WasmtimeApi& a = api();
    if (!a.ok) { err = "wasm runtime unavailable"; return false; }
    return true;
}

bool runWasmFilter(const WasmSkill& skill, const std::string& input,
                   const WasmLimits& lim, std::string& out, std::string& rerr) {
    out.clear(); rerr.clear();
    const WasmtimeApi& a = api();
    if (!a.ok) { rerr = "wasm runtime unavailable"; return false; }

    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_engine) {
        wasm_config_t* cfg = a.config_new();
        a.config_fuel(cfg, true);
        a.config_epoch(cfg, true);
        g_engine = a.engine_new(cfg);   // consumes cfg
        if (!g_engine) { rerr = "engine init failed"; return false; }
    }
    wasmtime_module_t* mod = getModule(a, skill, rerr);
    if (!mod) return false;

    wasmtime_store_t* store = a.store_new(g_engine, nullptr, nullptr);
    if (!store) { rerr = "store_new failed"; return false; }
    wasmtime_context_t* ctx = a.store_context(store);

    if (auto* e = a.ctx_set_fuel(ctx, lim.fuel)) {
        rerr = "set_fuel: " + takeErr(a, e); a.store_delete(store); return false;
    }
    a.ctx_set_epoch(ctx, 1);   // trap when engine epoch advances by 1

    // epoch watchdog: bump epoch once after timeoutMs unless we finished first.
    std::mutex wm; std::condition_variable wcv; bool done = false;
    std::thread watch([&]{
        std::unique_lock<std::mutex> wl(wm);
        if (!wcv.wait_for(wl, std::chrono::milliseconds(lim.timeoutMs), [&]{ return done; }))
            a.engine_inc_epoch(g_engine);
    });
    auto stopWatch = [&]{
        { std::lock_guard<std::mutex> wl(wm); done = true; }
        wcv.notify_all();
        if (watch.joinable()) watch.join();
    };
    auto fail = [&](const std::string& m) -> bool {
        rerr = m; stopWatch(); a.store_delete(store); return false;
    };

    wasmtime_instance_t inst; wasm_trap_t* trap = nullptr;
    if (auto* e = a.instance_new(ctx, mod, nullptr, 0, &inst, &trap))
        return fail("instance_new: " + takeErr(a, e));
    if (trap) return fail("instance trap");

    auto getExport = [&](const char* nm, wasmtime_extern_t& ext) -> bool {
        return a.instance_export_get(ctx, &inst, nm, std::strlen(nm), &ext);
    };
    wasmtime_extern_t exMem, exAlloc, exFilter;
    if (!getExport("memory", exMem)       || exMem.kind    != WASMTIME_EXTERN_MEMORY ||
        !getExport("icmg_alloc", exAlloc) || exAlloc.kind  != WASMTIME_EXTERN_FUNC   ||
        !getExport("icmg_filter", exFilter)|| exFilter.kind != WASMTIME_EXTERN_FUNC)
        return fail("module missing filter-v1 exports (memory/icmg_alloc/icmg_filter)");

    // icmg_alloc(len) -> ptr
    wasmtime_val_t aArgs[1]; aArgs[0].kind = WASMTIME_I32; aArgs[0].of.i32 = (int32_t)input.size();
    wasmtime_val_t aRes[1];
    if (auto* e = a.func_call(ctx, &exAlloc.of.func, aArgs, 1, aRes, 1, &trap))
        return fail("icmg_alloc: " + takeErr(a, e));
    if (trap) return fail("icmg_alloc trap");
    int32_t ptr = aRes[0].of.i32;

    uint8_t* mem = a.memory_data(ctx, &exMem.of.memory);
    size_t memsz = a.memory_data_size(ctx, &exMem.of.memory);
    if (ptr < 0 || (size_t)ptr + input.size() > memsz) return fail("alloc out of bounds");
    std::memcpy(mem + ptr, input.data(), input.size());

    // icmg_filter(ptr, len) -> i64 packed (out_ptr<<32 | out_len)
    wasmtime_val_t fArgs[2];
    fArgs[0].kind = WASMTIME_I32; fArgs[0].of.i32 = ptr;
    fArgs[1].kind = WASMTIME_I32; fArgs[1].of.i32 = (int32_t)input.size();
    wasmtime_val_t fRes[1];
    if (auto* e = a.func_call(ctx, &exFilter.of.func, fArgs, 2, fRes, 1, &trap))
        return fail("icmg_filter: " + takeErr(a, e));
    if (trap) return fail("icmg_filter trap (timeout/fuel)");

    uint64_t packed = (uint64_t)fRes[0].of.i64;
    uint32_t outPtr = (uint32_t)(packed >> 32);
    uint32_t outLen = (uint32_t)(packed & 0xffffffffu);

    mem = a.memory_data(ctx, &exMem.of.memory);          // may have grown
    memsz = a.memory_data_size(ctx, &exMem.of.memory);
    if (outPtr > memsz) outLen = 0;
    else outLen = (uint32_t)std::min<size_t>(outLen, memsz - outPtr);
    if (outLen > lim.maxOutBytes) outLen = (uint32_t)lim.maxOutBytes;
    out.assign((const char*)mem + outPtr, outLen);

    stopWatch();
    a.store_delete(store);
    return true;
}

} // namespace icmg::wasm
