// src/tkil/output_nano.hpp
// D1: "nano mode" — symbol-only compression for `icmg run` output.
//
// Collapses noisy build/test/lint output into one dense symbol line per
// diagnostic:
//
//     <file>:<kind>:<code>:<line>
//
// e.g.  src/main.rs:err:E0423:12
//       foo.cpp:warn:C4996:45
//       parser.c:err:-:88          (no error code available)
//
// Opt-in via `icmg run --nano <cmd>`. Pure + header-only so it is unit-testable
// without linking icmg_lib. NEVER on the hot path; the full output is still
// recorded so `--no-tier`/`--last-full` can recover it.
//
// Recognised diagnostic formats:
//   * gcc / clang / rustc:  file:line[:col]: error|warning[[CODE]]: msg
//   * MSVC:                 file(line[,col]) : error|warning CODE: msg
//
// Target savings: 95%+ on repeat build/test commands.
#pragma once
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::tkil {

struct NanoEntry {
    std::string file;   // source path as emitted by the compiler
    std::string kind;   // "err" | "warn"
    std::string code;   // error code (E0423, C2065) or "-" when absent
    std::string line;   // 1-based line number as a string ("-" if unknown)
};

// Normalise a raw compiler severity word to the 3-char kind tag.
inline std::string nanoKind(const std::string& sev) {
    // "error", "fatal error", "fatal", "panic" -> err ; "warning"/"warn" -> warn
    if (sev.find("warn") != std::string::npos) return "warn";
    return "err";
}

// Try to parse a single output line into a NanoEntry. Returns true on match.
inline bool parseNanoLine(const std::string& raw, NanoEntry& out) {
    // gcc / clang / rustc:  path:line[:col]: (error|warning)[ [CODE] ]:
    // The optional [CODE] is rustc-style ("error[E0423]:").
    static const std::regex gcc(
        R"(^\s*(.+?):(\d+):(?:\d+:)?\s*(error|warning|fatal error)(?:\[([A-Za-z0-9_]+)\])?:)",
        std::regex::icase);
    // MSVC:  path(line[,col]) : (error|warning|fatal error) CODE:
    static const std::regex msvc(
        R"(^\s*(.+?)\((\d+)(?:,\d+)?\)\s*:\s*(error|warning|fatal error)\s+([A-Za-z]+\d+))",
        std::regex::icase);

    std::smatch m;
    if (std::regex_search(raw, m, gcc)) {
        out.file = m[1].str();
        out.line = m[2].str();
        out.kind = nanoKind(m[3].str());
        out.code = m[4].matched ? m[4].str() : "-";
        return true;
    }
    if (std::regex_search(raw, m, msvc)) {
        out.file = m[1].str();
        out.line = m[2].str();
        out.kind = nanoKind(m[3].str());
        out.code = m[4].matched ? m[4].str() : "-";
        return true;
    }
    return false;
}

// Compress full command output into symbol-only lines + a summary tail.
// Duplicate symbol lines are collapsed (order-preserving). When no diagnostic
// is recognised, a single informational line is returned so the caller never
// prints an empty result.
inline std::string nanoCompress(const std::string& full_output) {
    std::vector<std::string> seen;   // order-preserving dedup of rendered lines
    int errs = 0, warns = 0, total_lines = 0;

    std::istringstream is(full_output);
    std::string ln;
    while (std::getline(is, ln)) {
        ++total_lines;
        NanoEntry e;
        if (!parseNanoLine(ln, e)) continue;
        std::string rendered = e.file + ":" + e.kind + ":" + e.code + ":" + e.line;
        if (std::find(seen.begin(), seen.end(), rendered) != seen.end()) continue;
        seen.push_back(rendered);
        if (e.kind == "warn") ++warns; else ++errs;
    }

    std::ostringstream out;
    if (seen.empty()) {
        out << "[nano: no diagnostics; " << total_lines << " line(s). "
               "--no-tier for full output]\n";
        return out.str();
    }
    for (const auto& s : seen) out << s << "\n";
    out << "[nano: " << errs << " err, " << warns << " warn from "
        << total_lines << " line(s); --no-tier for full]\n";
    return out.str();
}

} // namespace icmg::tkil
