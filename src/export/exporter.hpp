#pragma once
#include "../core/db.hpp"
#include <string>
#include <ostream>

namespace icmg::exporter {

// Streaming exporter (A6): writes JSON directly to ostream, one row at a time.
// Avoids loading entire DB into memory.
class Exporter {
public:
    explicit Exporter(core::Db& db);

    // Stream JSON export to ostream.
    // type: "memory"|"graph"|"abbreviations"|"sp"|"rules"|"" (all)
    void exportTo(std::ostream& out, const std::string& type = "") const;

    // Convenience: return as string (for small datasets / testing).
    std::string toJson(const std::string& type = "") const;

private:
    core::Db& db_;

    void writeMemoryNodes(std::ostream& out) const;
    void writeGraphNodes(std::ostream& out) const;
    void writeGraphEdges(std::ostream& out) const;
    void writeAbbreviations(std::ostream& out) const;
    void writeStoredProcs(std::ostream& out) const;
    void writeRules(std::ostream& out) const;

    static std::string esc(const std::string& s);
    static std::string jsonStr(const std::string& s);
};

} // namespace icmg::exporter
