// Phase 32 T3 — review command core: file-list parsing + per-file rule routing.
// Doesn't spawn `git`; tests the diff-output parsing logic in isolation.
#include "../test_main.hpp"
#include <sstream>
#include <vector>
#include <string>

static std::vector<std::string> parseDiffOutput(const std::string& out) {
    std::vector<std::string> changed;
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) changed.push_back(line);
    }
    return changed;
}

TEST("review: parse multi-line git diff output") {
    std::string out = "Views/OrderMenu.cs\nsrc/api/handlers.cpp\ndb/schema.sql\n";
    auto v = parseDiffOutput(out);
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[0], std::string("Views/OrderMenu.cs"));
    ASSERT_EQ(v[2], std::string("db/schema.sql"));
}

TEST("review: empty diff -> empty list") {
    auto v = parseDiffOutput("");
    ASSERT_EQ((int)v.size(), 0);
}

TEST("review: trailing CR stripped (Windows)") {
    std::string out = "a.cs\r\nb.cs\r\n";
    auto v = parseDiffOutput(out);
    ASSERT_EQ((int)v.size(), 2);
    ASSERT_EQ(v[0], std::string("a.cs"));
    ASSERT_EQ(v[1], std::string("b.cs"));
}

TEST("review: skip blank lines") {
    std::string out = "a.cs\n\n\nb.cs\n\n";
    auto v = parseDiffOutput(out);
    ASSERT_EQ((int)v.size(), 2);
}

TEST("review: file stem extraction (template lookup key)") {
    auto stem = [](const std::string& p) {
        auto slash = p.find_last_of("/\\");
        std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        auto dot = base.find_last_of('.');
        return dot == std::string::npos ? base : base.substr(0, dot);
    };
    ASSERT_EQ(stem("Views/OrderMenu.cs"), std::string("OrderMenu"));
    ASSERT_EQ(stem("src/foo.bar.test.ts"), std::string("foo.bar.test"));
    ASSERT_EQ(stem("README"), std::string("README"));
}

int main() {
    std::cout << "=== review tests ===\n";
    return icmg::test::run_all();
}
