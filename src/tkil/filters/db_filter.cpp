// Phase 21 Task 5c: DB CLI filter — sqlcmd / mysql / mariadb / psql.
// Strips ASCII border noise + middle rows; preserves errors, headers,
// row-count footers, and PRINT/NOTICE diagnostic lines.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include <regex>
#include <algorithm>

namespace icmg::tkil {

class DbFilter : public BaseFilter {
public:
    std::string name() const override { return "db"; }

    FilterResult filter(const std::string& raw, const std::string& command) override {
        FilterResult res;
        auto lines = splitLines(raw);
        res.original_lines = (int)lines.size();

        // Detect dialect from command prefix
        bool is_sqlcmd = command.rfind("sqlcmd", 0) == 0 ||
                         command.rfind("osql",   0) == 0;
        bool is_mysql  = command.rfind("mysql ",   0) == 0 ||
                         command.rfind("mariadb ", 0) == 0;
        bool is_psql   = command.rfind("psql ",    0) == 0;

        // Schema-dump exemption: if invoked with --skip-column-names or
        // detection misses (mysqldump etc.), output untouched.
        if (command.find("dump") != std::string::npos) {
            res.output = raw;
            res.filtered_lines = res.original_lines;
            return res;
        }

        const int MAX_DATA_ROWS = 20;
        const int CONTEXT_LINES = 3;

        std::vector<std::string> kept;
        kept.reserve(std::min<size_t>(lines.size(), 64));

        // Patterns to ALWAYS keep:
        // - SQL Server errors / messages
        static const std::regex re_tsql_err(R"(^(?:Server: )?Msg \d+, Level \d+|^Error|^Sqlcmd: )",
                                            std::regex::icase);
        // - MySQL errors
        static const std::regex re_mysql_err(R"(^ERROR \d+ \(\w+\)|^ERROR \d+:|^mysql: )",
                                              std::regex::icase);
        // - psql NOTICE/WARNING/ERROR
        static const std::regex re_psql_diag(R"(^(?:NOTICE|WARNING|ERROR|FATAL|HINT|DETAIL):)",
                                              std::regex::icase);
        // - Row-count footers
        static const std::regex re_rowcount(
            R"(^\(\d+ rows? affected\)|^\d+ rows? in set|^\(\d+ rows?\)|^Query OK)",
            std::regex::icase);
        // - PRINT output (sqlcmd) — bare lines without column structure
        // (we keep all non-tabular content if no border seen yet)
        // - MySQL ASCII border lines start with `+--` or `+==`
        static const std::regex re_mysql_border(R"(^\+[-=+]+\+\s*$)");
        // - PostgreSQL separator: line of `-+-` chars
        static const std::regex re_psql_sep(R"(^[-+]+\s*$)");

        bool in_data = false;
        int  data_rows_kept = 0;
        int  data_rows_skipped = 0;
        bool border_seen = false;
        bool last_was_skip_marker = false;

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];

            // Always-keep diagnostics
            if (std::regex_search(l, re_tsql_err) ||
                std::regex_search(l, re_mysql_err) ||
                std::regex_search(l, re_psql_diag) ||
                std::regex_search(l, re_rowcount)) {
                kept.push_back(l);
                last_was_skip_marker = false;
                continue;
            }

            // MySQL border: keep first one (delimits header from data),
            // drop subsequent ones
            if (std::regex_match(l, re_mysql_border)) {
                if (!border_seen) {
                    kept.push_back(l);
                    border_seen = true;
                    in_data = true;
                    data_rows_kept = 0;
                } else {
                    // closing border or middle border — drop unless this is the
                    // final border (followed by row count)
                    if (i + 1 < lines.size() &&
                        std::regex_search(lines[i + 1], re_rowcount)) {
                        kept.push_back(l);
                    }
                }
                continue;
            }

            // psql separator: similar
            if (is_psql && std::regex_match(l, re_psql_sep) && !border_seen) {
                kept.push_back(l);
                border_seen = true;
                in_data = true;
                data_rows_kept = 0;
                continue;
            }

            // sqlcmd/osql column-separator: line of dashes (e.g. "----  ----")
            if (is_sqlcmd && !border_seen && !l.empty()) {
                bool all_dash_space = true;
                for (char c : l) if (c != '-' && c != ' ' && c != '\t') { all_dash_space = false; break; }
                if (all_dash_space && l.size() >= 4) {
                    kept.push_back(l);
                    border_seen = true;
                    in_data = true;
                    data_rows_kept = 0;
                    continue;
                }
            }

            // Empty line — pass through if we haven't seen data yet
            if (l.empty()) {
                kept.push_back(l);
                continue;
            }

            // Pre-header: keep everything (PRINT, USE, SET, comment echoes)
            if (!in_data) {
                kept.push_back(l);
                continue;
            }

            // In-data: cap at MAX_DATA_ROWS, then drop middle rows
            if (data_rows_kept < MAX_DATA_ROWS) {
                kept.push_back(l);
                ++data_rows_kept;
            } else {
                ++data_rows_skipped;
                if (!last_was_skip_marker) {
                    kept.push_back("    ... [middle rows truncated]");
                    last_was_skip_marker = true;
                }
            }
        }

        // Stitch
        for (size_t i = 0; i < kept.size(); ++i) {
            res.output += kept[i];
            res.output += '\n';
        }
        res.filtered_lines = (int)kept.size();
        if (data_rows_skipped > 0) res.was_truncated = true;

        return applyHardLimit(res);
    }
};

ICMG_REGISTER_FILTER("db", DbFilter);

} // namespace icmg::tkil
