// `icmg hook bash-advice` pure classifier tests (2026-06-28).
// Spec for the pipe-aware redirect decision that closes the 82% search-escape
// hole (grep used as a pipe filter).
#include "../test_main.hpp"
#include "../../src/cli/bash_redirect.hpp"

using namespace icmg::cli;

// --- pipe ending in a self-filter => EXEMPT (model already self-filtering) ---

TEST("redirect: ls | grep is exempt") {
    auto d = classifyBashRedirect("ls -la | grep foo");
    ASSERT_TRUE(d.kind == RedirectKind::Exempt);
    ASSERT_TRUE(d.message.empty());
}

TEST("redirect: curl | grep is exempt") {
    auto d = classifyBashRedirect("curl -s https://api.example.com | grep id");
    ASSERT_TRUE(d.kind == RedirectKind::Exempt);
}

TEST("redirect: strings | grep is exempt") {
    auto d = classifyBashRedirect("strings cmake.exe | grep -i import");
    ASSERT_TRUE(d.kind == RedirectKind::Exempt);
}

TEST("redirect: pipe ending in head is exempt") {
    auto d = classifyBashRedirect("find . -name x.cpp | head -3");
    ASSERT_TRUE(d.kind == RedirectKind::Exempt);
}

TEST("redirect: multi-stage pipe ending in grep then wc is exempt") {
    auto d = classifyBashRedirect("cat log | grep ERROR | wc -l");
    ASSERT_TRUE(d.kind == RedirectKind::Exempt);
}

// --- pipe NOT ending in a self-filter => quoted redirect ---

TEST("redirect: cat | python => quoted redirect") {
    auto d = classifyBashRedirect("cat data.txt | python script.py");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectQuoted);
    ASSERT_CONTAINS(d.message, "icmg run \"cat data.txt | python script.py\"");
}

TEST("redirect: quoted message warns about quotes") {
    auto d = classifyBashRedirect("echo x | someproc");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectQuoted);
    ASSERT_CONTAINS(d.message, "quotes");
}

// --- logical-or is NOT a pipe ---

TEST("redirect: logical-or is not treated as a pipe") {
    auto d = classifyBashRedirect("test -f x || echo missing");
    // no real pipe -> falls to single-command path (echo... actually first tok 'test')
    ASSERT_TRUE(d.kind == RedirectKind::RedirectPlain);
}

// --- single-command per-command advice ---

TEST("redirect: sed points to fuzzy-edit") {
    auto d = classifyBashRedirect("sed -i 's/a/b/' file.txt");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectPlain);
    ASSERT_CONTAINS(d.message, "fuzzy-edit");
}

TEST("redirect: python points to calc") {
    auto d = classifyBashRedirect("python -c 'print(2+2)'");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectPlain);
    ASSERT_CONTAINS(d.message, "icmg calc");
}

TEST("redirect: tail points to context --tail") {
    auto d = classifyBashRedirect("tail -50 build.log");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectPlain);
    ASSERT_CONTAINS(d.message, "--tail N");
}

TEST("redirect: generic noisy command falls back to icmg run") {
    auto d = classifyBashRedirect("cmake --build build");
    ASSERT_TRUE(d.kind == RedirectKind::RedirectPlain);
    ASSERT_CONTAINS(d.message, "icmg run");
}

// --- self-filter detection unit ---

TEST("redirect: isSelfFilter recognizes common reducers") {
    ASSERT_TRUE(redir_detail::isSelfFilter("grep"));
    ASSERT_TRUE(redir_detail::isSelfFilter("head"));
    ASSERT_TRUE(redir_detail::isSelfFilter("wc"));
    ASSERT_TRUE(redir_detail::isSelfFilter("Select-String"));
    ASSERT_FALSE(redir_detail::isSelfFilter("python"));
    ASSERT_FALSE(redir_detail::isSelfFilter("cat"));
}

TEST("redirect: lastPipeStage skips logical-or") {
    ASSERT_EQ(redir_detail::lastPipeStage("a || b"), std::string(""));
    ASSERT_CONTAINS(redir_detail::lastPipeStage("a | grep b"), "grep b");
}
