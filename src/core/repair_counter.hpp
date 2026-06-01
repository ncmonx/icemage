// Phase 75: Repair loop guard.
//
// Tracks number of self-repair attempts in last 60 minutes.
// Exceeds threshold (default 3) → repair() returns false → caller halts.
// File: ~/.icmg/repair-counter.json
//   { "events": [{"ts": <unix>, "kind": "<name>"}, ...] }
// Trimmed to last 60min on every read.

#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::core {

class RepairCounter {
public:
    explicit RepairCounter(const std::string& path = "");

    // Returns true if caller should proceed (under threshold).
    // Records the attempt regardless. kind = "mirror-failover" / "backup-restore" / etc.
    bool tryRepair(const std::string& kind, int max_per_hour = 3);

    // Read current count for kind in last 60 min.
    int countLastHour(const std::string& kind);

    // Total count across all kinds in last 60 min.
    int totalLastHour();

    // Force-reset (e.g., user verified state).
    void reset();

    struct Event { int64_t ts; std::string kind; };
    std::vector<Event> recent(int seconds = 3600);

private:
    std::string path_;
    void load(std::vector<Event>& out);
    void save(const std::vector<Event>& events);
};

} // namespace icmg::core
