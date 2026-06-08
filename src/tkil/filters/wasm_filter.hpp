#pragma once
// Adapts a registered WASM skill to the Tkil BaseFilter interface (fail-open:
// on any wasm error the raw output passes through unchanged, with a note).
#include "../base_filter.hpp"
#include "../../wasm/wasm_runtime.hpp"
#include <utility>

namespace icmg::tkil {

class WasmFilter : public BaseFilter {
public:
    explicit WasmFilter(icmg::wasm::WasmSkill skill) : skill_(std::move(skill)) {}

    FilterResult filter(const std::string& raw, const std::string& /*command*/) override {
        FilterResult fr;
        fr.output = raw;  // default: passthrough
        std::string out, err;
        if (icmg::wasm::runWasmFilter(skill_, raw, icmg::wasm::WasmLimits{}, out, err)) {
            fr.output = out;
        } else {
            fr.notes = "wasm-filter passthrough: " + err;
        }
        return fr;
    }

    std::string name() const override { return "wasm:" + skill_.name; }

private:
    icmg::wasm::WasmSkill skill_;
};

} // namespace icmg::tkil
