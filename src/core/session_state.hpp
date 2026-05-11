#pragma once
#include <string>
#include <unordered_set>
#include <cstdint>

namespace icmg::core {

// Lightweight cross-invocation session key store.
// Persists to ~/.icmg/session-seen.json with 2-hour TTL.
// Enables dedup across separate process calls in the same AI session.
class SessionState {
public:
    static SessionState& instance();
    bool hasSeen(const std::string& key) const;
    void markSeen(const std::string& key);
    void clear();
    size_t size() const { return seen_.size(); }

private:
    SessionState();
    void load();
    void save();
    std::unordered_set<std::string> seen_;
    std::string path_;
    int64_t created_at_ = 0;
    static constexpr int64_t TTL_SECS = 7200; // 2h
};

} // namespace icmg::core
