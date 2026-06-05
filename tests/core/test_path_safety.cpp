// v1.65 S1: isSafeToolPath — reject shell-meta + traversal on untrusted
// (MCP) path args before shell interpolation.

#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"
#include <filesystem>

using icmg::core::isSafeToolPath;
using icmg::core::absolutePath;

TEST("safepath: legit relative + absolute paths allowed") {
    ASSERT_TRUE(isSafeToolPath("src/foo.cpp"));
    ASSERT_TRUE(isSafeToolPath("images/screenshot.png"));
    ASSERT_TRUE(isSafeToolPath("C:/Users/me/file.txt"));
    ASSERT_TRUE(isSafeToolPath("/home/me/a.icmg-port"));
    ASSERT_TRUE(isSafeToolPath("a/b/c/d.png"));
}

TEST("safepath: empty rejected") {
    ASSERT_FALSE(isSafeToolPath(""));
}

TEST("safepath: shell-meta rejected") {
    ASSERT_FALSE(isSafeToolPath("f.png\" ; rm -rf /"));   // quote+semicolon
    ASSERT_FALSE(isSafeToolPath("f.png && evil"));         // &
    ASSERT_FALSE(isSafeToolPath("f.png | cat"));           // pipe
    ASSERT_FALSE(isSafeToolPath("$(whoami).png"));         // $ + ()
    ASSERT_FALSE(isSafeToolPath("`id`.png"));              // backtick
    ASSERT_FALSE(isSafeToolPath("f.png > /etc/x"));        // redirect
    ASSERT_FALSE(isSafeToolPath("a'b.png"));               // single quote
    ASSERT_FALSE(isSafeToolPath("glob*.png"));             // wildcard
}

TEST("safepath: control chars / newline rejected") {
    ASSERT_FALSE(isSafeToolPath("f.png\nrm x"));
    ASSERT_FALSE(isSafeToolPath("f\t.png"));
    std::string withnul = "a"; withnul.push_back('\x01'); withnul += ".png";
    ASSERT_FALSE(isSafeToolPath(withnul));
}

TEST("safepath: parent-dir traversal rejected") {
    ASSERT_FALSE(isSafeToolPath(".."));
    ASSERT_FALSE(isSafeToolPath("../etc/passwd"));
    ASSERT_FALSE(isSafeToolPath("a/../../etc/passwd"));
    ASSERT_FALSE(isSafeToolPath("..\\..\\windows"));     // backslash variant
    ASSERT_FALSE(isSafeToolPath("dir/.."));               // trailing /..
}

TEST("safepath: dotfile + dot-in-name allowed (not traversal)") {
    ASSERT_TRUE(isSafeToolPath(".icmg/data.db"));
    ASSERT_TRUE(isSafeToolPath("my.file.name.png"));
    ASSERT_TRUE(isSafeToolPath("a..b.png"));   // '..' inside a name, no separator
}

// v2.0.9 (#err126-hardening): absolutePath must never throw — the throwing
// std::filesystem::absolute reaches PathCch on some Windows Server SKUs and
// raises filesystem_error err126, crashing path-arg commands (context/graph).
TEST("absolutePath: relative resolves to absolute, no throw") {
    std::string a = absolutePath("somedir/file.txt");
    ASSERT_TRUE(std::filesystem::path(a).is_absolute());
}

TEST("absolutePath: already-absolute stays absolute") {
#ifdef _WIN32
    std::string a = absolutePath("C:/foo/bar");
#else
    std::string a = absolutePath("/foo/bar");
#endif
    ASSERT_TRUE(std::filesystem::path(a).is_absolute());
}

TEST("absolutePath: empty input -> empty, no throw") {
    ASSERT_EQ(absolutePath(std::string("")), std::string(""));
}
