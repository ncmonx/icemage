// Phase 26 T3: `icmg memory extract-patterns` — promote recurring decisions
// to memoir.
//
// Group memory_nodes by topic prefix (first space-separated token).
// For prefixes with N+ entries (default 5):
//   - Compute Jaccard intersection of content tokens across all entries
//   - Top-K shared keywords as pattern signature
//   - Create or update memoir "pattern:<prefix>" summarizing
//
// Skip if pattern memoir's existing body matches signature hash (idempotent).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include "../../embed/embedder.hpp"   // fnv1a64
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#include <algorithm>

namespace icmg::cli {

class ExtractPatternsCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-extract-patterns"; }
    std::string description() const override { return "Promote recurring decisions to pattern memoirs"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory extract-patterns [options]\n\n"
            "Options:\n"
            "  --min N                   Min entries per prefix (default 5)\n"
            "  --topic-prefix S          Scope to prefix\n"
            "  --top-keywords K          Keywords per pattern (default 5)\n"
            "  --dry-run\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        int min_n = 5;
        try { min_n = std::stoi(flagValue(args, "--min", "5")); } catch (...) {}
        int top_k = 5;
        try { top_k = std::stoi(flagValue(args, "--top-keywords", "5")); } catch (...) {}
        std::string scope = flagValue(args, "--topic-prefix");
        bool dry = hasFlag(args, "--dry-run");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        // Group by prefix.
        std::map<std::string, std::vector<imem::MemoryNode>> groups;
        for (auto& n : store.all()) {
            std::string p = prefixOf(n.topic);
            if (p.empty() || p.find("memoir:") == 0 || p.find("pattern:") == 0) continue;
            if (!scope.empty() && p != scope) continue;
            groups[p].push_back(n);
        }

        int patterns = 0;
        for (auto& [prefix, nodes] : groups) {
            if ((int)nodes.size() < min_n) continue;
            // Token frequency across nodes
            std::map<std::string, int> tf;
            for (auto& n : nodes) {
                auto toks = tokens(n.topic + " " + n.content);
                for (auto& t : toks) ++tf[t];
            }
            // Pick tokens that appear in > 50% of entries
            int half = (int)nodes.size() / 2 + 1;
            std::vector<std::pair<std::string,int>> keys;
            for (auto& [t, c] : tf) if (c >= half) keys.push_back({t, c});
            std::sort(keys.begin(), keys.end(),
                      [](auto& a, auto& b){ return a.second > b.second; });
            if ((int)keys.size() > top_k) keys.resize(top_k);
            if (keys.empty()) continue;

            // Build pattern memoir content
            std::ostringstream body;
            body << "Pattern from " << nodes.size() << " entries with prefix '"
                 << prefix << "'.\n\n";
            body << "Common keywords: ";
            for (size_t i = 0; i < keys.size(); ++i) {
                if (i) body << ", ";
                body << keys[i].first << "(" << keys[i].second << ")";
            }
            body << "\n\nSamples:\n";
            int s = 0;
            for (auto& n : nodes) {
                if (++s > 3) break;
                body << "  - [" << n.id << "] " << truncate(n.content, 80) << "\n";
            }
            std::string content = body.str();
            std::string sig = embed::fnv1a64(prefix + content);

            // Skip if existing pattern memoir matches signature.
            std::string existing_sig;
            db.query("SELECT keywords FROM memory_nodes "
                     "WHERE topic = 'pattern:' || ? AND deleted_at IS NULL LIMIT 1",
                     {prefix},
                     [&](const core::Row& r){ if (!r.empty()) existing_sig = r[0]; });
            if (existing_sig.find("sig:" + sig) != std::string::npos) {
                std::cout << "  = pattern:" << prefix << " (unchanged)\n";
                continue;
            }

            std::cout << "  + pattern:" << prefix
                      << " (" << nodes.size() << " entries, "
                      << keys.size() << " keywords)\n";
            if (dry) continue;

            imem::MemoryNode pm;
            pm.topic   = "pattern:" + prefix;
            pm.content = content;
            pm.keywords = "pattern,sig:" + sig;
            pm.importance = 2;
            pm.zone = "default";
            try {
                // Soft-delete prior pattern memoir (idempotent replace).
                db.run("UPDATE memory_nodes SET deleted_at = strftime('%s','now') "
                       "WHERE topic = ? AND deleted_at IS NULL",
                       {pm.topic});
                store.store(pm, true);
                ++patterns;
            } catch (...) {}
        }
        std::cout << "patterns: " << patterns << " memoir(s) "
                  << (dry ? "would be " : "") << "created/updated.\n";
        return 0;
    }

private:
    static std::string prefixOf(const std::string& topic) {
        auto sp = topic.find(' ');
        std::string p = (sp == std::string::npos) ? topic : topic.substr(0, sp);
        if (p.size() > 30) p = p.substr(0, 30);
        return p;
    }
    static std::vector<std::string> tokens(const std::string& s) {
        static const std::set<std::string> stop = {
            "the","and","for","fix","bug","when","why","how","what","this","that",
            "must","can","run","get","with","from","into","onto","like","via",
            "use","using","need","needs","new","old","one","two","not","but"};
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (std::isalnum((unsigned char)c) || c == '_') cur.push_back((char)::tolower((unsigned char)c));
            else {
                if (cur.size() > 2 && !stop.count(cur)) out.push_back(cur);
                cur.clear();
            }
        }
        if (cur.size() > 2 && !stop.count(cur)) out.push_back(cur);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    static std::string truncate(const std::string& s, size_t n) {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }
};

ICMG_REGISTER_COMMAND("memory-extract-patterns", ExtractPatternsCommand);

} // namespace icmg::cli
