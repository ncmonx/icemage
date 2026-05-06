#include "graphml_exporter.hpp"
#include <sstream>

namespace icmg::viz {

GraphmlExporter::GraphmlExporter(core::Db& db) : db_(db) {}

std::string GraphmlExporter::escXml(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '&')  out += "&amp;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else if (c == '"')  out += "&quot;";
        else if (c == '\'') out += "&apos;";
        else                out += c;
    }
    return out;
}

std::string GraphmlExporter::toGraphml() const {
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<graphml xmlns=\"http://graphml.graphdrawing.org/graphml\"\n";
    ss << "         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n";
    ss << "         xsi:schemaLocation=\"http://graphml.graphdrawing.org/graphml"
          " http://graphml.graphdrawing.org/graphml/graphml.xsd\">\n\n";

    // Key declarations
    ss << "  <key id=\"d0\" for=\"node\" attr.name=\"label\"    attr.type=\"string\"/>\n";
    ss << "  <key id=\"d1\" for=\"node\" attr.name=\"lang\"     attr.type=\"string\"/>\n";
    ss << "  <key id=\"d2\" for=\"node\" attr.name=\"path\"     attr.type=\"string\"/>\n";
    ss << "  <key id=\"d3\" for=\"node\" attr.name=\"size\"     attr.type=\"long\"/>\n";
    ss << "  <key id=\"d4\" for=\"edge\" attr.name=\"edge_type\" attr.type=\"string\"/>\n";
    ss << "  <key id=\"d5\" for=\"edge\" attr.name=\"weight\"   attr.type=\"double\"/>\n\n";

    ss << "  <graph id=\"G\" edgedefault=\"directed\">\n";

    // Nodes
    db_.query("SELECT id,path,lang,size_bytes FROM graph_nodes ORDER BY id", {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  std::string id   = r[0];
                  std::string path = r.size()>1 ? escXml(r[1]) : "";
                  std::string lang = r.size()>2 ? escXml(r[2]) : "";
                  std::string sz   = r.size()>3 ? r[3] : "0";

                  std::string label = path;
                  auto pos = label.rfind('/');
                  if (pos == std::string::npos) pos = label.rfind('\\');
                  if (pos != std::string::npos) label = label.substr(pos + 1);

                  ss << "    <node id=\"n" << id << "\">\n";
                  ss << "      <data key=\"d0\">" << escXml(label) << "</data>\n";
                  ss << "      <data key=\"d1\">" << lang  << "</data>\n";
                  ss << "      <data key=\"d2\">" << path  << "</data>\n";
                  ss << "      <data key=\"d3\">" << sz    << "</data>\n";
                  ss << "    </node>\n";
              });

    // Edges
    int edgeId = 0;
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges", {},
              [&](const core::Row& r) {
                  if (r.size() < 3) return;
                  std::string src   = r[0];
                  std::string dst   = r[1];
                  std::string etype = escXml(r[2]);
                  std::string wt    = r.size()>3 ? r[3] : "1.0";

                  ss << "    <edge id=\"e" << edgeId++ << "\" source=\"n" << src
                     << "\" target=\"n" << dst << "\">\n";
                  ss << "      <data key=\"d4\">" << etype << "</data>\n";
                  ss << "      <data key=\"d5\">" << wt    << "</data>\n";
                  ss << "    </edge>\n";
              });

    ss << "  </graph>\n";
    ss << "</graphml>\n";
    return ss.str();
}

} // namespace icmg::viz
