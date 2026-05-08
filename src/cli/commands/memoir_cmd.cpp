// `icmg memoir` — long-form narrative memory (story / essay / case-study).
// Built atop existing memory_nodes with topic prefix "memoir:<title>".
// Differences from short memory:
//   - Never deduplicated (always force=true)
//   - Default importance = 2 (high decay resistance)
//   - Content not truncated in listing — separate `show` command for full
//   - Optional `--link <id>` cross-references prior memoir via keywords
//
// Use case: capture context that's too rich for short memory_nodes —
// post-mortems, architecture rationales, customer interview synthesis.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include "../../core/exec_utils.hpp"
#include "../../embed/embed_store.hpp"
#include "../../embed/embedder.hpp"
#include <map>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace icmg::cli {

class MemoirCommand : public BaseCommand {
public:
    std::string name()        const override { return "memoir"; }
    std::string description() const override { return "Long-form narrative memory (essays, post-mortems)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memoir <action> [args]\n\n"
            "Actions:\n"
            "  add --title T [--content-file F | --content TEXT] [--keywords K] [--link ID]\n"
            "  list [--limit N] [--zone Z]\n"
            "  show <id>\n"
            "  search <query> [--limit N]\n"
            "  link <id> --to <other-id>     Cross-reference\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string action = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        if (action == "add")    return add(store, rest);
        if (action == "list")   return list(db, rest);
        if (action == "show")   return show(store, rest);
        if (action == "search") return search(store, rest);
        if (action == "link")   return linkCmd(db, rest);
        if (action == "refine") return refine(db, store, rest);
        if (action == "export") return exportCmd(db, rest);

        std::cerr << "icmg memoir: unknown action: " << action << "\n";
        usage();
        return 1;
    }

private:
    int add(imem::MemoryStore& store, const std::vector<std::string>& args) {
        std::string title    = flagValue(args, "--title");
        std::string content  = flagValue(args, "--content");
        std::string content_file = flagValue(args, "--content-file");
        std::string keywords = flagValue(args, "--keywords");
        std::string zone     = flagValue(args, "--zone", "default");
        std::string link     = flagValue(args, "--link");

        if (title.empty()) { std::cerr << "icmg memoir add: --title required\n"; return 1; }
        if (!content_file.empty()) {
            std::ifstream f(content_file);
            if (!f) { std::cerr << "icmg memoir add: cannot read " << content_file << "\n"; return 1; }
            std::ostringstream s; s << f.rdbuf(); content = s.str();
        }
        if (content.empty()) {
            // Read from stdin as fallback for piping.
            std::ostringstream s; s << std::cin.rdbuf(); content = s.str();
        }
        if (content.empty()) { std::cerr << "icmg memoir add: --content or --content-file required\n"; return 1; }

        imem::MemoryNode n;
        n.topic   = "memoir:" + title;
        n.content = content;
        n.keywords = keywords.empty() ? "memoir" : "memoir," + keywords;
        if (!link.empty()) n.keywords += ",linked:" + link;
        n.importance = 2;     // high decay resistance
        n.zone = zone;
        int64_t id = store.store(n, /*force=*/true);
        std::cout << "Memoir #" << id << " stored: " << title << " (" << content.size() << " bytes)\n";
        return 0;
    }

    int list(core::Db& db, const std::vector<std::string>& args) {
        int limit = 20;
        try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        std::string zone = flagValue(args, "--zone");
        std::string sql = "SELECT id, topic, importance, frequency, last_used, "
                          "       LENGTH(content) FROM memory_nodes "
                          "WHERE topic LIKE 'memoir:%' AND deleted_at IS NULL";
        std::vector<std::string> params;
        if (!zone.empty()) { sql += " AND zone=?"; params.push_back(zone); }
        sql += " ORDER BY last_used DESC LIMIT ?";
        params.push_back(std::to_string(limit));

        std::cout << "ID  Importance  Used  Bytes  Title\n";
        std::cout << "--  ----------  ----  -----  -----\n";
        db.query(sql, params, [](const core::Row& r) {
            if (r.size() < 6) return;
            std::cout << std::left << std::setw(4) << r[0]
                      << std::setw(12) << r[2]
                      << std::setw(6) << r[3]
                      << std::setw(7) << r[5]
                      << r[1].substr(7) << "\n";   // strip "memoir:" prefix
        });
        return 0;
    }

    int show(imem::MemoryStore& store, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "icmg memoir show: requires <id>\n"; return 1; }
        int64_t id; try { id = std::stoll(args[0]); } catch (...) { return 1; }
        auto n = store.get(id);
        if (n.id == 0) { std::cerr << "Not found.\n"; return 1; }
        if (n.topic.find("memoir:") != 0) {
            std::cerr << "Node #" << id << " is not a memoir.\n";
            return 1;
        }
        std::cout << "# " << n.topic.substr(7) << "\n\n";
        std::cout << "Importance: " << n.importance
                  << "  Frequency: " << n.frequency
                  << "  Zone: " << n.zone << "\n";
        if (!n.keywords.empty()) std::cout << "Keywords: " << n.keywords << "\n";
        std::cout << "\n" << n.content << "\n";
        return 0;
    }

    int search(imem::MemoryStore& store, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "icmg memoir search: <query> required\n"; return 1; }
        int limit = 5;
        try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
        std::string query;
        for (auto& a : args) { if (a.empty() || a[0] == '-') continue; query = a; break; }
        if (query.empty()) return 1;
        // BM25 recall over memoir-prefixed nodes
        auto results = store.recallByTopic("memoir:", 100);
        // Filter by query substring (basic)
        std::vector<imem::MemoryNode> hits;
        std::string ql = query;
        for (auto& c : ql) c = (char)::tolower((unsigned char)c);
        for (auto& n : results) {
            std::string c = n.topic + " " + n.content;
            for (auto& ch : c) ch = (char)::tolower((unsigned char)ch);
            if (c.find(ql) != std::string::npos) {
                hits.push_back(std::move(n));
                if ((int)hits.size() >= limit) break;
            }
        }
        if (hits.empty()) { std::cout << "No matches.\n"; return 0; }
        for (auto& n : hits) {
            std::cout << "[" << n.id << "] " << n.topic.substr(7) << "\n";
            std::cout << "    " << n.content.substr(0, 120);
            if (n.content.size() > 120) std::cout << "...";
            std::cout << "\n\n";
        }
        return 0;
    }

    int refine(core::Db& db, imem::MemoryStore& store, const std::vector<std::string>& args) {
        if (args.empty()) { std::cerr << "memoir refine: <id> required\n"; return 1; }
        int64_t id = 0;
        try { id = std::stoll(args[0]); } catch (...) { return 1; }
        bool dry      = hasFlag(args, "--dry-run");
        bool no_store = hasFlag(args, "--no-store");

        auto n = store.get(id);
        if (n.id == 0 || n.topic.find("memoir:") != 0) {
            std::cerr << "memoir refine: not a memoir id: " << id << "\n";
            return 1;
        }

        // Pull related entries — recall on memoir keywords/topic, exclude self + other memoirs.
        std::string seed = n.topic.substr(7) + " " + n.keywords;
        auto related = store.recall(seed, 10, false);
        std::ostringstream evidence;
        for (auto& r : related) {
            if (r.id == id) continue;
            if (r.topic.find("memoir:") == 0) continue;
            if (r.topic.find("pattern:") == 0) continue;
            evidence << "- [" << r.topic << "] " << truncate(r.content, 200) << "\n";
        }

        // Assemble prompt for `icmg agent`.
        std::ostringstream prompt;
        prompt << "Task: refine the following memoir incrementally. Preserve the key claims, "
               << "integrate new evidence, fix outdated facts. Output ONLY the rewritten body "
               << "(no preamble, no commentary).\n\n";
        prompt << "## Existing memoir\n## Title: " << n.topic.substr(7) << "\n\n";
        prompt << n.content << "\n\n";
        prompt << "## New evidence (recent related entries)\n";
        if (evidence.str().empty()) prompt << "(none — refine for clarity only)\n";
        else                        prompt << evidence.str();

        if (dry) {
            std::cout << prompt.str();
            return 0;
        }

        // Pipe to `icmg agent --no-store --no-pack <task>`. Reuse agent for LLM call.
        std::string self = locateSelf();
        std::string esc = escapeArg(prompt.str());
        // Write prompt to temp file, pipe via < redirect.
        std::string tmp;
#ifdef _WIN32
        char tbuf[MAX_PATH]; GetTempPathA(MAX_PATH, tbuf);
        char tn[MAX_PATH]; GetTempFileNameA(tbuf, "icmg", 0, tn);
        tmp = tn;
#else
        char tpl[] = "/tmp/icmg_refineXXXXXX";
        int fd = mkstemp(tpl); if (fd >= 0) close(fd);
        tmp = tpl;
#endif
        { std::ofstream f(tmp); f << prompt.str(); }
        std::string cmd = "\"" + self + "\" agent --no-pack --no-store \"refine memoir\" < \"" + tmp + "\"";
        auto res = core::safeExecShell(cmd, false, 180000);
        std::remove(tmp.c_str());
        if (res.exit_code != 0 || res.out.empty()) {
            std::cerr << "memoir refine: agent call failed (exit=" << res.exit_code << ")\n";
            return res.exit_code;
        }

        if (no_store) {
            std::cout << res.out;
            return 0;
        }

        // Store new memoir + supersede old.
        imem::MemoryNode neu;
        neu.topic     = n.topic;
        neu.content   = res.out;
        neu.keywords  = n.keywords + ",refined-from:" + std::to_string(n.id);
        neu.importance = std::max(1, n.importance);
        neu.zone      = n.zone;
        int64_t new_id = store.store(neu, /*force=*/true);

        // Supersede old (importance demote + tag).
        db.run("UPDATE memory_nodes SET importance = 1, "
               "keywords = COALESCE(keywords,'') || ',superseded-by:' || ? "
               "WHERE id = ?",
               {std::to_string(new_id), std::to_string(n.id)});
        std::cout << "Refined memoir #" << n.id << " -> new #" << new_id << "\n";
        return 0;
    }

    static std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }
    static std::string escapeArg(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
    static std::string truncate(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }

    int linkCmd(core::Db& db, const std::vector<std::string>& args) {
        if (hasFlag(args, "--auto")) return autoLink(db, args);
        if (args.empty()) { std::cerr << "icmg memoir link: <id> --to <other-id>  OR  --auto\n"; return 1; }
        int64_t a, b;
        try { a = std::stoll(args[0]); b = std::stoll(flagValue(args, "--to")); } catch (...) {
            std::cerr << "Invalid IDs\n"; return 1;
        }
        linkPair(db, a, b);
        std::cout << "Linked memoir #" << a << " <-> #" << b << "\n";
        return 0;
    }

    // Phase 35 T1: auto-cluster memoirs by cosine similarity ≥ threshold.
    // Threshold lower than `consolidate` (0.92) — "related" not "duplicate".
    int autoLink(core::Db& db, const std::vector<std::string>& args) {
        double threshold = 0.75;
        try { threshold = std::stod(flagValue(args, "--threshold", "0.75")); } catch (...) {}
        bool dry = hasFlag(args, "--dry-run");

        // Pull memoirs.
        struct M { int64_t id; std::string topic; std::string keywords; };
        std::vector<M> memoirs;
        db.query("SELECT id, topic, COALESCE(keywords,'') FROM memory_nodes "
                 "WHERE topic LIKE 'memoir:%' AND deleted_at IS NULL", {},
                 [&](const core::Row& r){
                     if (r.size() < 3) return;
                     try { memoirs.push_back({std::stoll(r[0]), r[1], r[2]}); } catch (...) {}
                 });
        if (memoirs.size() < 2) {
            std::cout << "memoir link --auto: <2 memoirs to compare\n";
            return 0;
        }

        // Need embedder for cosine.
        embed::EmbedStore es(db);
        std::vector<int64_t> ids;
        for (auto& m : memoirs) ids.push_back(m.id);
        // Use a default 384-dim — matches MiniLM. If no embeddings, vec will be empty.
        auto rows = es.getMany("memory", ids, 384);
        std::map<int64_t, std::vector<float>> by_id;
        for (auto& kv : rows) by_id.emplace(kv.first, std::move(kv.second));
        if (by_id.empty()) {
            std::cerr << "memoir link --auto: no embeddings (run `icmg embed memory` first)\n";
            return 1;
        }

        // Pairwise cosine, skip already-linked.
        int linked = 0;
        for (size_t i = 0; i < memoirs.size(); ++i) {
            auto va = by_id.find(memoirs[i].id);
            if (va == by_id.end() || va->second.empty()) continue;
            for (size_t j = i + 1; j < memoirs.size(); ++j) {
                auto vb = by_id.find(memoirs[j].id);
                if (vb == by_id.end() || vb->second.empty()) continue;
                double sim = embed::cosine(va->second, vb->second);
                if (sim < threshold) continue;
                std::string tag_a = "linked:" + std::to_string(memoirs[j].id);
                std::string tag_b = "linked:" + std::to_string(memoirs[i].id);
                if (memoirs[i].keywords.find(tag_a) != std::string::npos &&
                    memoirs[j].keywords.find(tag_b) != std::string::npos) continue;   // already linked
                std::cout << "  + #" << memoirs[i].id << " <-> #" << memoirs[j].id
                          << "  (cosine=" << std::fixed << std::setprecision(2) << sim << ")\n";
                if (!dry) {
                    linkPair(db, memoirs[i].id, memoirs[j].id);
                    // Update local copy so subsequent iterations dedupe.
                    memoirs[i].keywords += "," + tag_a;
                    memoirs[j].keywords += "," + tag_b;
                }
                ++linked;
            }
        }
        std::cout << "memoir link --auto: " << linked << " new link(s) "
                  << (dry ? "would be added [dry-run]" : "added")
                  << "  (threshold=" << threshold << ")\n";
        return 0;
    }

    // Phase 36 T2: bulk export memoirs to markdown files.
    int exportCmd(core::Db& db, const std::vector<std::string>& args) {
        std::string out_dir = flagValue(args, "--out", "memoirs");
        std::string filter  = flagValue(args, "--filter");
        bool force = hasFlag(args, "--force");

        std::filesystem::create_directories(out_dir);

        std::string sql = "SELECT id, topic, content, importance, "
                          "COALESCE(keywords,''), zone, created_at "
                          "FROM memory_nodes WHERE topic LIKE 'memoir:%' "
                          "AND deleted_at IS NULL";
        std::vector<std::string> params;
        if (!filter.empty()) {
            sql += " AND topic LIKE ?";
            params.push_back(filter + "%");
        }
        sql += " ORDER BY id";

        int written = 0, skipped = 0;
        db.query(sql, params, [&](const core::Row& r){
            if (r.size() < 7) return;
            std::string id = r[0], topic = r[1], content = r[2];
            std::string imp = r[3], kw = r[4], zone = r[5], created = r[6];
            std::string title = topic.size() > 7 ? topic.substr(7) : topic;
            std::string slug = slugify(title);
            std::filesystem::path out = std::filesystem::path(out_dir) / (slug + ".md");
            if (std::filesystem::exists(out) && !force) { ++skipped; return; }
            std::ofstream f(out);
            if (!f) { ++skipped; return; }
            f << "---\n"
              << "id: " << id << "\n"
              << "title: " << title << "\n"
              << "importance: " << imp << "\n"
              << "zone: " << zone << "\n"
              << "keywords: " << kw << "\n"
              << "created_at: " << created << "\n"
              << "---\n\n"
              << "# " << title << "\n\n"
              << content << "\n";
            ++written;
        });
        std::cout << "memoir export: written=" << written << " skipped=" << skipped
                  << " (--force to overwrite existing)\n";
        return 0;
    }

    static std::string slugify(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (std::isalnum((unsigned char)c)) out.push_back((char)::tolower((unsigned char)c));
            else if (c == ' ' || c == '_' || c == '-' || c == '.') out.push_back('-');
        }
        // Collapse runs of '-'.
        std::string compact;
        for (char c : out) {
            if (c == '-' && !compact.empty() && compact.back() == '-') continue;
            compact.push_back(c);
        }
        if (!compact.empty() && compact.back() == '-') compact.pop_back();
        if (!compact.empty() && compact.front() == '-') compact.erase(0, 1);
        if (compact.empty()) compact = "untitled";
        if (compact.size() > 100) compact.resize(100);
        return compact;
    }

    void linkPair(core::Db& db, int64_t a, int64_t b) {
        for (auto pair : std::vector<std::pair<int64_t, int64_t>>{{a, b}, {b, a}}) {
            db.run("UPDATE memory_nodes SET keywords = COALESCE(keywords, '') || ',linked:' || ? "
                   "WHERE id = ?",
                   {std::to_string(pair.second), std::to_string(pair.first)});
        }
    }
};

ICMG_REGISTER_COMMAND("memoir", MemoirCommand);

} // namespace icmg::cli
