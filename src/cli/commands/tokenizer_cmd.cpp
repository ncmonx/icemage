// `icmg tokenizer` — manage opt-in exact BPE tokenizer vocabs.
//
// icmg is model-agnostic; users on GPT/OpenAI-family models (Copilot, Cursor,
// ChatGPT) get *exact* token counts from the matching tiktoken vocab. Default
// stays the zero-dependency heuristic. This command downloads a vocab on consent
// (like the ONNX/GGUF model fetch) and reports status. After download, set
// ICMG_TOKENIZER=bpe-cl100k (or bpe-o200k) to switch the count backend.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/http_stream.hpp"
#include "../../core/path_utils.hpp"      // icmgGlobalDir
#include "../../core/bpe_tokenizer.hpp"
#include "../../core/token_counter.hpp"   // countTokens (backend-aware)
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>

namespace icmg::cli {
namespace fs = std::filesystem;

class TokenizerCommand : public BaseCommand {
public:
    std::string name()        const override { return "tokenizer"; }
    std::string description() const override { return "Manage opt-in exact BPE tokenizer vocabs (GPT cl100k/o200k)"; }
    void usage() const override {
        std::cout << "Usage: icmg tokenizer download [cl100k|o200k] | status | count <text...>\n"
                  << "  download   fetch a tiktoken vocab (~1.7 MB) to ~/.icmg/tokenizer/\n"
                  << "  count ...  print token count of the text under the active backend\n"
                  << "  status     show which vocabs are present + the active backend\n"
                  << "  Then: ICMG_TOKENIZER=bpe-cl100k (or bpe-o200k) for exact GPT counts.\n";
    }

    int run(const std::vector<std::string>& args) override {
        const std::string dir = core::icmgGlobalDir() + "/tokenizer";
        const std::string sub = args.empty() ? "" : args[0];

        if (sub == "download") {
            std::string which = (args.size() > 1) ? args[1] : "cl100k";
            std::string fname, url, sha;
            if (which == "o200k") {
                fname = "o200k_base.tiktoken";
                url   = "https://openaipublic.blob.core.windows.net/encodings/o200k_base.tiktoken";
                sha   = "446a9538cb6c348e3516120d7c08b09f57c36495e2acfffe59a5bf8b0cfb1a2d";
            } else {
                which = "cl100k";
                fname = "cl100k_base.tiktoken";
                url   = "https://openaipublic.blob.core.windows.net/encodings/cl100k_base.tiktoken";
                sha   = "223921b76ee99bde995b7ff738513eef100fb51d18c93597a113bcffe865b2a7";
            }
            std::error_code ec;
            fs::create_directories(dir, ec);
            const std::string dest = dir + "/" + fname;

            std::cout << "Downloading " << which << " vocab (~1.7 MB) from openaipublic ...\n";
            auto r = core::downloadToFile(url, dest);
            if (!r.ok) {
                std::cerr << "icmg tokenizer: download failed: " << r.error
                          << " (http " << r.http_status << ")\n";
                return 1;
            }
            std::cout << "  " << r.bytes << " bytes -> " << dest << "\n";
            if (!core::verifySha256(dest, sha))
                std::cout << "  [warn] SHA256 mismatch (vocab may have changed upstream); file kept.\n";

            core::BpeTokenizer t;   // structural validation: must parse as ranks
            if (!t.loadRanks(dest)) {
                std::cerr << "icmg tokenizer: downloaded file did not parse as tiktoken ranks.\n";
                return 1;
            }
            std::cout << "  OK. Set ICMG_TOKENIZER=bpe-" << which << " for exact GPT-family counts.\n";
            return 0;
        }

        if (sub == "status") {
            const char* env = std::getenv("ICMG_TOKENIZER");
            std::cout << "Active backend (ICMG_TOKENIZER): "
                      << (env && *env ? env : "heuristic (default)") << "\n";
            for (const char* f : {"cl100k_base.tiktoken", "o200k_base.tiktoken"}) {
                std::error_code ec;
                bool present = fs::exists(dir + "/" + f, ec);
                std::cout << "  " << f << ": " << (present ? "present" : "absent") << "\n";
            }
            std::cout << "  vocab dir: " << dir << "\n";
            return 0;
        }

        if (sub == "count") {
            // Count tokens of the remaining args (joined by spaces) under the active
            // backend. Lets a user sanity-check, and validates the vocab end-to-end.
            std::string text;
            for (size_t i = 1; i < args.size(); ++i) { if (i > 1) text += ' '; text += args[i]; }
            std::cout << core::countTokens(text) << "\n";
            return 0;
        }

        usage();
        return sub.empty() ? 0 : 1;
    }
};

ICMG_REGISTER_COMMAND("tokenizer", TokenizerCommand);

}  // namespace icmg::cli
