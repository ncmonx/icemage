#pragma once
#include "base_extractor.hpp"

namespace icmg::graph {

class ShellExtractor : public BaseExtractor {
public:
    ExtractResult extract(const std::string& path,
                          const std::string& content) override;
    std::vector<std::string> extensions() const override {
        return {".sh", ".bash", ".zsh", ".ps1", ".psm1"};
    }
};

} // namespace icmg::graph
