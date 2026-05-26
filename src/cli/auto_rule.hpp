#pragma once
#include <string>
#include <vector>
#include <functional>

namespace icmg::cli {

enum class NLAction {
    NONE, ADD_RULE, REMOVE_RULE, EDIT_RULE,
    ADD_SKILL, REMOVE_SKILL
};

struct NLDetectResult {
    NLAction action = NLAction::NONE;
    std::string target_name;
    std::string content;
};

inline NLDetectResult detectNL(const std::string& line) {
    NLDetectResult r;
    if (line.size() < 8 || line.size() > 500) return r;
    return r;  // always NONE for now
}

} // namespace icmg::cli
