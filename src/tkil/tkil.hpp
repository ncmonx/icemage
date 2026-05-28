#pragma once
#include "../core/db.hpp"
#include "detector.hpp"
#include "base_filter.hpp"
#include <string>
#include <vector>

namespace icmg::tkil {

struct CmdRecord {
    int64_t     id          = 0;
    std::string command;
    int         frequency   = 0;
    int64_t     last_used   = 0;
    int         total_original_lines = 0;
    int         total_filtered_lines = 0;
    double      score       = 0.0;
};

class Tkil {
public:
    explicit Tkil(core::Db& db);

    // A1: argv-safe run + filter + print + record
    // v1.56 T1: `ultra` chains Stages 2-5 of the Tkil Ultra pipeline on
    // top of the per-cmd BaseFilter (Stage 1). Auto-trigger fires
    // regardless of the flag when output > 5 KB AND dup-ratio > 0.4.
    int runFiltered(const std::string& command,
                    bool raw    = false,
                    bool json   = false,
                    bool dry_run = false,   // A7
                    bool stream = false,    // A2
                    bool ultra  = false);   // v1.56 T1

    // Suggest commands by score (freq × recency)
    std::vector<CmdRecord> suggest(const std::string& prefix = "", int limit = 10);

    // Manual record
    void recordManual(const std::string& cmd);

    // Stats summary (A6)
    void printStats() const;

private:
    core::Db& db_;
    Detector  detector_;

    void recordCommand(const std::string& cmd, int orig_lines, int filt_lines);
    BaseFilter* getFilter(CmdType type) const;
    double computeScore(int freq, int64_t last_used) const;
};

// v1.41.x: forward decl so callers in this namespace resolve cleanly under
// MSVC (was previously declared `extern` inside Tkil::runFiltered which
// MSVC mangled into a global symbol mismatch). Definition lives in
// filters/regex_user_filter.cpp.
std::string applyUserFilters(const std::string& filtered,
                             const std::string& command);

} // namespace icmg::tkil
