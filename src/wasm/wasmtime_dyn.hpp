#pragma once
// Dynamic-load binding over the bundled wasmtime.dll.
//
// Why dynamic-load (not link-time): the DLL is already bundled, but icmg must
// run fine WITHOUT it (graceful-degrade). LoadLibrary + GetProcAddress means no
// import-lib, no CMake flag -- the feature lights up when wasmtime.dll resolves
// and reports "unavailable" otherwise.
//
// Types come from the vendored header (third_party/wasmtime/include); function
// POINTER types are derived with decltype(&fn) -- this needs only the
// DECLARATION (no symbol/link), so including the header costs nothing at link
// time. We never call the declared functions directly; only through these
// pointers resolved from the DLL.
#include <string>
#include <cstdint>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <wasmtime.h>   // vendored: third_party/wasmtime/include

namespace icmg::wasm {

struct WasmtimeApi {
    bool  ok  = false;
    void* dll = nullptr;

    // engine / config
    decltype(&wasm_config_new)                       config_new = nullptr;
    decltype(&wasmtime_config_consume_fuel_set)      config_fuel = nullptr;
    decltype(&wasmtime_config_epoch_interruption_set) config_epoch = nullptr;
    decltype(&wasm_engine_new_with_config)           engine_new = nullptr;
    decltype(&wasm_engine_delete)                    engine_delete = nullptr;
    decltype(&wasmtime_engine_increment_epoch)       engine_inc_epoch = nullptr;
    // store / context
    decltype(&wasmtime_store_new)        store_new = nullptr;
    decltype(&wasmtime_store_delete)     store_delete = nullptr;
    decltype(&wasmtime_store_context)    store_context = nullptr;
    decltype(&wasmtime_context_set_fuel) ctx_set_fuel = nullptr;
    decltype(&wasmtime_context_set_epoch_deadline) ctx_set_epoch = nullptr;
    // module / instance / call
    decltype(&wasmtime_module_new)          module_new = nullptr;
    decltype(&wasmtime_module_delete)       module_delete = nullptr;
    decltype(&wasmtime_instance_new)        instance_new = nullptr;
    decltype(&wasmtime_instance_export_get) instance_export_get = nullptr;
    decltype(&wasmtime_func_call)           func_call = nullptr;
    decltype(&wasmtime_memory_data)         memory_data = nullptr;
    decltype(&wasmtime_memory_data_size)    memory_data_size = nullptr;
    // error
    decltype(&wasmtime_error_message) error_message = nullptr;
    decltype(&wasmtime_error_delete)  error_delete = nullptr;
    // wat -> wasm (lets skills ship readable .wat; also used by fixtures)
    decltype(&wasmtime_wat2wasm) wat2wasm = nullptr;
    // byte-vec lifetime (for wat2wasm / error output buffers)
    decltype(&wasm_byte_vec_delete) byte_vec_delete = nullptr;
};

// Resolve every symbol from the bundled wasmtime.dll. Any miss -> ok=false
// (feature unavailable, NOT a crash). On non-Windows, returns ok=false (W3.5).
inline WasmtimeApi loadWasmtime(std::string& err) {
    WasmtimeApi api;
    err.clear();
#ifdef _WIN32
    HMODULE h = LoadLibraryA("wasmtime.dll");  // exe-dir first (app SxS manifest)
    if (!h) { err = "wasmtime.dll not found"; return api; }
    api.dll = (void*)h;

#define ICMG_WASM_BIND(field, sym)                                              \
    api.field = reinterpret_cast<decltype(api.field)>(                          \
        reinterpret_cast<void*>(GetProcAddress(h, sym)));                       \
    if (!api.field && err.empty()) err = std::string("missing symbol: ") + sym;

    ICMG_WASM_BIND(config_new,          "wasm_config_new")
    ICMG_WASM_BIND(config_fuel,         "wasmtime_config_consume_fuel_set")
    ICMG_WASM_BIND(config_epoch,        "wasmtime_config_epoch_interruption_set")
    ICMG_WASM_BIND(engine_new,          "wasm_engine_new_with_config")
    ICMG_WASM_BIND(engine_delete,       "wasm_engine_delete")
    ICMG_WASM_BIND(engine_inc_epoch,    "wasmtime_engine_increment_epoch")
    ICMG_WASM_BIND(store_new,           "wasmtime_store_new")
    ICMG_WASM_BIND(store_delete,        "wasmtime_store_delete")
    ICMG_WASM_BIND(store_context,       "wasmtime_store_context")
    ICMG_WASM_BIND(ctx_set_fuel,        "wasmtime_context_set_fuel")
    ICMG_WASM_BIND(ctx_set_epoch,       "wasmtime_context_set_epoch_deadline")
    ICMG_WASM_BIND(module_new,          "wasmtime_module_new")
    ICMG_WASM_BIND(module_delete,       "wasmtime_module_delete")
    ICMG_WASM_BIND(instance_new,        "wasmtime_instance_new")
    ICMG_WASM_BIND(instance_export_get, "wasmtime_instance_export_get")
    ICMG_WASM_BIND(func_call,           "wasmtime_func_call")
    ICMG_WASM_BIND(memory_data,         "wasmtime_memory_data")
    ICMG_WASM_BIND(memory_data_size,    "wasmtime_memory_data_size")
    ICMG_WASM_BIND(error_message,       "wasmtime_error_message")
    ICMG_WASM_BIND(error_delete,        "wasmtime_error_delete")
    ICMG_WASM_BIND(wat2wasm,            "wasmtime_wat2wasm")
    ICMG_WASM_BIND(byte_vec_delete,     "wasm_byte_vec_delete")
#undef ICMG_WASM_BIND

    api.ok = err.empty();
    if (!api.ok && api.dll) { FreeLibrary((HMODULE)api.dll); api.dll = nullptr; }
#else
    // POSIX: same dynamic-load via dlopen (W3.5). The .so/.dylib must be bundled
    // alongside the binary (CI step) for this to resolve; absent -> unavailable.
    void* h = dlopen("libwasmtime.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) h = dlopen("libwasmtime.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!h) { err = "libwasmtime.{so,dylib} not found"; return api; }
    api.dll = h;
#define ICMG_WASM_BIND(field, sym)                                              \
    api.field = reinterpret_cast<decltype(api.field)>(dlsym(h, sym));           \
    if (!api.field && err.empty()) err = std::string("missing symbol: ") + sym;
    ICMG_WASM_BIND(config_new,          "wasm_config_new")
    ICMG_WASM_BIND(config_fuel,         "wasmtime_config_consume_fuel_set")
    ICMG_WASM_BIND(config_epoch,        "wasmtime_config_epoch_interruption_set")
    ICMG_WASM_BIND(engine_new,          "wasm_engine_new_with_config")
    ICMG_WASM_BIND(engine_delete,       "wasm_engine_delete")
    ICMG_WASM_BIND(engine_inc_epoch,    "wasmtime_engine_increment_epoch")
    ICMG_WASM_BIND(store_new,           "wasmtime_store_new")
    ICMG_WASM_BIND(store_delete,        "wasmtime_store_delete")
    ICMG_WASM_BIND(store_context,       "wasmtime_store_context")
    ICMG_WASM_BIND(ctx_set_fuel,        "wasmtime_context_set_fuel")
    ICMG_WASM_BIND(ctx_set_epoch,       "wasmtime_context_set_epoch_deadline")
    ICMG_WASM_BIND(module_new,          "wasmtime_module_new")
    ICMG_WASM_BIND(module_delete,       "wasmtime_module_delete")
    ICMG_WASM_BIND(instance_new,        "wasmtime_instance_new")
    ICMG_WASM_BIND(instance_export_get, "wasmtime_instance_export_get")
    ICMG_WASM_BIND(func_call,           "wasmtime_func_call")
    ICMG_WASM_BIND(memory_data,         "wasmtime_memory_data")
    ICMG_WASM_BIND(memory_data_size,    "wasmtime_memory_data_size")
    ICMG_WASM_BIND(error_message,       "wasmtime_error_message")
    ICMG_WASM_BIND(error_delete,        "wasmtime_error_delete")
    ICMG_WASM_BIND(wat2wasm,            "wasmtime_wat2wasm")
    ICMG_WASM_BIND(byte_vec_delete,     "wasm_byte_vec_delete")
#undef ICMG_WASM_BIND
    api.ok = err.empty();
    if (!api.ok && api.dll) { dlclose(api.dll); api.dll = nullptr; }
#endif
    return api;
}

} // namespace icmg::wasm
