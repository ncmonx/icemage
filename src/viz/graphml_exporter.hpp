#pragma once
#include "../core/db.hpp"
#include <string>

namespace icmg::viz {

// GraphML format for yEd and other tools
class GraphmlExporter {
public:
    explicit GraphmlExporter(core::Db& db);
    std::string toGraphml() const;

private:
    core::Db& db_;
    static std::string escXml(const std::string& s);
};

} // namespace icmg::viz
