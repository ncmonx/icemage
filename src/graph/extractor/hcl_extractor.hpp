#pragma once
#include "base_extractor.hpp"

namespace icmg::graph {

// HCL / Terraform extractor (gap G4 vs graphify). Turns .tf/.hcl into graph
// signal: top-level blocks (resource / data / module / variable / output /
// provider / locals / terraform) -> classes as "<type>.<labels...>", the last
// label -> functions (quick name lookup), and a module block's `source` ->
// imports as "module:<source>" (a dependency edge). Comment-tolerant (# and
// //, block /* */). Regex-based, deterministic, never throws.
class HclExtractor : public BaseExtractor {
public:
    ExtractResult extract(const std::string& path,
                          const std::string& content) override;
    std::vector<std::string> extensions() const override {
        return {".tf", ".hcl", ".tfvars"};
    }
};

} // namespace icmg::graph
