// 2026-11-12: see bench_gate_trace.hpp.
#include "bench_gate_trace.hpp"

#include "../core/path_u8.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <set>

namespace icmg::imem {

double queryCoverage(const std::vector<std::string>& docTokens,
                     const std::vector<std::string>& queryTokens) {
    const std::set<std::string> q(queryTokens.begin(), queryTokens.end());
    if (q.empty()) return 0.0;
    const std::set<std::string> d(docTokens.begin(), docTokens.end());
    size_t hit = 0;
    for (const auto& t : q) hit += d.count(t);
    return (double)hit / (double)q.size();
}

std::string gateTraceJson(const GateTrace& t) {
    nlohmann::ordered_json j;
    j["qid"] = t.qid;
    j["cat"] = t.category;
    j["bm_s1"] = t.bmS1;
    j["bm_s2"] = t.bmS2;
    j["fused_s1"] = t.fusedS1;
    j["fused_s2"] = t.fusedS2;
    j["bm_cov"] = t.bmCov;
    j["fused_cov"] = t.fusedCov;
    j["pre"] = t.pre;
    j["post"] = t.post;
    j["gold"] = t.gold;
    return j.dump();
}

std::string gateTracePath() {
    const char* p = std::getenv(kGateTraceEnv);
    return p ? std::string(p) : std::string();
}

void appendGateTrace(const std::string& path, const GateTrace& t) {
    static std::mutex mu;
    if (path.empty()) return;
    const std::string line = gateTraceJson(t) + "\n";
    std::lock_guard<std::mutex> lk(mu);
    std::ofstream f(core::u8ToPath(path), std::ios::app | std::ios::binary);
    f << line;
}

}  // namespace icmg::imem
