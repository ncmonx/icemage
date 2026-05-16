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

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <regex>

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

static int doIndex(ContextNodeStore& store, const std::vector<std::string>& args) {
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

        ContextNode node;
        node.node_key    = "skill-" + slugify(meta.name);
        node.title       = meta.name;
        node.content     = meta.description + "\n\n" + meta.content_summary;
        node.source_file = fpath;
        node.tier        = "skill";
        node.tags        = meta.trigger_keywords;
        node.active      = true;

        store.upsert(node);
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
            "      BM25 search skill index.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        // Open project DB. `manifest` is invoked by the SessionStart hook even
        // before `icmg init` has materialised a project DB — fail soft for
        // that path so the hook stays silent instead of erroring.
        auto& cfg = core::Config::instance();
        std::unique_ptr<core::Db> db;
        try {
            db = std::make_unique<core::Db>(cfg.projectDbPath("."));
        } catch (const std::exception& e) {
            if (sub == "manifest") return 0;  // SessionStart-safe no-op
            std::cerr << "skill: cannot open project DB: " << e.what() << "\n";
            return 1;
        }
        ContextNodeStore store(*db);

        if (sub == "index")    return doIndex(store, rest);
        if (sub == "list")     return doList(store, rest);
        if (sub == "manifest") return doManifest(store, rest);
        if (sub == "search")   return doSearch(store, rest);

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
