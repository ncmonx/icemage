// v1.21.2 (U2): bench-recall scenario harness.
//
// `icmg bench-recall [--file <path>] [--json] [--semantic]`
//
// Reads a scenario file (default `bench/recall_scenarios.txt`). Each non-blank,
// non-`#` line is one scenario:
//
//     <query>|<expect-csv>[|<top-k>]
//
//   query       free-text recall query (cannot contain `|`)
//   expect-csv  comma-separated substrings; ALL must appear somewhere in the
//               combined topic+content of the top-K results
//   top-k       optional; default 10
//
// Exit: 0 if all scenarios pass, 1 otherwise (so CI can gate on it).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

struct Scenario {
    std::string query;
    std::vector<std::string> expect;
    int top_k = 10;
    int line  = 0;
};

static std::string trim(std::string s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back()))  s.pop_back();
    return s;
}

static std::vector<std::string> splitCsv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

static std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

class BenchRecallCommand : public BaseCommand {
public:
    std::string name()        const override { return "bench-recall"; }
    std::string description() const override {
        return "Run recall scenario harness (v1.21.2 U2)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg bench-recall [--file <path>] [--json] [--semantic]\n\n"
            "Scenario file format (one per line):\n"
            "  <query>|<expect-csv>[|<top-k>]\n\n"
            "  # comments and blank lines ignored\n\n"
            "Default file: bench/recall_scenarios.txt\n"
            "Exits 1 if any scenario fails (gate-friendly).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        std::string file = flagValue(args, "--file");
        if (file.empty()) file = "bench/recall_scenarios.txt";
        bool json_out = hasFlag(args, "--json");
        bool use_sem  = hasFlag(args, "--semantic");

        namespace fs = std::filesystem;
        if (!fs::exists(file)) {
            std::cerr << "bench-recall: scenario file not found: " << file << "\n";
            std::cerr << "  Create one with lines like:  query|expect1,expect2|10\n";
            return 1;
        }

        std::vector<Scenario> scenarios;
        {
            std::ifstream in(file);
            std::string line;
            int lineno = 0;
            while (std::getline(in, line)) {
                ++lineno;
                std::string t = trim(line);
                if (t.empty() || t[0] == '#') continue;
                Scenario sc; sc.line = lineno;
                auto p1 = t.find('|');
                if (p1 == std::string::npos) {
                    std::cerr << "bench-recall: " << file << ":" << lineno
                              << " missing `|` separator\n";
                    return 1;
                }
                sc.query = trim(t.substr(0, p1));
                std::string rest = t.substr(p1 + 1);
                auto p2 = rest.find('|');
                if (p2 == std::string::npos) {
                    sc.expect = splitCsv(rest);
                } else {
                    sc.expect = splitCsv(rest.substr(0, p2));
                    try { sc.top_k = std::stoi(trim(rest.substr(p2 + 1))); }
                    catch (...) { sc.top_k = 10; }
                }
                if (sc.query.empty() || sc.expect.empty()) {
                    std::cerr << "bench-recall: " << file << ":" << lineno
                              << " empty query or expect\n";
                    return 1;
                }
                scenarios.push_back(std::move(sc));
            }
        }

        if (scenarios.empty()) {
            std::cerr << "bench-recall: no scenarios in " << file << "\n";
            return 1;
        }

        core::Db db(core::Config::instance().projectDbPath("."));
        imem::MemoryStore mem(db);

        int passed = 0, failed = 0;
        std::vector<std::string> failures;
        if (json_out) std::cout << "[";
        bool first = true;

        for (const auto& sc : scenarios) {
            auto rows = use_sem
                ? mem.recallSemantic(sc.query, sc.top_k, 0.5)
                : mem.recall(sc.query, sc.top_k, false);

            std::string blob;
            for (const auto& n : rows) {
                blob += n.topic; blob += '\n';
                blob += n.content; blob += '\n';
            }
            std::string blob_lc = toLower(blob);

            std::vector<std::string> missing;
            for (const auto& exp : sc.expect) {
                if (blob_lc.find(toLower(exp)) == std::string::npos)
                    missing.push_back(exp);
            }
            bool ok = missing.empty();
            if (ok) ++passed; else { ++failed;
                std::ostringstream os;
                os << file << ":" << sc.line << "  query=\"" << sc.query
                   << "\"  missing=[";
                for (size_t i = 0; i < missing.size(); ++i) {
                    if (i) os << ",";
                    os << missing[i];
                }
                os << "]  hits=" << rows.size();
                failures.push_back(os.str());
            }

            if (json_out) {
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"line\":" << sc.line
                          << ",\"query\":\"" << sc.query << "\""
                          << ",\"ok\":" << (ok ? "true" : "false")
                          << ",\"hits\":" << rows.size()
                          << ",\"missing\":[";
                for (size_t i = 0; i < missing.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << "\"" << missing[i] << "\"";
                }
                std::cout << "]}";
            } else {
                std::cout << (ok ? "PASS " : "FAIL ")
                          << "[L" << sc.line << "] \"" << sc.query
                          << "\"  hits=" << rows.size();
                if (!ok) {
                    std::cout << "  missing={";
                    for (size_t i = 0; i < missing.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << missing[i];
                    }
                    std::cout << "}";
                }
                std::cout << "\n";
            }
        }

        if (json_out) std::cout << "]\n";
        else {
            std::cout << "\n=== bench-recall summary ===\n"
                      << "  passed: " << passed << "/" << scenarios.size() << "\n"
                      << "  failed: " << failed << "\n"
                      << "  mode:   " << (use_sem ? "semantic" : "bm25") << "\n";
            if (failed > 0) {
                std::cout << "\nFailures:\n";
                for (const auto& f : failures) std::cout << "  - " << f << "\n";
            }
        }

        return failed == 0 ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("bench-recall", BenchRecallCommand);

} // namespace icmg::cli
