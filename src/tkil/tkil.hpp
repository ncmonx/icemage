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
    int runFiltered(const std::string& command,
                    bool raw    = false,
                    bool json   = false,
                    bool dry_run = false,   // A7
                    bool stream = false);   // A2

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

} // namespace icmg::tkil
