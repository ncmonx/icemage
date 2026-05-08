// Phase 33 scaffold: tree-sitter AST extractor stub. Real impl in Phase 35.
//
// Compiled only when ICMG_HAS_TREESITTER is defined (CMake -DICMG_USE_TREESITTER=ON
// with libtree-sitter present). Acts as registry-priority extractor when active;
// silently absent otherwise. Existing regex extractors stay as fallback.
//
// To implement Phase 35:
//   1. Vendor per-language grammar .c files (csharp, sql, typescript, python)
//   2. ts_parser_new() per parser instance, set_language(grammar_X)
//   3. Walk tree via TSNode iteration or s-expr query (TSQuery)
//   4. Extract symbol_name, line range, body for each declaration node
//   5. Walk invocation_expression nodes for `calls` field
//   6. Walk base_list nodes for `bases`

#ifdef ICMG_HAS_TREESITTER

#include "base_symbol_extractor.hpp"
#include "../../core/registry.hpp"
#include "../../core/logger.hpp"
#include <vector>
#include <string>

namespace icmg::graph {

class TreeSitterExtractorStub : public BaseSymbolExtractor {
public:
    std::vector<Symbol> extractSymbols(const std::string& /*path*/,
                                        const std::string& /*src*/) override {
        // Phase 35 will replace with real tree-sitter parse.
        core::Logger::instance().warn(
            "tree-sitter extractor stub — Phase 35 not implemented yet. "
            "Returning empty; caller falls back to regex extractor."
        );
        return {};
    }
};

// Register under a scoped key so factory can pick by request, not auto-replace.
// Phase 35 will swap registration to override per-language defaults.
namespace {
struct _TsStubReg {
    _TsStubReg() {
        core::Registry<BaseSymbolExtractor>::instance().reg(
            "ast-stub",
            []() -> std::unique_ptr<BaseSymbolExtractor> {
                return std::make_unique<TreeSitterExtractorStub>();
            });
    }
} _ts_stub_inst;
}

} // namespace icmg::graph

#endif // ICMG_HAS_TREESITTER
