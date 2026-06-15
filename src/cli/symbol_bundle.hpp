#pragma once
// Feature F (2026-06-15): `icmg context --symbol <Name>` (file-less).
// Cross-file symbol bundle: definition + callers + callees in one shot,
// resolved via the code graph. Pure render helper here so it is unit-testable
// without a GraphStore; bundle_cmd.cpp gathers the data and calls render.
#include <string>
#include <vector>
#include <set>

namespace icmg::cli {

// One referenced symbol/file (a definition, caller, or callee).
struct SymRef {
    std::string name;        // symbol name; empty -> show path
    std::string kind;        // function | method | class | file | ...
    std::string path;
    int         line_start = 0;
    int         line_end   = 0;
};

struct SymbolBundleData {
    SymRef              def;            // the resolved definition (first match)
    std::string         body;          // sliced source of the definition
    std::vector<SymRef> callers;        // who calls def (reverse 'calls' edges)
    std::vector<SymRef> callees;        // what def calls (forward 'calls' edges)
    int                 total_matches = 1;  // >1 = ambiguous; def is first
};

inline std::string symRefLabel(const SymRef& r) {
    return r.name.empty() ? r.path : (r.name + "  (" + r.path + ")");
}

// Collapse name-collision fan-out: name-based call resolution can point one
// call (e.g. .push_back) at every same-named def across vendored deps. Keep
// the first ref per distinct name so a bundle reads as "what it calls", not
// resolver noise. Order-preserving.
inline void dedupRefsByName(std::vector<SymRef>& v) {
    std::set<std::string> seen;
    std::vector<SymRef> out;
    for (auto& r : v) {
        std::string key = r.name.empty() ? r.path : r.name;
        if (seen.insert(key).second) out.push_back(r);
    }
    v.swap(out);
}

// Render a compact, deterministic cross-file symbol bundle.
inline std::string renderSymbolBundle(const SymbolBundleData& d) {
    std::string out;
    out += "Symbol: " + (d.def.name.empty() ? d.def.path : d.def.name);
    if (!d.def.kind.empty()) out += "  [" + d.def.kind + "]";
    out += "\n";
    out += "  def: " + d.def.path;
    if (d.def.line_start > 0)
        out += "  L" + std::to_string(d.def.line_start) + "-" + std::to_string(d.def.line_end);
    out += "\n";
    if (d.total_matches > 1)
        out += "  (note: " + std::to_string(d.total_matches)
             + " definitions matched; showing first)\n";

    out += "\n--- Definition ---\n";
    out += d.body;
    if (!d.body.empty() && d.body.back() != '\n') out += "\n";

    out += "\n--- Callers (" + std::to_string(d.callers.size()) + ") ---\n";
    if (d.callers.empty()) out += "  (none found)\n";
    for (const auto& c : d.callers) out += "  <- " + symRefLabel(c) + "\n";

    out += "\n--- Callees (" + std::to_string(d.callees.size()) + ") ---\n";
    if (d.callees.empty()) out += "  (none found)\n";
    for (const auto& c : d.callees) out += "  -> " + symRefLabel(c) + "\n";

    return out;
}

} // namespace icmg::cli
