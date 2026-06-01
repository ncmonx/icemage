#pragma once
#include "../core/db.hpp"
#include <string>
#include <vector>

namespace icmg::viz {

class DotExporter {
public:
    explicit DotExporter(core::Db& db);

    // Generate Graphviz DOT format.
    // lang_filter: restrict to these languages (empty = all).
    std::string toDot(const std::vector<std::string>& lang_filter = {}) const;

private:
    core::Db& db_;

    static std::string langFillColor(const std::string& lang);
    static std::string escDot(const std::string& s);
};

} // namespace icmg::viz
