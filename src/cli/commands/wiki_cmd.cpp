// `icmg wiki build` — generate Markdown + HTML knowledge wiki from graph.
//
// Output structure (--out wiki/, default):
//   wiki/
//     index.md / index.html       — file tree + memoir list + stats
//     files/<path>.md / .html     — per-file: symbols, callers, neighbors, memory
//     symbols/<name>.md / .html   — per-symbol: signature, body excerpt, callers
//     memoirs/<id>.md / .html     — long-form narratives
//     style.css                   — single self-contained stylesheet
//
// HTML is static — no JS, no CDN. Each .html mirrors its .md sibling, both
// link cross-references (file ↔ symbol ↔ memoir) using anchor links.
//
// Use case: hand wiki/ to teammates, host on github pages, use for onboarding.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace icmg::cli {

static std::string slug(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out.push_back(c);
        else if (c == '/' || c == '\\' || c == ' ') out.push_back('-');
    }
    if (out.empty()) out = "_";
    return out;
}

static std::string esc(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default:  out.push_back(c);
        }
    }
    return out;
}

static const char* CSS = R"(body{font:14px -apple-system,sans-serif;max-width:980px;margin:2em auto;padding:0 1em;color:#111}
h1{border-bottom:2px solid #eee;padding-bottom:.3em}
h2{margin-top:2em;color:#0366d6}
nav{background:#f6f8fa;padding:.7em;border-radius:6px;margin-bottom:1em}
nav a{margin-right:1em;color:#0366d6;text-decoration:none}
code{background:#f6f8fa;padding:2px 5px;border-radius:3px;font:13px Consolas,monospace}
pre{background:#f6f8fa;padding:1em;border-radius:6px;overflow:auto}
table{border-collapse:collapse;margin:1em 0}
th,td{border:1px solid #ddd;padding:.4em .8em}
th{background:#f6f8fa}
.muted{color:#666;font-size:.85em}
.tag{display:inline-block;background:#e1e4e8;border-radius:10px;padding:1px 8px;font-size:.8em;margin-right:.4em})";

class WikiCommand : public BaseCommand {
public:
    std::string name()        const override { return "wiki"; }
    std::string description() const override { return "Generate Markdown + HTML wiki from graph"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg wiki <action> [options]\n\n"
            "Actions:\n"
            "  build [--out DIR]    Generate wiki/ (default: ./wiki)\n"
            "  serve [--port N]     Print URL hint; static files only (use any HTTP server)\n\n"
            "Options for build:\n"
            "  --no-html       Skip HTML output (markdown only)\n"
            "  --no-md         Skip markdown output (HTML only)\n"
            "  --max-files N   Cap files indexed (default: all)\n"
            "  --include-memoirs   Add memoir pages\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string action = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (action == "build") return build(rest);
        if (action == "serve") {
            std::string out = flagValue(rest, "--out", "wiki");
            int port = 8000; try { port = std::stoi(flagValue(rest, "--port", "8000")); } catch (...) {}
            std::cout << "Wiki is static — serve with any HTTP server.\n"
                      << "Examples:\n"
                      << "  python -m http.server " << port << " -d " << out << "\n"
                      << "  npx serve " << out << " -p " << port << "\n";
            return 0;
        }
        std::cerr << "icmg wiki: unknown action: " << action << "\n";
        usage();
        return 1;
    }

private:
    int build(const std::vector<std::string>& args) {
        std::string out_dir = flagValue(args, "--out", "wiki");
        bool no_html = hasFlag(args, "--no-html");
        bool no_md   = hasFlag(args, "--no-md");
        bool with_memoirs = hasFlag(args, "--include-memoirs");
        int max_files = -1;
        try { max_files = std::stoi(flagValue(args, "--max-files", "-1")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        fs::create_directories(out_dir);
        fs::create_directories(fs::path(out_dir) / "files");
        fs::create_directories(fs::path(out_dir) / "symbols");
        if (with_memoirs) fs::create_directories(fs::path(out_dir) / "memoirs");

        // Style sheet
        if (!no_html) {
            std::ofstream f(fs::path(out_dir) / "style.css");
            f << CSS;
        }

        // Collect files
        struct FileRow { int64_t id; std::string path; std::string lang; std::string zone; };
        std::vector<FileRow> files;
        std::string fsql = "SELECT id, path, COALESCE(lang,''), COALESCE(zone,'default') "
                           "FROM graph_nodes WHERE kind='file' OR kind IS NULL ORDER BY path";
        if (max_files > 0) fsql += " LIMIT " + std::to_string(max_files);
        db.query(fsql, {}, [&](const core::Row& r){
            if (r.size() < 4) return;
            FileRow x; try { x.id = std::stoll(r[0]); } catch (...) { return; }
            x.path = r[1]; x.lang = r[2]; x.zone = r[3];
            files.push_back(std::move(x));
        });

        // Per-file pages
        int file_count = 0;
        for (auto& f : files) {
            std::string slug_path = slug(f.path);
            // Symbols under this file
            std::vector<std::tuple<int64_t, std::string, std::string, int, int>> syms;
            db.query("SELECT id, COALESCE(symbol_name,''), COALESCE(kind,''), "
                     "COALESCE(line_start,0), COALESCE(line_end,0) "
                     "FROM graph_nodes WHERE parent_id = ? ORDER BY line_start",
                     {std::to_string(f.id)},
                     [&](const core::Row& r){
                         if (r.size() < 5) return;
                         try {
                             syms.emplace_back(std::stoll(r[0]), r[1], r[2],
                                               std::stoi(r[3]), std::stoi(r[4]));
                         } catch (...) {}
                     });
            // Markdown
            if (!no_md) {
                std::ofstream md(fs::path(out_dir) / "files" / (slug_path + ".md"));
                md << "# " << f.path << "\n\n";
                md << "**Language:** " << (f.lang.empty() ? "?" : f.lang)
                   << "  **Zone:** " << f.zone << "  **Symbols:** " << syms.size() << "\n\n";
                md << "[Index](../index.md)\n\n";
                if (!syms.empty()) {
                    md << "## Symbols\n\n| Name | Kind | Lines |\n|---|---|---|\n";
                    for (auto& [sid, name, kind, ls, le] : syms) {
                        md << "| [" << name << "](../symbols/" << slug(name) << ".md) | "
                           << kind << " | " << ls << "-" << le << " |\n";
                    }
                    md << "\n";
                }
            }
            // HTML
            if (!no_html) {
                std::ofstream h(fs::path(out_dir) / "files" / (slug_path + ".html"));
                h << "<!doctype html><html><head><meta charset='utf-8'><title>" << esc(f.path)
                  << "</title><link rel=stylesheet href='../style.css'></head><body>"
                  << "<nav><a href='../index.html'>Index</a></nav>"
                  << "<h1>" << esc(f.path) << "</h1>"
                  << "<p><span class=tag>" << esc(f.lang) << "</span><span class=tag>"
                  << esc(f.zone) << "</span><span class=muted>" << syms.size() << " symbols</span></p>";
                if (!syms.empty()) {
                    h << "<h2>Symbols</h2><table><tr><th>Name</th><th>Kind</th><th>Lines</th></tr>";
                    for (auto& [sid, name, kind, ls, le] : syms) {
                        h << "<tr><td><a href='../symbols/" << esc(slug(name)) << ".html'>"
                          << esc(name) << "</a></td><td>" << esc(kind) << "</td><td>"
                          << ls << "-" << le << "</td></tr>";
                    }
                    h << "</table>";
                }
                h << "</body></html>";
            }
            ++file_count;
        }

        // Symbol pages
        std::set<std::string> sym_seen;
        int sym_count = 0;
        std::string ssql = "SELECT id, COALESCE(symbol_name,''), COALESCE(kind,''), "
                           "COALESCE(parent_id,0), COALESCE(line_start,0), COALESCE(line_end,0), "
                           "COALESCE(context,'') FROM graph_nodes "
                           "WHERE symbol_name IS NOT NULL AND symbol_name != '' "
                           "ORDER BY symbol_name";
        db.query(ssql, {}, [&](const core::Row& r){
            if (r.size() < 7) return;
            std::string name = r[1];
            std::string kind = r[2];
            std::string parent_id = r[3];
            std::string content = r[6].size() > 2000 ? r[6].substr(0, 2000) + "..." : r[6];
            std::string sl = slug(name);
            if (sym_seen.count(sl)) return;
            sym_seen.insert(sl);

            std::string parent_path;
            db.query("SELECT path FROM graph_nodes WHERE id=?", {parent_id},
                     [&](const core::Row& pr){ if (!pr.empty()) parent_path = pr[0]; });

            if (!no_md) {
                std::ofstream md(fs::path(out_dir) / "symbols" / (sl + ".md"));
                md << "# " << name << "\n\n";
                md << "**Kind:** " << kind;
                if (!parent_path.empty()) md << "  **File:** [" << parent_path
                                              << "](../files/" << slug(parent_path) << ".md)";
                md << "  **Lines:** " << r[4] << "-" << r[5] << "\n\n";
                md << "[Index](../index.md)\n\n";
                if (!content.empty()) md << "## Body excerpt\n\n```\n" << content << "\n```\n";
            }
            if (!no_html) {
                std::ofstream h(fs::path(out_dir) / "symbols" / (sl + ".html"));
                h << "<!doctype html><html><head><meta charset='utf-8'><title>" << esc(name)
                  << "</title><link rel=stylesheet href='../style.css'></head><body>"
                  << "<nav><a href='../index.html'>Index</a></nav>"
                  << "<h1>" << esc(name) << "</h1>"
                  << "<p><span class=tag>" << esc(kind) << "</span>";
                if (!parent_path.empty())
                    h << "<a href='../files/" << esc(slug(parent_path)) << ".html'>"
                      << esc(parent_path) << "</a>";
                h << "<span class=muted> L" << r[4] << "-" << r[5] << "</span></p>";
                if (!content.empty()) h << "<h2>Body excerpt</h2><pre>" << esc(content) << "</pre>";
                h << "</body></html>";
            }
            ++sym_count;
        });

        // Memoir pages
        int memoir_count = 0;
        if (with_memoirs) {
            db.query("SELECT id, topic, content FROM memory_nodes "
                     "WHERE topic LIKE 'memoir:%' AND deleted_at IS NULL", {},
                     [&](const core::Row& r){
                         if (r.size() < 3) return;
                         std::string id = r[0], title = r[1].substr(7), body = r[2];
                         std::string sl = slug(title);
                         if (!no_md) {
                             std::ofstream md(fs::path(out_dir) / "memoirs" / (sl + ".md"));
                             md << "# " << title << "\n\n[Index](../index.md)\n\n" << body << "\n";
                         }
                         if (!no_html) {
                             std::ofstream h(fs::path(out_dir) / "memoirs" / (sl + ".html"));
                             h << "<!doctype html><html><head><meta charset='utf-8'><title>" << esc(title)
                               << "</title><link rel=stylesheet href='../style.css'></head><body>"
                               << "<nav><a href='../index.html'>Index</a></nav>"
                               << "<h1>" << esc(title) << "</h1><pre>" << esc(body) << "</pre>"
                               << "</body></html>";
                         }
                         ++memoir_count;
                     });
        }

        // Index
        if (!no_md) {
            std::ofstream md(fs::path(out_dir) / "index.md");
            md << "# Project Wiki\n\n"
               << "Generated by `icmg wiki build`.\n\n"
               << "**Files:** " << file_count
               << " | **Symbols:** " << sym_count
               << " | **Memoirs:** " << memoir_count << "\n\n"
               << "## Files\n\n";
            for (auto& f : files) {
                md << "- [" << f.path << "](files/" << slug(f.path) << ".md)\n";
            }
        }
        if (!no_html) {
            std::ofstream h(fs::path(out_dir) / "index.html");
            h << "<!doctype html><html><head><meta charset='utf-8'><title>Project Wiki</title>"
              << "<link rel=stylesheet href='style.css'></head><body>"
              << "<h1>Project Wiki</h1>"
              << "<p>Generated by <code>icmg wiki build</code>.</p>"
              << "<p><span class=tag>" << file_count << " files</span>"
              << "<span class=tag>" << sym_count << " symbols</span>"
              << "<span class=tag>" << memoir_count << " memoirs</span></p>"
              << "<h2>Files</h2><ul>";
            for (auto& f : files) {
                h << "<li><a href='files/" << esc(slug(f.path)) << ".html'>"
                  << esc(f.path) << "</a> <span class=muted>" << esc(f.zone) << "</span></li>";
            }
            h << "</ul></body></html>";
        }

        std::cout << "Wiki built in " << out_dir << "/\n"
                  << "  files=" << file_count
                  << " symbols=" << sym_count
                  << " memoirs=" << memoir_count << "\n";
        if (!no_html) std::cout << "  Open " << out_dir << "/index.html in a browser.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("wiki", WikiCommand);

} // namespace icmg::cli
