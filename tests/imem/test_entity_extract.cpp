// 2026-06-07: rule-based entity extraction (#luna-batch, Layer-0 enrichment).
#include "../test_main.hpp"
#include "../../src/imem/entity_extract.hpp"
#include <algorithm>

using namespace icmg::imem;

static bool has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

TEST("entity: extracts url / ip / env / mention") {
    auto e = extractEntities("see https://example.com/x at 10.0.0.5 set $HOME and %PATH% ask @cahyo");
    ASSERT_TRUE(has(e, "url:https://example.com/x"));
    ASSERT_TRUE(has(e, "ip:10.0.0.5"));
    ASSERT_TRUE(has(e, "env:HOME"));
    ASSERT_TRUE(has(e, "env:PATH"));
    ASSERT_TRUE(has(e, "mention:cahyo"));
}

TEST("entity: dedups repeats") {
    auto e = extractEntities("$X $X $X");
    int n = 0; for (auto& t : e) if (t == "env:X") ++n;
    ASSERT_EQ(n, 1);
}

TEST("entity: empty text -> no entities") {
    ASSERT_EQ(extractEntities("just plain words here").size(), (size_t)0);
}

TEST("entity: caps output") {
    std::string big;
    for (int i = 0; i < 50; ++i) big += " $V" + std::to_string(i);
    ASSERT_TRUE(extractEntities(big, 12).size() <= (size_t)12);
}
