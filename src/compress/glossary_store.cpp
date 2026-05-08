#include "glossary_store.hpp"
#include "../core/db.hpp"
#include <sstream>

namespace icmg::compress {

void GlossaryStore::save(const std::string& content_hash,
                          const std::map<std::string,std::string>& glossary) {
    for (auto& kv : glossary) {
        try {
            db_.run("INSERT OR REPLACE INTO compression_glossary "
                    "(content_hash, alias, original, freq) VALUES (?,?,?,1)",
                    {content_hash, kv.first, kv.second});
        } catch (...) { /* best-effort */ }
    }
}

std::map<std::string,std::string> GlossaryStore::load(const std::string& content_hash) {
    std::map<std::string,std::string> out;
    try {
        db_.query("SELECT alias, original FROM compression_glossary WHERE content_hash = ?",
                  {content_hash},
                  [&](const core::Row& r){
                      if (r.size() >= 2) out[r[0]] = r[1];
                  });
    } catch (...) {}
    return out;
}

void GlossaryStore::recordTelemetry(const std::string& cmd,
                                     int bytes_in, int bytes_out,
                                     int tok_in, int tok_out,
                                     int elapsed_ms,
                                     const std::string& mode) {
    try {
        db_.run("INSERT INTO compression_telemetry "
                "(cmd, bytes_in, bytes_out, tok_in, tok_out, elapsed_ms, mode) "
                "VALUES (?,?,?,?,?,?,?)",
                {cmd, std::to_string(bytes_in), std::to_string(bytes_out),
                 std::to_string(tok_in), std::to_string(tok_out),
                 std::to_string(elapsed_ms), mode});
    } catch (...) {}
}

GlossaryStore::Stats GlossaryStore::summary(int seconds_window) {
    Stats s;
    try {
        int64_t cutoff = (int64_t)::time(nullptr) - seconds_window;
        db_.query("SELECT COUNT(*), COALESCE(SUM(tok_in),0), COALESCE(SUM(tok_out),0), "
                  "       COALESCE(SUM(elapsed_ms),0) "
                  "FROM compression_telemetry WHERE created_at > ?",
                  {std::to_string(cutoff)},
                  [&](const core::Row& r){
                      if (r.size() >= 4) {
                          s.calls   = std::stoi(r[0]);
                          s.tok_in  = std::stoll(r[1]);
                          s.tok_out = std::stoll(r[2]);
                          s.ms      = std::stoll(r[3]);
                      }
                  });
    } catch (...) {}
    return s;
}

} // namespace icmg::compress
