#include "path_utils.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace fs = std::filesystem;
namespace icmg::core {

std::string homeDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, path)))
        return path;
    const char* h = std::getenv("USERPROFILE");
    return h ? h : "C:\\Users\\Default";
#else
    const char* h = std::getenv("HOME");
    return h ? h : "/tmp";
#endif
}

std::string icmgGlobalDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return std::string(path) + "\\icmg";
    return homeDir() + "\\icmg";
#else
    return homeDir() + "/.icmg";
#endif
}

std::string expandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    return homeDir() + path.substr(1);
}

std::string canonicalize(const std::string& path, bool require_exists) {
    std::string expanded = expandTilde(path);
    fs::path p(expanded);

    if (require_exists) {
        return fs::canonical(p).string();
    }
    // weakly_canonical doesn't require existence
    return fs::weakly_canonical(p).string();
}

bool isWithinRoot(const std::string& path, const std::string& root) {
    fs::path p  = fs::weakly_canonical(path);
    fs::path r  = fs::weakly_canonical(root);

    auto it_p = p.begin();
    auto it_r = r.begin();
    while (it_r != r.end()) {
        if (it_p == p.end() || *it_p != *it_r) return false;
        ++it_p; ++it_r;
    }
    return true;
}

bool isSQLiteFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    // SQLite magic: "SQLite format 3\000"
    char magic[16] = {};
    f.read(magic, 16);
    return std::string(magic, 6) == "SQLite";
}

} // namespace icmg::core
