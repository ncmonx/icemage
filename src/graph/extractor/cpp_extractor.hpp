#pragma once
#include "base_extractor.hpp"

namespace icmg::graph {

class CppExtractor : public BaseExtractor {
public:
    ExtractResult extract(const std::string& path,
                          const std::string& content) override;
    std::vector<std::string> extensions() const override {
        return {".cpp", ".cxx", ".cc", ".c", ".hpp", ".hxx", ".h"};
    }
};

} // namespace icmg::graph
