#include "gexf_exporter.hpp"
#include <sstream>
#include <chrono>
#include <ctime>

namespace icmg::viz {

GexfExporter::GexfExporter(core::Db& db) : db_(db) {}

std::string GexfExporter::escXml(const std::string& s) {
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

std::string GexfExporter::toGexf() const {
    // Date string for lastmodifieddate
    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char datebuf[32] = {};
    struct tm* tm_info = gmtime(&now);
    if (tm_info) strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tm_info);

    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<gexf xmlns=\"http://gexf.net/1.3\" version=\"1.3\""
       << " xmlns:viz=\"http://gexf.net/1.3/viz\""
       << " lastmodifieddate=\"" << datebuf << "\">\n";
    ss << "  <meta><creator>icmg</creator><description>Knowledge graph export</description></meta>\n";
    ss << "  <graph defaultedgetype=\"directed\">\n";

    // Attribute declarations
    ss << "    <attributes class=\"node\">\n";
    ss << "      <attribute id=\"0\" title=\"lang\" type=\"string\"/>\n";
    ss << "      <attribute id=\"1\" title=\"size_bytes\" type=\"long\"/>\n";
    ss << "      <attribute id=\"2\" title=\"context\" type=\"string\"/>\n";
    ss << "    </attributes>\n";
    ss << "    <attributes class=\"edge\">\n";
    ss << "      <attribute id=\"0\" title=\"edge_type\" type=\"string\"/>\n";
    ss << "    </attributes>\n";

    // Nodes
    ss << "    <nodes>\n";
    db_.query("SELECT id,path,lang,context,size_bytes FROM graph_nodes ORDER BY id", {},
              [&](const core::Row& r) {
                  if (r.size() < 2) return;
                  std::string id   = r[0];
                  std::string path = r.size()>1 ? escXml(r[1]) : "";
                  std::string lang = r.size()>2 ? escXml(r[2]) : "";
                  std::string ctx  = r.size()>3 ? escXml(r[3].substr(0, 200)) : "";
                  std::string sz   = r.size()>4 ? r[4] : "0";

                  // label = basename
                  std::string label = path;
                  auto pos = label.rfind('/');
                  if (pos == std::string::npos) pos = label.rfind('\\');
                  if (pos != std::string::npos) label = label.substr(pos + 1);

                  ss << "      <node id=\"" << id << "\" label=\"" << label << "\">\n";
                  ss << "        <attvalues>\n";
                  ss << "          <attvalue for=\"0\" value=\"" << lang << "\"/>\n";
                  ss << "          <attvalue for=\"1\" value=\"" << sz   << "\"/>\n";
                  ss << "          <attvalue for=\"2\" value=\"" << ctx  << "\"/>\n";
                  ss << "        </attvalues>\n";
                  ss << "      </node>\n";
              });
    ss << "    </nodes>\n";

    // Edges
    ss << "    <edges>\n";
    int edgeId = 0;
    db_.query("SELECT src,dst,edge_type,weight FROM graph_edges", {},
              [&](const core::Row& r) {
                  if (r.size() < 3) return;
                  std::string src   = r[0];
                  std::string dst   = r[1];
                  std::string etype = escXml(r[2]);
                  std::string wt    = r.size()>3 ? r[3] : "1.0";

                  ss << "      <edge id=\"" << edgeId++ << "\" source=\"" << src
                     << "\" target=\"" << dst << "\" weight=\"" << wt << "\">\n";
                  ss << "        <attvalues><attvalue for=\"0\" value=\"" << etype << "\"/></attvalues>\n";
                  ss << "      </edge>\n";
              });
    ss << "    </edges>\n";
    ss << "  </graph>\n";
    ss << "</gexf>\n";
    return ss.str();
}

} // namespace icmg::viz
