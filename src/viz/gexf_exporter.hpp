#pragma once
#include "../core/db.hpp"
#include <string>

namespace icmg::viz {

// GEXF (Graph Exchange XML Format) for Gephi
class GexfExporter {
public:
    explicit GexfExporter(core::Db& db);
    std::string toGexf() const;

private:
    core::Db& db_;
    static std::string escXml(const std::string& s);
};

} // namespace icmg::viz
