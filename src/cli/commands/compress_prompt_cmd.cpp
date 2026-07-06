// `icmg compress-prompt` -- honesty-gated salience prompt compressor (feature #6
// closure). Applies the TE2 salience selection + honesty gate from
// prompt_rewrite.hpp to arbitrary prompt text (stdin or --file), so LLMLingua-
// style prompt compression is a first-class, reusable op.
//
// Distinct from siblings (anti-dup):
//   icmg compress        reversible glossary dictionary (round-trippable)
//   icmg shrink          content-type router for command OUTPUT
//   icmg compress-prompt budget-target salience compression of a PROMPT, with
//                        an honesty gate (never emits something larger)
//
// Honesty gate: if compression would not actually shrink the text, the original
// is emitted verbatim -- the tool never blows a prompt up.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/prompt_rewrite.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class CompressPromptCommand : public BaseCommand {
public:
    std::string name()        const override { return "compress-prompt"; }
    std::string description() const override {
        return "Salience-compress a prompt to a char budget (honesty-gated, non-destructive)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg compress-prompt [--budget N] [--file <path>] [--json]\n"
            "  Compress prompt text (stdin or --file) to ~N chars via TE2 salience.\n"
            "  --budget N   target chars (default 3000)\n"
            "  --file P     read from file instead of stdin\n"
            "  --json       print the savings report to stderr as JSON\n"
            "  Honesty gate: if it wouldn't shrink, the original is emitted as-is.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        std::size_t budget = 3000;
        { std::string b = flagValue(args, "--budget");
          if (!b.empty()) { try { budget = (std::size_t)std::stoul(b); } catch (...) {} } }

        std::string input;
        std::string file = flagValue(args, "--file");
        if (!file.empty()) {
            std::ifstream f(file, std::ios::binary);
            if (!f) { std::cerr << "compress-prompt: cannot read " << file << "\n"; return 1; }
            std::ostringstream ss; ss << f.rdbuf(); input = ss.str();
        } else {
            input = core::slurpStdinSafe();
        }
        if (input.empty()) { std::cerr << "compress-prompt: no input (stdin or --file)\n"; return 1; }

        core::RewriteReport rep;
        std::string out = core::compressContext(input, budget, rep);
        std::cout << out;

        if (hasFlag(args, "--json")) {
            std::cerr << "{\"applied\":" << (rep.applied ? "true" : "false")
                      << ",\"before_tokens\":" << rep.before_tokens
                      << ",\"after_tokens\":" << rep.after_tokens
                      << ",\"saved_tokens\":" << (rep.before_tokens - rep.after_tokens) << "}\n";
        } else if (rep.applied) {
            std::cerr << "[compress-prompt] " << rep.before_tokens << " -> "
                      << rep.after_tokens << " tok (saved "
                      << (rep.before_tokens - rep.after_tokens) << ")\n";
        } else {
            std::cerr << "[compress-prompt] within budget; emitted as-is\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("compress-prompt", CompressPromptCommand);

} // namespace icmg::cli
