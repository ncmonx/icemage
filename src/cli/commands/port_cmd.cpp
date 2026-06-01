// v1.24.0: `icmg port` — cross-project file bundle (P1-P4).
//
// Workflow:
//   Project 1: icmg port export --files "menus/*.vue" --name menu-batch --out menus.icmg-port
//   Transport menus.icmg-port to Project 2.
//   Project 2: icmg port apply menus.icmg-port --to src/views/ [--path-map menus/=src/views/]
//
// Token savings vs naive "Read 6 files + emit 6 files":
//   - Naive   : ~60 KB tokens (6 × 5 KB read + 6 × 5 KB emit + boilerplate)
//   - port    : ~5-8 KB tokens (1 export + 1 apply, dry-run preview)
//   - 8-12× saving in typical N=6 menu scenario.
//
// Artifact format (`.icmg-port`):
//   Header (4 text lines):
//     ICMG-PORT v1
//     FILES: <N>
//     RAW: <bytes>
//     HASH: <hex32 = FNV-128 of payload>
//     ---
//   Payload: JSON {"files":[{"path":"...","content":"..."}, ...]}
// Text format chosen over zstd-binary to avoid new lib dep + cross-platform
// endianness pitfalls. Compression can be a v1.25 optimization.

#include "../base_command.hpp"
#include "port_artifact.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace icmg::cli {

namespace {

// v1.27.0: fnv128hex / parseArtifact / serializeArtifact moved to
// port_artifact.hpp for unit-test surface. Use port_artifact::* below.

// Glob expansion. Supports `*` and `?` within filename, `**` for recursive,
// `{a,b,c}` brace expansion. Returns absolute paths under cwd.
std::vector<fs::path> expandGlob(const std::string& pattern, const fs::path& root) {
    std::vector<fs::path> out;
    // Brace expansion: split "menus/{A,B,C}.vue" into ["menus/A.vue", ...].
    auto braceExpand = [](const std::string& p) -> std::vector<std::string> {
        std::vector<std::string> result{p};
        auto lb = p.find('{');
        if (lb == std::string::npos) return result;
        auto rb = p.find('}', lb);
        if (rb == std::string::npos) return result;
        std::string prefix = p.substr(0, lb);
        std::string suffix = p.substr(rb + 1);
        std::string body = p.substr(lb + 1, rb - lb - 1);
        std::vector<std::string> alts;
        std::stringstream ss(body);
        std::string tok;
        while (std::getline(ss, tok, ',')) alts.push_back(tok);
        result.clear();
        for (auto& a : alts) result.push_back(prefix + a + suffix);
        return result;
    };
    // Convert glob to regex.
    auto globToRegex = [](const std::string& g) {
        std::string r;
        r.reserve(g.size() * 2 + 4);
        r.push_back('^');
        for (size_t i = 0; i < g.size(); ++i) {
            char c = g[i];
            if (c == '*') {
                if (i + 1 < g.size() && g[i + 1] == '*') { r += ".*"; ++i; }
                else r += "[^/\\\\]*";
            } else if (c == '?') {
                r += "[^/\\\\]";
            } else if (c == '.' || c == '+' || c == '(' || c == ')' || c == '|'
                    || c == '^' || c == '$' || c == '[' || c == ']' || c == '\\') {
                r.push_back('\\'); r.push_back(c);
            } else {
                r.push_back(c);
            }
        }
        r.push_back('$');
        return r;
    };

    auto patterns = braceExpand(pattern);
    for (auto& p : patterns) {
        // If no wildcards and file exists, take it literally.
        if (p.find('*') == std::string::npos
            && p.find('?') == std::string::npos) {
            fs::path abs = fs::path(p).is_absolute() ? fs::path(p) : (root / p);
            if (fs::exists(abs) && fs::is_regular_file(abs)) {
                out.push_back(abs);
            }
            continue;
        }
        std::regex re(globToRegex(p), std::regex::ECMAScript);
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            auto rel = fs::relative(it->path(), root, ec).generic_string();
            if (std::regex_match(rel, re)) out.push_back(it->path());
        }
    }
    // Dedupe + sort for determinism.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string readFileAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFileAll(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << content;
}

}  // namespace

class PortCommand : public BaseCommand {
public:
    std::string name()        const override { return "port"; }
    std::string description() const override {
        return "Cross-project file bundle (export/apply/list/show/delete) — v1.24.0";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg port <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  export --files <glob>... --name <id> --out <file.icmg-port>\n"
            "      Bundle matched files into a transportable artifact.\n"
            "      Supports brace + wildcard globs, e.g. \"menus/{A,B,C}.vue\".\n"
            "      Skips files larger than --max-file-mb (default 1).\n\n"
            "  apply <file.icmg-port> --to <dir> [--path-map A=B] [--write] [--overwrite]\n"
            "      Default is --dry-run (preview). --write applies non-conflicting\n"
            "      files. --overwrite also replaces existing (backs up to .bak-<ts>).\n"
            "      --path-map rewrites source paths (repeatable).\n\n"
            "  list [--limit N]            Recent bundles created on this machine.\n"
            "  show <name>                 Manifest for one bundle (no content blob).\n"
            "  delete <name>               Remove DB row (does NOT delete .icmg-port file).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        core::Db db(core::Config::instance().projectDbPath("."));

        if (sub == "export") return doExport(db, rest);
        if (sub == "apply")  return doApply(db, rest);
        if (sub == "list")   return doList(db, rest);
        if (sub == "show")   return doShow(db, rest);
        if (sub == "delete") return doDelete(db, rest);

        std::cerr << "port: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    int doExport(core::Db& db, const std::vector<std::string>& args) {
        std::vector<std::string> globs;
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--files") {
                while (i + 1 < args.size() && args[i + 1][0] != '-') {
                    globs.push_back(args[++i]);
                }
            }
        }
        std::string name = flagValue(args, "--name");
        std::string out_path = flagValue(args, "--out");
        std::string mfmb_s = flagValue(args, "--max-file-mb");
        int max_file_mb = 1;
        if (!mfmb_s.empty()) try { max_file_mb = std::stoi(mfmb_s); } catch (...) {}

        if (globs.empty() || name.empty() || out_path.empty()) {
            std::cerr << "port export: --files <glob>... --name <id> --out <file> required\n";
            return 1;
        }

        fs::path root = fs::current_path();
        std::vector<fs::path> all_files;
        for (auto& g : globs) {
            auto matches = expandGlob(g, root);
            all_files.insert(all_files.end(), matches.begin(), matches.end());
        }
        std::sort(all_files.begin(), all_files.end());
        all_files.erase(std::unique(all_files.begin(), all_files.end()),
                        all_files.end());

        if (all_files.empty()) {
            std::cerr << "port export: 0 files matched\n";
            return 1;
        }

        json payload;
        payload["files"] = json::array();
        json manifest = json::array();
        int64_t raw_bytes = 0;
        int skipped_large = 0;
        size_t max_bytes = (size_t)max_file_mb * 1024 * 1024;

        for (auto& p : all_files) {
            std::error_code ec;
            auto sz = fs::file_size(p, ec);
            if (ec) continue;
            if (sz > max_bytes) { ++skipped_large; continue; }
            std::string content = readFileAll(p);
            std::string rel = fs::relative(p, root, ec).generic_string();
            payload["files"].push_back({{"path", rel}, {"content", content}});
            manifest.push_back({{"path", rel}, {"bytes", (int64_t)sz}});
            raw_bytes += (int64_t)sz;
        }

        std::string payload_str = payload.dump();
        std::string artifact = port_artifact::serializeArtifact(
            name, (int)payload["files"].size(), raw_bytes, payload_str);
        std::string sha = port_artifact::fnv128hex(payload_str);

        writeFileAll(fs::path(out_path), artifact);

        // Upsert DB row.
        std::string mf = manifest.dump();
        db.run("INSERT INTO port_bundles(name, source_project, file_count, "
               "total_bytes_raw, total_bytes_compressed, artifact_path, "
               "artifact_sha256, manifest) VALUES(?,?,?,?,?,?,?,?) "
               "ON CONFLICT(name) DO UPDATE SET "
               "source_project=excluded.source_project, "
               "file_count=excluded.file_count, "
               "total_bytes_raw=excluded.total_bytes_raw, "
               "total_bytes_compressed=excluded.total_bytes_compressed, "
               "artifact_path=excluded.artifact_path, "
               "artifact_sha256=excluded.artifact_sha256, "
               "manifest=excluded.manifest",
               {name, root.string(),
                std::to_string((int)payload["files"].size()),
                std::to_string(raw_bytes),
                std::to_string((int64_t)artifact.size()),
                out_path, sha, mf});

        std::cout << "port export: " << payload["files"].size() << " files, "
                  << raw_bytes << " raw bytes -> " << artifact.size()
                  << " artifact bytes (" << out_path << ")\n";
        if (skipped_large > 0)
            std::cout << "  skipped " << skipped_large
                      << " file(s) larger than " << max_file_mb << " MB\n";
        std::cout << "  bundle name: " << name << "\n  hash: " << sha << "\n";
        return 0;
    }

    int doApply(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty() || args[0][0] == '-') {
            std::cerr << "port apply: <file.icmg-port> required as first arg\n";
            return 1;
        }
        std::string art_path = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        std::string to = flagValue(rest, "--to");
        if (to.empty()) {
            std::cerr << "port apply: --to <dir> required\n";
            return 1;
        }
        bool do_write = hasFlag(rest, "--write");
        bool overwrite = hasFlag(rest, "--overwrite");
        if (overwrite) do_write = true;  // --overwrite implies write

        // path-map: collect all --path-map A=B pairs.
        std::vector<std::pair<std::string, std::string>> path_maps;
        for (size_t i = 0; i + 1 < rest.size(); ++i) {
            if (rest[i] == "--path-map") {
                std::string spec = rest[i + 1];
                auto eq = spec.find('=');
                if (eq != std::string::npos) {
                    path_maps.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
                }
                ++i;
            }
        }

        std::string blob = readFileAll(fs::path(art_path));
        if (blob.empty()) {
            std::cerr << "port apply: artifact empty or unreadable: " << art_path << "\n";
            return 1;
        }
        auto parsed = port_artifact::parseArtifact(blob);
        if (!parsed.ok) {
            std::cerr << "port apply: " << parsed.error << "\n";
            return 1;
        }

        json payload;
        try { payload = json::parse(parsed.payload); }
        catch (...) { std::cerr << "port apply: payload not JSON\n"; return 1; }
        if (!payload.contains("files") || !payload["files"].is_array()) {
            std::cerr << "port apply: payload missing files array\n";
            return 1;
        }

        fs::path target_root = fs::path(to);

        struct Plan { fs::path dst; std::string content; bool exists; };
        std::vector<Plan> plan;
        for (auto& f : payload["files"]) {
            std::string src_path = f.value("path", std::string(""));
            std::string content  = f.value("content", std::string(""));
            std::string dst_rel  = src_path;
            for (auto& [from, to_p] : path_maps) {
                auto pos = dst_rel.find(from);
                if (pos == 0) {  // anchor at start only
                    dst_rel = to_p + dst_rel.substr(from.size());
                }
            }
            fs::path dst = target_root / dst_rel;
            plan.push_back({dst, content, fs::exists(dst)});
        }

        // Summary table.
        int n_new = 0, n_conflict = 0;
        for (auto& p : plan) {
            if (p.exists) ++n_conflict;
            else          ++n_new;
        }
        std::cout << "port apply: " << plan.size() << " files in bundle\n"
                  << "  new:       " << n_new << "\n"
                  << "  conflict:  " << n_conflict << "\n"
                  << "  mode:      "
                  << (do_write ? (overwrite ? "WRITE (overwrite)" : "WRITE (skip conflicts)")
                               : "DRY-RUN")
                  << "\n\n";
        for (auto& p : plan) {
            std::cout << "  " << (p.exists ? "[conflict] " : "[new]      ")
                      << p.dst.generic_string() << "  ("
                      << p.content.size() << " bytes)\n";
        }
        std::cout << "\n";

        if (!do_write) {
            std::cout << "(dry-run; pass --write to apply, --overwrite to replace conflicts)\n";
            return 0;
        }

        int written = 0, conflicts_skipped = 0, backed_up = 0;
        for (auto& p : plan) {
            if (p.exists) {
                if (!overwrite) { ++conflicts_skipped; continue; }
                int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                fs::path bak = p.dst; bak += ".bak-" + std::to_string(ts);
                std::error_code ec;
                fs::rename(p.dst, bak, ec);
                if (!ec) ++backed_up;
            }
            writeFileAll(p.dst, p.content);
            ++written;
        }

        // Bump applied_count IF artifact matches a row on this machine.
        std::string sha = port_artifact::fnv128hex(parsed.payload);
        db.run("UPDATE port_bundles SET applied_count = applied_count + 1 "
               "WHERE artifact_sha256 = ?", {sha});

        std::cout << "port apply: wrote " << written << " file(s)";
        if (backed_up > 0) std::cout << " (backed up " << backed_up << " existing)";
        if (conflicts_skipped > 0)
            std::cout << ", skipped " << conflicts_skipped
                      << " conflict(s) — use --overwrite";
        std::cout << "\n";
        return 0;
    }

    int doList(core::Db& db, const std::vector<std::string>& args) {
        std::string lim_s = flagValue(args, "--limit");
        int limit = 20;
        if (!lim_s.empty()) try { limit = std::stoi(lim_s); } catch (...) {}
        std::cout << "=== Port bundles (local machine) ===\n";
        int n = 0;
        db.query(
            "SELECT name, source_project, file_count, total_bytes_raw, "
            "       applied_count, created_at FROM port_bundles "
            "ORDER BY created_at DESC LIMIT ?",
            {std::to_string(limit)},
            [&](const core::Row& r) {
                if (r.size() < 6) return;
                ++n;
                std::cout << "  " << std::left << std::setw(28) << r[0]
                          << "  files=" << std::setw(4) << r[2]
                          << " raw=" << std::setw(8) << r[3]
                          << " applied=" << r[4] << "\n";
                std::cout << "    src: " << r[1] << "\n";
            });
        if (n == 0) std::cout << "  (no bundles)\n";
        return 0;
    }

    int doShow(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "port show: <name> required\n"; return 1;
        }
        int found = 0;
        db.query(
            "SELECT name, source_project, file_count, total_bytes_raw, "
            "       artifact_path, artifact_sha256, manifest, applied_count "
            "FROM port_bundles WHERE name = ?",
            {args[0]},
            [&](const core::Row& r) {
                if (r.size() < 8) return;
                ++found;
                std::cout << "=== Bundle: " << r[0] << " ===\n"
                          << "  source:   " << r[1] << "\n"
                          << "  files:    " << r[2] << "\n"
                          << "  raw:      " << r[3] << " bytes\n"
                          << "  artifact: " << r[4] << "\n"
                          << "  sha:      " << r[5] << "\n"
                          << "  applied:  " << r[7] << " times\n"
                          << "  manifest:\n";
                try {
                    auto mf = json::parse(r[6]);
                    for (auto& e : mf) {
                        std::cout << "    " << std::setw(8) << e.value("bytes", 0)
                                  << "  " << e.value("path", std::string()) << "\n";
                    }
                } catch (...) {
                    std::cout << "    (manifest parse failed)\n";
                }
            });
        if (found == 0) {
            std::cerr << "port show: '" << args[0] << "' not found\n";
            return 1;
        }
        return 0;
    }

    int doDelete(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "port delete: <name> required\n"; return 1;
        }
        db.run("DELETE FROM port_bundles WHERE name = ?", {args[0]});
        std::cout << "port delete: '" << args[0] << "' removed from DB "
                  << "(artifact file on disk unchanged)\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("port", PortCommand);

}  // namespace icmg::cli
