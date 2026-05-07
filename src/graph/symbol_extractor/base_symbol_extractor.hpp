#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::graph {

// One symbol extracted from a file: class, function, method, sp, etc.
struct Symbol {
    std::string kind;        // class | interface | function | method | sp
    std::string name;        // bare identifier (e.g. "ProcessOrder")
    std::string signature;   // optional — full declaration line
    int         line_start = 0;  // 1-indexed
    int         line_end   = 0;
    std::string body_hash;       // FNV1a hash of body — for staleness
    std::vector<std::string> calls;   // identifiers referenced inside body
    std::vector<std::string> bases;   // for class: extends/implements parents
};

// Symbol extractor interface — register one per language.
// The extractor receives full file content and returns a flat list of Symbols.
class BaseSymbolExtractor {
public:
    virtual ~BaseSymbolExtractor() = default;
    virtual std::vector<Symbol> extractSymbols(const std::string& path,
                                                const std::string& content) = 0;
};

} // namespace icmg::graph
