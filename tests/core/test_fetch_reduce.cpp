#include "../test_main.hpp"
#include "../../src/core/fetch_reduce.hpp"

using icmg::core::reduceHtml;

TEST("fetch reduce: strips script") {
    std::string html = "<html><body>Hello<script>alert('x')</script>World</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("alert") == std::string::npos);
    ASSERT_TRUE(out.find("Hello") != std::string::npos);
    ASSERT_TRUE(out.find("World") != std::string::npos);
}

TEST("fetch reduce: strips style") {
    std::string html = "<html><head><style>body{color:red}</style></head><body>Text</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("color:red") == std::string::npos);
    ASSERT_TRUE(out.find("Text") != std::string::npos);
}

TEST("fetch reduce: strips nav/aside/footer") {
    std::string html =
        "<html><body><nav>menu</nav><main>real content</main>"
        "<aside>side</aside><footer>foot</footer></body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("menu") == std::string::npos);
    ASSERT_TRUE(out.find("side") == std::string::npos);
    ASSERT_TRUE(out.find("foot") == std::string::npos);
    ASSERT_TRUE(out.find("real content") != std::string::npos);
}

TEST("fetch reduce: extracts title") {
    std::string html = "<html><head><title>Page Title</title></head><body>Body</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("# Page Title") != std::string::npos);
}

TEST("fetch reduce: prefers <main> over body") {
    std::string html =
        "<html><body>Outer<main>Inner Main</main>More outer</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("Inner Main") != std::string::npos);
    // Outer body content should be excluded when <main> is present.
    ASSERT_TRUE(out.find("Outer") == std::string::npos
             || out.find("Outer") > out.find("Inner Main"));
}

TEST("fetch reduce: decodes entities") {
    std::string html = "<html><body>&amp; &lt; &gt; &quot;</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("&") != std::string::npos);
    ASSERT_TRUE(out.find("<") != std::string::npos);
    ASSERT_TRUE(out.find(">") != std::string::npos);
    ASSERT_TRUE(out.find("\"") != std::string::npos);
}

TEST("fetch reduce: caps long output") {
    std::string body = "<html><body>" + std::string(20000, 'x') + "</body></html>";
    auto out = reduceHtml(body, 1000);
    ASSERT_TRUE(out.size() <= 1100);  // cap + "... [truncated]" suffix
    ASSERT_TRUE(out.find("[truncated]") != std::string::npos);
}

TEST("fetch reduce: collapses whitespace") {
    std::string html = "<html><body>a    b\n\n\n  c</body></html>";
    auto out = reduceHtml(html);
    ASSERT_TRUE(out.find("a    b") == std::string::npos);
    ASSERT_TRUE(out.find("a b c") != std::string::npos);
}

int main() {
    std::cout << "=== fetch HTML reduce tests ===\n";
    return icmg::test::run_all();
}
