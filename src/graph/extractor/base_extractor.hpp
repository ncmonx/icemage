#pragma once
#include <string>
#include <vector>

namespace icmg::graph {

struct ExtractResult {
    std::string              context;    // first doc comment / description
    std::vector<std::string> imports;
    std::vector<std::string> classes;
    std::vector<std::string> functions;
    std::vector<std::string> tables;    // for SQL files
};

class BaseExtractor {
public:
    virtual ~BaseExtractor() = default;
    virtual ExtractResult extract(const std::string& path,
                                  const std::string& content) = 0;
    virtual std::vector<std::string> extensions() const = 0;
};

} // namespace icmg::graph
