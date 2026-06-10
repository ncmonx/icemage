// Scala graph extractor (regex). Wraps pure extractScala(); registers "scala".
#include "scala_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class ScalaExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".scala", ".sc"}; }
    ExtractResult extract(const std::string& /*path*/, const std::string& content) override {
        return extractScala(content);
    }
};

ICMG_REGISTER_EXTRACTOR("scala", ScalaExtractor);

}  // namespace icmg::graph
