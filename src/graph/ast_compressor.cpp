// ast_compressor.cpp — regex-based function/class body elision.
//
// v1.3 approach: line-by-line scan + balanced-brace tracking (C-family) or
// indentation tracking (Python). No tree-sitter dependency.
//
// v1.4 note: swap to real ts_node queries once grammar bindings are ready.

#include "ast_compressor.hpp"
#include <sstream>
#include <vector>
#include <regex>
#include <algorithm>
#include <cctype>

namespace icmg::graph {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> splitLines(const std::string& src) {
    std::vector<std::string> lines;
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    // If source ends with newline, getline produces an empty last element —
    // that is correct. If source is empty, lines is empty — also correct.
    return lines;
}

static std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        out += '\n';
    }
    return out;
}

// Count leading spaces (used for Python indent detection).
static int leadingSpaces(const std::string& line) {
    int n = 0;
    for (char c : line) {
        if (c == ' ')  { ++n; continue; }
        if (c == '\t') { n += 4; continue; } // treat tab as 4
        break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// C-family body elision (C, C++, Go, Rust, JS, TS)
//
// Algorithm:
//   Walk lines. When we find a line that looks like a function/method/class/
//   struct signature (heuristic: ends in '{' on same or next line, and is
//   NOT a control-flow statement), we remember it. We then consume balanced
//   braces to find the end of the body and replace it with { /* ... */ }.
//
// Heuristic signature-detector (line-level):
//   - Does NOT start with common control-flow keywords (if/else/for/while/
//     switch/do/return/case).
//   - Contains '(' and ')' before any '{'.
//   - OR is a struct/class/impl declaration.
// ---------------------------------------------------------------------------

// Returns true if the line looks like a function/method/struct/class header
// (not a control-flow statement).
static bool looksLikeCFamilyHeader(const std::string& line) {
    // Trim leading whitespace for matching.
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        ++start;
    std::string trimmed = line.substr(start);

    // Skip control-flow keywords.
    static const std::vector<std::string> cf = {
        "if ", "if(", "else", "for ", "for(", "while ", "while(",
        "switch ", "switch(", "do ", "do{", "return ", "return;",
        "case ", "default:", "} else", "} catch", "catch(",
        "#", "//", "/*", "*"
    };
    for (auto& kw : cf) {
        if (trimmed.size() >= kw.size() &&
            trimmed.substr(0, kw.size()) == kw) return false;
    }

    // Must have a '{' somewhere on this line (body opener).
    if (line.find('{') == std::string::npos) return false;

    // Check: line has '(' ')' before '{' (function-like),
    // OR is a struct/class/impl/interface declaration.
    size_t brace_pos = line.find('{');
    std::string before_brace = line.substr(0, brace_pos);

    // struct / class / impl / interface (C++, Rust, Go, JS, TS)
    static const std::regex struct_re(
        R"(\b(struct|class|impl|interface|enum)\b)",
        std::regex::ECMAScript);
    if (std::regex_search(before_brace, struct_re)) return true;

    // function-like: has parens before the brace
    size_t open_paren  = before_brace.find('(');
    size_t close_paren = before_brace.rfind(')');
    if (open_paren != std::string::npos &&
        close_paren != std::string::npos &&
        close_paren > open_paren) return true;

    return false;
}

// Compress C-family source: scan for function/method/struct bodies and
// replace with { /* ... */ }.
static std::string compressCFamily(const std::string& source) {
    // Work character-by-character to handle multi-line bodies correctly.
    // States: NORMAL, IN_LINE_COMMENT, IN_BLOCK_COMMENT, IN_STRING, IN_CHAR.
    // When we see what looks like a header (heuristic), we wait for the
    // opening '{', then consume the body by tracking brace depth.

    std::string out;
    out.reserve(source.size());

    const size_t n = source.size();
    size_t i = 0;

    // Collect chars into current line for header detection.
    std::string cur_line;
    // Pending comment lines to emit before a header.
    std::string pending_comment_lines;
    // Buffer of lines that are part of a potential header (multi-line sigs).
    std::string header_buf;

    // State machine
    enum State { NORMAL, LINE_COMMENT, BLOCK_COMMENT, IN_STRING, IN_CHAR };
    State state = NORMAL;

    // We process line-by-line for header detection, character-by-character
    // for body consumption. Combine: collect a line, check if it's a header,
    // if so scan for '{' (possibly multi-line), consume body, emit placeholder.

    auto lines = splitLines(source);
    std::vector<std::string> result;

    // We need to join line-level decisions with character-level brace counting.
    // Simpler approach: walk lines, detect headers, then consume body.

    size_t li = 0;
    while (li < lines.size()) {
        const std::string& line = lines[li];

        // Check if this line is a comment-only line (preserve always).
        std::string trimmed = line;
        size_t ts = trimmed.find_first_not_of(" \t");
        std::string stripped = (ts != std::string::npos) ? trimmed.substr(ts) : "";

        bool is_comment = (!stripped.empty() &&
                           (stripped[0] == '/' || stripped[0] == '*' || stripped[0] == '#'));

        // Check if line looks like a C-family header with '{' on same line.
        if (looksLikeCFamilyHeader(line)) {
            // Found a header. Find the opening '{'.
            // It might be on the same line or a subsequent line.
            size_t brace_start_line = li;
            size_t first_brace_col = line.find('{');

            // Emit the header portion (up to and including the '{').
            // Collect multi-line sig if needed.
            std::string header_text;
            size_t j = li;
            size_t brace_col = std::string::npos;
            while (j < lines.size()) {
                brace_col = lines[j].find('{');
                if (brace_col != std::string::npos) break;
                header_text += lines[j] + "\n";
                ++j;
            }
            if (j >= lines.size()) {
                // No '{' found — not a real body, emit as-is.
                result.push_back(line);
                ++li;
                continue;
            }

            // Emit header lines up to (and including) the opening '{'.
            for (size_t k = li; k < j; ++k) result.push_back(lines[k]);
            // Emit the line up to and including '{'.
            result.push_back(lines[j].substr(0, brace_col + 1) + " /* ... */ }");

            // Now consume the body: track brace depth starting AFTER the '{'.
            int depth = 1;
            size_t ci = brace_col + 1; // char index within lines[j]
            size_t bl = j;
            bool in_str = false, in_chr = false, in_lc = false, in_bc = false;

            auto advance = [&]() -> char {
                while (bl < lines.size()) {
                    if (ci < lines[bl].size()) return lines[bl][ci++];
                    // end of line — newline
                    ci = 0;
                    ++bl;
                    return '\n';
                }
                return '\0';
            };

            while (depth > 0 && bl < lines.size()) {
                char c = advance();
                if (c == '\0') break;
                if (in_bc) {
                    if (c == '*' && bl < lines.size() && ci < lines[bl].size() && lines[bl][ci] == '/') {
                        advance(); in_bc = false;
                    }
                    continue;
                }
                if (in_lc) { if (c == '\n') in_lc = false; continue; }
                if (in_str) { if (c == '\\') advance(); else if (c == '"') in_str = false; continue; }
                if (in_chr) { if (c == '\\') advance(); else if (c == '\'') in_chr = false; continue; }
                if (c == '"')  { in_str = true; continue; }
                if (c == '\'') { in_chr = true; continue; }
                if (c == '/' && bl < lines.size() && ci < lines[bl].size()) {
                    if (lines[bl][ci] == '/') { in_lc = true; advance(); continue; }
                    if (lines[bl][ci] == '*') { in_bc = true; advance(); continue; }
                }
                if (c == '{') ++depth;
                if (c == '}') { --depth; }
            }
            // Skip any remaining content on the closing '}' line and advance
            // past it. Using `li = bl + 1` (not `li = bl`) is critical when
            // the whole function body lived on the same line as the header —
            // otherwise we'd reprocess the same line forever.
            li = bl + 1;
            continue;
        }

        result.push_back(line);
        ++li;
    }

    return joinLines(result);
}

// ---------------------------------------------------------------------------
// Python body elision
//
// Algorithm:
//   Walk lines. When we find `def ` or `class ` at any indent level,
//   remember its indent. Consume all following lines that are more indented,
//   and replace them with a single `    ...` placeholder at one more indent
//   level. Preserve docstrings? For v1.3 simplicity we drop the body entirely
//   and emit `    ...` (the placeholder at base_indent+4 spaces).
// ---------------------------------------------------------------------------

static std::string compressPython(const std::string& source) {
    auto lines = splitLines(source);
    std::vector<std::string> result;

    size_t li = 0;
    while (li < lines.size()) {
        const std::string& line = lines[li];
        std::string trimmed = line;
        size_t ts = trimmed.find_first_not_of(" \t");
        std::string stripped = (ts != std::string::npos) ? trimmed.substr(ts) : "";

        bool is_def   = stripped.substr(0, 4) == "def "  || stripped.substr(0, 8) == "async def";
        bool is_class = stripped.substr(0, 6) == "class ";

        if (is_def || is_class) {
            int base_indent = leadingSpaces(line);
            result.push_back(line);
            ++li;
            // Consume body: all lines with indent > base_indent (skip blank lines too).
            bool emitted_placeholder = false;
            while (li < lines.size()) {
                const std::string& body_line = lines[li];
                // Blank lines: consume but don't break
                std::string bt = body_line;
                size_t bts = bt.find_first_not_of(" \t\r");
                if (bts == std::string::npos) {
                    // Blank line — consume silently within body
                    ++li;
                    continue;
                }
                int this_indent = leadingSpaces(body_line);
                if (this_indent <= base_indent) break; // back to same or outer level
                // This is a body line — consume it.
                if (!emitted_placeholder) {
                    // Emit placeholder at base_indent+4 spaces.
                    result.push_back(std::string(base_indent + 4, ' ') + "...");
                    emitted_placeholder = true;
                }
                ++li;
            }
            if (!emitted_placeholder) {
                // Empty body (e.g., `def foo(): pass` on next line was consumed).
                result.push_back(std::string(base_indent + 4, ' ') + "...");
            }
            continue;
        }

        result.push_back(line);
        ++li;
    }

    return joinLines(result);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string detectLangFromExtension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    // Lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "c")                      return "c";
    if (ext == "cpp" || ext == "cc" ||
        ext == "cxx" || ext == "hpp" ||
        ext == "hxx" || ext == "h")      return "cpp";
    if (ext == "py")                     return "python";
    if (ext == "js" || ext == "mjs")     return "js";
    if (ext == "ts" || ext == "tsx")     return "ts";
    if (ext == "go")                     return "go";
    if (ext == "rs")                     return "rust";
    return "";
}

std::string compressAst(const std::string& source, const std::string& lang) {
    if (source.empty()) return source;

    try {
        // C-family languages all use brace-based body scan.
        if (lang == "c"  || lang == "cpp" ||
            lang == "go" || lang == "rust" ||
            lang == "js" || lang == "ts") {
            return compressCFamily(source);
        }
        if (lang == "python") {
            return compressPython(source);
        }
        // Unsupported lang — return verbatim.
        return source;
    } catch (...) {
        // Fail-soft: never crash the caller.
        return source;
    }
}

} // namespace icmg::graph
