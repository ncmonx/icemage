#include "../test_main.hpp"
#include "../../src/core/url_sanitize.hpp"

using icmg::core::isUrlSafe;
using icmg::core::validateUrl;

TEST("url: accepts plain http") {
    ASSERT_TRUE(isUrlSafe("http://example.com/path"));
}

TEST("url: accepts https with simple query + fragment") {
    // & rejected (shell metachar). Single-param queries OK.
    ASSERT_TRUE(isUrlSafe("https://example.com/path?q=value#frag"));
}

TEST("url: accepts URL-encoded chars") {
    ASSERT_TRUE(isUrlSafe("https://example.com/path%20with%20spaces"));
}

TEST("url: rejects shell metacharacter $") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/$(echo)", r));
    ASSERT_TRUE(r.find("'$'") != std::string::npos);
}

TEST("url: rejects backtick") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/`whoami`", r));
}

TEST("url: rejects double quote") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/\"break", r));
}

TEST("url: rejects semicolon") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/;rm", r));
}

TEST("url: rejects pipe") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/|cat", r));
}

TEST("url: rejects ampersand") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/&malicious", r));
}

TEST("url: rejects file:// scheme") {
    std::string r;
    ASSERT_FALSE(validateUrl("file:///etc/passwd", r));
}

TEST("url: rejects empty") {
    std::string r;
    ASSERT_FALSE(validateUrl("", r));
}

TEST("url: rejects too long") {
    std::string r;
    std::string long_url = "https://x.com/" + std::string(5000, 'a');
    ASSERT_FALSE(validateUrl(long_url, r));
}

TEST("url: rejects newline") {
    std::string r;
    ASSERT_FALSE(validateUrl("https://x.com/\nbad", r));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
