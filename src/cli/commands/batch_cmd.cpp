// Phase 45 T2: `icmg batch` — Anthropic Batch API request spec emitter.
//
// Reads task list (one per line from file, or repeated --task flags), emits a
// JSON spec consumable by Anthropic's Batch API for a 50% discount on bulk ops.
//
// icmg does not call the API itself — it produces the JSON. User pipes to
// `claude api batch create batch.json` (or any compatible client).

#include "../base_command.hpp"
#include "../batch_builder.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class BatchCommand : public BaseCommand {
public:
    std::string name()        const override { return "batch"; }
    std::string description() const override {
        return "Emit Anthropic Batch API spec from task list (50% discount on bulk ops)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg batch [options]\n\n"
            "Sources (combinable):\n"
            "  --task \"<text>\"       Add one task (repeat for many)\n"
            "  --file <path>          Read tasks from file (one per line)\n\n"
            "Output:\n"
            "  --emit-json            Print Batch API JSON spec to stdout (default)\n"
            "  -o <file>              Write JSON to file\n\n"
            "Per-request defaults:\n"
            "  --model <name>         Default: claude-sonnet-4-5\n"
            "  --max-tokens N         Default: 2000\n"
            "  --no-think             Inject no-think directive in each request\n"
            "  --concise              Inject concise directive\n"
            "  --caveman              Inject caveman directive\n"
            "  --custom-id-prefix P   Default: \"task\" → task-1, task-2, ...\n\n"
            "Pipe pattern:\n"
            "  icmg batch --file tasks.txt --emit-json | curl -X POST \\\n"
            "    https://api.anthropic.com/v1/messages/batches \\\n"
            "    -H 'x-api-key: $ANTHROPIC_API_KEY' \\\n"
            "    -H 'anthropic-version: 2023-06-01' \\\n"
            "    -H 'Content-Type: application/json' --data @-\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || args.empty()) { usage(); return 0; }

        std::vector<std::string> tasks;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--task" && i + 1 < args.size()) {
                tasks.push_back(args[++i]);
            }
        }
        std::string file = flagValue(args, "--file");
        if (!file.empty()) {
            std::ifstream f(file);
            if (!f) {
                std::cerr << "icmg batch: cannot open " << file << "\n";
                return 1;
            }
            std::string line;
            while (std::getline(f, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) tasks.push_back(line);
            }
        }

        if (tasks.empty()) {
            std::cerr << "icmg batch: no tasks (use --task or --file)\n";
            return 1;
        }

        std::string model       = flagValue(args, "--model", "claude-sonnet-4-5");
        int max_tokens          = 2000;
        try { max_tokens = std::stoi(flagValue(args, "--max-tokens", "2000")); } catch (...) {}
        std::string id_prefix   = flagValue(args, "--custom-id-prefix", "task");
        bool no_think  = hasFlag(args, "--no-think");
        bool concise   = hasFlag(args, "--concise");
        bool caveman   = hasFlag(args, "--caveman");

        BatchOpts opts;
        opts.model = model;
        opts.max_tokens = max_tokens;
        opts.id_prefix = id_prefix;
        opts.directive = caveman ? BatchDirective::Caveman
                          : concise ? BatchDirective::Concise
                          : no_think ? BatchDirective::NoThink
                          : BatchDirective::None;
        nlohmann::json out = buildBatchSpec(tasks, opts);

        std::string out_path = flagValue(args, "-o");
        if (!out_path.empty()) {
            std::ofstream f(out_path);
            if (!f) { std::cerr << "icmg batch: cannot write " << out_path << "\n"; return 2; }
            f << out.dump(2) << "\n";
            std::cerr << "[icmg batch] " << tasks.size() << " requests → "
                      << out_path << " (estimated 50% discount via Batch API)\n";
        } else {
            std::cout << out.dump(2) << "\n";
            std::cerr << "[icmg batch] " << tasks.size() << " requests emitted "
                      << "(estimated 50% discount via Batch API)\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("batch", BatchCommand);

} // namespace icmg::cli
