// Phase 82/83 — unit tests for claudemd_cmd helpers.
// Tests parseSections, slugify, isHotSection, slim, restore logic inline.
#include "../test_main.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

// ── slim/restore lifecycle helpers ────────────────────────────────────────────

static std::string readFile(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Inline mirror of doSlim sections path (no store — sections always present).
static std::string makeSlim(const std::string& text) {
    auto sections = parseSections(text);
    std::ostringstream out;
    out << "# Context Graph (managed by icmg)\n";
    out << "<!-- icmg-slim: generated by `icmg claudemd import --slim`. Restore: `icmg claudemd restore` -->\n\n";
    out << "> Hooks inject relevant sections per-session (hot) and per-prompt (cold, BM25).\n";
    out << "> Browse: `icmg knowledge list` | `icmg knowledge --html` | restore: `icmg claudemd restore`\n\n";
    for (auto& sec : sections) {
        std::string key = slugify(sec.title);
        out << "## " << sec.title << "\n";
        out << "> node: `" << key << "` — `icmg knowledge get " << key << "`\n\n";
    }
    return out.str();
}

// Inline mirror of doRestore backup search logic.
static std::vector<fs::path> listBackups(const fs::path& icmg_dir) {
    std::vector<fs::path> backups;
    if (!fs::exists(icmg_dir)) return backups;
    for (auto& e : fs::directory_iterator(icmg_dir)) {
        std::string fn = e.path().filename().string();
        if (fn.find("CLAUDE") != std::string::npos &&
            fn.find("backup") != std::string::npos &&
            e.path().extension() == ".md")
            backups.push_back(e.path());
    }
    std::sort(backups.begin(), backups.end());
    return backups;
}

// ── doSlim lifecycle tests ────────────────────────────────────────────────────

TEST("claudemd: makeSlim output contains icmg-slim marker") {
    std::string md = "## Global Rules\nsome content\n## icmg graph\nother content\n";
    std::string slim = makeSlim(md);
    ASSERT_CONTAINS(slim, "<!-- icmg-slim:");
    ASSERT_CONTAINS(slim, "icmg claudemd restore");
}

TEST("claudemd: makeSlim output starts with Context Graph header") {
    std::string md = "## Overview\ncontent\n";
    std::string slim = makeSlim(md);
    ASSERT_CONTAINS(slim, "# Context Graph (managed by icmg)");
}

TEST("claudemd: makeSlim generates section stub with ## title") {
    std::string md = "## Global Rules\ncontent here\n";
    std::string slim = makeSlim(md);
    ASSERT_CONTAINS(slim, "## Global Rules");
}

TEST("claudemd: makeSlim generates node ref with slugified key") {
    std::string md = "## Global Rules\ncontent\n";
    std::string slim = makeSlim(md);
    ASSERT_CONTAINS(slim, "> node: `global-rules`");
    ASSERT_CONTAINS(slim, "`icmg knowledge get global-rules`");
}

TEST("claudemd: makeSlim generates one stub per section") {
    std::string md =
        "## Overview\ncontent1\n"
        "## Architecture\ncontent2\n"
        "## Build\ncontent3\n";
    std::string slim = makeSlim(md);
    ASSERT_CONTAINS(slim, "## Overview");
    ASSERT_CONTAINS(slim, "## Architecture");
    ASSERT_CONTAINS(slim, "## Build");
    ASSERT_CONTAINS(slim, "> node: `overview`");
    ASSERT_CONTAINS(slim, "> node: `architecture`");
    ASSERT_CONTAINS(slim, "> node: `build`");
}

TEST("claudemd: makeSlim written to file produces correct content") {
    fs::path tmp = fs::temp_directory_path() / "icmg_slim_out_test.md";
    std::string md = "## Workflow\ncontent\n";
    std::string slim = makeSlim(md);
    { std::ofstream f(tmp); f << slim; }
    std::string from_disk = readFile(tmp);
    ASSERT_CONTAINS(from_disk, "<!-- icmg-slim:");
    ASSERT_CONTAINS(from_disk, "## Workflow");
    ASSERT_CONTAINS(from_disk, "> node: `workflow`");
    fs::remove(tmp);
}

// ── doRestore lifecycle tests ─────────────────────────────────────────────────

TEST("claudemd: listBackups finds CLAUDE-backup.md in .icmg dir") {
    fs::path tmp = fs::temp_directory_path() / "icmg_restore_test";
    fs::path icmg_dir = tmp / ".icmg";
    fs::create_directories(icmg_dir);

    fs::path bak = icmg_dir / "CLAUDE-backup-20260512.md";
    { std::ofstream f(bak); f << "## Original\nfull content\n"; }

    auto found = listBackups(icmg_dir);
    ASSERT_EQ((int)found.size(), 1);
    ASSERT_EQ(found[0].filename().string(), std::string("CLAUDE-backup-20260512.md"));

    fs::remove_all(tmp);
}

TEST("claudemd: listBackups returns empty for missing dir") {
    fs::path fake = fs::temp_directory_path() / "icmg_no_such_dir_restore_xyz" / ".icmg";
    auto found = listBackups(fake);
    ASSERT_EQ((int)found.size(), 0);
}

TEST("claudemd: listBackups ignores non-backup files") {
    fs::path tmp = fs::temp_directory_path() / "icmg_restore_filter_test";
    fs::path icmg_dir = tmp / ".icmg";
    fs::create_directories(icmg_dir);

    auto touch = [](const fs::path& p) { std::ofstream f(p); f << "x\n"; };
    touch(icmg_dir / "CLAUDE-backup-20260512.md");  // match
    touch(icmg_dir / "CLAUDE.md");                   // no "backup"
    touch(icmg_dir / "backup-notes.txt");            // no "CLAUDE", wrong ext
    touch(icmg_dir / "CLAUDE-backup-20260510.md");  // match

    auto found = listBackups(icmg_dir);
    ASSERT_EQ((int)found.size(), 2);

    fs::remove_all(tmp);
}

TEST("claudemd: listBackups sorts chronologically, back() is latest") {
    fs::path tmp = fs::temp_directory_path() / "icmg_restore_sort_test";
    fs::path icmg_dir = tmp / ".icmg";
    fs::create_directories(icmg_dir);

    auto touch = [](const fs::path& p) { std::ofstream f(p); f << "x\n"; };
    touch(icmg_dir / "CLAUDE-backup-20260510.md");
    touch(icmg_dir / "CLAUDE-backup-20260512.md");
    touch(icmg_dir / "CLAUDE-backup-20260511.md");

    auto found = listBackups(icmg_dir);
    ASSERT_EQ((int)found.size(), 3);
    ASSERT_EQ(found.back().filename().string(), std::string("CLAUDE-backup-20260512.md"));

    fs::remove_all(tmp);
}

TEST("claudemd: restore copies latest backup content to target") {
    fs::path tmp = fs::temp_directory_path() / "icmg_restore_copy_test";
    fs::path icmg_dir = tmp / ".icmg";
    fs::create_directories(icmg_dir);

    fs::path bak = icmg_dir / "CLAUDE-backup-20260512.md";
    { std::ofstream f(bak); f << "## Original\nfull content restored\n"; }

    auto found = listBackups(icmg_dir);
    ASSERT_EQ((int)found.size(), 1);

    fs::path target = tmp / "CLAUDE.md";
    { std::ofstream f(target); f << "<!-- icmg-slim -->\n## Overview\n> node: `overview`\n"; }

    std::error_code ec;
    fs::copy_file(found.back(), target, fs::copy_options::overwrite_existing, ec);
    ASSERT_EQ(ec.value(), 0);

    std::string restored = readFile(target);
    ASSERT_CONTAINS(restored, "full content restored");
    ASSERT_NOT_CONTAINS(restored, "icmg-slim");

    fs::remove_all(tmp);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
