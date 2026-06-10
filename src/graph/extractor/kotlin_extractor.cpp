// Kotlin graph extractor (regex). Wraps pure extractKotlin(); registers "kotlin".
// (Previously .kt fell through to the Java extractor; now dedicated.)
#include "kotlin_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class KotlinExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".kt", ".kts"}; }
    ExtractResult extract(const std::string& /*path*/, const std::string& content) override {
        return extractKotlin(content);
    }
};

ICMG_REGISTER_EXTRACTOR("kotlin", KotlinExtractor);

}  // namespace icmg::graph
