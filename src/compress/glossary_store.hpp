// Phase 39 T1: persisted compression glossary + telemetry.
#pragma once
#include "../core/db.hpp"
#include <map>
#include <string>

namespace icmg::compress {

class GlossaryStore {
public:
    explicit GlossaryStore(core::Db& db) : db_(db) {}

    // Save mapping for given content hash. Idempotent on (hash, alias).
    void save(const std::string& content_hash,
              const std::map<std::string,std::string>& glossary);

    // Load mapping for content hash. Empty map if not found.
    std::map<std::string,std::string> load(const std::string& content_hash);

    // Telemetry record.
    void recordTelemetry(const std::string& cmd,
                          int bytes_in, int bytes_out,
                          int tok_in, int tok_out,
                          int elapsed_ms,
                          const std::string& mode);

    // Aggregate sums for last N seconds (for `icmg compress --stats`).
    struct Stats { int calls=0; int64_t tok_in=0; int64_t tok_out=0; int64_t ms=0; };
    Stats summary(int seconds_window = 86400 * 30);

private:
    core::Db& db_;
};

} // namespace icmg::compress
