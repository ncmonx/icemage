#pragma once
// v2.30.0 Model-AI ref flow, lookup half ("Cek Ref").
// Prompt hook order: Model-AI DB refs -> project A memory (existing) -> gate
// refFlowGate -> projects B/C (registry, isShareableProject, time budget).
// Spec: docs/plans/2026-11-13-model-ai-ref-flow.md sec 4.2

#include "ref_flow.hpp"

#include <functional>
#include <string>
#include <vector>

namespace icmg::core { class Db; }

namespace icmg::cli::refflow {

struct RefSection {
    std::string text;        // formatted block for additionalContext ("" = none)
    double best_score = 0;   // best ref coverage (refCoverage), 0 when none
    int hits = 0;
};

// Search the Model-AI DB and format the top refs. `already_injected(id)`
// filters refs injected earlier in the session; injected ids are passed to
// `mark_injected`. Hits are marked used. Never throws.
RefSection modelAiRefSection(core::Db& ref_db, const std::string& prompt, int limit,
                             const std::function<bool(const std::string&)>& already_injected,
                             const std::function<void(const std::string&)>& mark_injected);

struct ProjectEntry {
    std::string name;
    std::string path;
    std::string db_path;
};

// Registry rows from the global DB that may serve as B/C reference sources.
std::vector<ProjectEntry> shareableProjects(core::Db& global_db,
                                            const std::string& home,
                                            const std::string& cwd);

}  // namespace icmg::cli::refflow
