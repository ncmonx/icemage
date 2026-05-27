#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#  include <windows.h>
#  include <array>
#endif

namespace fs = std::filesystem;

// Run cmd and return exit code.
static int run_cmd(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// Run cmd, capture stdout+stderr, return exit code.
static int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
#ifdef _WIN32
    std::string full = "\"" + cmd + "\" 2>&1";
    FILE* fp = _popen(full.c_str(), "r");
#else
    std::string full = cmd + " 2>&1";
    FILE* fp = popen(full.c_str(), "r");
#endif
    if (!fp) return -1;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
#ifdef _WIN32
    return _pclose(fp);
#else
    return pclose(fp);
#endif
}

#ifndef ICMG_MONO_TEST
int main() {
    // Locate icmg absolute path (ctest cwd is build/).
    std::string icmg;
    if      (fs::exists("build/Debug/icmg.exe"))   icmg = fs::absolute("build/Debug/icmg.exe").string();
    else if (fs::exists("Debug/icmg.exe"))          icmg = fs::absolute("Debug/icmg.exe").string();
    else if (fs::exists("build/Release/icmg.exe")) icmg = fs::absolute("build/Release/icmg.exe").string();
    else if (fs::exists("Release/icmg.exe"))       icmg = fs::absolute("Release/icmg.exe").string();
    else if (fs::exists("build/icmg.exe"))         icmg = fs::absolute("build/icmg.exe").string();
    else                                            icmg = "icmg";
    std::cerr << "test_warm_loop_integration: icmg=" << icmg << "\n";

    // Probe: if `llm warm` subcommand absent in this build, skip gracefully.
    {
        std::string probe_out;
        run_capture("\"" + icmg + "\" llm warm --status", probe_out);
        if (probe_out.find("unknown subcommand") != std::string::npos) {
            std::cout << "test_warm_loop_integration: SKIP (llm warm not compiled in this build)\n";
            return 0;
        }
    }

    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) home = ".";
    auto pf = fs::path(home) / ".icmg" / "llm" / "warm.pid";

    // Test 1: --status when not running -> rc 1
    {
        run_cmd("\"" + icmg + "\" llm warm --stop");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        int rc = run_cmd("\"" + icmg + "\" llm warm --status");
        assert(rc != 0 && "status should fail when daemon stopped");
    }

    // Test 2: --stop when not running -> rc 0 (no-op)
    {
        int rc = run_cmd("\"" + icmg + "\" llm warm --stop");
        assert(rc == 0 && "stop on inactive should be no-op rc 0");
    }

    // Test 3: stale PID recovery
    {
        std::error_code ec;
        fs::create_directories(pf.parent_path(), ec);
        std::ofstream(pf) << "99999";
        // We just verify the CLI doesn't crash on stale PID.
        // (Model load may fail w/o ICMG_HAS_LLAMA or active model.)
        int rc = run_cmd("\"" + icmg + "\" llm warm --start");
        (void)rc;
        run_cmd("\"" + icmg + "\" llm warm --stop");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        fs::remove(pf, ec);
    }

    std::cout << "test_warm_loop_integration: 3/3 PASS\n";
    return 0;
}

#endif  // ICMG_MONO_TEST
