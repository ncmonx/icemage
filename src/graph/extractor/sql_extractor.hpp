#pragma once
#include "base_extractor.hpp"

namespace icmg::graph {

// SQL schema extractor (gap G3 vs graphify). Turns a .sql file into graph
// signal: CREATE TABLE names -> tables; CREATE VIEW/FUNCTION/PROCEDURE names
// -> functions; FOREIGN KEY ... REFERENCES / inline REFERENCES -> imports as
// "references:<table>" (a table-to-table edge). Dialect-tolerant (handles
// IF NOT EXISTS, quoted/backticked/bracketed and schema-qualified names,
// line `--` and block comments). Regex-based, deterministic, never throws.
class SqlExtractor : public BaseExtractor {
public:
    ExtractResult extract(const std::string& path,
                          const std::string& content) override;
    std::vector<std::string> extensions() const override { return {".sql", ".ddl"}; }
};

} // namespace icmg::graph
