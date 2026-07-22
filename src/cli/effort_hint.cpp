#include "effort_hint.hpp"
#include <sstream>

namespace icmg::cli {

const char* effortLabel(EffortLevel lvl) {
    switch (lvl) {
        case EffortLevel::Low:    return "low";
        case EffortLevel::Medium: return "medium";
        case EffortLevel::High:   return "high";
    }
    return "medium";
}

static EffortLevel bumpUp(EffortLevel l) {
    if (l == EffortLevel::Low)    return EffortLevel::Medium;
    if (l == EffortLevel::Medium) return EffortLevel::High;
    return EffortLevel::High;
}

static int budgetFor(EffortLevel l) {
    switch (l) {
        case EffortLevel::Low:    return 2000;
        case EffortLevel::Medium: return 8000;
        case EffortLevel::High:   return 16000;
    }
    return 8000;
}

EffortHint recommendEffort(Intent intent, int fanOut) {
    EffortHint h;
    EffortLevel base;
    std::string why;
    switch (intent) {
        case Intent::Simple:  base = EffortLevel::Low;    why = "routine task"; break;
        case Intent::Complex: base = EffortLevel::High;   why = "complex task"; break;
        default:              base = EffortLevel::Medium; why = "unclassified task"; break;
    }
    EffortLevel lvl = base;
    if (fanOut >= 25) {
        lvl = EffortLevel::High;
        why += ", wide blast radius (" + std::to_string(fanOut) + " touched)";
    } else if (fanOut >= 8) {
        lvl = bumpUp(base);
        why += ", multi-file scope (" + std::to_string(fanOut) + " touched)";
    }
    h.level = lvl;
    h.budget_tokens = budgetFor(lvl);
    h.rationale = why;
    return h;
}

std::string applyEffortDirective(const std::string& text, const EffortHint& h) {
    // Idempotent: don't double-wrap.
    if (text.find("<icmg-effort") != std::string::npos) return text;
    std::ostringstream os;
    os << "<icmg-effort level=\"" << effortLabel(h.level)
       << "\" budget=\"" << h.budget_tokens << "\">\n"
       << "Suggested extended-thinking budget: ~" << h.budget_tokens
       << " tokens (" << h.rationale << "). Advisory; scale to your model.\n"
       << "</icmg-effort>\n"
       << text;
    return os.str();
}

int estimateFanOut(const std::string& packBlob) {
    // Count symbol markers ("### ") and file bullets in a Files section.
    int count = 0;
    std::istringstream in(packBlob);
    std::string line;
    while (std::getline(in, line)) {
        // strip leading spaces
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        const std::string t = line.substr(s);
        if (t.rfind("### ", 0) == 0) ++count;          // a symbol entry
    }
    return count;
}

} // namespace icmg::cli
