// src/tkil/output_gist.hpp
// D4: semantic gisting — a one-line "TL;DR" of command output.
//
//   cargo test  -> "12 passed, 3 failed. first fail: user.rs:45"
//   git diff    -> "+42 -15 across 6 file(s)"
//   git log     -> "3 commit(s): feat(auth): refresh token; fix: npe"
//   build       -> "2 error(s), 5 warning(s)"
//
// Heuristic + pure (no LLM, <1ms). Opt-in via `icmg run --gist <cmd>`. The full
// output is still recorded so `--no-tier`/`--last-full` recover it.
//
// Header-only so it is unit-testable without linking icmg_lib.
#pragma once
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::tkil {

enum class GistKind { Test, Diff, Log, Build, Generic };

inline std::string gistLower(const std::string& s) {
    std::string lo;
    lo.reserve(s.size());
    for (char c : s) lo += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lo;
}

// Classify the command line into a gist domain. Command string is authoritative;
// output content is a fallback signal.
inline GistKind gistClassify(const std::string& command, const std::string& output) {
    std::string c = gistLower(command);
    auto has = [&](const std::string& w) { return c.find(w) != std::string::npos; };
    if (has("git diff") || has("git show")) return GistKind::Diff;
    if (has("git log"))                     return GistKind::Log;
    if (has(" test") || has("ctest") || has("pytest") || has("jest") ||
        has("vitest") || has("go test") || c.rfind("test", 0) == 0)
        return GistKind::Test;
    if (has("build") || has("make") || has("cmake") || has("cargo build") ||
        has("gcc") || has("clang") || has("msbuild") || has("ninja"))
        return GistKind::Build;
    // Fallback: sniff the output for a unified diff header.
    if (output.find("\ndiff --git ") != std::string::npos ||
        output.rfind("diff --git ", 0) == 0)
        return GistKind::Diff;
    return GistKind::Generic;
}

// Extract the integer that immediately precedes `word` (skipping spaces), e.g.
// "12 passed" -> 12. Scans ALL occurrences and returns the first one that is
// actually preceded by a number, so "result: FAILED. 3 failed" -> 3 (not the
// bare "FAILED"). Returns -1 if no numbered occurrence exists.
inline int gistIntBeforeWord(const std::string& lower_line, const std::string& word) {
    size_t from = 0;
    while (true) {
        size_t p = lower_line.find(word, from);
        if (p == std::string::npos) return -1;
        size_t e = p;
        while (e > 0 && std::isspace((unsigned char)lower_line[e - 1])) --e;
        size_t s = e;
        while (s > 0 && std::isdigit((unsigned char)lower_line[s - 1])) --s;
        if (s != e) {
            try { return std::stoi(lower_line.substr(s, e - s)); } catch (...) {}
        }
        from = p + word.size();  // no number here — try the next occurrence
    }
}

// --- domain gisters -------------------------------------------------------

inline std::string gistTest(const std::string& output) {
    int passed = -1, failed = -1;
    std::string first_fail;
    std::istringstream is(output);
    std::string ln;
    static const std::regex loc(R"(([A-Za-z0-9_./\\-]+\.[A-Za-z]+):(\d+))");
    while (std::getline(is, ln)) {
        std::string lo = gistLower(ln);
        int p = gistIntBeforeWord(lo, "passed");
        if (p >= 0) passed = p;
        int f = gistIntBeforeWord(lo, "failed");
        if (f >= 0) failed = f;
        if (first_fail.empty() &&
            (lo.find("fail") != std::string::npos ||
             lo.find("error") != std::string::npos ||
             lo.find("panic") != std::string::npos)) {
            std::smatch m;
            if (std::regex_search(ln, m, loc)) first_fail = m[1].str() + ":" + m[2].str();
        }
    }
    std::ostringstream out;
    if (passed < 0 && failed < 0) return "";  // not a recognisable test summary
    out << (passed < 0 ? 0 : passed) << " passed, " << (failed < 0 ? 0 : failed)
        << " failed";
    if (failed > 0 && !first_fail.empty()) out << ". first fail: " << first_fail;
    return out.str();
}

inline std::string gistDiff(const std::string& output) {
    int added = 0, removed = 0, files = 0;
    std::istringstream is(output);
    std::string ln;
    while (std::getline(is, ln)) {
        if (ln.rfind("+++ ", 0) == 0 || ln.rfind("--- ", 0) == 0) continue;  // headers
        if (ln.rfind("diff --git ", 0) == 0) { ++files; continue; }
        if (!ln.empty() && ln[0] == '+') ++added;
        else if (!ln.empty() && ln[0] == '-') ++removed;
    }
    if (added == 0 && removed == 0 && files == 0) return "";
    std::ostringstream out;
    out << "+" << added << " -" << removed << " across " << files << " file(s)";
    return out.str();
}

inline std::string gistLog(const std::string& output) {
    // Count commit lines (git log --oneline: "<hash> <subject>"), keep first 2
    // subjects.
    std::vector<std::string> subjects;
    int commits = 0;
    std::istringstream is(output);
    std::string ln;
    static const std::regex oneline(R"(^([0-9a-f]{7,40})\s+(.*)$)");
    while (std::getline(is, ln)) {
        std::smatch m;
        if (std::regex_search(ln, m, oneline)) {
            ++commits;
            if (subjects.size() < 2) subjects.push_back(m[2].str());
        } else if (ln.rfind("commit ", 0) == 0) {
            ++commits;  // full `git log` format
        }
    }
    if (commits == 0) return "";
    std::ostringstream out;
    out << commits << " commit(s)";
    if (!subjects.empty()) {
        out << ": ";
        for (size_t i = 0; i < subjects.size(); ++i) {
            if (i) out << "; ";
            out << subjects[i];
        }
    }
    return out.str();
}

inline std::string gistBuild(const std::string& output) {
    int errors = 0, warnings = 0;
    std::istringstream is(output);
    std::string ln;
    while (std::getline(is, ln)) {
        std::string lo = gistLower(ln);
        if (lo.find("error") != std::string::npos) ++errors;
        else if (lo.find("warning") != std::string::npos) ++warnings;
    }
    if (errors == 0 && warnings == 0) return "";
    std::ostringstream out;
    out << errors << " error(s), " << warnings << " warning(s)";
    return out.str();
}

// Produce the one-line gist (without trailing newline). Falls back to a generic
// line/byte summary when a domain gister yields nothing.
inline std::string gistOutput(const std::string& command, int exit_code,
                              const std::string& output) {
    GistKind k = gistClassify(command, output);
    std::string g;
    switch (k) {
        case GistKind::Test:  g = gistTest(output);  break;
        case GistKind::Diff:  g = gistDiff(output);  break;
        case GistKind::Log:   g = gistLog(output);   break;
        case GistKind::Build: g = gistBuild(output); break;
        default: break;
    }
    if (g.empty()) {
        // Generic: line count + exit status.
        int lines = 0;
        for (char c : output) if (c == '\n') ++lines;
        std::ostringstream out;
        out << lines << " line(s), exit " << exit_code;
        g = out.str();
    }
    return g;
}

// Render for `icmg run --gist`: the gist line + a recovery hint.
inline std::string renderGist(const std::string& command, int exit_code,
                              const std::string& output) {
    std::ostringstream out;
    out << "[gist] " << gistOutput(command, exit_code, output)
        << "\n[icmg run --no-tier] for full output\n";
    return out.str();
}

} // namespace icmg::tkil
