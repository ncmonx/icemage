// v1.58 F5: command-bigram predictor for predictive prefetch.
//
// Records (prev-cmd -> next-cmd) transition counts and predicts the
// most-likely next command. The dispatcher records each transition and,
// when ICMG_PREFETCH=1, pre-warms the predicted command's context in a
// background thread.
//
// In-memory + serialize/deserialize to a compact text blob (persisted to
// the global DB by the caller). No external deps.

#pragma once

#include <string>
#include <unordered_map>

namespace icmg::core {

class CmdBigram {
public:
    // Record a prev -> next transition.
    void record(const std::string& prev, const std::string& next);

    // Most-likely next command for prev (highest count). Empty if unknown.
    std::string predictNext(const std::string& prev) const;

    // P(next | prev) = count(prev,next) / total(prev). 0 if unseen.
    double confidence(const std::string& prev, const std::string& next) const;

    // Compact text blob: one "prev\tnext\tcount" line per transition.
    std::string serialize() const;
    void deserialize(const std::string& blob);

    std::size_t size() const { return table_.size(); }

private:
    // prev -> (next -> count)
    std::unordered_map<std::string, std::unordered_map<std::string, int>> table_;
};

}  // namespace icmg::core
