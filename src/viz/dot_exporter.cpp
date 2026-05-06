#include "dot_exporter.hpp"
#include <sstream>
#include <algorithm>
#include <unordered_set>

namespace icmg::viz {

DotExporter::DotExporter(core::Db& db) : db_(db) {}

std::string DotExporter::escDot(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

std::string DotExporter::langFillColor(const std::string& lang) {
    std::string l = lang;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l == "cpp" || l == "c" || l == "h" || l == "hpp") return "#4fc3f7";
    if (l == "python" || l == "py")                        return "#ffd54f";
    if (l == "javascript" || l == "js" || l == "ts")       return "#aed581";
    if (l == "rust" || l == "rs")                          return "#ff8a65";
    if (l == "go")                                          return "#80cbc4";
    if (l == "java")                                        return "#ef9a9a";
    if (l == "sql" || l == "mssql" || l == "mysql" ||
        l == "postgresql")                                  return "#ce93d8";
    if (l == "csharp" || l == "cs")                        return "#b39ddb";
    if (l == "php")                                         return "#90caf9";
    return "#b0bec5";
}

std::string DotExporter::toDot(const std::vector<std::string>& lang_filter) const {
    std::ostringstream ss;
    ss << "digraph icmg {\n";
    ss << "  graph [bgcolor=\"#1a1a2e\" fontname=\"Helvetica\" rankdir=LR];\n";
    ss << "  node [style=filled fontname=\"Helvetica\" fontcolor=white fontsize=10];\n";
    ss << "  edge [color=\"#42a5f5\" fontsize=8];\n\n";

    std::unordered_set<std::string> langSet(lang_filter.begin(), lang_filter.end());
    std::unordered_set<int64_t> includedIds;

    // Nodes
    db_.query("SELECT id,path,lang,size_bytes FROM graph_nodes ORDER BY id", {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  int64_t id = 0;
                  try { id = std::stoll(r[0]); } catch (...) { return; }
                  std::string path = r[1];
                  std::string lang = r.size() > 2 ? r[2] : "";

                  if (!langSet.empty()) {
                      std::string lo = lang;
                      std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                      bool match = false;
                      for (auto& f : lang_filter) {
                          std::string fl = f;
                          std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
                          if (lo == fl) { match = true; break; }
                      }
                      if (!match) return;
                  }

                  includedIds.insert(id);

                  // Label = basename
                  std::string label = path;
                  auto pos = path.find_last_of("/\\");
                  if (pos != std::string::npos) label = path.substr(pos + 1);

                  std::string color = langFillColor(lang);

                  ss << "  \"" << escDot(path) << "\" ["
                     << "label=\"" << escDot(label) << "\" "
                     << "fillcolor=\"" << color << "\" "
                     << "tooltip=\"" << escDot(path) << "\"];\n";
              });

    ss << "\n";

    // Edges
    db_.query("SELECT e.src,e.dst,e.edge_type,n1.path,n2.path "
              "FROM graph_edges e "
              "JOIN graph_nodes n1 ON n1.id=e.src "
              "JOIN graph_nodes n2 ON n2.id=e.dst",
              {},
              [&](const core::Row& r) {
                  if (r.size() < 5) return;
                  int64_t src = 0, dst = 0;
                  try { src = std::stoll(r[0]); dst = std::stoll(r[1]); } catch (...) { return; }

                  if (!includedIds.count(src) || !includedIds.count(dst)) return;

                  std::string etype  = r[2];
                  std::string srcPath = r[3];
                  std::string dstPath = r[4];

                  // Edge color by type
                  std::string color = "#42a5f5";
                  if (etype == "calls")    color = "#66bb6a";
                  if (etype == "inherits") color = "#ffa726";
                  if (etype == "includes") color = "#78909c";
                  if (etype == "sp_calls") color = "#ab47bc";

                  ss << "  \"" << escDot(srcPath) << "\" -> \"" << escDot(dstPath) << "\" "
                     << "[label=\"" << escDot(etype) << "\" color=\"" << color << "\"];\n";
              });

    ss << "}\n";
    return ss.str();
}

} // namespace icmg::viz
