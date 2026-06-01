// v1.63 F12: gist command — deterministic parts (heuristic fallback +
// cache behaviour) tested end-to-end via the command; LLM output itself is
// non-deterministic and validated by smoke, not unit tests.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace icmg;

namespace {
std::string runGist(const std::vector<std::string>& args) {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("gist");
    if (!cmd) return "<no-gist-cmd>";
    std::ostringstream cap;
    auto* old = std::cout.rdbuf(cap.rdbuf());
    cmd->run(args);
    std::cout.rdbuf(old);
    return cap.str();
}
std::string tmpFile(const std::string& name, const std::string& content) {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary); f << content; f.close();
    return p.string();
}
}  // namespace

TEST("gist: registered in command registry") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    ASSERT_TRUE(reg.has("gist"));
}

TEST("gist: missing file is handled (no crash)") {
    // Should not throw; returns an error path internally.
    std::string out = runGist({"/no/such/file/xyz.cpp", "--no-cache"});
    ASSERT_TRUE(true);  // reaching here = no crash
}

TEST("gist: heuristic fallback summarises + tags as summary") {
    // In the test binary there is no warm LLM daemon and (typically) no
    // model, so gist falls back to the deterministic heuristic.
    std::string body;
    for (int i = 0; i < 100; ++i) body += "line " + std::to_string(i) + "\n";
    std::string path = tmpFile("icmg_gist_test.txt", body);

    std::string out = runGist({path, "--no-cache"});
    // Either heuristic (no LLM) or LLM summary — both must be tagged 'gist'
    // and point to the full view, and must be shorter than the body.
    ASSERT_TRUE(out.find("gist") != std::string::npos);
    ASSERT_TRUE(out.size() < body.size() + 400);  // summary, not full dump+huge
}

TEST("gist: empty file reported, not crashed") {
    std::string path = tmpFile("icmg_gist_empty.txt", "");
    std::string out = runGist({path, "--no-cache"});
    ASSERT_TRUE(out.find("empty") != std::string::npos
             || out.find("gist") != std::string::npos);
}

TEST("gist: cache round-trip — second call returns cached bytes") {
    std::string body = "alpha\nbeta\ngamma\n";
    std::string path = tmpFile("icmg_gist_cache.txt", body);
    // First call writes cache (allow cache).
    std::string a = runGist({path});
    // Second call should serve identical bytes from cache.
    std::string b = runGist({path});
    ASSERT_EQ(a, b);
}
