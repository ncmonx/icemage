// Swift graph extractor (regex). Wraps pure extractSwift(); registers "swift".
#include "swift_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class SwiftExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".swift"}; }
    ExtractResult extract(const std::string& /*path*/, const std::string& content) override {
        return extractSwift(content);
    }
};

ICMG_REGISTER_EXTRACTOR("swift", SwiftExtractor);

}  // namespace icmg::graph
