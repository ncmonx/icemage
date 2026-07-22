#include "../test_main.hpp"
#include "../../src/cli/cache_layout.hpp"
#include <string>

using namespace icmg::cli;

static ContextSegment seg(const std::string& label, const std::string& body, Volatility v) {
    ContextSegment s; s.label = label; s.body = body; s.vol = v; return s;
}

TEST("cache-layout: stable segments ordered before volatile") {
    std::vector<ContextSegment> segs = {
        seg("task",   "TASK: fix bug",        Volatility::Volatile),
        seg("rules",  "RULE: no globals",     Volatility::Stable),
        seg("recall", "MEM: last session",    Volatility::Volatile),
        seg("conv",   "CONV: tabs=4",         Volatility::Stable),
    };
    auto l = assembleCacheAware(segs);
    // stable ("RULE","CONV") must appear before volatile ("TASK","MEM")
    auto pRule = l.text.find("RULE: no globals");
    auto pConv = l.text.find("CONV: tabs=4");
    auto pTask = l.text.find("TASK: fix bug");
    auto pMem  = l.text.find("MEM: last session");
    ASSERT_TRUE(pRule != std::string::npos && pConv != std::string::npos);
    ASSERT_TRUE(pRule < pConv);             // stable relative order preserved
    ASSERT_TRUE(pConv < pTask);             // all stable before all volatile
    ASSERT_TRUE(pTask < pMem);              // volatile relative order preserved
    ASSERT_EQ(l.stable_count, (size_t)2);
    ASSERT_EQ(l.volatile_count, (size_t)2);
}

TEST("cache-layout: only the stable prefix is wrapped in sentinels") {
    std::vector<ContextSegment> segs = {
        seg("rules", "STABLE_BODY", Volatility::Stable),
        seg("task",  "VOLATILE_BODY", Volatility::Volatile),
    };
    auto l = assembleCacheAware(segs);
    ASSERT_TRUE(l.wrapped);
    ASSERT_TRUE(hasCacheWrap(l.text));
    // The volatile body must sit OUTSIDE the cached region: it appears after
    // the closing sentinel.
    auto close = l.text.find("<</CACHED>>");
    auto vpos  = l.text.find("VOLATILE_BODY");
    ASSERT_TRUE(close != std::string::npos && vpos != std::string::npos);
    ASSERT_TRUE(close < vpos);
    // And the stable body is inside (before the close marker).
    ASSERT_TRUE(l.text.find("STABLE_BODY") < close);
}

TEST("cache-layout: prefix hash stable when only volatile changes (cache hit)") {
    auto mk = [](const std::string& taskBody) {
        std::vector<ContextSegment> s = {
            seg("rules", "RULE: immutable", Volatility::Stable),
            seg("conv",  "CONV: fixed",     Volatility::Stable),
            seg("task",  taskBody,          Volatility::Volatile),
        };
        return assembleCacheAware(s);
    };
    auto a = mk("TASK: alpha");
    auto b = mk("TASK: beta");        // different task, same stable content
    ASSERT_EQ(a.prefix_hash, b.prefix_hash);   // prefix identical -> cache hits
    ASSERT_TRUE(!a.prefix_hash.empty());
    ASSERT_EQ(a.prefix_bytes, b.prefix_bytes);
}

TEST("cache-layout: prefix hash changes when stable content changes (cache miss)") {
    auto mk = [](const std::string& rule) {
        std::vector<ContextSegment> s = {
            seg("rules", rule,           Volatility::Stable),
            seg("task",  "TASK: same",   Volatility::Volatile),
        };
        return assembleCacheAware(s);
    };
    auto a = mk("RULE: v1");
    auto b = mk("RULE: v2");
    ASSERT_TRUE(a.prefix_hash != b.prefix_hash);
}

TEST("cache-layout: no stable segments -> nothing wrapped, tail only") {
    std::vector<ContextSegment> segs = {
        seg("task",   "TASK: x", Volatility::Volatile),
        seg("recall", "MEM: y",  Volatility::Volatile),
    };
    auto l = assembleCacheAware(segs);
    ASSERT_TRUE(!l.wrapped);
    ASSERT_TRUE(!hasCacheWrap(l.text));
    ASSERT_EQ(l.prefix_bytes, (size_t)0);
    ASSERT_TRUE(l.prefix_hash.empty());
    ASSERT_TRUE(l.text.find("TASK: x") != std::string::npos);
    ASSERT_TRUE(l.text.find("MEM: y")  != std::string::npos);
}

TEST("cache-layout: empty input -> empty layout, no crash") {
    auto l = assembleCacheAware({});
    ASSERT_TRUE(!l.wrapped);
    ASSERT_EQ(l.stable_count, (size_t)0);
    ASSERT_EQ(l.volatile_count, (size_t)0);
    ASSERT_TRUE(l.text.empty() || l.text.find_first_not_of("\n") == std::string::npos);
}

TEST("cache-layout: empty-body segments are skipped") {
    std::vector<ContextSegment> segs = {
        seg("rules", "",            Volatility::Stable),   // skipped
        seg("conv",  "CONV: real",  Volatility::Stable),
        seg("task",  "",            Volatility::Volatile), // skipped
    };
    auto l = assembleCacheAware(segs);
    ASSERT_EQ(l.stable_count, (size_t)1);
    ASSERT_EQ(l.volatile_count, (size_t)0);
}

TEST("fnv1aHex: deterministic, 16 hex chars, distinct for distinct input") {
    auto h1 = fnv1aHex("hello");
    auto h2 = fnv1aHex("hello");
    auto h3 = fnv1aHex("world");
    ASSERT_EQ(h1, h2);
    ASSERT_EQ(h1.size(), (size_t)16);
    ASSERT_TRUE(h1 != h3);
    ASSERT_TRUE(h1.find_first_not_of("0123456789abcdef") == std::string::npos);
}

// ---- classifyMarkdownSections: pack-blob -> tagged segments -----------------

static const char* kPackBlob =
    "# Task Context: fix the parser\n"
    "task line\n"
    "## Relevant memory (3)\n"
    "- mem a\n"
    "- mem b\n"
    "## Conventions\n"
    "tabs=4, no globals\n"
    "## Files & Symbols (2)\n"
    "### Foo (fn, L1-9)\n"
    "## Recent diff\n"
    "+ added line\n";

TEST("classify: task/memory/diff volatile, conventions/files stable") {
    auto segs = classifyMarkdownSections(kPackBlob);
    // find by header substring
    auto find = [&](const std::string& needle) -> const ContextSegment* {
        for (auto& s : segs) if (s.label.find(needle) != std::string::npos) return &s;
        return nullptr;
    };
    ASSERT_TRUE(find("Task Context") && find("Task Context")->vol == Volatility::Volatile);
    ASSERT_TRUE(find("Relevant memory") && find("Relevant memory")->vol == Volatility::Volatile);
    ASSERT_TRUE(find("Conventions") && find("Conventions")->vol == Volatility::Stable);
    ASSERT_TRUE(find("Files & Symbols") && find("Files & Symbols")->vol == Volatility::Stable);
    ASSERT_TRUE(find("Recent diff") && find("Recent diff")->vol == Volatility::Volatile);
}

TEST("classify -> assemble: stable prefix hash survives task change (end-to-end)") {
    auto mk = [](const std::string& taskLine) {
        std::string blob = std::string("# Task Context: ") + taskLine + "\n" +
            "## Conventions\ntabs=4\n" +
            "## Files & Symbols (1)\n### Foo\n";
        return assembleCacheAware(classifyMarkdownSections(blob));
    };
    auto a = mk("do alpha");
    auto b = mk("do beta");
    ASSERT_EQ(a.prefix_hash, b.prefix_hash);   // conventions+files identical
    ASSERT_TRUE(a.wrapped);
    ASSERT_TRUE(a.stable_count >= 2u);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
