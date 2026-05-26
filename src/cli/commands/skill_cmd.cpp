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
#ifdef _WIN32
#include <windows.h>
#endif
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include "../../core/token_counter.hpp"
#include "../../imem/skill_chunker.hpp"
#include "../../embed/embedder.hpp"
#include "skill_recall.hpp"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <regex>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <unordered_map>

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

// v1.52.0: convert fs::path to UTF-8 std::string via Win API (avoids
// CP_ACP exception on non-1252 paths). On non-Windows, fall back to .string().
static std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
    const std::wstring& w = p.native();
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
#else
    return p.string();
#endif
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
        // Manual recursion: stdlib recursive_directory_iterator throws on
        // CP_ACP path conversion (Windows). Iterate per-dir with try/catch.
        std::vector<std::string> stack;
        stack.push_back(dir);
        while (!stack.empty()) {
            std::string cur = stack.back(); stack.pop_back();
            std::error_code ec;
            try {
                for (auto& entry : fs::directory_iterator(cur, ec)) {
                    if (ec) { ec.clear(); break; }
                    try {
                        const auto& pp = entry.path();
                        if (entry.is_directory(ec)) {
                            if (ec) { ec.clear(); continue; }
                            stack.push_back(pathToUtf8(pp));
                            continue;
                        }
                        if (pp.extension() != ".md") continue;
                        std::string path_u8 = pathToUtf8(pp);
                        std::string parent  = pathToUtf8(pp.parent_path().filename());
                        if (parent == "skills" || parent == "skill" ||
                            path_u8.find("/skills/") != std::string::npos ||
                            path_u8.find("\\skills\\") != std::string::npos) {
                            files.push_back(path_u8);
                        }
                    } catch (const std::exception&) { continue; }
                }
            } catch (const std::exception&) { continue; }
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

    // Lazy-construct embedder — returns nullptr if ONNX/python unavailable.
    auto embedder = icmg::embed::makeEmbedder();

    auto chunks = icmg::imem::SkillChunker::split(content, node_key);
    for (auto& chunk : chunks) {
        size_t tokens = icmg::core::estimateTokens(chunk.content);

        // Compute embedding BLOB (384-dim MiniLM → 1536 bytes).
        // Bind NULL when embedder unavailable, content empty, or dim wrong.
        std::vector<float> vec;
        if (embedder && embedder->available() && !chunk.content.empty()) {
            vec = embedder->embed(chunk.content);
        }

        const char* sql =
            "INSERT INTO skill_chunks(skill_id, parent_path, heading, content, token_count, embedding)"
            " VALUES (?, ?, ?, ?, ?, ?)";

        auto* raw = db.handle();
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) != SQLITE_OK) continue;

        sqlite3_bind_text(stmt, 1, skill_id_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, chunk.parent_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, chunk.heading.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, chunk.content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, (sqlite3_int64)tokens);

        if (vec.size() == 384) {
            int blob_bytes = (int)(vec.size() * sizeof(float));
            sqlite3_bind_blob(stmt, 6, vec.data(), blob_bytes, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 6);
        }

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
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

// ---- recallSkillChunks (shared helper) -------------------------------------
//
// Hybrid BM25+cosine recall over skill_chunks.
// Exposed via skill_recall.hpp so internals.cpp can call it without duplication.

std::vector<ScoredChunk> recallSkillChunks(
    Db&                db,
    const std::string& query,
    int                top_n,
    double             alpha,
    const std::string& skill_key_filter)
{
    if (query.empty()) return {};

    // Tokenize — mirrors ContextNodeStore::tokenize().
    auto tokenize = [](const std::string& text) -> std::vector<std::string> {
        std::vector<std::string> tokens;
        std::string tok;
        for (unsigned char c : text) {
            if (std::isalnum(c) || c == '_') {
                tok += static_cast<char>(std::tolower(c));
            } else {
                if (tok.size() >= 2) tokens.push_back(tok);
                tok.clear();
            }
        }
        if (tok.size() >= 2) tokens.push_back(tok);
        return tokens;
    };

    auto q_tokens = tokenize(query);

    struct ChunkCandidate {
        std::string parent_path;
        std::string heading;
        std::string content;
        std::string skill_key;
        std::vector<uint8_t> emb_blob;
    };

    std::vector<ChunkCandidate> candidates;

    auto rowToCandidate = [&](const Row& row) {
        if (row.size() < 5) return;
        ChunkCandidate c;
        c.parent_path = row[0];
        c.heading     = row[1];
        c.content     = row[2];
        c.skill_key   = row[3];
        if (row.size() >= 5 && !row[4].empty()) {
            const std::string& s = row[4];
            c.emb_blob.assign(
                reinterpret_cast<const uint8_t*>(s.data()),
                reinterpret_cast<const uint8_t*>(s.data()) + s.size());
        }
        candidates.push_back(std::move(c));
    };

    try {
        if (skill_key_filter.empty()) {
            db.query(
                "SELECT sc.parent_path, sc.heading, sc.content, cn.node_key, sc.embedding"
                " FROM skill_chunks sc"
                " JOIN context_nodes cn ON cn.id = sc.skill_id"
                " WHERE cn.tier = 'skill' AND cn.active = 1",
                {},
                rowToCandidate
            );
        } else {
            std::string skill_id_str;
            db.query(
                "SELECT id FROM context_nodes WHERE node_key=? AND tier='skill'",
                {skill_key_filter},
                [&](const Row& row) { if (!row.empty()) skill_id_str = row[0]; }
            );
            if (skill_id_str.empty()) return {};
            db.query(
                "SELECT sc.parent_path, sc.heading, sc.content, cn.node_key, sc.embedding"
                " FROM skill_chunks sc"
                " JOIN context_nodes cn ON cn.id = sc.skill_id"
                " WHERE sc.skill_id = ?",
                {skill_id_str},
                rowToCandidate
            );
        }
    } catch (...) { return {}; }

    if (candidates.empty()) return {};

    // BM25: tf × log(N/df) per query token.
    size_t N = candidates.size();
    std::unordered_map<std::string, int> df;
    for (auto& qt : q_tokens) {
        for (auto& cand : candidates) {
            auto tokens = tokenize(cand.content);
            for (auto& t : tokens) {
                if (t == qt) { df[qt]++; break; }
            }
        }
    }

    std::vector<double> bm25_raw(candidates.size(), 0.0);
    for (size_t ci = 0; ci < candidates.size(); ++ci) {
        auto tokens = tokenize(candidates[ci].content);
        std::unordered_map<std::string, int> tf;
        for (auto& t : tokens) tf[t]++;
        double score = 0.0;
        for (auto& qt : q_tokens) {
            auto it = tf.find(qt);
            if (it == tf.end() || it->second == 0) continue;
            int doc_freq = df.count(qt) ? df[qt] : 1;
            if (doc_freq == 0) doc_freq = 1;
            score += static_cast<double>(it->second)
                   * std::log(static_cast<double>(N) / static_cast<double>(doc_freq));
        }
        bm25_raw[ci] = score;
    }

    double max_bm25 = *std::max_element(bm25_raw.begin(), bm25_raw.end());
    std::vector<double> bm25_norm(candidates.size(), 0.0);
    if (max_bm25 > 1e-9) {
        for (size_t ci = 0; ci < candidates.size(); ++ci)
            bm25_norm[ci] = bm25_raw[ci] / max_bm25;
    }

    // Embed query once.
    std::vector<float> q_vec;
    {
        auto e = icmg::embed::makeEmbedder();
        if (e && e->available() && !query.empty()) {
            q_vec = e->embed(query);
        }
    }

    // Cosine similarity.
    auto cosineVec = [](const std::vector<float>& a, const std::vector<float>& b) -> double {
        if (a.size() != b.size() || a.empty()) return 0.0;
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (size_t i = 0; i < a.size(); ++i) {
            dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
            na  += static_cast<double>(a[i]) * static_cast<double>(a[i]);
            nb  += static_cast<double>(b[i]) * static_cast<double>(b[i]);
        }
        if (na < 1e-9 || nb < 1e-9) return 0.0;
        return dot / (std::sqrt(na) * std::sqrt(nb));
    };

    struct Scored { size_t idx; double score; };
    std::vector<Scored> ranked;
    ranked.reserve(candidates.size());

    for (size_t ci = 0; ci < candidates.size(); ++ci) {
        double cosine_score = 0.0;
        if (!q_vec.empty() && candidates[ci].emb_blob.size() == 384 * sizeof(float)) {
            std::vector<float> c_vec(384);
            std::memcpy(c_vec.data(), candidates[ci].emb_blob.data(), 384 * sizeof(float));
            cosine_score = cosineVec(q_vec, c_vec);
        }
        double final_score = alpha * cosine_score + (1.0 - alpha) * bm25_norm[ci];
        ranked.push_back({ci, final_score});
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    int take = std::min(top_n, (int)ranked.size());
    std::vector<ScoredChunk> results;
    results.reserve(take);
    for (int i = 0; i < take; ++i) {
        auto& cand = candidates[ranked[i].idx];
        results.push_back({cand.parent_path, cand.heading, cand.content,
                           cand.skill_key, ranked[i].score});
    }
    return results;
}

// ---- doAsk ------------------------------------------------------------------
//
// icmg skill ask "<query>" [--top N] [--alpha F] [--json] [--skill <key>]
//
// Hybrid recall over skill_chunks: BM25 over content + cosine over embedding BLOB.
// Gracefully degrades to BM25-only when embedder unavailable.
// Now delegates scoring to recallSkillChunks().

static int doAsk(Db& db, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "skill ask: missing <query>\n"
                     "Usage: icmg skill ask \"<query>\" [--top N] [--alpha F] [--json] [--skill <key>]\n";
        return 1;
    }

    std::string query;
    int top_n = 5;
    double alpha = 0.5;
    bool json_out = false;
    std::string skill_filter;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--json") {
            json_out = true;
        } else if (a == "--top" && i + 1 < args.size()) {
            try { top_n = std::max(1, std::min(50, std::stoi(args[++i]))); } catch (...) {}
        } else if (a == "--alpha" && i + 1 < args.size()) {
            try {
                double v = std::stod(args[++i]);
                alpha = std::max(0.0, std::min(1.0, v));
            } catch (...) {}
        } else if (a == "--skill" && i + 1 < args.size()) {
            skill_filter = args[++i];
        } else if (query.empty() && !a.empty() && a[0] != '-') {
            query = a;
        }
    }

    if (query.empty()) {
        std::cerr << "skill ask: missing <query>\n"
                     "Usage: icmg skill ask \"<query>\" [--top N] [--alpha F] [--json] [--skill <key>]\n";
        return 1;
    }

    // Delegate to shared helper.
    auto results = recallSkillChunks(db, query, top_n, alpha, skill_filter);

    if (results.empty()) {
        if (json_out) std::cout << "[]\n";
        return 0;
    }

    auto jsonEsc = [](const std::string& s) {
        std::string o; o.reserve(s.size());
        for (char c : s) {
            if (c == '"')  { o += "\\\""; continue; }
            if (c == '\\') { o += "\\\\"; continue; }
            if (c == '\n' || c == '\r') { o += ' '; continue; }
            o += c;
        }
        return o;
    };

    int take = (int)results.size();

    if (json_out) {
        std::cout << "[\n";
        for (int i = 0; i < take; ++i) {
            auto& r = results[i];
            std::string excerpt = r.content.substr(0, 200);
            std::cout << "  {"
                      << "\"path\":\"" << jsonEsc(r.parent_path) << "\","
                      << "\"heading\":\"" << jsonEsc(r.heading) << "\","
                      << "\"skill\":\"" << jsonEsc(r.skill_key) << "\","
                      << "\"score\":" << std::fixed << std::setprecision(4) << r.score << ","
                      << "\"content_excerpt\":\"" << jsonEsc(excerpt) << "\""
                      << "}" << (i + 1 < take ? "," : "") << "\n";
        }
        std::cout << "]\n";
        return 0;
    }

    for (int i = 0; i < take; ++i) {
        auto& r = results[i];
        std::string excerpt = r.content.substr(0, 200);
        std::cout << "[" << i + 1 << "] " << r.heading
                  << "  (score=" << std::fixed << std::setprecision(4) << r.score
                  << ", skill=" << r.skill_key << ")\n"
                  << "    path: " << r.parent_path << "\n"
                  << "    " << excerpt << "\n\n";
    }
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

// ---- add -------------------------------------------------------------------

// v1.51.0: write a user-authored skill .md to ~/.icmg/skills/<name>.md
static int doAdd(const std::vector<std::string>& args) {
    // args[0] = name, args[1] = content body
    if (args.size() < 2) {
        std::cerr << "Usage: icmg skill add <name> <content>\n";
        return 2;
    }
    const std::string& name = args[0];
    const std::string& body = args[1];

    // Sanitize: lowercase + [a-z0-9._-] only, other chars collapse to '-'.
    std::string san;
    for (char c : name) {
        char lc = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') ||
            lc == '.' || lc == '_' || lc == '-') {
            san += lc;
        } else if (!san.empty() && san.back() != '-') {
            san += '-';
        }
    }
    while (!san.empty() && san.back() == '-') san.pop_back();
    if (san.empty()) { std::cerr << "skill add: invalid skill name\n"; return 2; }

    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) { std::cerr << "skill add: HOME/USERPROFILE not set\n"; return 2; }

    fs::path dir = fs::path(home) / ".icmg" / "skills";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "skill add: cannot create " << dir.string() << ": " << ec.message() << "\n";
        return 1;
    }

    auto file = dir / (san + ".md");
    {
        std::ofstream f(file);
        if (!f) { std::cerr << "skill add: cannot write " << file.string() << "\n"; return 1; }
        f << "# " << name << "\n\n" << body << "\n";
    }

    std::cout << "skill saved: " << name << " -> " << file.string() << "\n";
    return 0;
}


// ---- remove ----------------------------------------------------------------

// v1.51.0: delete a user-authored skill .md from ~/.icmg/skills/<name>.md
static int doRemove(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: icmg skill remove <name>\n";
        return 2;
    }
    const std::string& name = args[0];

    // Sanitize: lowercase + [a-z0-9._-] only, other chars collapse to '-'.
    std::string san;
    for (char c : name) {
        char lc = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9') ||
            lc == '.' || lc == '_' || lc == '-') {
            san += lc;
        } else if (!san.empty() && san.back() != '-') {
            san += '-';
        }
    }
    while (!san.empty() && san.back() == '-') san.pop_back();
    if (san.empty()) { std::cerr << "skill remove: invalid skill name\n"; return 2; }

    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (!home) { std::cerr << "skill remove: HOME/USERPROFILE not set\n"; return 2; }

    auto file = fs::path(home) / ".icmg" / "skills" / (san + ".md");
    if (!fs::exists(file)) {
        std::cerr << "skill remove: skill not found: " << name << "\n";
        return 1;
    }
    std::error_code ec;
    fs::remove(file, ec);
    if (ec) {
        std::cerr << "skill remove: cannot delete " << file.string() << ": " << ec.message() << "\n";
        return 1;
    }
    std::cout << "skill removed: " << name << "\n";
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
            "      Access/rebuild skill_chunks rows for a skill.\n"
            "  ask <query> [--top N] [--alpha F] [--json] [--skill <key>]\n"
            "      Hybrid BM25+cosine recall over skill_chunks.\n"
            "      --top N      Return top N results (default 5, max 50).\n"
            "      --alpha F    Blend weight: 0.0=pure BM25, 1.0=pure cosine (default 0.5).\n"
            "      --json       JSON output: [{path, heading, skill, score, content_excerpt}].\n"
            "      --skill KEY  Restrict search to chunks of a single skill.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        // "add"/"remove"/"edit" write/delete files — no DB needed; handle before DB open.
        if (sub == "add")    return doAdd(rest);
        if (sub == "edit")   return doAdd(rest);  // alias: skill edit NAME BODY == add --update
        if (sub == "remove") return doRemove(rest);

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
            // `ask` with no query: will surface error in doAsk; with query but no DB: fail soft.
            if (sub == "ask") return 0;
            std::cerr << "skill: cannot open project DB: " << e.what() << "\n";
            return 1;
        }
        ContextNodeStore store(*db);

        if (sub == "index")    return doIndex(store, *db, rest);
        if (sub == "list")     return doList(store, rest);
        if (sub == "manifest") return doManifest(store, rest);
        if (sub == "search")   return doSearch(store, rest);
        if (sub == "chunk")    return doChunk(store, *db, rest);
        if (sub == "ask")      return doAsk(*db, rest);
        if (sub == "stats")    return doStats(store, rest);
        if (sub == "suggest")  return doSuggest(store, rest);
        if (sub == "add")      return doAdd(rest);
        if (sub == "edit")     return doAdd(rest);  // alias: skill edit NAME BODY == add --update
        if (sub == "remove")   return doRemove(rest);

        std::cerr << "skill: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }

// v1.18.0: per-skill size summary + total count.
static int doStats(ContextNodeStore& store,
                    const std::vector<std::string>& rest) {
    (void)rest;
    auto nodes = store.list("skill", true);
    std::cout << "icmg skill stats - " << nodes.size() << " skill(s) indexed\n";
    std::cout << "  "
              << std::left << std::setw(50) << "name"
              << std::setw(10) << "size_b" << "\n";
    size_t total = 0;
    for (auto& n : nodes) {
        std::cout << "  " << std::setw(50) << n.title.substr(0, 49)
                  << n.content.size() << "\n";
        total += n.content.size();
    }
    std::cout << "  total: " << total << " bytes across "
              << nodes.size() << " skills\n";
    return 0;
}

// v1.18.0: BM25-match prompt vs skill descriptions, return top-N.
static int doSuggest(ContextNodeStore& store,
                      const std::vector<std::string>& rest) {
    if (rest.empty() || rest[0].empty()) {
        std::cerr << "skill suggest: requires <prompt>\n";
        return 1;
    }
    std::string prompt = rest[0];
    int top = 3;
    for (size_t i = 1; i + 1 < rest.size(); ++i) {
        if (rest[i] == "--top") {
            try { top = std::stoi(rest[i+1]); } catch (...) {}
        }
    }
    auto hits = store.search(prompt, "skill", top, 0.05);
    std::cout << "icmg skill suggest - top " << hits.size()
              << " for: " << prompt.substr(0, 80) << "\n";
    for (auto& h : hits) {
        std::cout << "  - " << h.title << "  (" << h.node_key << ")\n";
        if (!h.content.empty()) {
            std::string snippet = h.content.substr(0, 120);
            std::cout << "    " << snippet
                      << (h.content.size() > 120 ? "..." : "") << "\n";
        }
    }
    if (hits.empty()) {
        std::cout << "  (no matches - try `icmg skill index`)\n";
    }
    return 0;
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
