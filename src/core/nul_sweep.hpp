#pragma once
// Stray `nul` file detection for `icmg doctor` (2026-07-16).
//
// The 2026-07-16 daemon bug (and its sibling safeExecShell sites) created
// literal files named `nul` when a Windows `>nul` redirect was executed by
// bash (which treats `nul` as an ordinary filename, not the null device).
// Those fixes stop NEW files from appearing, but leftover `nul` files remain
// scattered in whatever directories `icmg` was run from -- and one on PATH
// (e.g. ~/bin/nul) actively breaks tooling, because a bare `nul` token on a
// command line resolves to that executable-looking file. This lets `icmg
// doctor` find and (non-dry-run) remove them.
#include <cctype>
#include <string>
#include <vector>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace icmg::core {

// True iff `filename` (a bare name, not a path) is the reserved Windows device
// name `nul`, case-insensitively -- i.e. a stray-nul artifact. A real file can
// only be named this under a shell (bash) that doesn't treat it as a device;
// Win32 itself refuses to create it, so any such file is an artifact of the
// bug. Pure + deterministic.
inline bool isStrayNulName(const std::string& filename) {
    if (filename.size() != 3) return false;
    return (filename[0] == 'n' || filename[0] == 'N') &&
           (filename[1] == 'u' || filename[1] == 'U') &&
           (filename[2] == 'l' || filename[2] == 'L');
}

// UTF-8 bytes of a path (component), without throwing on non-ACP characters.
// `fs::path::string()` calls the ACP narrow conversion on Windows and raises
// Win32 error 1113 (no mapping) for filenames outside the active code page --
// so a directory holding an unmappable-named file would crash the sweep.
// `u8string()` yields UTF-8 and never throws. Pure.
inline std::string nulSweepU8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Delete a stray `nul` file. On Windows a path whose final component is `nul`
// is intercepted by the Win32 layer and mapped to the NUL *device*, so a plain
// `fs::remove()` / `DeleteFile("...\nul")` silently targets the device and
// leaves the real file on disk. The `\\?\` prefix disables that DOS-device
// name parsing (and MAX_PATH normalization), letting the literal file be
// opened and removed. `p` is assumed absolute (the sweep always passes an
// absolute path). Returns true iff the file is gone afterwards. Never throws.
inline bool removeStrayNul(const std::filesystem::path& p) {
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef _WIN32
    // Build the extended-length path: \\?\C:\dir\nul (backslashes required).
    // NB: do NOT route through fs::absolute()/GetFullPathNameW here -- on a
    // final `nul` component that API resolves the name to the device path
    // (\\.\nul), which is exactly what we must avoid. The sweep always hands us
    // an absolute path already, so take its wstring verbatim.
    std::wstring w = p.wstring();
    for (auto& ch : w) if (ch == L'/') ch = L'\\';
    if (w.rfind(LR"(\\?\)", 0) != 0) w = LR"(\\?\)" + w;
    if (_wremove(w.c_str()) == 0) return true;
    // Fall back to the normal remove (e.g. already gone).
    fs::remove(p, ec);
    return !fs::exists(p, ec);
#else
    fs::remove(p, ec);
    return !fs::exists(p, ec);
#endif
}

// Scan each of `dirs` NON-recursively for stray `nul` files (see above) and
// return their UTF-8 paths. When `remove` is true, each hit is also deleted
// (best-effort). Never throws: every filesystem error is swallowed per-entry,
// missing/!directory inputs are skipped, and duplicate inputs (same lexical
// path) are swept only once. Deterministic given a filesystem state.
inline std::vector<std::string> sweepStrayNulFiles(
        const std::vector<std::filesystem::path>& dirs, bool remove) {
    namespace fs = std::filesystem;
    std::vector<std::string> hits;
    std::vector<std::string> seen;
    for (const auto& d : dirs) {
        std::error_code ec;
        std::string key = nulSweepU8(fs::weakly_canonical(d, ec));
        if (ec || key.empty()) key = nulSweepU8(d);
        bool dup = false;
        for (const auto& s : seen) if (s == key) { dup = true; break; }
        if (dup) continue;
        seen.push_back(key);

        std::error_code dec;
        if (!fs::exists(d, dec) || !fs::is_directory(d, dec)) continue;
        for (fs::directory_iterator it(d, dec), end; it != end; it.increment(dec)) {
            if (dec) break;
            std::error_code fec;
            if (!it->is_regular_file(fec) || fec) continue;
            std::string fn = nulSweepU8(it->path().filename());
            if (!isStrayNulName(fn)) continue;
            hits.push_back(nulSweepU8(it->path()));
            if (remove) removeStrayNul(it->path());
        }
    }
    return hits;
}

}  // namespace icmg::core
