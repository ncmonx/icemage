// Phase 39 T1: compressor implementation.

#include "compressor.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace icmg::compress {

namespace {

// FNV1a 64-bit → 16-char hex.
std::string fnv1a_hex(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

bool isCacheRegion(const std::string& s) {
    return s.find("<<CACHED>>") != std::string::npos
        || s.find("<<cached>>") != std::string::npos;
}

bool isSourceCodeKind(const std::string& kind) {
    static const std::vector<std::string> exts = {
        ".cs", ".ts", ".tsx", ".cpp", ".cc", ".cxx", ".hpp", ".h",
        ".py", ".rs", ".go", ".java", ".kt", ".swift", ".m", ".mm"
    };
    for (auto& e : exts) {
        if (kind.size() >= e.size()
            && kind.compare(kind.size() - e.size(), e.size(), e) == 0) return true;
    }
    if (kind == "code" || kind == "source") return true;
    return false;
}

// Split lines preserving newlines.
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        cur.push_back(c);
        if (c == '\n') { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Collapse runs of identical adjacent lines: "foo\nfoo\nfoo\n" → "foo\n<repeated 3×>\n"
std::string dedupLines(const std::string& s) {
    auto lines = splitLines(s);
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < lines.size()) {
        size_t j = i + 1;
        while (j < lines.size() && lines[j] == lines[i]) ++j;
        size_t run = j - i;
        out += lines[i];
        if (run >= 3) {
            out += "<icmg-repeat ";
            out += std::to_string(run - 1);
            out += "x>\n";
        } else if (run == 2) {
            out += lines[i];   // keep one extra rather than alias for tiny runs
        }
        i = j;
    }
    return out;
}

// Collect candidates matching a regex; return frequency map.
std::unordered_map<std::string,int> countRegex(const std::string& s, const std::regex& re) {
    std::unordered_map<std::string,int> freq;
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        freq[it->str()]++;
    }
    return freq;
}

// Replace whole-token occurrences (boundary-aware) of `needle` with `alias`.
std::string replaceAll(const std::string& s, const std::string& needle, const std::string& alias) {
    if (needle.empty()) return s;
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        size_t pos = s.find(needle, i);
        if (pos == std::string::npos) { out.append(s, i, std::string::npos); break; }
        out.append(s, i, pos - i);
        out += alias;
        i = pos + needle.size();
    }
    return out;
}

// Boilerplate filler words/phrases (LLMLingua-inspired conservative set).
const std::vector<std::string>& fillerWords() {
    static const std::vector<std::string> w = {
        " really ", " actually ", " basically ", " just ",
        " simply ", " literally ", " honestly ", " truly ",
        " very very ", " quite ", " somewhat ", " rather "
    };
    return w;
}

std::string stripFiller(const std::string& s) {
    std::string out = s;
    // Loop until stable — adjacent occurrences may need multiple passes
    // since each replace consumes a leading space.
    for (int pass = 0; pass < 5; ++pass) {
        std::string prev = out;
        for (auto& w : fillerWords()) {
            out = replaceAll(out, w, " ");
        }
        if (out == prev) break;
    }
    return out;
}

} // namespace

int Compressor::estimateTokens(const std::string& s) {
    // Heuristic: 1 token ≈ 4 chars for English ASCII per OpenAI rule of thumb.
    // Anthropic tokenizer ≈ same order. Round up.
    return (int)((s.size() + 3) / 4);
}

std::string Compressor::contentHash(const std::string& s) {
    return fnv1a_hex(s);
}

bool Compressor::shouldCompress(const std::string& input,
                                 const std::string& content_kind,
                                 std::string* skip_reason) const {
    int tok = estimateTokens(input);
    if (tok < opts_.threshold_tok) {
        if (skip_reason) *skip_reason = "below threshold (" + std::to_string(tok)
                                     + "<" + std::to_string(opts_.threshold_tok) + ")";
        return false;
    }
    if (isSourceCodeKind(content_kind)) {
        if (skip_reason) *skip_reason = "source-code content (lossy edit risk)";
        return false;
    }
    if (opts_.respect_cache_sentinel && isCacheRegion(input)) {
        if (skip_reason) *skip_reason = "contains <<CACHED>> sentinel; pass-through";
        return false;
    }
    return true;
}

CompressResult Compressor::compress(const std::string& input,
                                     const std::string& content_kind) {
    auto t0 = std::chrono::steady_clock::now();
    CompressResult r;
    r.bytes_in = (int)input.size();
    r.tok_in   = estimateTokens(input);
    r.content_hash = fnv1a_hex(input);
    r.mode_used = opts_.mode;

    std::string skip;
    if (!shouldCompress(input, content_kind, &skip)) {
        r.skipped = true;
        r.skip_reason = skip;
        r.text = input;
        r.body_only = input;
        r.bytes_out = (int)input.size();
        r.tok_out = r.tok_in;
        auto t1 = std::chrono::steady_clock::now();
        r.elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        return r;
    }

    std::string body = input;

    // Stage 1: line dedup.
    body = dedupLines(body);

    // Stage 2: path glossary (paths with /).
    {
        std::regex path_re(R"([A-Za-z0-9_./\-]*[/][A-Za-z0-9_./\-]+)");
        auto freq = countRegex(body, path_re);
        std::vector<std::pair<std::string,int>> ranked;
        for (auto& kv : freq) {
            if ((int)kv.first.size() >= opts_.min_path_len
                && kv.second >= opts_.min_path_freq) {
                ranked.emplace_back(kv.first, kv.second);
            }
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b){
                      return a.first.size() * a.second > b.first.size() * b.second;
                  });
        int idx = 1;
        for (auto& kv : ranked) {
            std::string alias = "@P" + std::to_string(idx++);
            r.glossary[alias] = kv.first;
            body = replaceAll(body, kv.first, alias);
        }
    }

    // Stage 3: identifier glossary (long camelCase/snake_case).
    {
        std::regex id_re(R"([A-Za-z_][A-Za-z0-9_]{7,})");
        auto freq = countRegex(body, id_re);
        std::vector<std::pair<std::string,int>> ranked;
        for (auto& kv : freq) {
            // Skip aliases we just minted.
            if (kv.first.size() >= 2 && kv.first[0] == 'P'
                && r.glossary.count("@" + kv.first)) continue;
            if ((int)kv.first.size() >= opts_.min_ident_len
                && kv.second >= opts_.min_ident_freq) {
                ranked.emplace_back(kv.first, kv.second);
            }
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b){
                      return a.first.size() * a.second > b.first.size() * b.second;
                  });
        int idx = 1;
        for (auto& kv : ranked) {
            std::string alias = "$I" + std::to_string(idx++);
            r.glossary[alias] = kv.first;
            body = replaceAll(body, kv.first, alias);
        }
    }

    // Stage 4: aggressive — filler strip.
    if (opts_.mode == Mode::Aggressive) {
        body = stripFiller(body);
    }

    // Build output.
    std::ostringstream os;
    os << "<icmg-glossary v=1 hash=" << r.content_hash << ">\n";
    for (auto& kv : r.glossary) os << kv.first << "=" << kv.second << "\n";
    os << "</icmg-glossary>\n<icmg-body>\n" << body;
    if (body.empty() || body.back() != '\n') os << "\n";
    os << "</icmg-body>\n";

    r.body_only = body;
    r.text = os.str();
    r.bytes_out = (int)r.text.size();
    r.tok_out = estimateTokens(r.text);
    auto t1 = std::chrono::steady_clock::now();
    r.elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return r;
}

std::string Compressor::expand(const std::string& text,
                                const std::map<std::string,std::string>& glossary,
                                bool strict,
                                std::string* err) {
    std::string out = text;
    // Replace longest aliases first to avoid partial-prefix collisions.
    std::vector<std::pair<std::string,std::string>> sorted(glossary.begin(), glossary.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b){ return a.first.size() > b.first.size(); });
    for (auto& kv : sorted) {
        out = replaceAll(out, kv.first, kv.second);
    }
    if (strict) {
        // Detect any leftover @P\d+ or $I\d+.
        std::regex leftover(R"((@P\d+|\$I\d+))");
        std::smatch m;
        if (std::regex_search(out, m, leftover)) {
            if (err) *err = "unknown alias: " + m.str();
        }
    }
    (void)err;
    return out;
}

bool Compressor::parsePreface(const std::string& compressed,
                               std::map<std::string,std::string>* glossary,
                               std::string* body) {
    auto gstart = compressed.find("<icmg-glossary");
    if (gstart == std::string::npos) return false;
    auto gend = compressed.find("</icmg-glossary>", gstart);
    if (gend == std::string::npos) return false;
    auto bstart = compressed.find("<icmg-body>", gend);
    if (bstart == std::string::npos) return false;
    bstart += std::string("<icmg-body>").size();
    if (bstart < compressed.size() && compressed[bstart] == '\n') ++bstart;
    auto bend = compressed.find("</icmg-body>", bstart);
    if (bend == std::string::npos) bend = compressed.size();

    if (glossary) {
        glossary->clear();
        // Parse lines between header and </icmg-glossary>
        auto lines_start = compressed.find('\n', gstart);
        if (lines_start == std::string::npos || lines_start > gend) return false;
        std::istringstream is(compressed.substr(lines_start + 1, gend - lines_start - 1));
        std::string line;
        while (std::getline(is, line)) {
            if (line.empty()) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            (*glossary)[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    if (body) {
        *body = compressed.substr(bstart, bend - bstart);
    }
    return true;
}

} // namespace icmg::compress
