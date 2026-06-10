// Ruby graph extractor (regex). Thin wrapper over the pure extractRuby() so the
// logic stays unit-testable. Registered as language "ruby"; the scanner maps
// .rb/.rake/.gemspec -> "ruby".
#include "ruby_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class RubyExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override {
        return {".rb", ".rake", ".gemspec"};
    }
    ExtractResult extract(const std::string& /*path*/,
                          const std::string& content) override {
        return extractRuby(content);
    }
};

ICMG_REGISTER_EXTRACTOR("ruby", RubyExtractor);

}  // namespace icmg::graph
