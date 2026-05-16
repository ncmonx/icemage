#pragma once
#include <string>

namespace icmg::core {

class Db;  // fwd

enum class SymbolKind { UNKNOWN, FILE, SYMBOL, SKILL };

struct SymbolMention {
    SymbolKind  kind   = SymbolKind::UNKNOWN;
    std::string value;   // resolved path / symbol name / node_key
    double      score  = 0.0;  // 0-1 confidence
};

// Strip leading '@'. Lookup priority: SKILL > SYMBOL > FILE.
// SYMBOL lookup uses graph_nodes.symbol_name (Phase 18 two-tier graph).
// FILE lookup matches graph_nodes.path basename (case-insensitive on Windows).
// SKILL lookup matches context_nodes.node_key starting with "skills/".
SymbolMention resolveSymbolMention(const std::string& token, Db& db);

} // namespace icmg::core
