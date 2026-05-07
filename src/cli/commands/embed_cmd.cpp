// Phase 23 Task 1+2: `icmg embed` — backfill embeddings for memory + graph nodes.
// Run-once tool; subsequent runs skip rows whose body_hash is unchanged.
//
// Why deferred (not auto-on-store): Python sidecar takes ~2s to spawn +
// load model, which would dominate every `icmg store` call. Backfill in
// batches keeps store path fast.
//
// Usage:
//   icmg embed memory               # embed all non-deleted memory_nodes
//   icmg embed graph                # embed all graph_nodes (file + symbol)
//   icmg embed memory --limit 100   # process at most N
//   icmg embed --force              # ignore body_hash; re-embed all

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../embed/embedder.hpp"
#include "../../embed/embed_store.hpp"
#include <iostream>

namespace icmg::cli {

class EmbedCommand : public BaseCommand {
public:
    std::string name()        const override { return "embed"; }
    std::string description() const override { return "Build/refresh embeddings (semantic recall index)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg embed <kind> [options]\n\n"
            "Kinds:\n"
            "  memory          Embed non-deleted memory_nodes (topic + content)\n"
            "  graph           Embed graph_nodes (path + body) for symbol search\n\n"
            "Options:\n"
            "  --limit N       Stop after N rows (default: all)\n"
            "  --force         Re-embed even if body_hash unchanged\n"
            "  --status        Show current embedding count + skip\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        bool status = hasFlag(args, "--status");
        bool force  = hasFlag(args, "--force");
        int  limit  = -1;
        try { limit = std::stoi(flagValue(args, "--limit", "-1")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        embed::EmbedStore es(db);

        if (status) {
            int mem = es.count("memory"), gph = es.count("graph");
            std::cout << "Embeddings: memory=" << mem << " graph=" << gph << "\n";
            auto e = embed::makeEmbedder();
            std::cout << "Embedder: " << (e ? "available (" + e->model() + ", dim=" + std::to_string(e->dim()) + ")"
                                            : "unavailable (Python sidecar missing)") << "\n";
            return 0;
        }

        std::string kind;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { kind = a; break; } }
        if (kind != "memory" && kind != "graph") {
            std::cerr << "icmg embed: kind must be 'memory' or 'graph'\n";
            return 1;
        }

        auto embedder = embed::makeEmbedder();
        if (!embedder) {
            std::cerr << "icmg embed: Python sidecar unavailable. Install:\n"
                      << "  pip install sentence-transformers\n"
                      << "Then ensure embed/icmg_embedder.py is alongside the icmg binary,\n"
                      << "or set ICMG_EMBED_SCRIPT=<path>.\n";
            return 2;
        }

        int processed = 0, skipped = 0, errors = 0;
        std::string sql = (kind == "memory")
            ? "SELECT id, topic, content FROM memory_nodes WHERE deleted_at IS NULL"
            : "SELECT id, path, COALESCE(context,'') FROM graph_nodes";   // graph col is `context`

        // Validate UTF-8: walk bytes, replace any invalid sequence with '?'.
        // Catches stray cp1252 bytes (0x80-0x9F that aren't valid UTF-8 start),
        // truncated multi-byte tails, and overlong/illegal sequences. Without
        // this, JSON serialization in the sidecar protocol breaks (nlohmann
        // produces an exception, sidecar returns error, caller crashes).
        auto sanitize = [](const std::string& in) {
            std::string out; out.reserve(in.size());
            size_t i = 0;
            while (i < in.size()) {
                unsigned char c = (unsigned char)in[i];
                int need = 0;
                if      (c < 0x80) { out.push_back((char)c); ++i; continue; }
                else if ((c & 0xE0) == 0xC0) need = 1;
                else if ((c & 0xF0) == 0xE0) need = 2;
                else if ((c & 0xF8) == 0xF0) need = 3;
                else { out.push_back('?'); ++i; continue; }   // illegal start
                if (i + need >= in.size()) { out.push_back('?'); ++i; continue; }
                bool ok = true;
                for (int k = 1; k <= need; ++k) {
                    if ((((unsigned char)in[i+k]) & 0xC0) != 0x80) { ok = false; break; }
                }
                if (!ok) { out.push_back('?'); ++i; continue; }
                for (int k = 0; k <= need; ++k) out.push_back(in[i+k]);
                i += need + 1;
            }
            return out;
        };

        struct Row { int64_t id; std::string text; };
        std::vector<Row> rows;
        db.query(sql, {}, [&](const core::Row& r) {
            if (r.size() < 3) return;
            try {
                Row x;
                x.id = std::stoll(r[0]);
                // ASCII separator " - " avoids Windows cp1252 encoding pitfalls
                // when caller pipe is non-UTF8.
                if (kind == "memory") x.text = r[1] + " - " + r[2];
                else                  x.text = r[1] + "\n" + r[2];
                x.text = sanitize(x.text);
                if (x.text.size() > 8192) {
                    x.text.resize(8192);
                    // Avoid splitting UTF-8 multi-byte sequence at boundary.
                    while (!x.text.empty() && (((unsigned char)x.text.back() & 0xC0) == 0x80)) {
                        x.text.pop_back();
                    }
                }
                rows.push_back(std::move(x));
            } catch (...) {}
        });

        std::cout << "Embedding " << rows.size() << " " << kind << " rows..." << std::flush;
        for (auto& r : rows) {
            if (limit >= 0 && processed >= limit) break;
            std::string h = embed::fnv1a64(r.text);
            if (!force && es.fresh(r.id, kind, h)) { ++skipped; continue; }
            auto v = embedder->embed(r.text);
            if (v.empty()) { ++errors; continue; }
            es.put(r.id, kind, v, embedder->model(), h);
            ++processed;
            if (processed % 25 == 0) std::cout << "." << std::flush;
        }
        std::cout << "\n  embedded=" << processed
                  << " skipped=" << skipped
                  << " errors=" << errors << "\n";
        return errors > 0 ? 1 : 0;
    }
};

ICMG_REGISTER_COMMAND("embed", EmbedCommand);

} // namespace icmg::cli
