// v1.58 F5: command-bigram predictor — implementation.

#include "cmd_bigram.hpp"

#include <sstream>

namespace icmg::core {

void CmdBigram::record(const std::string& prev, const std::string& next) {
    if (prev.empty() || next.empty()) return;
    table_[prev][next] += 1;
}

std::string CmdBigram::predictNext(const std::string& prev) const {
    auto it = table_.find(prev);
    if (it == table_.end()) return {};
    const std::string* best = nullptr;
    int best_count = 0;
    for (const auto& [next, count] : it->second) {
        if (count > best_count) { best_count = count; best = &next; }
    }
    return best ? *best : std::string{};
}

double CmdBigram::confidence(const std::string& prev,
                             const std::string& next) const {
    auto it = table_.find(prev);
    if (it == table_.end()) return 0.0;
    int total = 0, hit = 0;
    for (const auto& [n, c] : it->second) {
        total += c;
        if (n == next) hit = c;
    }
    return total > 0 ? static_cast<double>(hit) / total : 0.0;
}

std::string CmdBigram::serialize() const {
    std::ostringstream os;
    for (const auto& [prev, succ] : table_) {
        for (const auto& [next, count] : succ) {
            os << prev << '\t' << next << '\t' << count << '\n';
        }
    }
    return os.str();
}

void CmdBigram::deserialize(const std::string& blob) {
    table_.clear();
    std::istringstream is(blob);
    std::string line;
    while (std::getline(is, line)) {
        auto t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        auto t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::string prev = line.substr(0, t1);
        std::string next = line.substr(t1 + 1, t2 - t1 - 1);
        int count = 0;
        try { count = std::stoi(line.substr(t2 + 1)); } catch (...) { continue; }
        if (!prev.empty() && !next.empty() && count > 0)
            table_[prev][next] += count;
    }
}

}  // namespace icmg::core
