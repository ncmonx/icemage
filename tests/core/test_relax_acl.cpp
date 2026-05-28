// v1.56 hotfix: integration test for relaxAclEveryone.
//
// Verifies on Windows that after calling relaxAclEveryone(file), the file's
// DACL contains an ACE granting BUILTIN\Users full control. On non-Win the
// helper is a documented no-op and the test just asserts it returns true.

#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <aclapi.h>
#  include <sddl.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path makeTmpFile() {
    fs::path tmp = fs::temp_directory_path() / ("icmg-acl-test-" +
        std::to_string(reinterpret_cast<uintptr_t>(&tmp)) + ".tmp");
    { std::ofstream of(tmp.string()); of << "x"; }
    return tmp;
}

#ifdef _WIN32
// Returns true if the file's DACL has an access-allowed ACE for the given
// well-known-SID string with at least GENERIC_ALL / FILE_ALL_ACCESS bits.
bool daclHasFullControl(const std::string& path, const char* sid_str) {
    PACL pdacl = nullptr;
    PSECURITY_DESCRIPTOR psd = nullptr;
    DWORD rc = GetNamedSecurityInfoA(
        const_cast<LPSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &pdacl, nullptr, &psd);
    if (rc != ERROR_SUCCESS || !pdacl) {
        if (psd) LocalFree(psd);
        return false;
    }

    PSID target_sid = nullptr;
    if (!ConvertStringSidToSidA(sid_str, &target_sid)) {
        LocalFree(psd);
        return false;
    }

    bool found = false;
    for (DWORD i = 0; i < pdacl->AceCount; ++i) {
        ACE_HEADER* hdr = nullptr;
        if (!GetAce(pdacl, i, reinterpret_cast<LPVOID*>(&hdr))) continue;
        if (hdr->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
        ACCESS_ALLOWED_ACE* ace = reinterpret_cast<ACCESS_ALLOWED_ACE*>(hdr);
        PSID ace_sid = reinterpret_cast<PSID>(&ace->SidStart);
        if (EqualSid(ace_sid, target_sid)) {
            // Mask should grant either GENERIC_ALL or its mapped FILE bits.
            // Win API often maps GENERIC_ALL -> FILE_ALL_ACCESS (0x1F01FF) on
            // file objects, so check both.
            DWORD m = ace->Mask;
            if ((m & GENERIC_ALL) == GENERIC_ALL
                || (m & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) {
                found = true;
                break;
            }
        }
    }
    LocalFree(target_sid);
    LocalFree(psd);
    return found;
}
#endif

}  // namespace

TEST("relaxAclEveryone: returns true on supported platforms") {
    fs::path tmp = makeTmpFile();
    bool ok = icmg::core::relaxAclEveryone(tmp.string());
    ASSERT_TRUE(ok);
    std::error_code ec;
    fs::remove(tmp, ec);
}

#ifdef _WIN32
TEST("relaxAclEveryone: grants BUILTIN\\Users full control on Win") {
    fs::path tmp = makeTmpFile();
    bool ok = icmg::core::relaxAclEveryone(tmp.string());
    ASSERT_TRUE(ok);
    // BUILTIN\Users = S-1-5-32-545
    ASSERT_TRUE(daclHasFullControl(tmp.string(), "S-1-5-32-545"));
    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST("relaxAclEveryone: grants NT AUTHORITY\\SYSTEM full control on Win") {
    fs::path tmp = makeTmpFile();
    bool ok = icmg::core::relaxAclEveryone(tmp.string());
    ASSERT_TRUE(ok);
    // SYSTEM = S-1-5-18
    ASSERT_TRUE(daclHasFullControl(tmp.string(), "S-1-5-18"));
    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST("relaxAclEveryone: idempotent (re-invoke leaves ACL with both ACEs)") {
    fs::path tmp = makeTmpFile();
    icmg::core::relaxAclEveryone(tmp.string());
    bool ok = icmg::core::relaxAclEveryone(tmp.string());
    ASSERT_TRUE(ok);
    ASSERT_TRUE(daclHasFullControl(tmp.string(), "S-1-5-32-545"));
    ASSERT_TRUE(daclHasFullControl(tmp.string(), "S-1-5-18"));
    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST("relaxAclEveryone: empty path returns false") {
    bool ok = icmg::core::relaxAclEveryone("");
    ASSERT_FALSE(ok);
}
#endif

TEST("personaDbPath: returns non-empty when selfExePath resolves") {
    std::string p = icmg::core::personaDbPath();
    // In the test harness selfExePath should return the test exe path, so
    // personaDbPath is the test exe's parent + icmg-persona.db.
    ASSERT_FALSE(p.empty());
    ASSERT_TRUE(p.find("icmg-persona.db") != std::string::npos);
}
