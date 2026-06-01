// v1.22.0 (SC1+SC2+SC4): `icmg style-clone` — propagate UI layout from one
// reference file to many targets without re-reading the reference per-target.
//
// Subcommands:
//   extract <ref> --save-as <name>
//   apply <name> --to <glob> [--write]
//   diff <fileA> <fileB>
//   verify <name> --glob <pattern>
//   list
//   show <name>
//
// Default is `--dry-run`: apply emits a textual diff to stdout but does NOT
// touch any target. Pass `--write` to mutate.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../style_clone/layout_extractor.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string readFileAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool writeFileAtomic(const fs::path& p, const std::string& content) {
    fs::path tmp = p; tmp += ".style-clone.new";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return false;
        f << content;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::copy_file(tmp, p, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) return false;
    }
    return true;
}

// Apply a layout pattern to a target file. Strategy v1.22.0:
//   1. Parse target → its layout tree.
//   2. For each tag-position in target, replace its class list with the
//      classes from the same-position pattern node (preserve target's
//      attribute values + data bindings).
// This is a CONSERVATIVE patch — it only touches `class="..."` /
// `:class="..."` / `className="..."` attribute values. Nesting + tag names
// are NOT rewritten in v1.22.0. (Aggressive structural rewrite is v1.23+.)
struct ApplyResult {
    int    classes_changed = 0;
    int    nodes_visited   = 0;
    int    bytes_before    = 0;
    int    bytes_after     = 0;
    std::string new_content;
    std::vector<std::string> sample_diffs;  // first 5 changed-class examples
};

static std::vector<const style_clone::LayoutNode*>
flattenInOrder(const style_clone::LayoutNode& root) {
    std::vector<const style_clone::LayoutNode*> out;
    std::function<void(const style_clone::LayoutNode&)> walk = [&](auto& n){
        if (&n != &root) out.push_back(&n);
        for (auto& ch : n.children) walk(ch);
    };
    walk(root);
    return out;
}

static ApplyResult applyPatternToFile(const style_clone::LayoutTree& pattern,
                                      const fs::path& target_path) {
    ApplyResult r;
    std::string src = readFileAll(target_path);
    r.bytes_before = (int)src.size();
    if (src.empty()) { r.new_content = src; r.bytes_after = 0; return r; }

    std::string lang = style_clone::detectLang(target_path.string());
    auto target_tree = style_clone::extractLayout(src, lang);
    auto target_nodes = flattenInOrder(target_tree.root);
    auto pat_nodes    = flattenInOrder(pattern.root);

    // Build replacement plan: positions in source where `class="..."`
    // attribute starts on a tag whose in-order index has a pattern match.
    // We rewrite source IN-PLACE without re-parsing — find each tag opening
    // and substitute the class value if the same-index pattern node has
    // classes and tags match.
    static const std::regex re_tag_open(
        R"(<([A-Za-z][A-Za-z0-9\-_.]*)([^>]*)(/?>))",
        std::regex::ECMAScript);
    std::string out;
    out.reserve(src.size() + 64);
    size_t cursor = 0;
    size_t in_order_idx = 0;
    auto begin = std::sregex_iterator(src.begin(), src.end(), re_tag_open);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        auto m = *it;
        out.append(src, cursor, m.position() - cursor);
        cursor = m.position();
        std::string tag  = m[1].str();
        std::string body = m[2].str();
        std::string tail = m[3].str();
        if (in_order_idx < target_nodes.size()
         && in_order_idx < pat_nodes.size()
         && target_nodes[in_order_idx]->tag == tag
         && pat_nodes[in_order_idx]->tag    == tag
         && !pat_nodes[in_order_idx]->classes.empty()) {
            // Build new class= "..." attr from pattern.
            std::string new_classes;
            for (auto& c : pat_nodes[in_order_idx]->classes) {
                if (!new_classes.empty()) new_classes += " ";
                new_classes += c;
            }
            // Strip existing class / :class / className from body.
            static const std::regex re_strip(
                R"(\s*(?:class|:class|className)\s*=\s*"[^"]*")",
                std::regex::ECMAScript);
            std::string stripped = std::regex_replace(body, re_strip, "");
            // Re-emit canonical class attr first.
            std::string new_body = " class=\"" + new_classes + "\"" + stripped;
            std::string rebuilt = "<" + tag + new_body + tail;
            out.append(rebuilt);
            if (r.sample_diffs.size() < 5) {
                std::ostringstream s;
                s << "<" << tag << " ... > class: ["
                  << (target_nodes[in_order_idx]->classes.empty() ? "<none>"
                     : target_nodes[in_order_idx]->classes[0])
                  << "...] -> [" << new_classes << "]";
                r.sample_diffs.push_back(s.str());
            }
            ++r.classes_changed;
        } else {
            out.append(m[0].str());
        }
        cursor = m.position() + m.length();
        ++in_order_idx;
    }
    out.append(src, cursor, std::string::npos);
    r.nodes_visited = (int)in_order_idx;
    r.new_content   = out;
    r.bytes_after   = (int)out.size();
    return r;
}

static std::vector<fs::path> expandGlob(const std::string& pattern) {
    std::vector<fs::path> out;
    fs::path base = fs::current_path();
    std::string norm = pattern;
    // Very small glob: support `dir/*.ext` and bare path.
    auto star = norm.find('*');
    if (star == std::string::npos) {
        if (fs::exists(norm)) out.emplace_back(norm);
        return out;
    }
    fs::path glob_path = norm;
    fs::path dir = glob_path.parent_path();
    if (dir.empty()) dir = ".";
    std::string fname = glob_path.filename().string();
    // Build regex from glob.
    std::string rgx; rgx.reserve(fname.size() * 2);
    for (char c : fname) {
        if (c == '*') rgx += ".*";
        else if (c == '?') rgx += ".";
        else if (c == '.') rgx += "\\.";
        else rgx += c;
    }
    std::regex re(rgx);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (std::regex_match(e.path().filename().string(), re))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

class StyleCloneCommand : public BaseCommand {
public:
    std::string name()        const override { return "style-clone"; }
    std::string description() const override {
        return "Propagate UI layout from a reference file to N targets (v1.22.0)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg style-clone <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  extract <ref> --save-as <name>\n"
            "      Parse the reference file's layout (tags + classes + attr\n"
            "      names) and cache it under <name>. Read once, reuse N times.\n"
            "  apply <name> --to <glob> [--write]\n"
            "      Apply cached pattern to all files matching <glob>. Default\n"
            "      is --dry-run (emit textual diff only). Pass --write to\n"
            "      mutate.\n"
            "  diff <fileA> <fileB>\n"
            "      Structural diff (classes ignored when target has bindings)\n"
            "      between two files. Useful before clone-apply.\n"
            "  verify <name> --glob <pattern>\n"
            "      Surface files that no longer match the pattern's hash —\n"
            "      i.e. drifted since last apply.\n"
            "  list\n"
            "      List stored patterns + applied_count + node_count.\n"
            "  show <name>\n"
            "      Dump pattern metadata + class tokens.\n\n"
            "Supported langs: vue, jsx, tsx, html, svelte. Unknown extensions\n"
            "fall back to generic HTML-ish parsing.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        core::Db db(core::Config::instance().projectDbPath("."));

        if (sub == "extract")  return doExtract(db, rest);
        if (sub == "apply")    return doApply(db, rest);
        if (sub == "diff")     return doDiff(rest);
        if (sub == "verify")   return doVerify(db, rest);
        if (sub == "list")     return doList(db);
        if (sub == "show")     return doShow(db, rest);

        std::cerr << "style-clone: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    int doExtract(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "style-clone extract: <ref-file> required\n"; return 1;
        }
        std::string ref  = args[0];
        std::string name = flagValue(args, "--save-as");
        if (name.empty()) {
            std::cerr << "style-clone extract: --save-as <name> required\n"; return 1;
        }
        if (!fs::exists(ref)) {
            std::cerr << "style-clone extract: ref not found: " << ref << "\n"; return 1;
        }
        std::string src  = readFileAll(ref);
        std::string lang = style_clone::detectLang(ref);
        if (lang.empty()) {
            std::cerr << "style-clone extract: unknown lang for '" << ref
                      << "'; falling back to generic HTML-ish parse\n";
        }
        auto tree = style_clone::extractLayout(src, lang);
        auto j    = style_clone::layoutToJson(tree);
        auto hash = style_clone::structuralHash(tree);
        std::string class_tokens;
        for (auto& c : tree.class_set) {
            if (!class_tokens.empty()) class_tokens += ",";
            class_tokens += c;
        }

        // Upsert
        db.run(
            "INSERT INTO style_patterns(name, source_path, lang, layout_tree, "
            "    class_tokens, structural_hash, node_count) "
            "VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(name) DO UPDATE SET "
            "    source_path = excluded.source_path, "
            "    lang = excluded.lang, "
            "    layout_tree = excluded.layout_tree, "
            "    class_tokens = excluded.class_tokens, "
            "    structural_hash = excluded.structural_hash, "
            "    node_count = excluded.node_count, "
            "    updated_at = strftime('%s','now')",
            {name, ref, tree.detected_lang, j.dump(), class_tokens,
             hash, std::to_string(tree.node_count)});

        std::cout << "style-clone extract: cached '" << name
                  << "' from " << ref << " ("
                  << tree.node_count << " nodes, "
                  << tree.class_set.size() << " classes, hash="
                  << hash << ")\n";
        return 0;
    }

    int doApply(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "style-clone apply: <name> required\n"; return 1;
        }
        std::string name = args[0];
        std::string glob = flagValue(args, "--to");
        if (glob.empty()) {
            std::cerr << "style-clone apply: --to <glob> required\n"; return 1;
        }
        bool do_write = hasFlag(args, "--write");

        // Load pattern.
        std::string layout_json, lang_stored;
        db.query("SELECT layout_tree, lang FROM style_patterns WHERE name = ?",
                 {name}, [&](const core::Row& r){
                     if (r.size() < 2) return;
                     layout_json = r[0]; lang_stored = r[1];
                 });
        if (layout_json.empty()) {
            std::cerr << "style-clone apply: pattern '" << name
                      << "' not found. Run `icmg style-clone list`.\n";
            return 1;
        }
        style_clone::LayoutTree pattern;
        try { pattern = style_clone::layoutFromJson(json::parse(layout_json)); }
        catch (...) {
            std::cerr << "style-clone apply: pattern data corrupt\n"; return 1;
        }

        auto targets = expandGlob(glob);
        if (targets.empty()) {
            std::cerr << "style-clone apply: no files match '" << glob << "'\n";
            return 1;
        }
        std::cout << "style-clone apply: " << targets.size() << " target(s)"
                  << (do_write ? " (--write mode)" : " (dry-run; pass --write to mutate)")
                  << "\n\n";

        int total_changes = 0, total_bytes_before = 0, total_bytes_after = 0;
        for (const auto& tp : targets) {
            auto r = applyPatternToFile(pattern, tp);
            total_changes      += r.classes_changed;
            total_bytes_before += r.bytes_before;
            total_bytes_after  += r.bytes_after;
            std::cout << "  " << tp.string() << "  "
                      << r.classes_changed << "/" << r.nodes_visited
                      << " class attrs rewritten\n";
            for (const auto& d : r.sample_diffs)
                std::cout << "    " << d << "\n";
            if (do_write && r.classes_changed > 0) {
                if (writeFileAtomic(tp, r.new_content)) {
                    std::cout << "    -> WRITTEN\n";
                } else {
                    std::cerr << "    !! write failed for " << tp.string() << "\n";
                }
            }
        }

        // Bump applied_count.
        db.run("UPDATE style_patterns SET applied_count = applied_count + ? "
               "WHERE name = ?",
               {std::to_string((int)targets.size()), name});

        std::cout << "\nTotal: " << total_changes
                  << " class attrs across " << targets.size() << " file(s); "
                  << total_bytes_before << " -> " << total_bytes_after
                  << " bytes (delta " << (total_bytes_after - total_bytes_before)
                  << ").\n";
        if (!do_write) {
            std::cout << "Dry-run; re-run with --write to apply.\n";
        }
        return 0;
    }

    int doDiff(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "style-clone diff: <fileA> <fileB> required\n"; return 1;
        }
        std::string a = args[0], b = args[1];
        if (!fs::exists(a) || !fs::exists(b)) {
            std::cerr << "style-clone diff: file not found\n"; return 1;
        }
        auto ta = style_clone::extractLayout(readFileAll(a), style_clone::detectLang(a));
        auto tb = style_clone::extractLayout(readFileAll(b), style_clone::detectLang(b));
        auto ha = style_clone::structuralHash(ta);
        auto hb = style_clone::structuralHash(tb);
        std::cout << "A=" << a << "  hash=" << ha
                  << "  nodes=" << ta.node_count
                  << "  classes=" << ta.class_set.size() << "\n";
        std::cout << "B=" << b << "  hash=" << hb
                  << "  nodes=" << tb.node_count
                  << "  classes=" << tb.class_set.size() << "\n";
        std::cout << (ha == hb ? "MATCH" : "DIFFER")
                  << " (structural hash)\n";
        return ha == hb ? 0 : 1;
    }

    int doVerify(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "style-clone verify: <name> required\n"; return 1;
        }
        std::string name = args[0];
        std::string glob = flagValue(args, "--glob");
        if (glob.empty()) {
            std::cerr << "style-clone verify: --glob <pattern> required\n"; return 1;
        }
        std::string stored_hash;
        db.query("SELECT structural_hash FROM style_patterns WHERE name = ?",
                 {name}, [&](const core::Row& r){
                     if (!r.empty()) stored_hash = r[0];
                 });
        if (stored_hash.empty()) {
            std::cerr << "style-clone verify: pattern '" << name << "' not found\n";
            return 1;
        }
        auto targets = expandGlob(glob);
        int in_sync = 0, drifted = 0;
        for (const auto& tp : targets) {
            auto t = style_clone::extractLayout(readFileAll(tp),
                                                style_clone::detectLang(tp.string()));
            auto h = style_clone::structuralHash(t);
            bool match = (h == stored_hash);
            std::cout << "  " << (match ? "OK    " : "DRIFT ")
                      << tp.string() << "  " << h << "\n";
            (match ? in_sync : drifted)++;
        }
        std::cout << "\n" << in_sync << " in sync, " << drifted
                  << " drifted (need re-apply)\n";
        return drifted == 0 ? 0 : 1;
    }

    int doList(core::Db& db) {
        int n = 0;
        db.query("SELECT name, source_path, lang, node_count, applied_count, "
                 "       structural_hash, created_at "
                 "FROM style_patterns ORDER BY updated_at DESC", {},
                 [&](const core::Row& r){
                     if (r.size() < 7) return;
                     ++n;
                     std::cout << std::left << std::setw(20) << r[0]
                               << "  " << std::setw(6)  << r[2]
                               << "  nodes=" << std::setw(4) << r[3]
                               << "  applied=" << std::setw(4) << r[4]
                               << "  " << r[1] << "\n";
                 });
        if (n == 0) std::cout << "(no patterns stored)\n";
        return 0;
    }

    int doShow(core::Db& db, const std::vector<std::string>& args) {
        if (args.empty()) {
            std::cerr << "style-clone show: <name> required\n"; return 1;
        }
        int found = 0;
        db.query("SELECT name, source_path, lang, node_count, applied_count, "
                 "       structural_hash, class_tokens, created_at, updated_at "
                 "FROM style_patterns WHERE name = ?",
                 {args[0]}, [&](const core::Row& r){
                     if (r.size() < 9) return;
                     ++found;
                     std::cout << "name:       " << r[0] << "\n"
                               << "source:     " << r[1] << "\n"
                               << "lang:       " << r[2] << "\n"
                               << "nodes:      " << r[3] << "\n"
                               << "applied:    " << r[4] << "\n"
                               << "hash:       " << r[5] << "\n"
                               << "class set:  " << r[6] << "\n";
                 });
        if (!found) {
            std::cerr << "style-clone show: '" << args[0] << "' not found\n";
            return 1;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("style-clone", StyleCloneCommand);

} // namespace icmg::cli
