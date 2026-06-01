// v1.27.0 (Phase 2): TDD coverage for template_engine slot-fill.

#include "../test_main.hpp"
#include "../../src/compress/template_engine.hpp"

#include <string>

using icmg::compress::applyTemplate;

TEST("template_engine: simple slot subst") {
    std::string layout = "<div class=\"<%cls%>\">hello</div>";
    std::string slots = R"({"cls":"btn-primary"})";
    auto out = applyTemplate(layout, slots);
    ASSERT_EQ(out, std::string("<div class=\"btn-primary\">hello</div>"));
}

TEST("template_engine: multiple slots") {
    std::string layout = "<%greet%>, <%name%>!";
    std::string slots = R"({"greet":"Hello","name":"World"})";
    ASSERT_EQ(applyTemplate(layout, slots), std::string("Hello, World!"));
}

TEST("template_engine: missing slot left as literal") {
    std::string layout = "<%a%>-<%b%>";
    std::string slots = R"({"a":"X"})";
    ASSERT_EQ(applyTemplate(layout, slots), std::string("X-<%b%>"));
}

TEST("template_engine: whitespace in key trimmed") {
    std::string layout = "<% key %>";
    std::string slots = R"({"key":"value"})";
    ASSERT_EQ(applyTemplate(layout, slots), std::string("value"));
}

TEST("template_engine: non-string slot value dumped as JSON") {
    std::string layout = "n=<%count%>";
    std::string slots = R"({"count":42})";
    ASSERT_EQ(applyTemplate(layout, slots), std::string("n=42"));
}

TEST("template_engine: unclosed %> appended literal") {
    std::string layout = "before <%key";
    std::string slots = R"({"key":"x"})";
    // No closing — entire `<%key` preserved.
    auto out = applyTemplate(layout, slots);
    ASSERT_TRUE(out.find("<%key") != std::string::npos);
}

TEST("template_engine: empty layout returns empty") {
    ASSERT_EQ(applyTemplate("", R"({})"), std::string(""));
}

TEST("template_engine: no slots in layout passthrough") {
    ASSERT_EQ(applyTemplate("plain text", R"({"foo":"bar"})"),
              std::string("plain text"));
}

TEST("template_engine: malformed JSON returns layout unchanged + sets error") {
    std::string err;
    std::string out = applyTemplate("layout <%k%>", "not-json", &err);
    ASSERT_EQ(out, std::string("layout <%k%>"));
    ASSERT_TRUE(!err.empty());
}

TEST("template_engine: non-object JSON returns layout unchanged + sets error") {
    std::string err;
    std::string out = applyTemplate("layout <%k%>", R"(["array","not","obj"])", &err);
    ASSERT_EQ(out, std::string("layout <%k%>"));
    ASSERT_TRUE(!err.empty());
}

TEST("template_engine: nested <% inside slot value preserved") {
    std::string layout = "<%v%>";
    std::string slots = R"({"v":"<%inner%>"})";
    // Subst once; nested marker NOT re-expanded.
    ASSERT_EQ(applyTemplate(layout, slots), std::string("<%inner%>"));
}

TEST("template_engine: adjacent slots no separator") {
    std::string layout = "<%a%><%b%><%c%>";
    std::string slots = R"({"a":"1","b":"2","c":"3"})";
    ASSERT_EQ(applyTemplate(layout, slots), std::string("123"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
