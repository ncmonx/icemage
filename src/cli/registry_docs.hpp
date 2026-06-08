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

// Build {name, description} docs from the live registry. Optionally exclude one
// command name (e.g. the caller itself) to keep its own entry out of results.
inline std::vector<core::CmdDoc> registryDocs(const std::string& exclude = "") {
    auto& reg = core::Registry<BaseCommand>::instance();
    std::vector<core::CmdDoc> docs;
    for (const auto& k : reg.keys()) {
        if (!exclude.empty() && k == exclude) continue;
        auto cmd = reg.create(k);
        docs.push_back({k, cmd ? cmd->description() : std::string()});
    }
    return docs;
}

}  // namespace icmg::cli
