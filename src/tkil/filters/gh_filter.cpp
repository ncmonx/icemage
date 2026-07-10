// GhFilter: strips pretty-print whitespace from `gh api ...` JSON output.
//
// Root cause (2026-07-10): `gh api <endpoint>` (GitHub REST API) had NO
// registered Tkil filter -- CmdType had no `Gh` case, so every call fell
// through to CmdType::Default and passed through byte-for-byte. In
// production telemetry this showed up as a session where `icmg savings`
// reported only ~17% total savings (much lower than the 60-99% typical for
// git/grep/build commands) -- a single `gh api gists/<id>` call emitted
// 36,741 raw bytes with filtered_bytes IDENTICAL to raw (0% saved), and
// `gh api repos/<owner>/<repo>` likewise.
//
// `gh api`'s default output is pretty-printed JSON (2-space indent) unless
// the caller passes --jq/-q. That indentation is pure formatting noise for
// an LLM -- minifying it is a strictly LOSSLESS transform (the parsed value
// is byte-for-byte identical before/after), typically cutting 30-50% of
// bytes. Non-JSON `gh` output (gh pr view tables, plain-text errors) and
// malformed/truncated JSON both fall through to raw passthrough -- this
// filter never invents a truncation that could hide real API data.
#include "../base_filter.hpp"
#include "../../core/registry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace icmg::tkil {

class GhFilter : public BaseFilter {
public:
    FilterResult filter(const std::string& raw_output,
                         const std::string& /*command*/) override {
        FilterResult r;
        r.original_lines = (int)std::count(raw_output.begin(), raw_output.end(), '\n') + 1;

        // Try to parse + re-emit compact. On any parse failure (non-JSON gh
        // output, e.g. `gh pr view` tables, or malformed/truncated JSON),
        // fall through to the raw text unchanged -- never lose data.
        try {
            auto parsed = nlohmann::json::parse(raw_output);
            r.output = parsed.dump();  // no indent arg = compact
            r.notes = "gh: JSON minified (pretty-print whitespace stripped, lossless)";
        } catch (const nlohmann::json::parse_error&) {
            r.output = raw_output;
            r.notes = "gh: non-JSON or unparsable output, passed through unchanged";
        }

        r.filtered_lines = (int)std::count(r.output.begin(), r.output.end(), '\n') + 1;
        return r;
    }

    std::string name() const override { return "gh"; }
};

ICMG_REGISTER_FILTER("gh", GhFilter);

} // namespace icmg::tkil
