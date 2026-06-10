// Lua graph extractor (regex). Wraps pure extractLua(); registers "lua".
#include "lua_extract.hpp"
#include "../../core/registry.hpp"

namespace icmg::graph {

class LuaExtractor : public BaseExtractor {
public:
    std::vector<std::string> extensions() const override { return {".lua"}; }
    ExtractResult extract(const std::string& /*path*/, const std::string& content) override {
        return extractLua(content);
    }
};

ICMG_REGISTER_EXTRACTOR("lua", LuaExtractor);

}  // namespace icmg::graph
