#pragma once
// Dependency probe: name the missing module behind a Windows err126 WITHOUT
// Process Monitor / admin. For each bundled runtime DLL in the exe dir, try to
// load it; if it fails (or to be thorough), parse its PE import table and test
// each imported DLL for resolvability -- the ones that fail are the modules
// absent on this machine (e.g. a Vulkan ICD / a system feature DLL present on
// Win10/11 but not on Server 2019). POSIX build: no-op (returns empty).
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace icmg::core {

struct DllProbe {
    std::string dll;                       // candidate bundled DLL
    bool present = false;                   // file exists in exe dir
    bool loaded = false;                    // LoadLibraryEx succeeded
    int  err = 0;                          // GetLastError if load failed
    std::vector<std::string> missingImports; // imported DLLs that don't resolve
};

#ifdef _WIN32
// Parse the import-table DLL names of a PE file (best-effort; empty on any error).
inline std::vector<std::string> peImportNames(const std::string& path) {
    std::vector<std::string> out;
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    HANDLE map = CreateFileMappingA(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map) { CloseHandle(f); return out; }
    BYTE* base = (BYTE*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(map); CloseHandle(f); return out; }
    auto cleanup = [&]() { UnmapViewOfFile(base); CloseHandle(map); CloseHandle(f); };
    auto* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { cleanup(); return out; }
    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { cleanup(); return out; }
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.Size == 0 || dir.VirtualAddress == 0) { cleanup(); return out; }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    int nsec = nt->FileHeader.NumberOfSections;
    auto rva2off = [&](DWORD rva) -> DWORD {
        for (int i = 0; i < nsec; ++i) {
            DWORD va = sec[i].VirtualAddress;
            DWORD sz = sec[i].SizeOfRawData;
            if (rva >= va && rva < va + sz) return sec[i].PointerToRawData + (rva - va);
        }
        return 0;
    };
    DWORD impOff = rva2off(dir.VirtualAddress);
    if (!impOff) { cleanup(); return out; }
    auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + impOff);
    for (; imp->Name; ++imp) {
        DWORD nameOff = rva2off(imp->Name);
        if (!nameOff) break;
        out.emplace_back((const char*)(base + nameOff));
        if (out.size() > 256) break;  // sanity
    }
    // Delay-import table (dir index 13): modules loaded on FIRST CALL, not at DLL
    // load. The static-import walk above misses these -- a delay-imported module
    // absent on this machine is exactly the err126 that only surfaces at runtime
    // (e.g. `icmg context`) while the DLL itself loads fine.
    auto& ddir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (ddir.Size && ddir.VirtualAddress) {
        DWORD dOff = rva2off(ddir.VirtualAddress);
        if (dOff) {
            auto* dd = (IMAGE_DELAYLOAD_DESCRIPTOR*)(base + dOff);
            for (; dd->DllNameRVA; ++dd) {
                DWORD nOff = rva2off(dd->DllNameRVA);
                if (!nOff) break;
                out.emplace_back((const char*)(base + nOff));
                if (out.size() > 512) break;  // sanity
            }
        }
    }
    cleanup();
    return out;
}

// True if a DLL (by name) can be located/loaded on this machine.
inline bool dllResolvable(const std::string& name) {
    if (HMODULE h = GetModuleHandleA(name.c_str())) { (void)h; return true; }
    HMODULE h = LoadLibraryExA(name.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (h) { FreeLibrary(h); return true; }
    return false;
}
#endif  // _WIN32

// Probe a set of bundled DLLs (by filename) located in exeDir. Windows-only;
// returns empty on POSIX. Each result reports load success + any imported DLLs
// that fail to resolve (the actual missing modules).
inline std::vector<DllProbe> probeBundledDlls(const std::string& exeDir,
                                              const std::vector<std::string>& candidates) {
    std::vector<DllProbe> results;
#ifdef _WIN32
    UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    for (const auto& dll : candidates) {
        DllProbe r; r.dll = dll;
        std::string full = exeDir;
        if (!full.empty() && full.back() != '\\' && full.back() != '/') full += '\\';
        full += dll;
        DWORD attr = GetFileAttributesA(full.c_str());
        r.present = (attr != INVALID_FILE_ATTRIBUTES);
        if (!r.present) { results.push_back(r); continue; }
        HMODULE h = LoadLibraryExA(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h) { r.loaded = true; FreeLibrary(h); }
        else   { r.err = (int)GetLastError(); }
        // Always walk imports so we can name the absent transitive module(s).
        for (const auto& imp : peImportNames(full)) {
            if (!dllResolvable(imp)) r.missingImports.push_back(imp);
        }
        results.push_back(r);
    }
    SetErrorMode(oldMode);
#else
    (void)exeDir; (void)candidates;
#endif
    return results;
}

}  // namespace icmg::core
