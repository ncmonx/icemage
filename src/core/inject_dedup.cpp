// v1.14.0: inject dedup impl. Thread-safe FNV1a hash + atomic set.

#include "inject_dedup.hpp"

#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace icmg::core::inject_dedup {

namespace {
std::mutex g_mu;
std::unordered_set<uint64_t> g_seen;

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}
}  // namespace

bool seenBefore(const std::string& content) {
    if (content.empty()) return false;  // never dedup empties
    uint64_t h = fnv1a(content);
    std::lock_guard<std::mutex> lk(g_mu);
    auto [it, inserted] = g_seen.insert(h);
    return !inserted;  // true = was already present (dedup hit)
}

void resetSession() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_seen.clear();
}

size_t uniqueCount() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_seen.size();
}

}  // namespace icmg::core::inject_dedup
