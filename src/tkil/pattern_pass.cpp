// v1.56 T1 Stage 3: pattern-collapse profiles — dispatcher.

#include "pattern_pass.hpp"
#include "dedup_pass.hpp"  // isAlwaysVerbatim

#include <vector>
#include <mutex>

namespace icmg::tkil {

namespace {

std::vector<PatternProfile>& registry() {
    static std::vector<PatternProfile> r;
    return r;
}

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

void registerPatternProfile(const PatternProfile& p) {
    std::lock_guard<std::mutex> g(registryMutex());
    registry().push_back(p);
}

std::string patternPass(const std::string& input, const std::string& cmdline) {
    if (input.empty()) return input;
    std::lock_guard<std::mutex> g(registryMutex());
    for (const auto& p : registry()) {
        if (p.matches && p.matches(cmdline)) {
            return p.apply(input);
        }
    }
    return input;
}

}  // namespace icmg::tkil
