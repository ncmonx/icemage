// Phase 53 T3: testable HTML reducer extracted from FetchCommand.
#pragma once
#include <string>

namespace icmg::core {

// Strips chrome (script, style, nav, aside, footer, header), prefers <main>
// or <article> body, decodes common HTML entities, collapses whitespace,
// caps length. Pure function, regex-based, no DOM dep.
std::string reduceHtml(const std::string& body, size_t cap = 8192);

} // namespace icmg::core
