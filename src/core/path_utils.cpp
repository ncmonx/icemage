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
#  include <aclapi.h>     // SetEntriesInAclA, SetNamedSecurityInfoA
#  include <sddl.h>       // ConvertStringSidToSidA
#  pragma comment(lib, "advapi32.lib")
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
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

std::string selfExePath() {
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return {};
    return std::string(buf, n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return {};
    try {
        return fs::canonical(buf).string();
    } catch (...) {
        return std::string(buf);
    }
#else
    try {
        return fs::canonical("/proc/self/exe").string();
    } catch (...) {
        return {};
    }
#endif
}

// v1.56 hotfix: persona DB at icmg.exe directory (shared across Win users
// on same host). Falls back to empty string when selfExePath fails — callers
// use globalDbPath() in that case.
std::string personaDbPath() {
    std::string exe = selfExePath();
    if (exe.empty()) return {};
    fs::path exe_dir = fs::path(exe).parent_path();
    return (exe_dir / "icmg-persona.db").string();
}

// v1.56 hotfix: relax Win ACL — grant BUILTIN\Users + SYSTEM full control on
// the target path. This makes shared icmg installs work across Win users
// and SYSTEM service launches. Idempotent: re-running adds the same ACEs
// without effect. No-op on non-Win.
bool relaxAclEveryone(const std::string& path) {
#ifndef _WIN32
    (void)path;
    return true;   // POSIX bits handled elsewhere via chmod
#else
    if (path.empty()) return false;

    // Build a DACL with two access-allowed ACEs:
    //   1. BUILTIN\Users  (S-1-5-32-545) → GENERIC_ALL
    //   2. NT AUTHORITY\SYSTEM (S-1-5-18) → GENERIC_ALL
    PSID users_sid = nullptr;
    PSID system_sid = nullptr;
    if (!ConvertStringSidToSidA("S-1-5-32-545", &users_sid)) return false;
    if (!ConvertStringSidToSidA("S-1-5-18", &system_sid)) {
        LocalFree(users_sid);
        return false;
    }

    EXPLICIT_ACCESSA ea[2] = {};
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode        = SET_ACCESS;
    ea[0].grfInheritance       = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[0].Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType  = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName    = (LPSTR)users_sid;

    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode        = SET_ACCESS;
    ea[1].grfInheritance       = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[1].Trustee.TrusteeForm  = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType  = TRUSTEE_IS_USER;
    ea[1].Trustee.ptstrName    = (LPSTR)system_sid;

    PACL pNewDacl = nullptr;
    DWORD rc_acl = SetEntriesInAclA(2, ea, nullptr, &pNewDacl);
    bool ok = false;
    if (rc_acl == ERROR_SUCCESS && pNewDacl) {
        DWORD rc_set = SetNamedSecurityInfoA(
            const_cast<LPSTR>(path.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, pNewDacl, nullptr);
        ok = (rc_set == ERROR_SUCCESS);
    }
    if (pNewDacl) LocalFree(pNewDacl);
    LocalFree(users_sid);
    LocalFree(system_sid);
    return ok;
#endif
}

} // namespace icmg::core
