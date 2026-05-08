// Phase 31 T3: `icmg index` — single-shot maintenance pipeline.
//
// Orchestrates: graph update → embed memory → embed graph → memory consolidate
// → memory extract-patterns → memory decay. Default --dry-run for mutating
// stages (consolidate/patterns/decay); --apply commits.
//
// Each stage shells out to self via safeExecShell so output cap & filter rules
// already in place per command apply naturally.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace icmg::cli {

class IndexCommand : public BaseCommand {
public:
    std::string name()        const override { return "index"; }
    std::string description() const override { return "Unified maintenance: scan + embed + consolidate + patterns + decay"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg index [options]\n\n"
            "Pipeline:\n"
            "  1. graph update --since <window>\n"
            "  2. embed memory + embed graph (skip-fresh body_hash)\n"
            "  3. memory consolidate (--dry-run unless --apply)\n"
            "  4. memory extract-patterns (--dry-run unless --apply)\n"
            "  5. memory decay --dry-run (always preview)\n\n"
            "Options:\n"
            "  --apply           Commit consolidate/patterns; without it stays dry-run\n"
            "  --skip-embed      Skip embed steps (avoid Python sidecar spawn)\n"
            "  --since 1d        Scan window (default 1d; 30 minutes via 30m, 7 days via 7d)\n"
            "  --json            Machine-parseable summary\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool apply      = hasFlag(args, "--apply");
        bool skip_embed = hasFlag(args, "--skip-embed");
        bool json_out   = hasFlag(args, "--json");
        std::string since = flagValue(args, "--since", "1d");

        std::string self = locateSelf();
        auto t0 = std::chrono::steady_clock::now();

        struct Step { std::string name; int rc; std::string head; int dur_ms; };
        std::vector<Step> log;

        auto runStep = [&](const std::string& name, const std::string& cmd) {
            auto sb = std::chrono::steady_clock::now();
            std::cout << "  [" << name << "] " << cmd << "\n";
            auto res = core::safeExecShell(cmd, false, 600000);
            auto se = std::chrono::steady_clock::now();
            int dur = (int)std::chrono::duration_cast<std::chrono::milliseconds>(se - sb).count();
            // Take first 120 chars of stdout as summary head.
            std::string head = res.out.substr(0, 200);
            // Strip trailing newline.
            while (!head.empty() && (head.back() == '\n' || head.back() == '\r')) head.pop_back();
            std::cout << "    -> " << head.substr(0, 160)
                      << "  (" << dur << "ms)\n";
            log.push_back({name, res.exit_code, head, dur});
            return res.exit_code;
        };

        std::cout << "icmg index: maintenance pass " << (apply ? "[APPLY]" : "[dry-run]") << "\n";

        runStep("graph update", "\"" + self + "\" graph update --since " + since);

        if (!skip_embed) {
            runStep("embed memory", "\"" + self + "\" embed memory");
            runStep("embed graph",  "\"" + self + "\" embed graph");
        } else {
            std::cout << "  [embed] skipped (--skip-embed)\n";
        }

        std::string apply_flag = apply ? "" : " --dry-run";
        runStep("consolidate",     "\"" + self + "\" memory consolidate" + apply_flag);
        runStep("extract-patterns","\"" + self + "\" memory extract-patterns" + apply_flag);
        runStep("decay",           "\"" + self + "\" memory decay --dry-run");

        auto t1 = std::chrono::steady_clock::now();
        int total_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        if (json_out) {
            std::cout << "{\"steps\":" << log.size()
                      << ",\"total_ms\":" << total_ms
                      << ",\"apply\":" << (apply ? "true" : "false")
                      << ",\"results\":[";
            for (size_t i = 0; i < log.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << log[i].name
                          << "\",\"rc\":" << log[i].rc
                          << ",\"ms\":" << log[i].dur_ms << "}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "\nDone in " << total_ms << "ms. ";
            if (!apply) std::cout << "Re-run with --apply to commit consolidate/patterns/decay.\n";
            else        std::cout << "Mutations committed.\n";
        }
        int err = 0;
        for (auto& s : log) if (s.rc != 0) ++err;
        return err > 0 ? 1 : 0;
    }

private:
    static std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }
};

ICMG_REGISTER_COMMAND("index", IndexCommand);

} // namespace icmg::cli
