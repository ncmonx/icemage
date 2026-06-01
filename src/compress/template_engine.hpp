// v1.27.0 (Phase 2): Template slot-fill engine.
//
// Pure function: apply `<%key%>` substitution against a JSON slot map.
// Reused from GLOSS `<%token%>` syntax for consistency. Unknown slots
// leave the `<%key%>` literal in place (NOT crashed) for graceful degrade.

#pragma once

#include <string>

namespace icmg::compress {

// Apply slot subst on `layout_tree` given JSON-encoded `slots_json` map.
// `slots_json` must be a JSON object {"key":"value", ...}. Non-object →
// returns layout_tree unchanged. Missing keys left as literal `<%key%>`.
// Returns expanded string. Does not throw — JSON parse failure returns
// layout_tree unchanged + sets `out_error` if non-null.
std::string applyTemplate(const std::string& layout_tree,
                           const std::string& slots_json,
                           std::string* out_error = nullptr);

}  // namespace icmg::compress
