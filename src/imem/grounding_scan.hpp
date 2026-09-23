// 2026-09-23: grounding scan -- `icmg memory-health --grounding` (IDE-C from
// docs/plans/2026-09-23-ai-agi-landscape-scan.md; inspired by "Grounding Agent
// Memory: Environment-Probing Curation", arXiv 2609.11060).
//
// Insight: memories cite files ("fixed src/cli/foo.cpp"). When the graph no
// longer contains a cited file, the file was renamed/deleted AFTER the memory
// was written -- so the memory is probably stale. We PROBE our own code graph
// (read-only, deterministic, zero-LLM) and FLAG; we never delete.
// Pure functions, no IO/DB -- the cmd layer feeds memories + known basenames.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace icmg::imem {

struct GroundedMem {
    int64_t     id = 0;
    std::string topic;
    std::string content;
};

struct GroundingIssue {
    int64_t                  mem_id = 0;
    std::string              topic;
    std::vector<std::string> missing;   // refs whose basename left the graph
};

namespace grounding_detail {

// Extensions that indicate a real code/script file. Deliberately EXCLUDES
// prose/config (md, json, yml, txt): those are often outside the graph scan,
// and flagging README.md on every memory would drown the signal in noise.
inline bool isCodeExt(const std::string& ext) {
    static const std::set<std::string> k = {
        "cpp", "hpp", "h", "c", "cc", "cxx", "hxx", "hh", "inl",
        "py", "js", "ts", "tsx", "jsx", "cs", "java", "go", "rs", "rb",
        "sh", "ps1", "psm1", "bat", "cmake", "sql", "rc", "proto"};
    return k.count(ext) > 0;
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// C/C++ SYSTEM headers come from compilers/SDKs, never from the project graph.
// "sys/..." and a short list of ubiquitous names cover the live noise (POSIX,
// C std, Windows, CUDA/HIP vendor headers use *_fp16.h-style names).
inline bool isSystemHeader(const std::string& tok_lower) {
    if (tok_lower.rfind("sys/", 0) == 0 || tok_lower.rfind("hip/", 0) == 0 ||
        tok_lower.rfind("cuda", 0) == 0 || tok_lower.rfind("cublas", 0) == 0 ||
        tok_lower.rfind("hipblas/", 0) == 0 || tok_lower.rfind("rccl/", 0) == 0)
        return true;
    static const std::set<std::string> k = {
        "stdio.h", "stdlib.h", "stdint.h", "stddef.h", "stdbool.h", "string.h",
        "unistd.h", "fcntl.h", "signal.h", "errno.h", "math.h", "time.h",
        "assert.h", "limits.h", "ctype.h", "wchar.h", "pthread.h", "dirent.h",
        "windows.h", "winsock2.h", "shlobj.h", "io.h", "process.h", "nccl.h"};
    return k.count(tok_lower) > 0;
}

}  // namespace grounding_detail

// Pull path-like tokens ("src/cli/foo.cpp", "db.hpp", `quoted.ps1`) out of
// freeform memory text. Backslashes normalize to '/'. Skips URLs, globs, and
// tokens whose extension is not a known code extension. Deduped, in order.
inline std::vector<std::string> extractFileRefs(const std::string& content) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    const size_t n = content.size();
    size_t i = 0;
    auto isTokChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
               c == '.' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?';
    };
    while (i < n) {
        if (!isTokChar(content[i])) { ++i; continue; }
        size_t j = i;
        while (j < n && isTokChar(content[j])) ++j;
        std::string tok = content.substr(i, j - i);
        i = j;
        // strip trailing sentence punctuation the tokenizer swallowed
        while (!tok.empty() && (tok.back() == '.' || tok.back() == ':')) tok.pop_back();
        if (tok.size() < 3) continue;
        if (tok.find("://") != std::string::npos) continue;          // URL
        if (tok.find('*') != std::string::npos ||
            tok.find('?') != std::string::npos) continue;            // glob
        std::replace(tok.begin(), tok.end(), '\\', '/');
        // "./x.js" / "../y.js" are module import SPECIFIERS (bundler-resolved),
        // not file citations -- auto-captured import lists would drown the report
        if (tok.rfind("./", 0) == 0 || tok.rfind("../", 0) == 0) continue;
        if (grounding_detail::isSystemHeader(grounding_detail::toLower(tok)))
            continue;                                                // SDK header
        // drive prefix ("C:/...") is fine after normalize; bare "word:word" is not a path
        const size_t colon = tok.find(':');
        if (colon != std::string::npos && !(colon == 1 && tok.size() > 2 && tok[2] == '/'))
            continue;
        const size_t dot = tok.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= tok.size()) continue;
        const std::string ext = grounding_detail::toLower(tok.substr(dot + 1));
        if (!grounding_detail::isCodeExt(ext)) continue;
        // basename stem must contain a letter ("2609.24971" is a paper, not a file)
        const size_t slash = tok.rfind('/');
        const std::string stem =
            tok.substr(slash == std::string::npos ? 0 : slash + 1,
                       dot - (slash == std::string::npos ? 0 : slash + 1));
        bool has_alpha = false;
        for (char c : stem)
            if (std::isalpha(static_cast<unsigned char>(c))) { has_alpha = true; break; }
        if (!has_alpha) continue;
        if (seen.insert(grounding_detail::toLower(tok)).second) out.push_back(tok);
    }
    return out;
}

// Flag memories whose referenced file basenames are absent from the graph.
// `known_basenames` must be lowercased basenames of live graph nodes. Ranked:
// most missing refs first (more broken evidence), then newer id. Capped.
inline std::vector<GroundingIssue> findUngroundedMemories(
    const std::vector<GroundedMem>& mems,
    const std::set<std::string>& known_basenames,
    int max_out = 25) {
    std::vector<GroundingIssue> out;
    for (const auto& m : mems) {
        GroundingIssue issue;
        for (const auto& ref : extractFileRefs(m.content)) {
            const size_t slash = ref.rfind('/');
            const std::string base = grounding_detail::toLower(
                slash == std::string::npos ? ref : ref.substr(slash + 1));
            if (known_basenames.count(base) > 0) continue;
            // TS convention: import "x.js" resolves to source "x.ts"(".tsx")
            const size_t edot = base.rfind('.');
            if (edot != std::string::npos) {
                const std::string ext = base.substr(edot + 1);
                const std::string stem = base.substr(0, edot);
                if ((ext == "js" && (known_basenames.count(stem + ".ts") ||
                                     known_basenames.count(stem + ".tsx"))) ||
                    (ext == "jsx" && known_basenames.count(stem + ".tsx")))
                    continue;
            }
            issue.missing.push_back(ref);
        }
        if (issue.missing.empty()) continue;
        issue.mem_id = m.id;
        issue.topic = m.topic;
        out.push_back(std::move(issue));
    }
    std::sort(out.begin(), out.end(), [](const GroundingIssue& a, const GroundingIssue& b) {
        if (a.missing.size() != b.missing.size()) return a.missing.size() > b.missing.size();
        return a.mem_id > b.mem_id;
    });
    if ((int)out.size() > max_out) out.resize(max_out);
    return out;
}

}  // namespace icmg::imem
