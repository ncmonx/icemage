// src/cli/commands/govern_cmd.cpp
// v2.0.0 C1+C3+C4+C5+F1: `icmg govern` — deterministic injection governor.
//   govern --report            : honest per-source fill share (F1).
//   govern [--budget <tokens>] : budgeted, U-ordered working-set emit.
//   govern snapshot            : capture working-set manifest for this session (C4).
//   govern rebuild             : re-anchor pinned-only, hard-capped 2500 tok (C4/F2).
//   govern advise --fill <pct> : band-rate-limited idle-compact nudge (C5; advisory only).
// Zero-model, deterministic. Pure cores: working_set.hpp, ws_snapshot.hpp, compact_advisor.hpp.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/working_set.hpp"
#include "../../core/ws_snapshot.hpp"
#include "../../core/compact_advisor.hpp"
#include "../../imem/memory_store.hpp"

#include <nlohmann/json.hpp>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

// Build icmg-injectable candidate sources from project memory. The recall query can be
// focused on a task (--focus) to bias the working-set toward what you're working on.
std::vector<core::Source> buildCandidates(
        imem::MemoryStore& mem,
        const std::string& query = "decisions rules known-issue plan") {
    auto hits = mem.recall(query, 40, false);
    std::vector<core::Source> candidates;
    candidates.reserve(hits.size());
    for (const auto& h : hits) {
        core::Source s;
        s.id        = h.topic;
        s.text      = h.content;
        s.tokens    = (int)(h.content.size() / 4);  // ~4 chars/token heuristic
        s.relevance = h.score;
        s.priority  = (h.topic.find("decisions") != std::string::npos) ? 2 : 1;
        s.pinned    = (h.pinned != 0);
        candidates.push_back(std::move(s));
    }
    return candidates;
}

std::string sessionId() {
    const char* s = std::getenv("ICMG_SESSION_ID");
    return s ? std::string(s) : std::string("default");
}

std::string bandFilePath() {
    return ".icmg/compact_band_" + sessionId() + ".txt";
}

}  // namespace

class GovernCommand : public BaseCommand {
public:
    std::string name() const override { return "govern"; }
    std::string description() const override {
        return "Deterministic injection governor: report/budget/snapshot/rebuild/advise";
    }
    void usage() const override {
        std::cout << "Usage: icmg govern [--report] [--budget <tokens>] | snapshot | rebuild | advise --fill <pct>\n"
                  << "  " << description() << "\n";
    }

    int run(const std::vector<std::string>& args) override {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        const std::string sub = args.empty() ? "" : args[0];

        // ---- C4: snapshot ----
        if (sub == "snapshot") {
            auto ws = core::selectWorkingSet(buildCandidates(mem), 4000);
            auto m  = core::snapshotManifest(ws);
            nlohmann::json mj = m.nodeIds, pj = m.pinnedIds;
            std::int64_t ts = (std::int64_t)std::time(nullptr);
            db.run("INSERT OR REPLACE INTO working_set_snapshot"
                   "(session_id, ts, manifest_json, pinned_json) VALUES(?,?,?,?)",
                   {sessionId(), std::to_string(ts), mj.dump(), pj.dump()});
            std::cout << "[govern snapshot] session " << sessionId() << ": "
                      << m.nodeIds.size() << " ids (" << m.pinnedIds.size()
                      << " pinned) saved.\n";
            return 0;
        }

        // ---- C4: rebuild ----
        if (sub == "rebuild") {
            std::string pinnedJson;
            db.query("SELECT pinned_json FROM working_set_snapshot WHERE session_id=? "
                     "ORDER BY ts DESC LIMIT 1", {sessionId()},
                     [&](const core::Row& r) { if (!r.empty()) pinnedJson = r[0]; });
            core::Manifest m;
            if (!pinnedJson.empty()) {
                for (const auto& id : nlohmann::json::parse(pinnedJson))
                    m.pinnedIds.push_back(id.get<std::string>());
            }
            auto ws = core::rebuildFromManifest(m, 2500, buildCandidates(mem));  // F2 hard cap
            std::cout << "[govern rebuild] session " << sessionId() << ": re-anchored "
                      << ws.items.size() << " pinned (~" << ws.totalTokens
                      << " tok, cap 2500):\n";
            for (const auto& s : ws.items) std::cout << "  - " << s.id << "\n";
            return 0;
        }

        // ---- C5: advise ----
        if (sub == "advise") {
            int fill = 0;
            for (size_t i = 1; i + 1 < args.size(); ++i)
                if (args[i] == "--fill") fill = std::atoi(args[i + 1].c_str());
            if (fill < 0) fill = 0;
            if (fill > 100) fill = 100;   // #11: clamp nonsense fill (e.g. 999) to valid range
            int lastBand = -1;
            { std::ifstream f(bandFilePath()); if (f) f >> lastBand; }
            auto n = core::idleCompactAdvice(fill, lastBand, 75);
            if (n.fire) {
                std::cout << n.message << "\n";
                std::ofstream f(bandFilePath()); if (f) f << n.band;
            }
            return 0;
        }

        // ---- C1+C3+F1: report / budget ----
        bool report = false;
        int budget = 4000;
        std::string focus = "decisions rules known-issue plan";  // --focus biases the recall
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--report") report = true;
            else if (args[i] == "--budget" && i + 1 < args.size()) budget = std::stoi(args[++i]);
            else if (args[i] == "--focus" && i + 1 < args.size()) focus = args[++i];
        }
        auto candidates = buildCandidates(mem, focus);

        if (report) {
            int total = 0;
            for (const auto& c : candidates) total += c.tokens;
            std::cout << "[govern --report] icmg-injectable candidates: "
                      << candidates.size() << " sources, ~" << total << " tokens.\n"
                      << "Run `icmg context-budget` for the full live-window per-source share.\n"
                      << "F1 honesty: governor only caps icmg-injected context; CC conversation\n"
                      << "turns + UI attachments are outside icmg's control.\n";
            return 0;
        }

        auto ws = core::selectWorkingSet(candidates, budget);
        auto ordered = core::orderUShaped(ws.items);
        std::cout << "[govern] budget=" << budget << " tokens, kept "
                  << ordered.size() << "/" << candidates.size()
                  << " (~" << ws.totalTokens << " tok), U-ordered:\n";
        for (const auto& s : ordered) std::cout << "  - " << s.id << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("govern", GovernCommand);

}  // namespace icmg::cli
