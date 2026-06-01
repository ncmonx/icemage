// Phase 67 T2: per-session reference registry.
//
// First emission of a content chunk (memory hit, graph slice, file fragment)
// gets [ICMG-MEM-N] / [ICMG-GR-N] / [ICMG-FILE-N] ID. Subsequent calls
// within the same session can emit `Reuse [ICMG-MEM-N]` instead of full body.
//
// Session ID = hash of CWD + day-bucket of latest .claude transcript mtime;
// stable within a working day per project. Persisted to .icmg/refs-<id>.json.

#pragma once
#include <string>
#include <unordered_map>

namespace icmg::cli {

class RefRegistry {
public:
    explicit RefRegistry(const std::string& project_root);
    ~RefRegistry();

    // Returns existing ref or assigns a new one. `kind` is "MEM" / "GR" / "FILE" / "FAIL".
    // Returns empty string when registry inactive (e.g. corrupted state file).
    std::string getOrAssign(const std::string& kind, const std::string& content);

    // Has this content been emitted before in this session?
    bool seen(const std::string& kind, const std::string& content) const;

    void flush();  // explicit save; also called by destructor

private:
    std::string state_path_;
    std::unordered_map<std::string, std::string> hash_to_id_;  // "MEM:<hash>" → "[ICMG-MEM-3]"
    std::unordered_map<std::string, int> next_id_;             // "MEM" → 4
    bool dirty_ = false;
    bool active_ = false;

    static std::string contentHash(const std::string& s);
    void load();
};

} // namespace icmg::cli
