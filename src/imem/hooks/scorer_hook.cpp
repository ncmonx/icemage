#include "../../core/hook_bus.hpp"
#include "../../core/registry.hpp"
#include "../scorer.hpp"

namespace icmg::imem {

// POST_STORE hook: invalidate Scorer IDF table whenever corpus changes
static void scorerInvalidate(core::HookContext& /*ctx*/) {
    Scorer::instance().invalidate();
}

ICMG_REGISTER_HOOK(core::HookEvent::POST_STORE, scorerInvalidate, 50);

} // namespace icmg::imem
