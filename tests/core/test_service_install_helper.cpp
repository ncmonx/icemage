// v1.1.1 — contract tests for installResidentService + cleanupLegacySchtasks
#include "../test_main.hpp"
#include "../../src/core/service_install.hpp"
#include "../../src/core/path_utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST("installResidentService — skip env returns true no-op") {
    const char* prior_raw = std::getenv("ICMG_SKIP_SERVICE");
    std::string prior = prior_raw ? prior_raw : "";
#ifdef _WIN32
    _putenv_s("ICMG_SKIP_SERVICE", "1");
#else
    setenv("ICMG_SKIP_SERVICE", "1", 1);
#endif
    std::string err;
    bool ok = icmg::core::installResidentService(&err);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(err.empty());
#ifdef _WIN32
    _putenv_s("ICMG_SKIP_SERVICE", prior.c_str());
#else
    if (prior.empty()) unsetenv("ICMG_SKIP_SERVICE");
    else setenv("ICMG_SKIP_SERVICE", prior.c_str(), 1);
#endif
}

TEST("cleanupLegacySchtasks — idempotent, returns non-negative") {
    int first  = icmg::core::cleanupLegacySchtasks();
    ASSERT_TRUE(first >= 0);
    int second = icmg::core::cleanupLegacySchtasks();
    ASSERT_EQ(second, 0);
}

#ifdef _WIN32
TEST("installResidentService — writes VBS launcher on Windows") {
    const char* prior_raw = std::getenv("ICMG_SKIP_SERVICE");
    std::string prior = prior_raw ? prior_raw : "";
    _putenv_s("ICMG_SKIP_SERVICE", "");
    std::string err;
    (void)icmg::core::installResidentService(&err);
    fs::path vbs = fs::path(icmg::core::icmgGlobalDir()) / "service-launcher.vbs";
    ASSERT_TRUE(fs::exists(vbs));
    _putenv_s("ICMG_SKIP_SERVICE", prior.c_str());
}
#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
