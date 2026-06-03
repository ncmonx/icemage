#pragma once
// Named composite flows: one command triggers a deterministic chain of EXISTING
// icmg commands, so related features interlink instead of being invoked one-by-one
// (and forgotten). Pure data + a tiny substitution helper -- no registry/exec
// dependency here, so it is unit-testable; the CLI layer dispatches the steps.
//
// A step is an argv vector. The literal token "{ARG}" in any step is replaced by
// the trailing argument the user passes to the flow (empty if none).
#include <string>
#include <vector>

namespace icmg::core {

struct Flow {
    std::string name;
    std::string desc;
    std::vector<std::vector<std::string>> steps;  // each = {command, arg, ...}
};

// Built-in flows. Kept small + composed only of existing commands.
inline const std::vector<Flow>& builtinFlows() {
    static const std::vector<Flow> flows = {
        {"change-done",
         "After a code change: refresh graph + log the workflow step",
         {{"graph", "update"}, {"wflog", "add", "{ARG}"}}},
        {"sanity",
         "Read-only health sweep: doctor + system health",
         {{"doctor"}, {"health"}}},
        {"refresh",
         "Re-scan the graph and show token savings so far",
         {{"graph", "update"}, {"savings"}}},
    };
    return flows;
}

// Find a flow by exact name, or nullptr if unknown.
inline const Flow* findFlow(const std::string& name) {
    for (const auto& f : builtinFlows())
        if (f.name == name) return &f;
    return nullptr;
}

// True if any step contains the "{ARG}" placeholder, i.e. the flow needs a trailing
// argument. Running such a flow without one would inject an empty token (e.g.
// `wflog add ""` -> a junk empty entry), so the CLI must require the arg.
inline bool flowNeedsArg(const Flow& f) {
    for (const auto& step : f.steps)
        for (const auto& tok : step)
            if (tok == "{ARG}") return true;
    return false;
}

// Return a copy of `steps` with every "{ARG}" token replaced by `arg`.
inline std::vector<std::vector<std::string>>
substituteArg(const std::vector<std::vector<std::string>>& steps, const std::string& arg) {
    std::vector<std::vector<std::string>> out = steps;
    for (auto& step : out)
        for (auto& tok : step)
            if (tok == "{ARG}") tok = arg;
    return out;
}

}  // namespace icmg::core
