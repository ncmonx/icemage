#pragma once
// Shared helper: snapshot the LIVE command registry as {name, description} docs
// for the command recommender (suggest) and the neighbor map (map). Kept in the
// CLI layer (not command_suggest.hpp) so the pure scorer header stays registry-
// free + unit-testable. One source of truth -> no per-command duplication.
#include "base_command.hpp"
#include "../core/registry.hpp"
#include "../core/command_suggest.hpp"
#include <string>
#include <vector>

namespace icmg::cli {

// Curated synonym keywords for commands whose description words don't match the
// natural phrasing a user/AI would type. Keyed by command name; folded into the
// matching corpus only (the displayed description stays clean). Add an entry here
// when `icmg suggest "<phrasing>"` routes to the wrong command -- one source of
// truth, model-free, unit-tested via rankCommands.
inline std::string synonymKeywords(const std::string& cmd) {
    // Direction matters most for the callers/callees + impact pair (the demoed
    // failure: "who calls X" wrongly hit graph-callees because its desc has "calls").
    if (cmd == "graph-callers")
        return "who calls invokes invoked-by inbound callers caller used-by upstream";
    if (cmd == "graph-callees")
        return "callees outbound downstream fan-out what-this-symbol-uses dependencies-of";
    if (cmd == "graph-reverse-impact")
        return "who breaks what depends on me affected-by reverse blast-radius dependents";
    if (cmd == "graph-transitive-impact")
        return "what does this reach forward what-i-affect downstream transitive";
    if (cmd == "graph-impact")
        return "what files break if i change this impacted affected blast-radius";
    if (cmd == "graph-symbol")
        return "find function class method definition where-is-symbol locate-symbol";
    if (cmd == "graph-path")
        return "shortest path between two files how-are-these-connected route";
    if (cmd == "recall")
        return "remember past decision lookup memory what-did-we-decide history";
    if (cmd == "graph-query")
        return "ask question natural language about codebase what-handles explain subgraph";
    return std::string();
}

// Build {name, description, keywords} docs from the live registry. Optionally
// exclude one command name (e.g. the caller itself) to keep its own entry out.
inline std::vector<core::CmdDoc> registryDocs(const std::string& exclude = "") {
    auto& reg = core::Registry<BaseCommand>::instance();
    std::vector<core::CmdDoc> docs;
    for (const auto& k : reg.keys()) {
        if (!exclude.empty() && k == exclude) continue;
        auto cmd = reg.create(k);
        docs.push_back({k, cmd ? cmd->description() : std::string(), synonymKeywords(k)});
    }
    return docs;
}

}  // namespace icmg::cli
