// Dart graph extractor (regex). Wraps pure extractDart(); registers "dart".
#include "dart_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class DartExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".dart"}; }
    ExtractResult extract(const std::string& /*path*/, const std::string& content) override {
        return extractDart(content);
    }
};

ICMG_REGISTER_EXTRACTOR("dart", DartExtractor);

}  // namespace icmg::graph
