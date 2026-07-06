// v1.29.0 #2: mono icmg_test binary entry point.
//
// When built with ICMG_MONO_TEST=1 + ICMG_MONO_TEST_RESET=1:
//   - All 124 per-test `int main()` bodies are #ifndef'd out (per-test
//     guard added by tests/* files).
//   - This file provides the single `int main()` + the betweenTests()
//     impl that resets shared singletons between TESTs.
//
// Singletons reset on each test boundary:
//   - icmg::imem::Scorer::instance().reset() — clears BM25 corpus stats
//   - More to add as mono mode discovers state leaks.
//
// Skipped (instance-based, no singleton leak):
//   - core::Db — each test constructs its own (typically :memory:)
//   - graph::GraphStore — per-test ctor takes Db&
//   - imem::MemoryStore — per-test ctor takes Db&

#include "test_main.hpp"
#include "../src/imem/scorer.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace icmg::test {

void betweenTests() {
    try {
        ::icmg::imem::Scorer::instance().reset();
    } catch (...) {}
    // Reset process-env knobs so a test that sets them (e.g. isolateDaemon in
    // test_causal sets ICMG_RECALL_CACHE=0 / ICMG_NO_DAEMON=1) cannot leak into
    // the next test in the mono binary. Each test starts from icmg defaults
    // (cache on, daemon on); tests needing them off re-set at their own start.
#ifdef _WIN32
    _putenv_s("ICMG_RECALL_CACHE", "");
    _putenv_s("ICMG_NO_DAEMON", "");
#else
    unsetenv("ICMG_RECALL_CACHE");
    unsetenv("ICMG_NO_DAEMON");
#endif
    // Other singletons to reset here as mono mode reveals state leaks.
}

}  // namespace icmg::test

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (a.rfind("--filter=", 0) == 0) {
            filter = a.substr(9);
        } else if (a == "--list") {
            for (auto& tc : icmg::test::registry()) std::cout << tc.name << "\n";
            return 0;
        }
    }
    return icmg::test::run_all(filter);
}
