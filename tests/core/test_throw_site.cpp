// Issue #222: err126-class crashes surface as an uncaught std::system_error
// whose THROW SITE is unknown — the "last DLL loaded" hint proved to be a red
// herring twice (ntmarta.dll). throw_site captures the stack of first-chance
// MSVC C++ exceptions (vectored handler) and resolves frames to module+offset
// so the fatal handler can name the throwing module deterministically.
// Pure formatting + ring logic tested here; the Win32 capture is exercised by
// an in-process throw/catch integration check.

#include "../test_main.hpp"
#include "../../src/core/throw_site.hpp"

#include <stdexcept>
#include <string>

using namespace icmg::core;

TEST("throw_site: formatThrowFrame -> module+0xOFFSET") {
    ThrowFrame f{"ggml-vulkan.dll", 0x1a2bu};
    ASSERT_EQ(formatThrowFrame(f), std::string("ggml-vulkan.dll+0x1a2b"));
}

TEST("throw_site: formatThrowFrame unknown module -> ?+0xADDR") {
    ThrowFrame f{"", 0x7ff600001000u};
    ASSERT_EQ(formatThrowFrame(f), std::string("?+0x7ff600001000"));
}

TEST("throw_site: formatThrowStack joins frames, caps at maxFrames") {
    std::vector<ThrowFrame> st{
        {"a.dll", 0x10}, {"b.dll", 0x20}, {"c.dll", 0x30}};
    auto s = formatThrowStack(st, 2);
    ASSERT_CONTAINS(s, std::string("a.dll+0x10"));
    ASSERT_CONTAINS(s, std::string("b.dll+0x20"));
    ASSERT_NOT_CONTAINS(s, std::string("c.dll"));
}

TEST("throw_site: empty stack -> empty string") {
    ASSERT_EQ(formatThrowStack({}, 8), std::string(""));
}

TEST("throw_site: recordThrowStack + lastThrowStackFormatted round-trip") {
    clearThrowStacksForTest();
    ASSERT_EQ(lastThrowStackFormatted(), std::string(""));
    recordThrowStack({{"x.dll", 0x40}});
    recordThrowStack({{"y.dll", 0x50}});
    auto s = lastThrowStackFormatted();
    ASSERT_CONTAINS(s, std::string("y.dll+0x50"));   // most recent wins
    ASSERT_NOT_CONTAINS(s, std::string("x.dll"));
}

#ifdef _WIN32
TEST("throw_site: vectored capture names this test module on a real throw") {
    clearThrowStacksForTest();
    installThrowSiteCapture();
    try {
        throw std::runtime_error("probe");
    } catch (const std::exception&) {
        // first-chance handler ran before this catch
    }
    auto s = lastThrowStackFormatted();
    // The throw happened in THIS binary -> a frame must resolve to a module
    // name (never all-unknown), proving capture + resolution work in-process.
    ASSERT_TRUE(!s.empty());
    ASSERT_CONTAINS(s, std::string(".exe"));
}
#endif

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
