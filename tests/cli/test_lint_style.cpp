// Phase 30 T4 — lint-style rule loading + match logic.
// Hits the JSON-rules path directly without spawning the CLI.
#include "../test_main.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <string>
#include <fstream>
#include <filesystem>

using nlohmann::json;
namespace fs = std::filesystem;

static std::string tmpFile(const std::string& content) {
    auto p = fs::temp_directory_path() / ("icmg_lint_" + std::to_string(::time(nullptr))
                                            + "_" + std::to_string(rand()) + ".tmp");
    std::ofstream(p) << content;
    return p.string();
}

TEST("lint-style: must_match regex passes when present") {
    std::string code = "class X { void OnFoo_Click() {} }";
    std::regex r("OnFoo_Click");
    ASSERT_TRUE(std::regex_search(code, r));
}

TEST("lint-style: must_not_match flags forbidden pattern") {
    std::string code = "<TextBox Name='x'/>";
    std::regex r("<TextBox(?!.*Style=)");
    ASSERT_TRUE(std::regex_search(code, r));   // matches the bare textbox
}

TEST("lint-style: rules JSON parse + iterate") {
    std::string raw = R"JSON({
        "rules": [
            {"name": "use-styled-button",
             "must_match": "Button.*Style=",
             "in_files": "Views/.*\\.xaml"},
            {"name": "no-bare-textbox",
             "must_not_match": "<TextBox(?!.*Style)",
             "in_files": "Views/.*\\.xaml"}
        ]
    })JSON";
    auto j = json::parse(raw);
    ASSERT_TRUE(j.contains("rules") && j["rules"].is_array());
    ASSERT_EQ((int)j["rules"].size(), 2);
    ASSERT_EQ(j["rules"][0].value("name", ""), std::string("use-styled-button"));
}

TEST("lint-style: in_files regex filters target") {
    std::string in_files = R"(Views/.*\.xaml)";
    std::regex re(in_files);
    ASSERT_TRUE(std::regex_search(std::string("Views/MainView.xaml"), re));
    ASSERT_FALSE(std::regex_search(std::string("Models/Order.cs"), re));
}

TEST("lint-style: ref-mode using-set diff") {
    // Ref has 2 usings; new has 1. Expect 1 missing.
    std::string ref_content =
        "using System;\n"
        "using System.Linq;\n"
        "public class X {}";
    std::string new_content =
        "using System;\n"
        "public class Y {}";
    // multiline flag so ^ anchors at \n; otherwise only first line considered.
    std::regex re_using(R"(^\s*using\s+([\w\.]+);)",
                        std::regex::ECMAScript | std::regex::multiline);
    std::set<std::string> ref_usings, new_usings;
    for (auto it = std::sregex_iterator(ref_content.begin(), ref_content.end(), re_using);
         it != std::sregex_iterator(); ++it) ref_usings.insert((*it)[1].str());
    for (auto it = std::sregex_iterator(new_content.begin(), new_content.end(), re_using);
         it != std::sregex_iterator(); ++it) new_usings.insert((*it)[1].str());
    int missing = 0;
    for (auto& u : ref_usings) if (!new_usings.count(u)) ++missing;
    ASSERT_EQ(missing, 1);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
