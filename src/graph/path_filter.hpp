#pragma once
// Pure path classification for graph-view hygiene (2026-06-12). Shared by the
// repo skeleton and the temporal (graph-recent) ranking so every "view of the
// codebase" applies the same filters: drop vendored/generated files, drop test
// files, and scope to the project root. Header-only + no I/O -> unit-testable.
#include <cctype>
#include <string>

namespace icmg::graph {

// Lowercase + forward-slash normalize (Windows paths compare case-insensitively).
inline std::string normPath(const std::string& path) {
    std::string p; p.reserve(path.size());
    for (char c : path) p += (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
    return p;
}

// True if `path` lives under a vendored / generated directory (segment match,
// not substring -- "src/vendored_notes.cpp" is NOT vendored, "a/vendor/b" is).
inline bool isVendoredPath(const std::string& path) {
    std::string s = "/" + normPath(path) + "/";
    static const char* segs[] = {
        "/third_party/", "/node_modules/", "/vendor/", "/.git/", "/dist/",
        "/.venv/", "/site-packages/", "/external/", "/.cache/", "/target/",
    };
    for (const char* seg : segs) if (s.find(seg) != std::string::npos) return true;
    if (s.find("/build/")  != std::string::npos) return true;
    if (s.find("/build-")  != std::string::npos) return true;   // build-msvc-full, build-* dirs
    return false;
}

// True if `path` is a test/spec file: under a tests/test/spec/__tests__ dir, or a
// test_* / *_test.* / *.test.* / *.spec.* basename (segment/affix, not substring).
inline bool isTestPath(const std::string& path) {
    std::string p = normPath(path);
    std::string s = "/" + p + "/";
    if (s.find("/tests/") != std::string::npos || s.find("/test/") != std::string::npos ||
        s.find("/spec/")  != std::string::npos || s.find("/__tests__/") != std::string::npos)
        return true;
    size_t sl = p.find_last_of('/');
    std::string base = (sl == std::string::npos) ? p : p.substr(sl + 1);
    if (base.rfind("test_", 0) == 0)               return true;   // test_foo.cpp
    if (base.find("_test.")  != std::string::npos) return true;   // foo_test.go
    if (base.find(".test.")  != std::string::npos) return true;   // foo.test.ts
    if (base.find(".spec.")  != std::string::npos) return true;   // foo.spec.ts
    return false;
}

// True if `path` is inside `root` (both normalized; empty root => always true).
inline bool pathUnderRoot(const std::string& path, const std::string& root) {
    if (root.empty()) return true;
    std::string p = normPath(path), r = normPath(root);
    if (!r.empty() && r.back() == '/') r.pop_back();
    return p.size() >= r.size() && p.compare(0, r.size(), r) == 0;
}

// Should this file node appear in a "codebase view"? Combines the three filters.
inline bool keepProjectFile(const std::string& path,
                            bool excludeVendored, bool includeTests,
                            const std::string& rootPrefix) {
    if (excludeVendored && isVendoredPath(path)) return false;
    if (!includeTests && isTestPath(path))       return false;
    if (!pathUnderRoot(path, rootPrefix))        return false;
    return true;
}

}  // namespace icmg::graph
