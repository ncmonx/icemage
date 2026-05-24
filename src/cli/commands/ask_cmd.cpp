// Phase 36 T1: `icmg ask "<question>"` — natural-language meta-router.
//
// Strategy:
//   1. Build per-entry text = description + paraphrases joined.
//   2. If embedder available -> embed question + each entry, cosine rank.
//   3. Else -> BM25 over command + description + paraphrase tokens.
//   4. Print top-K matches with scores. --exec runs top-1 (with confirm).
//
// No LLM call. Works offline once embeddings cached. BM25 fallback covers
// no-Python install case.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../embed/embedder.hpp"
#include "../../llm/llama_runner.hpp"
#include "help_corpus.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <cstdlib>

namespace icmg::cli {

class AskCommand : public BaseCommand {
public:
    std::string name()        const override { return "ask"; }
    std::string description() const override { return "Natural-language router: cosine-match question against icmg commands"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg ask \"<question>\" [options]\n\n"
            "Returns top-N matching commands ranked by relevance.\n\n"
            "Options:\n"
            "  --top N         Show top N (default 3)\n"
            "  --exec          After top-1 shown, run it (with confirm)\n"
            "  --no-confirm    Skip confirm with --exec (CI use)\n"
            "  --json          Machine-parseable\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool exec       = hasFlag(args, "--exec");
        bool no_confirm = hasFlag(args, "--no-confirm");
        bool json_out   = hasFlag(args, "--json");
        int top = 3;
        try { top = std::stoi(flagValue(args, "--top", "3")); } catch (...) {}

        std::string question;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!question.empty()) question += " ";
            question += a;
        }
        if (question.empty()) {
            std::cerr << "icmg ask: <question> required\n";
            return 1;
        }

        // v1.31.0 A6: --backend=local routes to LlamaRunner for natural-
        // language answer (vs default meta-router which matches command corpus).
        // Falls back to meta-router on any LLM failure -- never blocks.
        std::string backend = flagValue(args, "--backend", "router");
        if (backend == "local") {
            if (!llm::LlamaRunner::available()) {
                std::cerr << "icmg ask --backend=local: build lacks ICMG_USE_LLAMA. "
                             "Falling back to router.\n";
            } else {
                namespace fs = std::filesystem;
                const char* home =
#ifdef _WIN32
                    std::getenv("USERPROFILE");
#else
                    std::getenv("HOME");
#endif
                fs::path lldir = (home && *home ? fs::path(home) : fs::current_path()) / ".icmg" / "llm";
                std::error_code _ec;
                if (fs::exists(lldir / "disabled", _ec)) {
                    std::cerr << "icmg ask --backend=local: opt-out active. Run `icmg llm enable`.\n";
                    return 4;
                }
                std::string active;
                { std::ifstream af(lldir / "active"); std::getline(af, active); }
                if (active.empty()) {
                    std::cerr << "icmg ask --backend=local: no active model. Run `icmg llm use <id>`.\n";
                    return 5;
                }
                fs::path gguf = lldir / active / "model.gguf";
                if (!fs::exists(gguf, _ec)) {
                    std::cerr << "icmg ask --backend=local: model not installed: " << gguf.string() << "\n";
                    return 6;
                }
                llm::LlamaRunner r;
                if (!r.load(gguf.string())) {
                    std::cerr << "icmg ask --backend=local: load failed: " << r.lastError()
                              << " -- falling back to router.\n";
                } else {
                    llm::InferParams ip;
                    ip.max_tokens = 256;
                    ip.temperature = 0.4f;
                    std::string prompt = "You are icmg, a CLI assistant. Answer concisely.\n\nQuestion: "
                                         + question + "\n\nAnswer:";
                    auto res = r.infer(prompt, ip);
                    if (res.ok) {
                        std::cout << res.text << "\n";
                        return 0;
                    }
                    std::cerr << "icmg ask --backend=local: infer failed: " << res.error
                              << " -- falling back to router.\n";
                }
            }
        }

        auto corpus = helpCorpus();
        struct Score { std::string command; std::string desc; double score; };
        std::vector<Score> scored;

        // Try embedder; fall back to BM25-style token overlap.
        auto embedder = embed::makeEmbedder();
        if (embedder && embedder->available()) {
            auto qvec = embedder->embed(question);
            for (auto& e : corpus) {
                std::string blob = e.description;
                for (auto& p : e.paraphrases) blob += " " + p;
                auto evec = embedder->embed(blob);
                double sim = (double)embed::cosine(qvec, evec);
                scored.push_back({e.command, e.description, sim});
            }
        } else {
            // BM25-style: token Jaccard over question vs (cmd + desc + paraphrases).
            auto tokens = [](const std::string& s) {
                std::set<std::string> toks;
                std::string cur;
                for (char c : s) {
                    if (std::isalnum((unsigned char)c) || c == '_') {
                        cur.push_back((char)::tolower((unsigned char)c));
                    } else if (!cur.empty()) {
                        if (cur.size() > 2) toks.insert(cur);
                        cur.clear();
                    }
                }
                if (cur.size() > 2) toks.insert(cur);
                return toks;
            };
            auto qtok = tokens(question);
            for (auto& e : corpus) {
                std::string blob = e.command + " " + e.description;
                for (auto& p : e.paraphrases) blob += " " + p;
                auto etok = tokens(blob);
                size_t inter = 0;
                for (auto& t : qtok) if (etok.count(t)) ++inter;
                size_t uni = qtok.size() + etok.size() - inter;
                double j = uni ? (double)inter / uni : 0.0;
                scored.push_back({e.command, e.description, j});
            }
        }

        std::sort(scored.begin(), scored.end(),
                  [](const Score& a, const Score& b){ return a.score > b.score; });
        if ((int)scored.size() > top) scored.resize(top);

        if (json_out) {
            std::cout << "{\"q\":\"" << escJ(question) << "\",\"matches\":[";
            for (size_t i = 0; i < scored.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"cmd\":\"" << escJ(scored[i].command)
                          << "\",\"desc\":\"" << escJ(scored[i].desc)
                          << "\",\"score\":" << std::fixed << std::setprecision(3)
                          << scored[i].score << "}";
            }
            std::cout << "]}\n";
            return 0;
        }

        std::cout << "Q: " << question << "\n\nTop matches:\n";
        for (size_t i = 0; i < scored.size(); ++i) {
            std::cout << "  " << (i+1) << ". [" << std::fixed
                      << std::setprecision(2) << scored[i].score << "]  "
                      << scored[i].command << "\n";
            std::cout << "         " << scored[i].desc << "\n";
        }

        if (exec && !scored.empty()) {
            std::cout << "\nRun top-1: " << scored[0].command << "\n";
            if (!no_confirm) {
                std::cout << "Confirm? [y/N]: " << std::flush;
                std::string ans;
                std::getline(std::cin, ans);
                if (ans != "y" && ans != "Y" && ans != "yes") {
                    std::cout << "(skipped)\n";
                    return 0;
                }
            }
            auto res = core::safeExecShell(scored[0].command, true, 60000);
            if (!res.out.empty()) std::cout << res.out;
            return (res.exit_code == 0) ? 0 : 1;
        }
        std::cout << "\nRun any: copy the command above. Use --exec for top-1 auto-run.\n";
        return 0;
    }

private:
    static std::string escJ(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("ask", AskCommand);

} // namespace icmg::cli
