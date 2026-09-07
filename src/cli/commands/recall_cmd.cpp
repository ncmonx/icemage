#include "../base_command.hpp"
#include "../recall_json.hpp"   // v1.70.0 #176
#include "../recall_index.hpp"  // 2026-06-15 progressive-disclosure
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/global_db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/atom_store.hpp"
#include "../../imem/scorer.hpp"
#include "../ref_registry.hpp"
#include "../session_dedup.hpp"   // Cache-hit optimizer #2: TTL-aware recall dedup
#include "../effort_hint.hpp"     // v2.21 research B: adaptive depth via classifyIntent
#include "../asof_parse.hpp"      // brain v2.22 #1: --as-of time-travel parsing
#include "../coarse_recall.hpp"   // brain v2.22 #4: coarse-to-fine tail collapse
#include "../../imem/deep_forget.hpp"  // 2026-09-07 A: forget --deep residue scan
#include "../last_session.hpp"    // A2: recall --last-session
#include "../../core/persona_db.hpp"        // #moments: merge persona _moments
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include "../../imem/moment_helpers.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <algorithm>
#include <filesystem>

namespace icmg::cli {

static std::string timeAgo(int64_t epoch) {
    if (epoch <= 0) return "never";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = now - epoch;
    if (diff < 60)   return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff/60) + "m ago";
    if (diff < 86400)return std::to_string(diff/3600) + "h ago";
    return std::to_string(diff/86400) + "d ago";
}

static void escapeJson(std::ostream& o, const std::string& s) {
    for (char c : s) {
        if      (c == '"')  o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\t') o << "\\t";
        else                o << c;
    }
}

class RecallCommand : public BaseCommand {
public:
    std::string name()        const override { return "recall"; }
    std::string description() const override { return "Recall memory nodes by query"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg recall <query> [options]\n\n"
            "Options:\n"
            "  --limit N       Max results (default: 10)\n"
            "  --adaptive      Bind depth to task intent (simple 3 / unknown 7 / complex 12)\n"
            "  --topic X       Filter by topic prefix\n"
            "  --zone Z        Restrict corpus to zone (sharper IDF, faster)\n"
            "  --semantic      Hybrid BM25+vec recall (Phase 23). Falls back to BM25 if no embedder.\n"
            "  --alpha N       Blend weight 0..1 (1=BM25 only, 0=vec only, default 0.5)\n"
            "  --pure          Equivalent to --semantic --alpha 0\n"
            "  --causal        Expand BM25 hits 1-hop over causal edges (feature #1; see `memory link`)\n"
            "  --auto-tier     Cheap BM25 by default; escalate to semantic only for hard queries (feature #3)\n"
            "  --all-projects  Cross-project recall (aggregates from registered projects)\n"
            "  --fuzzy         Fuzzy search fallback\n"
            "  --at-commit SHA Filter to memories stored at a specific git commit (prefix ok)\n"
            "  --as-of T       Time-travel: recall what was believed at T (epoch,\n"
            "                  7d/24h/30m ago, or YYYY-MM-DD). Includes since-superseded facts.\n"
            "  --full          Never collapse the tail (default: oversized sets keep top\n"
            "                  hits full, rest as 1-line index; fetch via --get <id>)\n"
            "  --no-dedup      Show nodes already returned this session (default: suppress)\n"
            "  --explain       Show score breakdown\n"
            "  --history       Show recent queries\n"
            "  --last-session  Re-onboard: most-recent session snapshot + last wflog (Open items flagged)\n"
            "  --index         Layer-1 index: #id|icon|title|~tok (progressive disclosure)\n"
            "  --timeline      Layer-1 chronological view, grouped by day (newest first)\n"
            "  --get IDS       Layer-2: fetch full content for comma-separated ids\n"
            "  --by topic|file Group --index output (default: topic; file=by graph-node ref)\n"
            "  --json          JSON output\n"
            "  --since EPOCH   Only return nodes created at or after EPOCH (unix seconds).\n"
            "                  Accepts relative durations: 7d, 24h, 30m (from now).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        // 2026-06-15 progressive disclosure: --get <ids> fetches full detail for
        // the IDs the agent picked from a prior --index view. No query needed.
        {
            std::string ids = flagValue(args, "--get");
            if (!ids.empty()) return runGet(ids, hasFlag(args, "--json"));
        }
        // A2: --last-session surfaces the single most-recent session snapshot +
        // last wflog (Open items flagged). Onboards you back into where you left
        // off. No query needed. Short-circuits before normal recall parsing.
        if (hasFlag(args, "--last-session")) {
            return runLastSession(hasFlag(args, "--json"));
        }

        bool index    = hasFlag(args, "--index");
        bool timeline = hasFlag(args, "--timeline");
        std::string by = flagValue(args, "--by", "topic");
        bool history  = hasFlag(args, "--history");
        bool json     = hasFlag(args, "--json");
        bool explain  = hasFlag(args, "--explain");
        bool fuzzy    = hasFlag(args, "--fuzzy");
        bool no_dedup = hasFlag(args, "--no-dedup");
        bool semantic = hasFlag(args, "--semantic") || hasFlag(args, "--pure");
        bool causal   = hasFlag(args, "--causal");  // feature #1: 1-hop causal expansion over BM25
        bool autoTier = hasFlag(args, "--auto-tier");  // feature #3: cheap BM25, escalate to semantic only if weak
        bool pure_vec = hasFlag(args, "--pure");
        double alpha = 0.5;
        try { alpha = std::stod(flagValue(args, "--alpha", "0.5")); } catch (...) {}
        if (pure_vec) alpha = 0.0;
        bool all_projects = hasFlag(args, "--all-projects");
        // v1.1.0 Task 4: --unseen returns only entries not yet served in this
        // session. Session id from ICMG_SESSION_ID env (set by SessionStart hook)
        // or fallback to PID-based string.
        bool unseen = hasFlag(args, "--unseen");
        bool atoms    = hasFlag(args, "--atoms");  // v1.79 atom-FTS hybrid
        std::string session_id = flagValue(args, "--session", "");
        if (session_id.empty()) {
            const char* env_sid = std::getenv("ICMG_SESSION_ID");
            if (env_sid && *env_sid) session_id = env_sid;
        }
        std::string topic     = flagValue(args, "--topic");
        std::string zone      = flagValue(args, "--zone");
        std::string at_commit = flagValue(args, "--at-commit");
        int limit = 10;
        try { limit = std::stoi(flagValue(args, "--limit", "10")); } catch (...) {}

        // --as-of: brain v2.22 #1 time-travel recall. Accepts unix epoch,
        // relative duration (7d/24h/30m = that long AGO), or YYYY-MM-DD.
        int64_t as_of_epoch = 0;
        {
            std::string asof_raw = flagValue(args, "--as-of", "");
            if (!asof_raw.empty()) {
                int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                as_of_epoch = parseAsOf(asof_raw, now_sec);
                if (as_of_epoch <= 0) {
                    std::cerr << "icmg recall: bad --as-of value '" << asof_raw
                              << "' (want epoch, 7d/24h/30m, or YYYY-MM-DD)\n";
                    return 1;
                }
            }
        }

        // --since: filter nodes by creation time.  Accepts a unix epoch (seconds) or
        // relative durations like "7d", "24h", "30m" measured backwards from now.
        int64_t since_epoch = 0;
        {
            std::string since_raw = flagValue(args, "--since", "");
            if (!since_raw.empty()) {
                // Try relative duration first: <N>d / <N>h / <N>m
                auto tryRelative = [&]() -> bool {
                    if (since_raw.size() < 2) return false;
                    char unit = since_raw.back();
                    if (unit != 'd' && unit != 'h' && unit != 'm') return false;
                    try {
                        int64_t n = std::stoll(since_raw.substr(0, since_raw.size() - 1));
                        int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        int64_t mul = (unit == 'd') ? 86400 : (unit == 'h') ? 3600 : 60;
                        since_epoch = now_sec - n * mul;
                        return true;
                    } catch (...) { return false; }
                };
                if (!tryRelative()) {
                    try { since_epoch = std::stoll(since_raw); } catch (...) {}
                }
            }
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        // History mode
        if (history) {
            auto qs = store.queryHistory(20);
            if (json) {
                std::cout << "[";
                for (size_t i = 0; i < qs.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << "\""; escapeJson(std::cout, qs[i]); std::cout << "\"";
                }
                std::cout << "]\n";
            } else {
                std::cout << "Recent queries:\n";
                for (auto& q : qs) std::cout << "  " << q << "\n";
            }
            return 0;
        }

        // Need query
        std::string query;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            query = a; break;
        }
        if (query.empty()) {
            std::cerr << "icmg recall: query required\n";
            return 1;
        }

        // v2.21 research B: adaptive recall depth. A fixed top-N over-fetches
        // for routine tasks and under-fetches for cross-module work. --adaptive
        // binds N to the intent classifier already shipped for effort hints:
        // Simple -> 3, Unknown -> 7, Complex -> 12. An explicit --limit wins.
        if (hasFlag(args, "--adaptive") && flagValue(args, "--limit").empty()) {
            cli::Intent it = cli::classifyIntent(query);
            limit = cli::adaptiveRecallDepth(it);
            std::cerr << "[icmg recall] adaptive depth: " << limit
                      << " (" << (it == cli::Intent::Simple ? "simple" :
                                  it == cli::Intent::Complex ? "complex" : "unclassified")
                      << " task)\n";
        }

        std::vector<imem::MemoryNode> results;
        if (as_of_epoch > 0) {
            // Time-travel: rank within the point-in-time corpus. Mutually
            // exclusive with the live-recall modes below (past view is its own
            // corpus; mixing with dedup/cache/semantic would lie).
            results = store.recallAsOf(query, as_of_epoch, limit);
        } else if (atoms) {
            // v1.79 ICM dual-memory: match the semantic atom FTS, then return
            // the SOURCE memory nodes (clustered) so output shape is identical
            // to normal recall. Default recall path is untouched.
            imem::AtomStore as(db);
            for (int64_t sid : as.recallAtomSources(query, limit)) {
                imem::MemoryNode n = store.get(sid);
                if (n.id != 0) results.push_back(std::move(n));
            }
        } else if (all_projects) {
            // Phase 21 Task 5: aggregate top-K from each registered project DB.
            results = recallAllProjects(query, limit, fuzzy);
        } else if (!topic.empty()) {
            results = store.recallByTopic(topic, limit);
        } else if (!zone.empty()) {
            results = store.recallInZone(query, zone, limit, fuzzy);
        } else if (causal) {
            results = store.recallCausal(query, limit);
        } else if (autoTier) {
            results = store.recallAuto(query, limit);
        } else if (semantic) {
            results = store.recallSemantic(query, limit, alpha);
        } else if (unseen) {
            results = store.recallUnseen(query, session_id, limit, fuzzy);
        } else {
            results = store.recall(query, limit, fuzzy);
        }

        // --since post-filter: erase nodes created before the requested cutoff epoch.
        if (since_epoch > 0) {
            results.erase(
                std::remove_if(results.begin(), results.end(),
                    [&](const imem::MemoryNode& n){ return n.created_at < since_epoch; }),
                results.end());
        }

        // 2026-06-06 (#moments): auto-merge persona _moments on the DEFAULT query path
        // (cross-project, fail-open). Skipped for topic/zone/semantic/all-projects/unseen/
        // atom/at-commit special modes to avoid double-counting. De-dup by content.
        {
            bool mergeMoments = !query.empty() && topic.empty() && zone.empty()
                && !semantic && !all_projects && !unseen && !atoms && at_commit.empty()
                && since_epoch == 0;
            if (mergeMoments && core::personaDbAvailable()) {
                try {
                    core::ProfileStore ps(core::personaDb());
                    for (auto& r : ps.searchFts(core::currentUser(), query, limit)) {
                        if (r.zone != "_moments") continue;
                        bool dup = false;
                        for (auto& n : results) if (n.content == r.content) { dup = true; break; }
                        if (!dup) results.push_back(imem::profileRowToNode(r));
                    }
                } catch (...) {}
            }
        }

        // Phase 82 T4 + Cache-hit optimizer #2: in-session recall dedup — suppress
        // nodes already returned recently. Prevents identical results flooding
        // multi-turn context. Switched from RefRegistry (calendar-day scoped, which
        // over-suppressed memory for a long-lived GUI across separate conversations)
        // to a TTL window (session_dedup): re-injection within an active conversation
        // is suppressed, but a later conversation re-surfaces the memory once the TTL
        // lapses. Bypass with --no-dedup.
        if (!no_dedup && !json) {
            try {
                std::string ddpath =
                    (std::filesystem::current_path() / ".icmg" / "recall-dedup.txt").string();
                int64_t ttl = recallDedupTTL();
                std::vector<imem::MemoryNode> deduped;
                std::vector<std::string> suppressed_ids;   // v2.21 research A
                for (auto& n : results) {
                    std::string key = std::to_string(n.id);
                    if (!wasInjectedRecently(ddpath, key, ttl)) {
                        markInjected(ddpath, key);
                        deduped.push_back(std::move(n));
                    } else {
                        suppressed_ids.push_back(key);
                    }
                }
                if (!suppressed_ids.empty()) {
                    // v2.21 research A (session-aware recall delta): the model
                    // must still SEE that prior facts apply -- one compact
                    // stdout line with ids instead of full re-emission.
                    std::cout << formatPriorRefLine(suppressed_ids) << "\n";
                    std::cerr << "[icmg recall] " << suppressed_ids.size()
                              << " node(s) suppressed (seen this session; use --no-dedup to show)\n";
                }
                results = std::move(deduped);
            } catch (...) {}
        }

        // --at-commit: filter to memories whose git_sha starts with the given prefix.
        if (!at_commit.empty()) {
            results.erase(
                std::remove_if(results.begin(), results.end(),
                    [&](const imem::MemoryNode& n){
                        return n.git_sha.substr(0, at_commit.size()) != at_commit;
                    }),
                results.end());
        }

        if (json) {
            printJson(results);
        } else if (timeline) {
            std::cout << formatTimeline(results);
        } else if (index) {
            std::cout << formatIndex(results, by);
        } else if (explain) {
            printExplain(query, results);
        } else {
            // brain v2.22 #4: coarse-to-fine. Oversized sets keep the strongest
            // hits full; the tail collapses to 1-line index rows (re-fetch via
            // `recall --get <ids>`). --full opts out.
            size_t keep = hasFlag(args, "--full")
                              ? results.size()
                              : coarseKeepCount(results);
            if (keep >= results.size()) {
                printDefault(results);
            } else {
                std::vector<imem::MemoryNode> head(results.begin(), results.begin() + keep);
                std::vector<imem::MemoryNode> tail(results.begin() + keep, results.end());
                printDefault(head);
                std::cout << "-- " << tail.size()
                          << " more (collapsed to save tokens; icmg recall --get <id>):\n";
                std::cout << formatIndex(tail, "topic");
            }
        }

        return 0;
    }

    // Layer-2 of progressive disclosure: given comma-separated ids (from a prior
    // --index), fetch + print each node's FULL content. Missing ids -> stderr,
    // fail-open (exit 0) so one bad id never blanks the rest.
    int runGet(const std::string& ids, bool json) {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        std::vector<imem::MemoryNode> got;
        size_t pos = 0;
        while (pos <= ids.size()) {
            size_t comma = ids.find(',', pos);
            std::string tok = ids.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            // trim spaces
            while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
            while (!tok.empty() && tok.back()  == ' ') tok.pop_back();
            if (!tok.empty()) {
                int64_t id = 0;
                try { id = std::stoll(tok); } catch (...) { id = 0; }
                if (id > 0) {
                    imem::MemoryNode n = store.get(id);
                    if (n.id != 0) got.push_back(std::move(n));
                    else std::cerr << "[icmg recall --get] node #" << id << " not found\n";
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (json) printJson(got);
        else      printFull(got);
        return 0;
    }

    // A2: --last-session. Pulls the single most-recent session snapshot and the
    // most-recent wflog into a SessionView, then renders (pure formatter in
    // last_session.hpp). Re-onboards "where did we leave off" in one command.
    int runLastSession(bool json) {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        SessionView v;
        // Most-recent session snapshot: reserved topic prefixes, newest by time.
        db.query(
            "SELECT topic, content, COALESCE(last_used, created_at) AS ts "
            "FROM memory_nodes WHERE deleted_at IS NULL AND ("
            "  topic LIKE 'session-snapshot%' OR topic LIKE 'auto-compact-%' "
            "  OR topic LIKE 'session:%' OR topic LIKE 'session %') "
            "ORDER BY ts DESC LIMIT 1",
            {},
            [&](const core::Row& r){
                if (r.size() < 3) return;
                v.has_snapshot = true;
                v.snap_topic   = r[0];
                v.snap_content = r[1];
                int64_t ts = 0; try { ts = std::stoll(r[2]); } catch (...) {}
                v.snap_age = timeAgo(ts);
            });

        // Most-recent wflog (log-saved <goal>): extract Goal/Decisions/Open.
        db.query(
            "SELECT content, COALESCE(last_used, created_at) AS ts "
            "FROM memory_nodes WHERE deleted_at IS NULL AND topic LIKE 'log-saved %' "
            "ORDER BY ts DESC LIMIT 1",
            {},
            [&](const core::Row& r){
                if (r.size() < 2) return;
                v.has_wflog       = true;
                v.log_goal        = wflogField(r[0], "Goal");
                v.log_decisions   = wflogField(r[0], "Decisions");
                v.log_open        = wflogField(r[0], "Open");
                int64_t ts = 0; try { ts = std::stoll(r[1]); } catch (...) {}
                v.log_age = timeAgo(ts);
            });

        if (json) {
            std::cout << "{\"has_snapshot\":" << (v.has_snapshot ? "true" : "false")
                      << ",\"snapshot_topic\":\""; escapeJson(std::cout, v.snap_topic);
            std::cout << "\",\"snapshot_age\":\""; escapeJson(std::cout, v.snap_age);
            std::cout << "\",\"has_wflog\":" << (v.has_wflog ? "true" : "false")
                      << ",\"goal\":\""; escapeJson(std::cout, v.log_goal);
            std::cout << "\",\"decisions\":\""; escapeJson(std::cout, v.log_decisions);
            std::cout << "\",\"open\":\""; escapeJson(std::cout, v.log_open);
            std::cout << "\"}\n";
        } else {
            std::cout << renderLastSession(v);
        }
        return 0;
    }

    // Layer-2 detail print: FULL content, untruncated (the whole point of
    // --get is to pay for the bytes the agent deliberately chose to fetch).
    void printFull(const std::vector<imem::MemoryNode>& nodes) const {
        if (nodes.empty()) { std::cout << "No results.\n"; return; }
        for (auto& n : nodes) {
            std::cout << "#" << n.id << " [" << n.topic << "]\n";
            std::cout << n.content << "\n";
            if (!n.keywords.empty()) std::cout << "  Keywords: " << n.keywords << "\n";
            std::cout << "  Used: " << n.frequency << "x, last: " << timeAgo(n.last_used);
            if (!n.git_sha.empty()) std::cout << "  @" << n.git_sha;
            std::cout << "\n\n";
        }
    }

private:
    // Phase 21 Task 5: cross-project recall — iterate registered projects,
    // recall top-K from each, merge + re-sort by score, return top-K overall.
    // Each child Db opens read-only via the existing Config override path.
    std::vector<imem::MemoryNode>
    recallAllProjects(const std::string& query, int limit, bool fuzzy) {
        std::vector<imem::MemoryNode> all;
        try {
            auto& gdb = core::GlobalDb::instance();
            gdb.init();
            auto projects = gdb.listProjects();
            if (projects.empty()) {
                std::cerr << "icmg recall: no registered projects (use `icmg project register`)\n";
                return all;
            }
            for (auto& p : projects) {
                if (p.db_path.empty()) continue;
                try {
                    core::Db pdb(p.db_path);
                    imem::MemoryStore pstore(pdb);
                    auto sub = pstore.recall(query, limit, fuzzy);
                    // Tag results with project name in topic prefix for visibility
                    for (auto& n : sub) {
                        n.topic = "[" + p.name + "] " + n.topic;
                        all.push_back(std::move(n));
                    }
                } catch (...) { /* skip unreadable project DBs */ }
            }
        } catch (const std::exception& e) {
            std::cerr << "icmg recall --all-projects: " << e.what() << "\n";
            return all;
        }
        // Re-sort merged corpus by score desc; truncate to limit.
        std::sort(all.begin(), all.end(),
                  [](const imem::MemoryNode& a, const imem::MemoryNode& b){
                      return a.score > b.score;
                  });
        if ((int)all.size() > limit) all.resize(limit);
        return all;
    }

    void printDefault(const std::vector<imem::MemoryNode>& nodes) const {
        if (nodes.empty()) { std::cout << "No results.\n"; return; }
        for (auto& n : nodes) {
            // Citable header: "[score] #id <icon> topic" so each result can be
            // re-fetched (`recall --get <id>`) or cited by id.
            std::ostringstream sc;
            sc << std::fixed << std::setprecision(1) << n.score;
            std::cout << formatCitationHeader(n, sc.str()) << "\n";
            std::cout << "  \"";
            // Truncate long content
            if (n.content.size() > 120)
                std::cout << n.content.substr(0, 117) << "...";
            else
                std::cout << n.content;
            std::cout << "\"\n";
            if (!n.keywords.empty())
                std::cout << "  Keywords: " << n.keywords << "\n";
            std::cout << "  Used: " << n.frequency << "x"
                      << ", last: " << timeAgo(n.last_used);
            if (!n.git_sha.empty()) std::cout << "  @" << n.git_sha;
            if (n.source != "unknown" && !n.source.empty()) std::cout << "  [from: " << n.source << "]";
            std::cout << "\n\n";
        }
    }

    void printExplain(const std::string& query,
                      const std::vector<imem::MemoryNode>& nodes) const {
        if (nodes.empty()) { std::cout << "No results.\n"; return; }
        auto& scorer = imem::Scorer::instance();
        for (auto& n : nodes) {
            auto d = scorer.scoreDetailed(query, n);
            std::cout << std::fixed << std::setprecision(2)
                      << "[" << d.total << "] " << n.topic << " — \""
                      << n.content.substr(0, 60) << "\"\n";
            std::cout << "  BM25=" << d.bm25
                      << " × recency=" << d.recency
                      << " × freq="   << d.freq
                      << " × importance=" << d.importance_mult
                      << " = " << d.total << "\n";
            if (!d.matched_tokens.empty()) {
                std::cout << "  Matched: [";
                for (size_t i = 0; i < d.matched_tokens.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << d.matched_tokens[i];
                }
                std::cout << "]\n";
            }
            std::cout << "\n";
        }
    }

    void printJson(const std::vector<imem::MemoryNode>& nodes) const {
        // v1.70.0 #176: emit via nlohmann + safeDump so output is always
        // valid UTF-8 (memory content may hold raw non-UTF-8 bytes).
        std::cout << recallNodesToJson(nodes) << "\n";
    }
};

class ForgetCommand : public BaseCommand {
public:
    std::string name()        const override { return "forget"; }
    std::string description() const override { return "Soft-delete a memory node"; }

    int run(const std::vector<std::string>& args) override {
        // Phase 36 T3: --pattern bulk-delete by topic LIKE.
        std::string pattern = flagValue(args, "--pattern");
        if (!pattern.empty()) return forgetPattern(pattern, args);

        if (args.empty()) {
            std::cerr << "icmg forget: requires <id> OR --pattern <SQL-LIKE>\n"
                         "  --deep   after forgetting, scan derived nodes (snapshots, wflog,\n"
                         "           consolidated notes) for residue of the forgotten content\n"; return 1;
        }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg forget: invalid id\n"; return 1;
        }
        bool yes  = hasFlag(args, "--yes");
        bool deep = hasFlag(args, "--deep");   // 2026-09-07 A: unlearning propagation

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (!yes) {
            auto node = store.get(id);
            if (node.id == 0) {
                std::cerr << "icmg forget: node #" << id << " not found\n";
                return 1;
            }
            std::cerr << "Delete: [" << node.topic << "] \""
                      << node.content.substr(0, 80) << "\"?\n";
            std::cerr << "Add --yes to confirm.\n";
            return 1;
        }

        // --deep: BEFORE removing, capture content so residue can be found.
        std::string forgotten_text;
        if (deep) {
            auto node = store.get(id);
            if (node.id != 0) forgotten_text = node.topic + " " + node.content;
        }

        store.remove(id);
        std::cout << "Forgot node #" << id << "\n";

        // 2026-09-07 A (arXiv 2609.04875): a forgotten node's content usually
        // leaked into derived artifacts (session snapshots, wflog entries,
        // consolidated notes -- all memory_nodes rows). Surface the residue;
        // NEVER auto-delete (each hit needs a human/agent decision).
        if (deep && !forgotten_text.empty()) {
            std::vector<imem::ResidueCandidate> cands;
            db.query("SELECT id, topic, content FROM memory_nodes "
                     "WHERE deleted_at IS NULL AND id != ?",
                     {std::to_string(id)},
                     [&](const core::Row& r) {
                         if (r.size() < 3) return;
                         imem::ResidueCandidate c;
                         try { c.id = std::stoll(r[0]); } catch (...) { return; }
                         c.source = r[1];
                         c.text   = r[1] + " " + r[2];
                         cands.push_back(std::move(c));
                     });
            auto hits = imem::findResidue(forgotten_text, cands);
            if (hits.empty()) {
                std::cout << "deep: no residue found in " << cands.size()
                          << " live node(s)\n";
            } else {
                std::cout << "deep: content residue in " << hits.size()
                          << " derived node(s) -- review each:\n";
                for (const auto& h : hits) {
                    std::cout << "  #" << h.id << "  ("
                              << (int)(h.overlap * 100) << "% of forgotten tokens)  "
                              << h.source.substr(0, 60) << "\n";
                }
                std::cout << "purge one: icmg forget <id> --yes   (add --deep to chase further)\n";
            }
        }
        return 0;
    }

private:
    int forgetPattern(const std::string& pattern, const std::vector<std::string>& args) {
        bool dry = hasFlag(args, "--dry-run");
        bool yes = hasFlag(args, "--yes");
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int n = 0;
        db.query("SELECT COUNT(*) FROM memory_nodes WHERE topic LIKE ? AND deleted_at IS NULL",
                 {pattern},
                 [&](const core::Row& r){ if (!r.empty()) n = std::stoi(r[0]); });
        std::cout << "forget --pattern '" << pattern << "': " << n << " node(s) match\n";
        if (n == 0) return 0;
        if (dry || !yes) {
            if (dry) std::cout << "  [dry-run] no DB change\n";
            else     std::cout << "  Add --yes to confirm soft-delete.\n";
            return 0;
        }
        db.run("UPDATE memory_nodes SET deleted_at = strftime('%s','now') "
               "WHERE topic LIKE ? AND deleted_at IS NULL", {pattern});
        std::cout << "  soft-deleted " << n << " node(s).\n";
        return 0;
    }
};

class RestoreCommand : public BaseCommand {
public:
    std::string name()        const override { return "restore"; }
    std::string description() const override { return "Restore soft-deleted memory node"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg restore: requires <id>\n"; return 1; }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg restore: invalid id\n"; return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        store.restore(id);
        std::cout << "Restored node #" << id << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("recall",  RecallCommand);
ICMG_REGISTER_COMMAND("forget",  ForgetCommand);
ICMG_REGISTER_COMMAND("restore", RestoreCommand);

} // namespace icmg::cli
