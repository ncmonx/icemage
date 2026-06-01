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
#include <cctype>
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
        if (args.empty()) {
            std::cerr << "icmg memoir link: <id> --to <other-id> [--relation <type>]  OR  --auto\n"
                      << "  --relation: related_to (default) | depends_on | refines |\n"
                      << "              contradicts | alternative_to | caused_by |\n"
                      << "              instance_of | part_of | supersedes\n";
            return 1;
        }
        int64_t a, b;
        try { a = std::stoll(args[0]); b = std::stoll(flagValue(args, "--to")); } catch (...) {
            std::cerr << "Invalid IDs\n"; return 1;
        }
        // v1.20.8 (M5): typed relations. Default `related_to` preserves
        // backward compat with existing `linked:N` tags.
        std::string rel = flagValue(args, "--relation", "related_to");
        static const std::vector<std::string> valid_rels = {
            "related_to", "depends_on", "refines", "contradicts",
            "alternative_to", "caused_by", "instance_of", "part_of", "supersedes"
        };
        bool rel_ok = false;
        for (auto& r : valid_rels) if (r == rel) { rel_ok = true; break; }
        if (!rel_ok) {
            std::cerr << "icmg memoir link: invalid --relation '" << rel
                      << "'. Use --help to see valid types.\n";
            return 1;
        }
        linkPair(db, a, b, rel);
        std::cout << "Linked memoir #" << a << " --" << rel << "--> #" << b << "\n";
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

    // Phase 36 T2 + v1.21.0 (M6): bulk export memoirs.
    // v1.21.0: `--format <md|ai|ascii|dot>` controls output style.
    //   md    — original per-file YAML+markdown (default)
    //   ai    — single stdout dump optimized for LLM context (no YAML noise,
    //           compact heading + bullet keywords + content body)
    //   ascii — single stdout dump with box-drawing memoir cards, links shown
    //   dot   — v1.21.9 (M6 completion): Graphviz DOT graph. Node = memoir;
    //           edge = memoir_edges entry (typed via rel:<type>:N keyword,
    //           v1.20.8 typed-relations format). Pipe to `dot -Tsvg`.
    //           Importance maps to node color: critical=red, high=orange,
    //           medium=yellow, low=gray.
    int exportCmd(core::Db& db, const std::vector<std::string>& args) {
        std::string fmt     = flagValue(args, "--format", "md");
        std::string out_dir = flagValue(args, "--out", "memoirs");
        std::string filter  = flagValue(args, "--filter");
        bool force = hasFlag(args, "--force");

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

        if (fmt == "ai") {
            int n = 0;
            db.query(sql, params, [&](const core::Row& r){
                if (r.size() < 7) return;
                std::string title = r[1].size() > 7 ? r[1].substr(7) : r[1];
                std::cout << "## " << title << " (#" << r[0] << ")\n";
                if (!r[4].empty()) std::cout << "**tags:** " << r[4] << "\n";
                if (r[3] != "1") std::cout << "**importance:** " << r[3] << "\n";
                std::cout << "\n" << r[2] << "\n\n---\n\n";
                ++n;
            });
            std::cerr << "memoir export -f ai: " << n << " memoir(s) emitted\n";
            return 0;
        }
        if (fmt == "ascii") {
            int n = 0;
            db.query(sql, params, [&](const core::Row& r){
                if (r.size() < 7) return;
                std::string title = r[1].size() > 7 ? r[1].substr(7) : r[1];
                std::string content = r[2];
                if (content.size() > 200) content = content.substr(0, 200) + "...";
                std::cout << "+" << std::string(70, '-') << "+\n"
                          << "| #" << std::left << std::setw(4) << r[0]
                          << " " << std::setw(63) << title.substr(0, 63) << " |\n"
                          << "+" << std::string(70, '-') << "+\n"
                          << "| " << std::setw(68) << content.substr(0, 68) << " |\n";
                if (!r[4].empty()) {
                    std::string kw = r[4]; if (kw.size() > 65) kw = kw.substr(0, 62) + "...";
                    std::cout << "| tags: " << std::setw(62) << kw << " |\n";
                }
                std::cout << "+" << std::string(70, '-') << "+\n\n";
                ++n;
            });
            std::cerr << "memoir export -f ascii: " << n << " memoir(s)\n";
            return 0;
        }

        // v1.21.9 (M6 completion): Graphviz DOT format.
        if (fmt == "dot") {
            int n_nodes = 0, n_edges = 0;
            std::cout << "digraph memoirs {\n"
                         "  rankdir=LR;\n"
                         "  node [shape=box, style=\"rounded,filled\", "
                              "fontname=\"Helvetica\", fontsize=10];\n"
                         "  edge [fontname=\"Helvetica\", fontsize=8, "
                              "color=\"#666666\"];\n\n";
            // Collect ids → emit nodes + parse `rel:<type>:N` from keywords for edges.
            struct Item { std::string id; std::string title;
                          std::string imp; std::string kw; };
            std::vector<Item> items;
            db.query(sql, params, [&](const core::Row& r){
                if (r.size() < 7) return;
                Item it;
                it.id = r[0];
                std::string topic = r[1];
                it.title = topic.size() > 7 ? topic.substr(7) : topic;
                it.imp = r[3];
                it.kw  = r[4];
                items.push_back(std::move(it));
            });
            auto color = [](const std::string& imp_s) -> const char* {
                int imp = 1; try { imp = std::stoi(imp_s); } catch (...) {}
                if (imp >= 3) return "#ff6b6b";  // critical — red
                if (imp == 2) return "#ffa94d";  // high — orange
                if (imp == 1) return "#ffe066";  // medium — yellow
                return "#ced4da";                // low — gray
            };
            auto escape = [](std::string s) {
                std::string out;
                for (char c : s) {
                    if (c == '"' || c == '\\') out += '\\';
                    out += c;
                }
                if (out.size() > 60) out = out.substr(0, 57) + "...";
                return out;
            };
            for (const auto& it : items) {
                std::cout << "  m" << it.id << " [label=\"#" << it.id
                          << " " << escape(it.title)
                          << "\", fillcolor=\"" << color(it.imp) << "\"];\n";
                ++n_nodes;
            }
            std::cout << "\n";
            // Edges via keywords: tokens "rel:<type>:<dst-id>" or "linked:<dst-id>".
            for (const auto& it : items) {
                std::stringstream ss(it.kw);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    // trim
                    while (!tok.empty() && std::isspace((unsigned char)tok.front()))
                        tok.erase(tok.begin());
                    while (!tok.empty() && std::isspace((unsigned char)tok.back()))
                        tok.pop_back();
                    std::string rel_type, dst_id;
                    if (tok.rfind("rel:", 0) == 0) {
                        auto p1 = tok.find(':', 4);
                        if (p1 == std::string::npos) continue;
                        rel_type = tok.substr(4, p1 - 4);
                        dst_id   = tok.substr(p1 + 1);
                    } else if (tok.rfind("linked:", 0) == 0) {
                        rel_type = "related_to";
                        dst_id   = tok.substr(7);
                    } else { continue; }
                    if (dst_id.empty()) continue;
                    std::cout << "  m" << it.id << " -> m" << dst_id
                              << " [label=\"" << rel_type << "\"];\n";
                    ++n_edges;
                }
            }
            std::cout << "}\n";
            std::cerr << "memoir export -f dot: " << n_nodes << " node(s), "
                      << n_edges << " edge(s) — pipe to `dot -Tsvg -o out.svg`\n";
            return 0;
        }

        // md (default) — file-per-memoir
        std::filesystem::create_directories(out_dir);
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

    // v1.20.8 (M5): typed memoir relations. Tag format:
    //   - legacy:        `linked:N`        (still parsed for back-compat)
    //   - new typed:     `rel:<type>:N`    (e.g. `rel:depends_on:42`)
    // Stored in `keywords` column (comma-separated). Both sides get the
    // matching tag so traversal works in either direction.
    void linkPair(core::Db& db, int64_t a, int64_t b,
                  const std::string& rel = "related_to") {
        std::string tag_prefix = (rel == "related_to") ? "linked:" : ("rel:" + rel + ":");
        for (auto pair : std::vector<std::pair<int64_t, int64_t>>{{a, b}, {b, a}}) {
            db.run("UPDATE memory_nodes SET keywords = COALESCE(keywords, '') || ',' || ? || ? "
                   "WHERE id = ?",
                   {tag_prefix, std::to_string(pair.second), std::to_string(pair.first)});
        }
    }
};

ICMG_REGISTER_COMMAND("memoir", MemoirCommand);

} // namespace icmg::cli
