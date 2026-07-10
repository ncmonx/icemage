#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/tkil/base_filter.hpp"
#include <nlohmann/json.hpp>
#include <string>

using icmg::core::Registry;
using icmg::tkil::BaseFilter;

// ---- GhFilter -------------------------------------------------------------
// Root cause (2026-07-10): found while investigating why `icmg savings`
// showed only ~17% saved for a release/CI-heavy session -- `gh api <endpoint>`
// (GitHub REST API JSON, always pretty-printed by default with 2-space
// indent unless --jq/-q is used) had NO registered Tkil filter at all, so it
// fell through to CmdType::Default and passed through byte-for-byte. In
// production telemetry a single `gh api gists/<id>` call emitted 36,741 raw
// bytes with filtered_bytes IDENTICAL (0% saved), and `gh api repos/<owner>/
// <repo>` likewise (6,165 -> 6,166, i.e. worse than raw).
//
// Fix: minify (strip all pretty-print whitespace/indentation) when the raw
// output round-trips as valid JSON -- a strictly LOSSLESS transform (the
// parsed value is byte-for-byte identical after re-parsing), typically
// cutting 30-50% of bytes for `gh api`'s default 2-space-indent output. Falls
// through to pass-the-raw-bytes-through unchanged for non-JSON `gh`
// subcommands (gh pr view, gh run list, plain-text errors) -- never crashes,
// never corrupts, never invents a truncation that could hide real data.

TEST("GhFilter: pretty-printed JSON object gets minified with zero data loss") {
    auto f = Registry<BaseFilter>::instance().create("gh");
    std::string pretty = R"JSON({
  "id": "7d6a2efa9d6191e28ff3f6a26e6ba7c7",
  "files": {
    "ctest.json": {
      "content": "{\"schemaVersion\":1,\"label\":\"ctest\",\"message\":\"2304/2304\"}"
    }
  },
  "owner": {
    "login": "ncmonx"
  }
})JSON";
    auto result = f->filter(pretty, "gh api gists/7d6a2efa9d6191e28ff3f6a26e6ba7c7");
    // Strictly smaller (indentation stripped).
    ASSERT_TRUE(result.output.size() < pretty.size());
    // Lossless: re-parsing the filtered output yields an IDENTICAL json value
    // to re-parsing the original.
    auto original_parsed = nlohmann::json::parse(pretty);
    auto filtered_parsed = nlohmann::json::parse(result.output);
    ASSERT_TRUE(original_parsed == filtered_parsed);
}

TEST("GhFilter: already-compact JSON (no indentation) passes through with no error") {
    auto f = Registry<BaseFilter>::instance().create("gh");
    std::string compact = R"({"id":"abc123","name":"icemage"})";
    auto result = f->filter(compact, "gh api repos/ncmonx/icemage");
    auto original_parsed = nlohmann::json::parse(compact);
    auto filtered_parsed = nlohmann::json::parse(result.output);
    ASSERT_TRUE(original_parsed == filtered_parsed);
}

TEST("GhFilter: non-JSON gh output (e.g. gh pr view table) passes through unchanged, no crash") {
    auto f = Registry<BaseFilter>::instance().create("gh");
    std::string table = "title:\tv2.16.0\nstate:\tOPEN\nurl:\thttps://github.com/x/y/pull/1\n";
    auto result = f->filter(table, "gh pr view 1");
    ASSERT_CONTAINS(result.output, "v2.16.0");
    ASSERT_CONTAINS(result.output, "OPEN");
}

TEST("GhFilter: malformed/truncated JSON does not crash, falls back to raw passthrough") {
    auto f = Registry<BaseFilter>::instance().create("gh");
    std::string broken = R"({"id": "abc, "unterminated)";
    auto result = f->filter(broken, "gh api gists/xyz");
    // Must not throw and must not silently discard the (unparseable) content.
    ASSERT_CONTAINS(result.output, "abc");
}

TEST("GhFilter: large JSON array of gist/repo objects still round-trips losslessly") {
    auto f = Registry<BaseFilter>::instance().create("gh");
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 50; ++i) {
        arr.push_back({{"name", "repo" + std::to_string(i)}, {"stars", i * 3}});
    }
    std::string pretty = arr.dump(2);
    auto result = f->filter(pretty, "gh api users/ncmonx/repos");
    ASSERT_TRUE(result.output.size() < pretty.size());
    auto original_parsed = nlohmann::json::parse(pretty);
    auto filtered_parsed = nlohmann::json::parse(result.output);
    ASSERT_TRUE(original_parsed == filtered_parsed);
}
