// M10: curl binary must be `curl.exe` on Windows, not bare `curl`.
// ROOT: v1.81 routed safeExecShell via pwsh on non-MSYS Windows; in PowerShell
// `curl` is an alias for Invoke-WebRequest (NOT curl.exe), whose output is not
// raw response body -> json::parse fails -> "failed to query github".
// `curl.exe` suffix bypasses the alias. curl.exe ships in Win10 1803+.
#include "../test_main.hpp"
#include "../../src/core/exec_utils.hpp"
#include <string>

TEST("curlBin: returns curl.exe on Windows, curl elsewhere") {
#ifdef _WIN32
    ASSERT_EQ(icmg::core::curlBin(), std::string("curl.exe"));
#else
    ASSERT_EQ(icmg::core::curlBin(), std::string("curl"));
#endif
}

TEST("curlBin: usable as command prefix") {
    std::string cmd = std::string(icmg::core::curlBin()) + " -sL https://example.com";
#ifdef _WIN32
    ASSERT_CONTAINS(cmd, "curl.exe ");
#else
    ASSERT_CONTAINS(cmd, "curl ");
#endif
    ASSERT_NOT_CONTAINS(cmd, "Invoke-WebRequest");
}
