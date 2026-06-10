#pragma once
// Dense structured-summary instruction for compaction handoff.
//
// When a session is compacted, the most expensive loss is re-discovery: the
// model re-reads files, re-derives decisions, re-asks answered questions. A
// FIXED, information-dense summary format survives compaction far better than
// free-form prose -- so this emits a template the model fills in, ordered
// goal -> done -> state -> next -> keep (the most resumable facts first).
//
// This is icmg's own wording (the structured-self-summary TECHNIQUE is a common
// pattern; no third-party text is reproduced here -- keeps icmg's license clean).
// Pure + header-only so it is unit-testable in isolation.
#include <string>

namespace icmg::cli {

// Build the dense-summary instruction. `turns` / `compactions` are stamped into
// the header for continuity bookkeeping (0 = unknown, still valid).
inline std::string denseSummaryPrompt(int turns = 0, int compactions = 0) {
    std::string s;
    s += "Compact this conversation into a DENSE, structured summary so work can "
         "resume with zero re-discovery. Hard rules: keep it under ~1000 tokens; "
         "every line must carry information; use SPECIFIC names (file paths, "
         "functions, commands, versions, ports, ids); no pleasantries, no "
         "meta-commentary.\n\n";
    s += "## Session Summary  (turns: " + std::to_string(turns)
       + " | compactions: " + std::to_string(compactions) + ")\n\n";
    s += "### Goal\n"
         "One sentence: what the user is trying to accomplish.\n\n";
    s += "### Done\n"
         "- Completed actions, decisions, and concrete outputs -- with exact "
         "names/paths/values.\n"
         "- Errors hit and how each was resolved.\n\n";
    s += "### State\n"
         "The exact current state of the code/task right now; what was in flight "
         "at the moment of compaction.\n\n";
    s += "### Next\n"
         "- What remains, in order; open questions or blockers.\n\n";
    s += "### Keep\n"
         "- Constraints, preferences, and decisions that must NOT be lost; "
         "concrete values (model names, flags, paths, identifiers).\n";
    return s;
}

}  // namespace icmg::cli
