// Phase 67 T22: `icmg shorten` — heuristic prompt rewriter.
//
// Reads stdin (verbose user prompt), strips fillers + politeness + redundancy,
// emits concise version. Saves input tokens before send.
//
// Targets common verbosity patterns:
//   "Could you please help me to..." → ""
//   "I would like you to..."         → ""
//   "Make sure that you..."          → "Ensure"
//   "I think that..."                → ""
//   double articles, repeated whitespace, parenthetical hedges.

#include "../base_command.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/registry.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class ShortenCommand : public BaseCommand {
public:
    std::string name()        const override { return "shorten"; }
    std::string description() const override {
        return "Rewrite verbose prompt to concise form (heuristic strip)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg shorten [options]\n\n"
            "Reads stdin, prints shortened prompt to stdout. Pipe-style.\n\n"
            "Options:\n"
            "  --diff      Print before/after stats\n"
            "  --aggressive  Also strip parenthetical asides\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool show_diff   = hasFlag(args, "--diff");
        bool aggressive  = hasFlag(args, "--aggressive");

        std::ostringstream buf;
        buf.str(core::slurpStdinSafe());
        std::string in = buf.str();
        std::string out = shorten(in, aggressive);

        if (show_diff) {
            std::cerr << "[shorten] " << in.size() << "B → " << out.size() << "B"
                      << "  (" << (100 - (in.empty() ? 0 : 100 * out.size() / in.size()))
                      << "% smaller)\n";
        }
        std::cout << out;
        return 0;
    }

private:
    static std::string shorten(const std::string& s, bool aggressive) {
        std::string r = s;
        // Politeness + filler phrases (case-insensitive). Order matters — longer
        // patterns first so "could you please help me" hits before "could you".
        const std::vector<std::pair<std::regex, std::string>> rules = {
            {std::regex(R"(\b(?:could|can|would)\s+you\s+please\s+help\s+me\s+(?:to\s+)?)", std::regex::icase), ""},
            {std::regex(R"(\b(?:could|can|would)\s+you\s+please\s+)",                       std::regex::icase), ""},
            {std::regex(R"(\bI\s+(?:would|'d)\s+like\s+(?:you\s+)?to\s+)",                  std::regex::icase), ""},
            {std::regex(R"(\bI\s+(?:think|believe|feel)\s+that\s+)",                        std::regex::icase), ""},
            {std::regex(R"(\bIt\s+(?:would\s+be\s+)?(?:great|nice|helpful)\s+if\s+you\s+(?:could\s+)?)", std::regex::icase), ""},
            {std::regex(R"(\bplease\s+make\s+sure\s+(?:that\s+)?you\s+)",                   std::regex::icase), "Ensure "},
            {std::regex(R"(\bmake\s+sure\s+that\s+)",                                       std::regex::icase), "Ensure "},
            {std::regex(R"(\bif\s+(?:that's|that\s+is)\s+(?:ok|okay|alright)\s*[,.]?\s*)",  std::regex::icase), ""},
            {std::regex(R"(\b(?:just|really|basically|actually|simply|literally|honestly)\s+)", std::regex::icase), ""},
            {std::regex(R"(\b(?:I\s+was\s+wondering|I\s+wanted\s+to\s+ask)\s+(?:if\s+)?)",  std::regex::icase), ""},
            {std::regex(R"(\bplease\s+)",                                                   std::regex::icase), ""},
            {std::regex(R"(\bthanks?\s+(?:in\s+advance|so\s+much|a\s+lot)[\s.!]*)",         std::regex::icase), ""},
            {std::regex(R"(\bThank\s+you\b[\s.!]*)",                                        std::regex::icase), ""},
            {std::regex(R"(\bsorry\s+(?:for\s+the\s+)?(?:bother|trouble|hassle)[\s,.]*)",   std::regex::icase), ""},
        };
        for (auto& [re, repl] : rules) r = std::regex_replace(r, re, repl);

        if (aggressive) {
            // Drop parenthetical asides "(just an idea)", "(if possible)"
            r = std::regex_replace(r, std::regex(R"(\s*\([^)]{1,80}\))"), "");
        }

        // Collapse repeated whitespace.
        r = std::regex_replace(r, std::regex(R"([ \t]+)"), " ");
        r = std::regex_replace(r, std::regex(R"(\n{3,})"), "\n\n");
        // Trim leading/trailing whitespace per line.
        std::ostringstream out;
        std::istringstream is(r);
        std::string line;
        while (std::getline(is, line)) {
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
            while (!line.empty() && (line.back()  == ' ' || line.back()  == '\t' || line.back() == '\r')) line.pop_back();
            out << line << "\n";
        }
        std::string final = out.str();
        // Trim trailing newlines from final result.
        while (!final.empty() && (final.back() == '\n' || final.back() == ' ')) final.pop_back();
        return final;
    }
};

ICMG_REGISTER_COMMAND("shorten", ShortenCommand);

} // namespace icmg::cli
