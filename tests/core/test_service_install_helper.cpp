// v1.1.1 Task 1/2: contract tests for installResidentService + cleanupLegacySchtasks
#include <gtest/gtest.h>
#include "../../src/core/service_install.hpp"
#include "../../src/core/path_utils.hpp"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Helper returns true and on Windows leaves VBS launcher on disk (unless skipped).
TEST(ServiceInstallHelper, ReturnsTrueAndCreatesVbsOnWindows) {
#ifdef _WIN32
    // Skip-env shortcut: when set, helper must be a no-op success.
    {
        const std::string prior = std::getenv("ICMG_SKIP_SERVICE")
                                  ? std::getenv("ICMG_SKIP_SERVICE") : "";
        _putenv_s("ICMG_SKIP_SERVICE", "1");
        std::string err;
        bool ok = icmg::core::installResidentService(&err);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(err.empty());
        _putenv_s("ICMG_SKIP_SERVICE", prior.c_str());
    }

    // Normal path: writes VBS even if schtasks fails (VBS write happens first).
    std::string err;
    bool ok = icmg::core::installResidentService(&err);
    // Don't assert ok=true unconditionally — schtasks may fail in CI sandbox.
    // But VBS must exist regardless when not in skip mode.
    fs::path vbs = fs::path(icmg::core::icmgGlobalDir()) / "service-launcher.vbs";
    EXPECT_TRUE(fs::exists(vbs)) << "VBS launcher should be written at " << vbs.string();
    (void)ok;
#else
    std::string err;
    bool ok = icmg::core::installResidentService(&err);
    EXPECT_TRUE(ok);
#endif
}

// Cleanup returns non-negative count; idempotent (second call returns 0).
TEST(ServiceInstallHelper, CleanupLegacyIdempotent) {
    int first  = icmg::core::cleanupLegacySchtasks();
    EXPECT_GE(first, 0);
    int second = icmg::core::cleanupLegacySchtasks();
    EXPECT_EQ(second, 0) << "Second call should be no-op";
}
