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
