// v1.25.0 (W3): compressed-write expander implementation.
//
// Supports 4 magic headers:
//   @@ICMG-RAW@@              — explicit no-compression marker (pass-through)
//   @@ICMG-GLOSS@@            — glossary-compressed (existing Compressor)
//   @@ICMG-DIFF base=<sha10>@@ — unified diff against existing file's hash
//   @@ICMG-TPL  id=<name>@@    — slot-fill JSON against style_patterns row
//
// Unified diff parser is minimal — supports the subset bash/AI typically
// emit: `@@ -<start>,<count> +<start>,<count> @@` hunk headers + ` ` / `-`
// / `+` line prefixes. Mixed line endings (\r\n) tolerated. Context lines
// must match exactly; on mismatch we return the original input + ok=false.

#include "write_expander.hpp"
#include "compressor.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::compress {

namespace {

// FNV-1a 64-bit hex (10 chars from low bytes). Matches the "10-char SHA"
// the AI is asked to emit in the @@ICMG-DIFF base=<sha10>@@ header. We use
// FNV (not real SHA) because (a) we already use it elsewhere in icmg and
// (b) collision resistance for "did file change since last read" is fine.
std::string fnv10(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    static const char* hex = "0123456789abcdef";
    char raw[8];
    std::memcpy(raw, &h, 8);
    std::string out;
    for (int i = 0; i < 5; ++i) {
        out.push_back(hex[(unsigned char)raw[i] >> 4]);
        out.push_back(hex[(unsigned char)raw[i] & 0x0f]);
    }
    return out;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(s[i]);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        out += v[i];
        if (i + 1 < v.size()) out += '\n';
    }
    return out;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Minimal unified-diff applier. Returns expanded text on success;
// throws std::runtime_error on context mismatch / malformed input.
std::string applyUnifiedDiff(const std::string& base,
                              const std::vector<std::string>& diff_lines) {
    std::vector<std::string> base_lines = splitLines(base);
    std::vector<std::string> out;
    size_t base_i = 0;       // cursor into base_lines
    size_t i = 0;
    while (i < diff_lines.size()) {
        const std::string& line = diff_lines[i];
        // Find next hunk header `@@ -<a>,<b> +<c>,<d> @@`.
        if (line.size() < 4 || line[0] != '@' || line[1] != '@') {
            ++i; continue;
        }
        // Parse hunk header.
        int old_start = 0, old_count = 0, new_start = 0, new_count = 0;
        {
            // Extract numbers via simple scan.
            const char* p = line.c_str();
            while (*p && *p != '-') ++p;
            if (*p == '-') {
                ++p;
                old_start = std::atoi(p);
                while (*p && *p != ',' && *p != ' ') ++p;
                if (*p == ',') { ++p; old_count = std::atoi(p); }
                else old_count = 1;
            }
            while (*p && *p != '+') ++p;
            if (*p == '+') {
                ++p;
                new_start = std::atoi(p);
                while (*p && *p != ',' && *p != ' ') ++p;
                if (*p == ',') { ++p; new_count = std::atoi(p); }
                else new_count = 1;
            }
            (void)new_start; (void)new_count;
        }
        // Copy base lines up to (old_start - 1) verbatim.
        size_t target = (old_start > 0) ? (size_t)(old_start - 1) : 0;
        while (base_i < target && base_i < base_lines.size()) {
            out.push_back(base_lines[base_i++]);
        }
        // Apply hunk body.
        ++i;
        int old_seen = 0;
        while (i < diff_lines.size() && old_seen < old_count) {
            const std::string& hl = diff_lines[i];
            if (hl.size() >= 2 && hl[0] == '@' && hl[1] == '@') break;
            char tag = hl.empty() ? ' ' : hl[0];
            std::string body = hl.size() >= 1 ? hl.substr(1) : "";
            if (tag == ' ') {
                // Context: must match base.
                if (base_i >= base_lines.size() || base_lines[base_i] != body) {
                    throw std::runtime_error("context mismatch at base line "
                        + std::to_string(base_i + 1));
                }
                out.push_back(body);
                ++base_i;
                ++old_seen;
            } else if (tag == '-') {
                if (base_i >= base_lines.size() || base_lines[base_i] != body) {
                    throw std::runtime_error("removal mismatch at base line "
                        + std::to_string(base_i + 1));
                }
                ++base_i;
                ++old_seen;
            } else if (tag == '+') {
                out.push_back(body);
                // does not advance base_i / old_seen
            } else {
                // Tolerate trailing `\ No newline at end of file` marker.
                if (!hl.empty() && hl[0] == '\\') { ++i; continue; }
                throw std::runtime_error("unknown diff line tag");
            }
            ++i;
        }
    }
    // Copy remaining base lines.
    while (base_i < base_lines.size()) out.push_back(base_lines[base_i++]);
    return joinLines(out);
}

}  // namespace

ExpandResult expandCompressedWrite(const std::string& ai_content,
                                    const std::string& base_path) {
    ExpandResult res;
    res.bytes_in = (int)ai_content.size();

    auto lines = splitLines(ai_content);
    if (lines.empty()) {
        res.mode = "raw";
        res.content = ai_content;
        res.bytes_out = res.bytes_in;
        return res;
    }
    const std::string& first = lines.front();

    auto startsWith = [](const std::string& s, const std::string& pre) {
        return s.size() >= pre.size()
            && std::memcmp(s.data(), pre.data(), pre.size()) == 0;
    };

    // ---- @@ICMG-RAW@@ ----------------------------------------------------
    if (startsWith(first, "@@ICMG-RAW@@")) {
        std::string body;
        for (size_t i = 1; i < lines.size(); ++i) {
            body += lines[i];
            if (i + 1 < lines.size()) body += '\n';
        }
        res.mode = "raw";
        res.content = body;
        res.bytes_out = (int)body.size();
        return res;
    }

    // ---- @@ICMG-DIFF base=<sha10>@@ -------------------------------------
    if (startsWith(first, "@@ICMG-DIFF")) {
        // Parse sha10 hint.
        std::string expected_sha;
        auto eq = first.find("base=");
        if (eq != std::string::npos) {
            size_t end = first.find_first_of(" @", eq + 5);
            expected_sha = first.substr(eq + 5,
                end == std::string::npos ? std::string::npos : end - eq - 5);
        }
        if (base_path.empty()) {
            res.ok = false;
            res.mode = "diff";
            res.error = "ICMG-DIFF requires base file path";
            res.content = ai_content;  // pass-through
            res.bytes_out = res.bytes_in;
            return res;
        }
        std::string base = readFile(base_path);
        if (!expected_sha.empty()) {
            std::string actual = fnv10(base);
            if (actual != expected_sha) {
                res.ok = false;
                res.mode = "diff";
                res.error = "base SHA mismatch (expected "
                          + expected_sha + ", got " + actual + ")";
                res.content = ai_content;
                res.bytes_out = res.bytes_in;
                return res;
            }
        }
        std::vector<std::string> diff_lines(lines.begin() + 1, lines.end());
        try {
            res.content = applyUnifiedDiff(base, diff_lines);
            res.mode = "diff";
            res.bytes_out = (int)res.content.size();
            return res;
        } catch (const std::exception& e) {
            res.ok = false;
            res.mode = "diff";
            res.error = e.what();
            res.content = ai_content;
            res.bytes_out = res.bytes_in;
            return res;
        }
    }

    // ---- @@ICMG-GLOSS@@ -------------------------------------------------
    if (startsWith(first, "@@ICMG-GLOSS@@")) {
        // Glossary mode requires the @@MAP { ... } @@ block at the top.
        // Lightweight format: line2 = JSON {"c1":"long text",...}.
        // Body uses <%c1%> tokens which we substitute. If the map block is
        // missing we pass through (AI was instructed but failed).
        if (lines.size() < 3) {
            res.ok = false;
            res.mode = "glossary";
            res.error = "GLOSS mode requires map line + body";
            res.content = ai_content;
            res.bytes_out = res.bytes_in;
            return res;
        }
        // Naive parse: only handle simple {"key":"value",...} (no nested).
        const std::string& mapline = lines[1];
        std::vector<std::pair<std::string, std::string>> kv;
        size_t pos = 0;
        while ((pos = mapline.find('"', pos)) != std::string::npos) {
            size_t key_end = mapline.find('"', pos + 1);
            if (key_end == std::string::npos) break;
            std::string key = mapline.substr(pos + 1, key_end - pos - 1);
            size_t colon = mapline.find(':', key_end);
            if (colon == std::string::npos) break;
            size_t val_start = mapline.find('"', colon);
            if (val_start == std::string::npos) break;
            size_t val_end = mapline.find('"', val_start + 1);
            if (val_end == std::string::npos) break;
            std::string val = mapline.substr(val_start + 1, val_end - val_start - 1);
            kv.emplace_back(std::move(key), std::move(val));
            pos = val_end + 1;
        }
        std::string body;
        for (size_t i = 2; i < lines.size(); ++i) {
            body += lines[i];
            if (i + 1 < lines.size()) body += '\n';
        }
        // Token substitution.
        for (const auto& [k, v] : kv) {
            std::string tok = "<%" + k + "%>";
            size_t at = 0;
            while ((at = body.find(tok, at)) != std::string::npos) {
                body.replace(at, tok.size(), v);
                at += v.size();
            }
        }
        res.mode = "glossary";
        res.content = body;
        res.bytes_out = (int)body.size();
        return res;
    }

    // ---- @@ICMG-TPL id=<name>@@ -----------------------------------------
    if (startsWith(first, "@@ICMG-TPL")) {
        // Template-fill not yet implemented (requires style_patterns DB lookup
        // + slot-fill engine). For v1.25.0 we pass-through and log so the
        // feature surface exists; full expander ships in a follow-up.
        res.ok = false;
        res.mode = "template";
        res.error = "ICMG-TPL expansion not yet implemented in v1.25.0 (W3 partial)";
        res.content = ai_content;
        res.bytes_out = res.bytes_in;
        return res;
    }

    // No magic header → pass-through (AI didn't follow the rule, or
    // write-mode was off and content is raw). Mark mode=raw and return ok.
    res.mode = "raw";
    res.content = ai_content;
    res.bytes_out = res.bytes_in;
    return res;
}

}  // namespace icmg::compress
