#pragma once
#include "base_extractor.hpp"

namespace icmg::graph {

class MarkdownExtractor : public BaseExtractor {
public:
    ExtractResult extract(const std::string& path,
                          const std::string& content) override;
    std::vector<std::string> extensions() const override {
        return {".md", ".markdown", ".rst", ".txt"};
    }
};

} // namespace icmg::graph
