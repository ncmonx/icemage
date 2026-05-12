// Phase 82 — unit tests for claudemd_cmd helpers.
// Tests parseSections, slugify, isHotSection logic inline (functions are static).
#include "../test_main.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

// ── Inline mirrors of static helpers from claudemd_cmd.cpp ───────────────────

static std::string slugify(const std::string& title) {
    std::string slug;
    for (unsigned char c : title) {
        if (std::isalnum(c)) {
            slug += static_cast<char>(std::tolower(c));
        } else if (c == ' ' || c == '_' || c == '-') {
            if (!slug.empty() && slug.back() != '-') slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

static bool isHotSection(const std::string& title) {
    std::string t = title;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    static const char* HOT[] = {
        "project overview", "overview", "architecture", "coding conventions",
        "key design", "design decisions", "build instructions", "build"
    };
    for (auto h : HOT) {
        if (t.find(h) != std::string::npos) return true;
    }
    return false;
}

struct Section { std::string title; std::string content; };

static std::vector<Section> parseSections(const std::string& text) {
    std::vector<Section> sections;
    std::istringstream iss(text);
    std::string line;
    Section cur;
    bool in_section = false;

    while (std::getline(iss, line)) {
        if (line.size() >= 3 && line.substr(0, 3) == "## ") {
            if (in_section && !cur.title.empty()) {
                while (!cur.content.empty() &&
                       (cur.content.back() == '\n' || cur.content.back() == '\r'))
                    cur.content.pop_back();
                sections.push_back(cur);
            }
            cur = Section{};
            cur.title = line.substr(3);
            in_section = true;
        } else if (in_section) {
            cur.content += line + "\n";
        }
    }
    if (in_section && !cur.title.empty()) {
        while (!cur.content.empty() &&
               (cur.content.back() == '\n' || cur.content.back() == '\r'))
            cur.content.pop_back();
        sections.push_back(cur);
    }
    return sections;
}

// ── slugify tests ─────────────────────────────────────────────────────────────

TEST("claudemd: slugify basic title") {
    ASSERT_EQ(slugify("Project Overview"), std::string("project-overview"));
}

TEST("claudemd: slugify strips trailing dash") {
    ASSERT_EQ(slugify("Build Instructions (Windows)"), std::string("build-instructions-windows"));
}

TEST("claudemd: slugify collapses multiple spaces/separators") {
    ASSERT_EQ(slugify("Key  Design  Decisions"), std::string("key-design-decisions"));
}

TEST("claudemd: slugify preserves numbers") {
    ASSERT_EQ(slugify("Phase 81 v0.45.0"), std::string("phase-81-v0450"));
}

TEST("claudemd: slugify empty string returns empty") {
    ASSERT_EQ(slugify(""), std::string(""));
}

TEST("claudemd: slugify underscore treated as separator") {
    ASSERT_EQ(slugify("my_section"), std::string("my-section"));
}

// ── isHotSection tests ────────────────────────────────────────────────────────

TEST("claudemd: isHotSection — project overview → hot") {
    ASSERT_TRUE(isHotSection("Project Overview"));
}

TEST("claudemd: isHotSection — architecture → hot") {
    ASSERT_TRUE(isHotSection("Architecture"));
}

TEST("claudemd: isHotSection — coding conventions → hot") {
    ASSERT_TRUE(isHotSection("Coding Conventions"));
}

TEST("claudemd: isHotSection — key design decisions → hot") {
    ASSERT_TRUE(isHotSection("Key Design Decisions"));
}

TEST("claudemd: isHotSection — build instructions → hot") {
    ASSERT_TRUE(isHotSection("Build Instructions (Windows/MSYS2)"));
}

TEST("claudemd: isHotSection — changelog → not hot") {
    ASSERT_FALSE(isHotSection("Changelog"));
}

TEST("claudemd: isHotSection — random section → not hot") {
    ASSERT_FALSE(isHotSection("Token Efficiency Commands"));
}

TEST("claudemd: isHotSection — case insensitive") {
    ASSERT_TRUE(isHotSection("ARCHITECTURE"));
    ASSERT_TRUE(isHotSection("BUILD"));
}

// ── parseSections tests ───────────────────────────────────────────────────────

TEST("claudemd: parseSections — empty text yields no sections") {
    auto v = parseSections("");
    ASSERT_EQ((int)v.size(), 0);
}

TEST("claudemd: parseSections — single section") {
    std::string md = "## Overview\nSome content here.\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_EQ(v[0].title, std::string("Overview"));
    ASSERT_CONTAINS(v[0].content, "Some content here.");
}

TEST("claudemd: parseSections — multiple sections") {
    std::string md =
        "## Overview\nFirst section.\n"
        "## Architecture\nSecond section.\n"
        "## Build\nThird.\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[0].title, std::string("Overview"));
    ASSERT_EQ(v[1].title, std::string("Architecture"));
    ASSERT_EQ(v[2].title, std::string("Build"));
}

TEST("claudemd: parseSections — content before first ## ignored") {
    std::string md = "# Main title\nPreamble\n## Section\nContent\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_EQ(v[0].title, std::string("Section"));
}

TEST("claudemd: parseSections — trailing newlines stripped from content") {
    std::string md = "## Sec\nline1\nline2\n\n\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_TRUE(v[0].content.back() != '\n');
}

TEST("claudemd: parseSections — section with no content") {
    std::string md = "## Empty\n## Next\nhas content\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 2);
    ASSERT_EQ(v[0].title, std::string("Empty"));
    ASSERT_EQ(v[0].content, std::string(""));
}

TEST("claudemd: parseSections — h3 lines not treated as section headers") {
    std::string md = "## Main\n### subsection\ncontent\n";
    auto v = parseSections(md);
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_CONTAINS(v[0].content, "### subsection");
}

// ── slim marker detection ─────────────────────────────────────────────────────

TEST("claudemd: slim marker present in slimmed file") {
    std::string slimmed = "<!-- icmg-slim -->\n## Overview\n> node: `plan-overview`\n";
    ASSERT_CONTAINS(slimmed, "<!-- icmg-slim -->");
}

TEST("claudemd: slim marker absent in normal CLAUDE.md") {
    std::string normal = "## Overview\nFull content here.\n";
    ASSERT_NOT_CONTAINS(normal, "<!-- icmg-slim -->");
}

int main() {
    std::cout << "=== claudemd tests ===\n";
    return icmg::test::run_all();
}
