#pragma once
#include <string>

namespace icmg::tkil {

struct FilterResult {
    std::string output;
    int         original_lines = 0;
    int         filtered_lines = 0;
    bool        was_truncated  = false;
    std::string notes;  // v1.44.0 B1: tee-fallback / parse-fail diagnostic
};

constexpr int MAX_OUTPUT_LINES = 500;  // A9: hard limit

class BaseFilter {
public:
    virtual ~BaseFilter() = default;
    virtual FilterResult filter(const std::string& raw_output,
                                const std::string& command) = 0;
    virtual std::string name() const = 0;
};

} // namespace icmg::tkil
