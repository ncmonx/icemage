// TDD guard for #4 (stable tool list -- anti KV-cache bust):
//   Registry::keys() must return names in a DETERMINISTIC (sorted) order. The
//   MCP server advertises the tool list to an AI client every connection; tool
//   definitions sit in the cached prompt prefix. If keys() iterates an
//   unordered_map its order can vary between builds/runs, reshuffling the tool
//   list and busting the client's KV-cache (cached input ~10% price re-billed as
//   fresh ~100%). Sorting keys() makes the advertised list stable. Pure -- no DB.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace icmg;

namespace {
struct DummyReg {
    virtual ~DummyReg() = default;
};
}  // namespace

TEST("registry: keys() is sorted, stable, and complete") {
    auto& reg = core::Registry<DummyReg>::instance();
    const char* names[] = {"zebra", "alpha", "mike", "bravo", "yankee", "charlie"};
    for (const char* n : names)
        reg.reg(n, []() { return std::make_unique<DummyReg>(); });

    auto keys = reg.keys();
    ASSERT_EQ((int)keys.size(), 6);

    bool sorted = true;
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (keys[i - 1] > keys[i]) sorted = false;
    ASSERT_TRUE(sorted);

    auto keys2 = reg.keys();
    ASSERT_TRUE(keys == keys2);

    std::vector<std::string> want = {"alpha", "bravo", "charlie", "mike", "yankee", "zebra"};
    ASSERT_TRUE(keys == want);
}
