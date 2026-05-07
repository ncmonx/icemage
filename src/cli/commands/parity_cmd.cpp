// `icmg parity <ref> <new>` — symbol-level feature-parity check.
//
// Use case: "buat menu baru copy dari ProductMenu" — Claude sometimes drops
// handlers / properties. Run parity post-creation to catch drift.
//
// Output:
//   MISSING_IN_NEW  — symbols in ref not present in new (the bug class)
//   EXTRA_IN_NEW    — added in new (informational, not an error)
//   RENAMED?        — heuristic match (Levenshtein <= 3) — opt-in
//
// Exit code = count of MISSING_IN_NEW. 0 = full parity.
//
// Both files must be in graph_nodes (run `icmg graph scan` first). Falls
// back gracefully with clear error if not.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include "../../graph/graph_node.hpp"
#include <iostream>
#include <set>
#include <map>
#include <regex>
#include <algorithm>

namespace icmg::cli {

class ParityCommand : public BaseCommand {
public:
    std::string name()        const override { return "parity"; }
    std::string description() const override { return "Symbol-level parity check between two files"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg parity <ref-file> <new-file> [options]\n\n"
            "Lists symbols in ref absent from new (likely missed during clone).\n\n"
            "Options:\n"
            "  --kind K            Filter to one kind (method/property/field/class)\n"
            "  --ignore PATTERN    Regex skip (matches against symbol_name)\n"
            "  --detect-renames    Levenshtein heuristic (off by default)\n"
            "  --json              Machine output\n\n"
            "Exit code: count of MISSING_IN_NEW. 0 = full parity.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        std::vector<std::string> positional;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a.empty()) continue;
            if (a[0] == '-') {
                if (a == "--kind" || a == "--ignore") ++i;   // skip value
                continue;
            }
            positional.push_back(a);
        }
        if (positional.size() < 2) {
            std::cerr << "icmg parity: requires <ref-file> <new-file>\n";
            usage();
            return 1;
        }
        std::string ref = positional[0];
        std::string nw  = positional[1];

        std::string kind_filter = flagValue(args, "--kind");
        std::string ignore_pat  = flagValue(args, "--ignore");
        bool detect_renames     = hasFlag(args, "--detect-renames");
        bool json_out           = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto ref_node = store.getNode(ref);
        auto new_node = store.getNode(nw);
        if (!ref_node) {
            std::cerr << "icmg parity: ref file not in graph: " << ref
                      << " (run `icmg graph scan` first)\n";
            return 2;
        }
        if (!new_node) {
            std::cerr << "icmg parity: new file not in graph: " << nw
                      << " (run `icmg graph scan` first)\n";
            return 2;
        }

        auto ref_syms = store.childrenOf(ref_node->id);
        auto new_syms = store.childrenOf(new_node->id);

        std::regex ig;
        bool has_ignore = false;
        if (!ignore_pat.empty()) {
            try { ig = std::regex(ignore_pat); has_ignore = true; }
            catch (...) { std::cerr << "icmg parity: invalid --ignore regex\n"; return 1; }
        }
        auto matches_filter = [&](const graph::GraphNode& s) {
            if (!kind_filter.empty() && s.kind != kind_filter) return false;
            if (has_ignore && std::regex_search(s.symbol_name, ig)) return false;
            return true;
        };

        // Build symbol-name sets per file.
        std::map<std::string, std::string> ref_map; // name -> kind
        std::map<std::string, std::string> new_map;
        for (auto& s : ref_syms) if (matches_filter(s)) ref_map[s.symbol_name] = s.kind;
        for (auto& s : new_syms) if (matches_filter(s)) new_map[s.symbol_name] = s.kind;

        std::vector<std::pair<std::string, std::string>> missing, extra;
        for (auto& [name, kind] : ref_map) {
            if (new_map.find(name) == new_map.end()) missing.push_back({name, kind});
        }
        for (auto& [name, kind] : new_map) {
            if (ref_map.find(name) == ref_map.end()) extra.push_back({name, kind});
        }

        // Rename heuristic: pair MISSING ↔ EXTRA by Levenshtein distance.
        std::vector<std::tuple<std::string, std::string, int>> renames;
        if (detect_renames) {
            std::set<size_t> matched_extra;
            for (auto& [m_name, m_kind] : missing) {
                int best = 999; size_t best_idx = SIZE_MAX;
                for (size_t i = 0; i < extra.size(); ++i) {
                    if (matched_extra.count(i)) continue;
                    if (extra[i].second != m_kind) continue;
                    int d = lev(m_name, extra[i].first);
                    if (d < best) { best = d; best_idx = i; }
                }
                if (best <= 3 && best_idx != SIZE_MAX) {
                    renames.emplace_back(m_name, extra[best_idx].first, best);
                    matched_extra.insert(best_idx);
                }
            }
        }

        // Output.
        if (json_out) {
            std::cout << "{\"ref\":\"" << escape_json(ref) << "\",\"new\":\""
                      << escape_json(nw) << "\","
                      << "\"missing\":[";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << escape_json(missing[i].first)
                          << "\",\"kind\":\"" << escape_json(missing[i].second) << "\"}";
            }
            std::cout << "],\"extra\":[";
            for (size_t i = 0; i < extra.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << escape_json(extra[i].first)
                          << "\",\"kind\":\"" << escape_json(extra[i].second) << "\"}";
            }
            std::cout << "],\"renamed\":[";
            for (size_t i = 0; i < renames.size(); ++i) {
                if (i) std::cout << ",";
                auto& [a, b, d] = renames[i];
                std::cout << "{\"from\":\"" << escape_json(a)
                          << "\",\"to\":\"" << escape_json(b)
                          << "\",\"dist\":" << d << "}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "Parity: " << ref << " -> " << nw << "\n";
            std::cout << "  MISSING (" << missing.size() << "): ";
            for (auto& [n, k] : missing) std::cout << " " << n << "(" << k << ")";
            std::cout << "\n";
            std::cout << "  EXTRA   (" << extra.size() << "): ";
            for (auto& [n, k] : extra) std::cout << " " << n << "(" << k << ")";
            std::cout << "\n";
            if (detect_renames && !renames.empty()) {
                std::cout << "  RENAMED?:";
                for (auto& [a, b, d] : renames)
                    std::cout << " " << a << " -> " << b << " (d=" << d << ")";
                std::cout << "\n";
            }
            std::cout << "Exit: " << missing.size() << "\n";
        }

        return (int)missing.size();
    }

private:
    static std::string flagValue(const std::vector<std::string>& args,
                                  const std::string& key, const std::string& def = "") {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == key && i + 1 < args.size()) return args[i + 1];
        }
        return def;
    }

    // Levenshtein distance, capped — early exit when threshold exceeded keeps cost low.
    static int lev(const std::string& a, const std::string& b) {
        const int N = (int)a.size(), M = (int)b.size();
        if (std::abs(N - M) > 5) return 999;
        std::vector<int> prev(M + 1), cur(M + 1);
        for (int j = 0; j <= M; ++j) prev[j] = j;
        for (int i = 1; i <= N; ++i) {
            cur[0] = i;
            for (int j = 1; j <= M; ++j) {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            }
            std::swap(prev, cur);
        }
        return prev[M];
    }

    static std::string escape_json(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("parity", ParityCommand);

} // namespace icmg::cli
