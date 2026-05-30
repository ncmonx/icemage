#include "recall_cache.hpp"
#include <algorithm>
#include <ctime>
#include <vector>

namespace icmg { namespace core {

void RecallCache::setCap(std::size_t e, std::size_t b) {
    cap_entries_ = e; cap_bytes_ = b; evictToFit();
}

void RecallCache::touch(std::list<Entry>::iterator it, std::int64_t now) {
    it->last_used = now; ++it->hits;
    lru_.splice(lru_.begin(), lru_, it);   // move to front (MRU)
}

std::optional<std::string> RecallCache::getAt(const std::string& key, std::int64_t now) {
    auto m = map_.find(key);
    if (m == map_.end()) { ++agg_.misses; return std::nullopt; }
    auto it = m->second;
    if (ttl_ > 0 && now - it->created > ttl_ && !it->pinned) {
        bytes_ -= it->bytes; lru_.erase(it); map_.erase(m); ++agg_.misses; return std::nullopt;
    }
    ++agg_.hits; touch(it, now); return it->value;
}

void RecallCache::putAt(const std::string& key, const std::string& value, std::int64_t now) {
    auto m = map_.find(key);
    if (m != map_.end()) { bytes_ -= m->second->bytes; lru_.erase(m->second); map_.erase(m); }
    Entry e{key, value, key.size() + value.size(), 0, now, now, false};
    bytes_ += e.bytes;
    lru_.push_front(std::move(e));
    map_[key] = lru_.begin();
    if (sink_) sink_(key, value, key.size() + value.size());
    evictToFit();
}

std::optional<std::string> RecallCache::get(const std::string& k) {
    return getAt(k, (std::int64_t)std::time(nullptr));
}
void RecallCache::put(const std::string& k, const std::string& v) {
    putAt(k, v, (std::int64_t)std::time(nullptr));
}

void RecallCache::evictLRUUnpinned() {
    if (lru_.empty()) return;
    for (auto it = std::prev(lru_.end()); ; --it) {
        if (!it->pinned) {
            bytes_ -= it->bytes; map_.erase(it->key); lru_.erase(it); ++agg_.evictions; return;
        }
        if (it == lru_.begin()) return;   // all pinned
    }
}

void RecallCache::evictToFit() {
    while ((lru_.size() > cap_entries_ || bytes_ > cap_bytes_) && !lru_.empty()) {
        std::size_t before = lru_.size();
        evictLRUUnpinned();
        if (lru_.size() == before) break;   // only pinned remain
    }
}

void RecallCache::flush() { lru_.clear(); map_.clear(); bytes_ = 0; }

void RecallCache::pin(const std::string& key) {
    auto m = map_.find(key); if (m != map_.end()) m->second->pinned = true;
}

void RecallCache::pinHot(std::size_t topN) {
    std::vector<std::list<Entry>::iterator> v;
    for (auto it = lru_.begin(); it != lru_.end(); ++it) { it->pinned = false; v.push_back(it); }
    std::sort(v.begin(), v.end(), [](auto a, auto b){ return a->hits > b->hits; });
    for (std::size_t i = 0; i < topN && i < v.size(); ++i) v[i]->pinned = true;
}

CacheStats RecallCache::stats() const {
    CacheStats s = agg_;
    s.entries = lru_.size(); s.bytes = bytes_;
    s.cap_entries = cap_entries_; s.cap_bytes = cap_bytes_;
    return s;
}

std::size_t governorTargetBytes(std::uint64_t avail_mb, std::size_t cur,
                                std::size_t floor_b, std::size_t ceil_b,
                                std::uint64_t total_mb) {
    if (total_mb == 0) return floor_b;
    double used_frac = 1.0 - (double)avail_mb / (double)total_mb;
    std::size_t base = cur ? cur : floor_b;
    std::size_t target = base;
    if (used_frac >= 0.85)      target = base / 2;          // RAM tight -> shrink
    else if (used_frac <= 0.60) target = base * 2;          // ample -> grow
    if (target < floor_b) target = floor_b;
    if (target > ceil_b)  target = ceil_b;
    return target;
}

void runGovernorOnce(RecallCache& c, std::uint64_t avail_mb, std::uint64_t total_mb) {
    auto s = c.stats();
    std::size_t target = governorTargetBytes(avail_mb, s.bytes, 4u << 20, 64u << 20, total_mb);
    c.pinHot(16);                                                   // protect 16 hottest
    c.setCap(s.cap_entries ? s.cap_entries : 256, target);         // setCap -> evictToFit
}

}} // namespace
