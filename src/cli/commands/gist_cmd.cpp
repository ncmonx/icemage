// v1.63 F12: `icmg gist <file>` — dense TL;DR of a large file before Claude
// reads it. Uses the local LLM when available; falls back to a deterministic
// heuristic summary (head + structure + line count) otherwise. Cached by
// file-content hash so re-running on an unchanged file is instant.
//
// Output is ALWAYS tagged as a summary with a pointer to the full view —
// it is lossy by design and must never be mistaken for the file content.
//
//   icmg gist <file> [--max-tokens N] [--no-cache] [--raw]
//
// Cache: .icmg/gist-cache/<fnv-of-content>.txt (re-summarised when content
// changes because the hash changes).

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef ICMG_USE_LLAMA
#  include "../../llm/llama_runner.hpp"
#  include "../../llm/warm_client.hpp"
#  include "../../llm/warm_pool.hpp"
#  include "../../llm/chat_template.hpp"
#endif

namespace icmg::cli {

namespace {

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Deterministic, LLM-free summary: first ~30 lines + total line/byte count.
// Used as the fallback and unit-tested. Tagged so callers know it's partial.
std::string heuristicGist(const std::string& body, const std::string& path) {
    std::istringstream is(body);
    std::string line;
    std::ostringstream out;
    int total_lines = 0;
    std::ostringstream head;
    const int kHead = 30;
    while (std::getline(is, line)) {
        if (total_lines < kHead) head << line << "\n";
        ++total_lines;
    }
    out << "[gist: heuristic — no LLM; first " << kHead << " lines of "
        << total_lines << " (" << body.size() << " B)]\n"
        << head.str()
        << "--- [summary only; `icmg context " << path << "` for full] ---\n";
    return out.str();
}

// Build the LLM summarisation prompt (testable, pure).
std::string buildGistPrompt(const std::string& body, const std::string& path) {
    std::ostringstream p;
    p << "Summarize the following file concisely (<=180 words). State its "
         "purpose, the key symbols/sections, and any critical caveat. Do NOT "
         "reproduce the code. File: " << path << "\n\n"
      << "```\n" << body << "\n```\n";
    return p.str();
}

}  // namespace

class GistCommand : public BaseCommand {
public:
    std::string name()        const override { return "gist"; }
    std::string description() const override {
        return "Dense LLM/heuristic TL;DR of a file (cached by content hash)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg gist <file> [--max-tokens N] [--no-cache] [--raw]\n\n"
            "  Summarize a large file before reading it in full.\n"
            "  Uses the local LLM when available; deterministic heuristic\n"
            "  fallback otherwise. Cached by content hash.\n"
            "  Output is a SUMMARY — use `icmg context <file>` for full content.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string file;
        for (const auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            file = a; break;
        }
        if (file.empty()) { std::cerr << "icmg gist: need <file>\n"; return 2; }

        namespace fs = std::filesystem;
        std::ifstream f(file, std::ios::binary);
        if (!f) { std::cerr << "icmg gist: cannot open " << file << "\n"; return 2; }
        std::ostringstream ss; ss << f.rdbuf();
        std::string body = ss.str();
        if (body.empty()) { std::cout << "[gist: empty file]\n"; return 0; }

        bool no_cache = hasFlag(args, "--no-cache");
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx",
                      (unsigned long long)fnv1a(body));
        fs::path cache = fs::path(".icmg") / "gist-cache" / (std::string(hex) + ".txt");

        // Cache hit (content unchanged → same hash).
        if (!no_cache) {
            std::error_code ec;
            if (fs::exists(cache, ec)) {
                std::ifstream cf(cache, std::ios::binary);
                std::ostringstream cb; cb << cf.rdbuf();
                std::cout << cb.str();
                return 0;
            }
        }

        std::string gist;

#ifdef ICMG_USE_LLAMA
        int max_tokens = 256;
        try { max_tokens = std::stoi(flagValue(args, "--max-tokens", "256")); }
        catch (...) {}
        std::string prompt = icmg::llm::buildChatMLPrompt(
            "You are a precise code summariser.", buildGistPrompt(body, file));
        llm::InferParams ip;
        ip.max_tokens  = max_tokens;
        ip.temperature = 0.2f;   // factual summary, low creativity
        ip.stop        = icmg::llm::chatMLStopToken();
        bool got = false;
        if (icmg::llm::warmAvailable()) {
            if (auto w = icmg::llm::tryWarmInfer(prompt, ip,
                            std::chrono::milliseconds(150)); w) {
                gist = "[gist: LLM summary — `icmg context " + file +
                       "` for full]\n" + w->text + "\n";
                got = true;
            }
        }
        if (!got) {
            std::string err;
            if (auto* run = llm::WarmPool::instance().acquire(err)) {
                auto res = run->infer(prompt, ip);
                if (res.ok) {
                    gist = "[gist: LLM summary — `icmg context " + file +
                           "` for full]\n" + res.text + "\n";
                    got = true;
                }
            }
        }
        if (!got) gist = heuristicGist(body, file);   // LLM unavailable
#else
        gist = heuristicGist(body, file);
#endif

        // Write cache (best-effort).
        if (!no_cache) {
            std::error_code ec;
            fs::create_directories(cache.parent_path(), ec);
            std::ofstream cf(cache, std::ios::binary);
            if (cf) cf << gist;
        }
        std::cout << gist;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("gist", GistCommand);

}  // namespace icmg::cli
