#pragma once
#include <string>

namespace icmg {
namespace cli {

// Build the `context_nodes.content` payload stored for a skill.
//
// Layout: the lead `description` line (kept first so `skill list` / `manifest`
// stay clean), a blank-line separator, then the FULL skill body.
//
// History: indexing previously stored `description + first-500-chars`, so
// reading a skill via `icmg context skill-<name>` (which returns
// context_nodes.content) appeared truncated. Skills must be shown in full --
// no character cap.
inline std::string buildSkillNodeContent(const std::string& description,
                                         const std::string& fullContent) {
    if (description.empty()) return fullContent;
    if (fullContent.empty()) return description;
    return description + "\n\n" + fullContent;
}

}  // namespace cli
}  // namespace icmg
