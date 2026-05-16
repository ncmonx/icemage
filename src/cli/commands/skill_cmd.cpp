// icmg skill — skill index management.
//
// Subcommands:
//   index [--dir PATH] [--force]
//       Scan skill .md files from ~/.claude/ and project .claude/,
//       extract metadata (name, description, trigger keywords),
//       upsert into context_nodes with tier='skill'.
//   list [--json]
//       List indexed skills.
//   search <query>
//       BM25 search skill index.
//   chunk <skill_key> [--list [--json] | --get <parent_path> | --reindex]
//       Access skill_chunks rows for a skill.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include "../../core/token_counter.hpp"
#include "../../imem/skill_chunker.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <regex>
#include <iomanip>

namespace fs = std::filesystem;
using namespace icmg::core;

namespace icmg::cli {

// ---- skill metadata extraction ---------------------------------------------

struct SkillMeta {
    std::string name;
    std::string description;
    std::string trigger_keywords;
    std::string content_summary;
    std::string source_file;
};

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string slugify(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (c == ' ' || c == '_' || c == '-') {
            if (!out.empty() && out.back() != '-') out += '-';
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

static std::string extractField(const std::string& text, const std::string& field) {
    // Match "field: value" in YAML frontmatter or first H1/description line
    std::regex re("(?:^|\\n)" + field + ":\\s*(.+)");
    std::smatch m;
    if (std::regex_search(text, m, re)) return m[1].str();
    return "";
}

static std::string extractTriggerKeywords(const std::string& text) {
    // Look for "Use when:", "Trigger", "TRIGGER when", keywords section
    std::vector<std::string> keywords;

    // Extract words from "Use when: X" or "Trigger: X" lines
    std::regex trigger_re("(?:use when|trigger[s]?|when to use)[:\\s]+(.+)", std::regex::icase);
    std::sregex_iterator it(text.begin(), text.end(), trigger_re);
    std::sregex_iterator end;
    for (; it != end; ++it) {
        std::string line = (*it)[1].str();
        // Tokenize and take first 8 words
        std::istringstream iss(line);
        std::string tok;
        int count = 0;
        while (iss >> tok && count++ < 8) {
            // Strip punctuation
            tok.erase(std::remove_if(tok.begin(), tok.end(),
                [](char c){ return !std::isalnum(c) && c != '-'; }), tok.end());
            if (tok.size() >= 3) keywords.push_back(tok);
        }
    }

    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < keywords.size() && i < 15; ++i) {
        if (i) out << ",";
        out << "\"" << keywords[i] << "\"";
    }
    out << "]";
    return out.str();
}

static SkillMeta parseSkillFile(const std::string& path) {
    SkillMeta meta;
    meta.source_file = path;

    std::string text = readFile(path);
    if (text.empty()) return meta;

    // Name from frontmatter "name:" or filename stem
    meta.name = extractField(text, "name");
    if (meta.name.empty()) {
        meta.name = fs::path(path).stem().string();
    }

    // Description from frontmatter "description:" or first paragraph
    meta.description = extractField(text, "description");
    if (meta.description.empty()) {
        // First non-empty, non-header line
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line[0] != '#' && line[0] != '-' && line.size() > 10) {
                meta.description = line.substr(0, 200);
                break;
            }
        }
    }

    meta.trigger_keywords = extractTriggerKeywords(text);

    // Content summary: first 500 chars
    meta.content_summary = text.substr(0, std::min((int)text.size(), 500));

    return meta;
}

static std::vector<std::string> collectSkillFiles(const std::string& extra_dir = "") {
    std::vector<std::string> files;
    std::vector<std::string> search_dirs;

    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home) {
        search_dirs.push_back(std::string(home) + "/.claude");
        search_dirs.push_back(std::string(home) + "/.claude/plugins");
    }
    search_dirs.push_back(".claude");
    if (!extra_dir.empty()) search_dirs.push_back(extra_dir);

    for (auto& dir : search_dirs) {
        if (!fs::exists(dir)) continue;
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            auto& p = entry.path();
            if (p.extension() == ".md") {
                // Only index files in skills/ subdirs or named like a skill
                std::string stem = p.stem().string();
                std::string parent = p.parent_path().filename().string();
                if (parent == "skills" || parent == "skill" ||
                    p.string().find("/skills/") != std::string::npos ||
                    p.string().find("\\skills\\") != std::string::npos) {
                    files.push_back(p.string());
                }
            }
        }
    }
    return files;
}

// ---- subcommands -----------------------------------------------------------

// ---- chunk helpers ----------------------------------------------------------

// Insert or replace all chunks for a skill into skill_chunks table.
// skill_id: the integer rowid in context_nodes.
// node_key: used as prefix for chunk parent_path.
// content: full markdown text of the skill.
static void upsertChunks(Db& db, const std::string& skill_id_str,
                          const std::string& node_key,
                          const std::string& content) {
    // Delete existing chunks for this skill.
    db.run("DELETE FROM skill_chunks WHERE skill_id=?", {skill_id_str});

    auto chunks = icmg::imem::SkillChunker::split(content, node_key);
    for (auto& chunk : chunks) {
        size_t tokens = icmg::core::estimateTokens(chunk.content);
        db.run(
            "INSERT INTO skill_chunks(skill_id, parent_path, heading, content, token_count)"
            " VALUES (?, ?, ?, ?, ?)",
            {skill_id_str,
             chunk.parent_path,
             chunk.heading,
             chunk.content,
             std::to_string(tokens)}
        );
    }
}

// ---- doIndex ----------------------------------------------------------------

static int doIndex(ContextNodeStore& store, Db& db,
                   const std::vector<std::string>& args) {
    std::string extra_dir;
    bool force = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--dir"   && i+1 < args.size()) extra_dir = args[++i];
        else if (args[i] == "--force") force = true;
    }

    auto files = collectSkillFiles(extra_dir);
    int indexed = 0;

    for (auto& fpath : files) {
        auto meta = parseSkillFile(fpath);
        if (meta.name.empty()) continue;

        std::string node_key = "skill-" + slugify(meta.name);

        ContextNode node;
        node.node_key    = node_key;
        node.title       = meta.name;
        node.content     = meta.description + "\n\n" + meta.content_summary;
        node.source_file = fpath;
        node.tier        = "skill";
        node.tags        = meta.trigger_keywords;
        node.active      = true;

        int64_t row_id = store.upsert(node);

        // Populate skill_chunks for this skill.
        std::string full_content = readFile(fpath);
        if (!full_content.empty()) {
            upsertChunks(db, std::to_string(row_id), node_key, full_content);
        }

        ++indexed;
    }

    std::cout << "skill index: " << indexed << " skills indexed from "
              << files.size() << " file(s)\n";
    return 0;
}

static int doList(ContextNodeStore& store, const std::vector<std::string>& args) {
    bool json_out = false;
    for (auto& a : args) if (a == "--json") json_out = true;

    auto nodes = store.list("skill", true);

    if (json_out) {
        std::cout << "[\n";
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& n = nodes[i];
            std::cout << "  {\"name\":\"" << n.title
                      << "\",\"key\":\"" << n.node_key
                      << "\",\"source\":\"" << n.source_file << "\"}"
                      << (i+1 < nodes.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
        return 0;
    }

    std::cout << std::left << std::setw(40) << "SKILL" << "SOURCE\n";
    std::cout << std::string(70, '-') << "\n";
    for (auto& n : nodes) {
        std::string src = n.source_file;
        if (src.size() > 45) src = "..." + src.substr(src.size() - 42);
        std::cout << std::left << std::setw(40) << n.title.substr(0, 39) << src << "\n";
    }
    std::cout << "\n" << nodes.size() << " skill(s) indexed\n";
    return 0;
}

// v1.2.0: SessionStart auto-inject — emit "Available skills" block listing
// every indexed skill with its canonical access pattern (`icmg context
// skills/<name>` or `icmg recall "<keyword>"`). Plain markdown so it folds
// into SessionStart additionalContext.
static int doManifest(ContextNodeStore& store, const std::vector<std::string>& args) {
    bool json_out = false;
    int limit = 50;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--json") json_out = true;
        else if (a == "--limit" && i + 1 < args.size()) {
            try { limit = std::max(1, std::stoi(args[++i])); } catch (...) {}
        }
    }

    auto nodes = store.list("skill", true);
    if ((int)nodes.size() > limit) nodes.resize(limit);

    if (json_out) {
        std::cout << "{\"skills\":[";
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& n = nodes[i];
            // Cheap JSON escape — fine for slug + first-line title.
            auto esc = [](const std::string& s) {
                std::string o;
                for (char c : s) {
                    if (c == '"' || c == '\\') o += '\\';
                    if (c == '\n' || c == '\r') { o += ' '; continue; }
                    o += c;
                }
                return o;
            };
            std::string desc = n.content.substr(0, 120);
            if (!i) std::cout << "\n";
            std::cout << "  {\"name\":\"" << esc(n.node_key)
                      << "\",\"title\":\"" << esc(n.title)
                      << "\",\"description\":\"" << esc(desc)
                      << "\",\"access\":\"icmg context " << esc(n.node_key) << "\"}"
                      << (i+1 < nodes.size() ? ",\n" : "\n");
        }
        std::cout << "]}\n";
        return 0;
    }

    if (nodes.empty()) return 0;  // emit nothing — keeps SessionStart noise minimal

    std::cout << "## Available skills (stored knowledge, " << nodes.size() << " indexed)\n";
    std::cout << "Direct access patterns — use these instead of grep/Read when "
                 "the user names a skill:\n\n";
    for (auto& n : nodes) {
        std::string desc = n.content;
        // First line of content as short hint.
        auto nl = desc.find('\n');
        if (nl != std::string::npos) desc = desc.substr(0, nl);
        if (desc.size() > 90) desc = desc.substr(0, 87) + "...";
        std::cout << "- **" << n.title << "** (`" << n.node_key << "`)";
        if (!desc.empty()) std::cout << " — " << desc;
        std::cout << "\n  Access: `icmg context " << n.node_key
                  << "` | `icmg recall \"" << n.title << " <query>\"`\n";
    }
    std::cout << "\n";
    return 0;
}

// ---- doChunk ----------------------------------------------------------------
//
// icmg skill chunk <skill_key> [--list [--json] | --get <path> | --reindex]
//
// fail-soft: if DB cannot be opened the caller has already returned 0
// for "manifest"-like paths. Here we receive an open Db reference, so
// error paths return rc=1 per spec.

static int doChunk(ContextNodeStore& store, Db& db,
                   const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "skill chunk: missing <skill_key>\n";
        return 1;
    }

    const std::string& skill_key = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    // Resolve skill_key → id in context_nodes.
    std::string skill_id_str;
    std::string skill_content;
    db.query(
        "SELECT id, content FROM context_nodes WHERE node_key=? AND tier='skill'",
        {skill_key},
        [&](const Row& row) {
            if (row.size() >= 2) { skill_id_str = row[0]; skill_content = row[1]; }
        }
    );
    if (skill_id_str.empty()) {
        std::cerr << "skill chunk: skill '" << skill_key << "' not found in DB\n";
        return 1;
    }

    // Parse sub-flag (default: --list)
    bool do_list    = rest.empty();
    bool json_out   = false;
    bool do_reindex = false;
    std::string get_path;

    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == "--list")    { do_list = true; }
        else if (rest[i] == "--json") { json_out = true; }
        else if (rest[i] == "--reindex") { do_reindex = true; }
        else if (rest[i] == "--get" && i + 1 < rest.size()) {
            get_path = rest[++i];
        } else {
            // Unrecognised flag
            std::cerr << "skill chunk: unknown option '" << rest[i] << "'\n";
            return 1;
        }
    }

    // --reindex: re-read source_file, re-split, replace rows
    if (do_reindex) {
        // Try to read the original source_file from context_nodes.
        std::string source_file;
        db.query(
            "SELECT source_file FROM context_nodes WHERE id=?",
            {skill_id_str},
            [&](const Row& row) {
                if (!row.empty()) source_file = row[0];
            }
        );

        std::string raw;
        if (!source_file.empty()) {
            raw = readFile(source_file);
        }
        // Fall back to the stored content column if file not readable.
        if (raw.empty()) raw = skill_content;

        upsertChunks(db, skill_id_str, skill_key, raw);

        int n = 0;
        db.query(
            "SELECT COUNT(*) FROM skill_chunks WHERE skill_id=?",
            {skill_id_str},
            [&](const Row& row) { if (!row.empty()) n = std::stoi(row[0]); }
        );
        std::cout << "skill chunk: reindexed '" << skill_key << "' — " << n << " chunk(s)\n";
        return 0;
    }

    // --get <parent_path>: print raw content for that chunk
    if (!get_path.empty()) {
        std::string chunk_content;
        db.query(
            "SELECT content FROM skill_chunks WHERE skill_id=? AND parent_path=?",
            {skill_id_str, get_path},
            [&](const Row& row) { if (!row.empty()) chunk_content = row[0]; }
        );
        if (chunk_content.empty()) {
            std::cerr << "skill chunk: path '" << get_path << "' not found for skill '"
                      << skill_key << "'\n";
            return 1;
        }
        std::cout << chunk_content;
        return 0;
    }

    // --list (default)
    struct ChunkRow { std::string parent_path, heading, content; int tokens; };
    std::vector<ChunkRow> rows;
    db.query(
        "SELECT parent_path, heading, content, token_count FROM skill_chunks"
        " WHERE skill_id=? ORDER BY id",
        {skill_id_str},
        [&](const Row& row) {
            if (row.size() >= 4) {
                ChunkRow cr;
                cr.parent_path = row[0];
                cr.heading     = row[1];
                cr.content     = row[2];
                cr.tokens      = std::stoi(row[3]);
                rows.push_back(std::move(cr));
            }
        }
    );

    if (json_out) {
        std::cout << "[\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            auto& r = rows[i];
            // Minimal JSON escape
            auto esc = [](const std::string& s) {
                std::string o; o.reserve(s.size());
                for (char c : s) {
                    if (c == '"')  { o += "\\\""; continue; }
                    if (c == '\\') { o += "\\\\"; continue; }
                    if (c == '\n' || c == '\r') { o += ' '; continue; }
                    o += c;
                }
                return o;
            };
            std::cout
                << "  {\"path\":\"" << esc(r.parent_path)
                << "\",\"heading\":\"" << esc(r.heading)
                << "\",\"bytes\":" << r.content.size()
                << ",\"tokens\":" << r.tokens
                << "}" << (i + 1 < rows.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
        return 0;
    }

    // Table output
    std::cout << std::left
              << std::setw(45) << "PARENT_PATH"
              << std::setw(30) << "HEADING"
              << std::setw(8)  << "BYTES"
              << "TOKENS\n";
    std::cout << std::string(88, '-') << "\n";
    for (auto& r : rows) {
        std::string pp = r.parent_path;
        if (pp.size() > 44) pp = "..." + pp.substr(pp.size() - 41);
        std::string hd = r.heading;
        if (hd.size() > 29) hd = hd.substr(0, 26) + "...";
        std::cout << std::left
                  << std::setw(45) << pp
                  << std::setw(30) << hd
                  << std::setw(8)  << r.content.size()
                  << r.tokens << "\n";
    }
    std::cout << "\n" << rows.size() << " chunk(s) for '" << skill_key << "'\n";
    return 0;
}

static int doSearch(ContextNodeStore& store, const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "skill search: missing query\n"; return 1; }
    std::string query = args[0];
    auto results = store.search(query, "skill", 5, 0.0);
    if (results.empty()) {
        std::cout << "no skills matched '" << query << "'\n";
        return 0;
    }
    for (auto& n : results) {
        std::cout << n.title << "\n";
        std::cout << "  key:    " << n.node_key    << "\n";
        std::cout << "  source: " << n.source_file << "\n";
        std::cout << "  hint:   " << n.content.substr(0, 120) << "\n\n";
    }
    return 0;
}

// ---- command ---------------------------------------------------------------

class SkillCommand : public BaseCommand {
public:
    std::string name()        const override { return "skill"; }
    std::string description() const override { return "Skill index management (index/list/search)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg skill <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  index [--dir PATH] [--force]\n"
            "      Scan skill .md files → upsert into context_nodes (tier=skill).\n"
            "  list [--json]\n"
            "      List all indexed skills.\n"
            "  manifest [--json] [--limit N]\n"
            "      Emit skill discovery block (used by SessionStart hook).\n"
            "  search <query>\n"
            "      BM25 search skill index.\n"
            "  chunk <skill_key> [--list [--json] | --get <path> | --reindex]\n"
            "      Access/rebuild skill_chunks rows for a skill.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        // Open project DB. `manifest` and `chunk` (no DB) are invoked by the
        // SessionStart hook even before `icmg init` has materialised a project
        // DB — fail soft for those paths so the hook stays silent.
        auto& cfg = core::Config::instance();
        std::unique_ptr<core::Db> db;
        try {
            std::string db_path = cfg.projectDbOverride().empty()
                                  ? cfg.projectDbPath(".")
                                  : cfg.projectDbOverride();
            db = std::make_unique<core::Db>(db_path);
        } catch (const std::exception& e) {
            if (sub == "manifest") return 0;  // SessionStart-safe no-op
            // `chunk` with no skill_key: SessionStart hook path — fail soft.
            // `chunk` with a skill_key: real user invocation — surface the error.
            if (sub == "chunk" && rest.empty()) return 0;
            std::cerr << "skill: cannot open project DB: " << e.what() << "\n";
            return 1;
        }
        ContextNodeStore store(*db);

        if (sub == "index")    return doIndex(store, *db, rest);
        if (sub == "list")     return doList(store, rest);
        if (sub == "manifest") return doManifest(store, rest);
        if (sub == "search")   return doSearch(store, rest);
        if (sub == "chunk")    return doChunk(store, *db, rest);

        std::cerr << "skill: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("skill", SkillCommand);

// v1.1.0 Task 5: `icmg skill-index` alias.
// Pre-v1.0 docs + agent-prompt examples consistently used hyphenated form;
// nested-subcommand refactor left "unknown command" regression. Alias
// forwards to `skill index <args...>` so legacy invocations still work.
class SkillIndexAliasCommand : public BaseCommand {
public:
    std::string name()        const override { return "skill-index"; }
    std::string description() const override {
        return "Alias for `icmg skill index` (compat with pre-v1.0 docs)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg skill-index <action> [options]\n\n"
            "Alias for `icmg skill index` — same actions:\n"
            "  list / search / refresh / show <id>\n";
    }
    int run(const std::vector<std::string>& args) override {
        SkillCommand inner;
        std::vector<std::string> forwarded;
        forwarded.reserve(args.size() + 1);
        forwarded.push_back("index");
        for (auto& a : args) forwarded.push_back(a);
        return inner.run(forwarded);
    }
};
ICMG_REGISTER_COMMAND("skill-index", SkillIndexAliasCommand);

} // namespace icmg::cli
