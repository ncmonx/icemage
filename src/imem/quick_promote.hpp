// 2026-08-25 brain v2.22 #3: quick-note promotion by heat (MemoryOS FIFO-
// promotion insight). `store --quick` captures notes under quick:<epoch> with
// no real topic; the ones the agent keeps recalling (high frequency) have
// PROVEN their value and deserve a permanent, searchable topic. Pure decision
// helpers -- the cmd layer applies the re-topic.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include "memory_node.hpp"

namespace icmg::imem {

struct QuickPromotion {
    int64_t     id        = 0;
    int         frequency = 0;
    std::string from_topic;      // quick:<epoch>
    std::string to_topic;        // suggested permanent topic
};

// A node is a quick capture iff its topic uses the quick:<epoch> convention.
inline bool isQuickTopic(const std::string& topic) {
    return topic.rfind("quick:", 0) == 0;
}

// Deterministic topic suggestion: "hot:" + up to 3 keyword tokens joined by
// '-' (lowercased, alnum+_ only). Falls back to first content words when the
// node has no keywords. Empty when nothing usable (caller skips those).
inline std::string suggestPromotedTopic(const MemoryNode& n, int max_tokens = 3) {
    auto sanitize = [](std::string t) {
        std::string out;
        for (char c : t) {
            unsigned char u = (unsigned char)c;
            if (std::isalnum(u) || c == '_') out += (char)std::tolower(u);
        }
        return out;
    };
    std::vector<std::string> toks;
    auto harvest = [&](const std::string& src, char sep) {
        std::string cur;
        for (char c : src) {
            if (c == sep || c == ' ') {
                std::string s = sanitize(cur);
                if (s.size() >= 3 && (int)toks.size() < max_tokens) toks.push_back(s);
                cur.clear();
            } else cur += c;
        }
        std::string s = sanitize(cur);
        if (s.size() >= 3 && (int)toks.size() < max_tokens) toks.push_back(s);
    };
    harvest(n.keywords, ',');
    if (toks.empty()) harvest(n.content.substr(0, 96), ' ');
    if (toks.empty()) return "";
    std::string topic = "hot:";
    for (size_t i = 0; i < toks.size(); ++i) {
        if (i) topic += "-";
        topic += toks[i];
    }
    return topic;
}

// Quick nodes recalled at least min_freq times, hottest first, capped.
// Skips deleted/invalidated nodes and nodes with no usable topic suggestion.
inline std::vector<QuickPromotion> findQuickPromotions(
        const std::vector<MemoryNode>& nodes,
        int min_freq = 3,
        int max_out  = 25) {
    std::vector<QuickPromotion> out;
    for (const auto& n : nodes) {
        if (!isQuickTopic(n.topic)) continue;
        if (n.deleted_at > 0 || n.invalidated_at > 0) continue;
        if (n.frequency < min_freq) continue;
        std::string to = suggestPromotedTopic(n);
        if (to.empty()) continue;
        out.push_back({n.id, n.frequency, n.topic, to});
    }
    std::sort(out.begin(), out.end(), [](const QuickPromotion& a, const QuickPromotion& b) {
        if (a.frequency != b.frequency) return a.frequency > b.frequency;
        return a.id < b.id;
    });
    if ((int)out.size() > max_out) out.resize(max_out);
    return out;
}

} // namespace icmg::imem
