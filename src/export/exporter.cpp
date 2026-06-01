#include "exporter.hpp"
#include <sstream>

namespace icmg::exporter {

Exporter::Exporter(core::Db& db) : db_(db) {}

// Escape JSON string value
std::string Exporter::esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20)  { char buf[8]; snprintf(buf,8,"\\u%04x",c); out += buf; }
        else                out += c;
    }
    return out;
}

std::string Exporter::jsonStr(const std::string& s) {
    return "\"" + esc(s) + "\"";
}

// ---- section writers -------------------------------------------------------

void Exporter::writeMemoryNodes(std::ostream& out) const {
    out << "\"memory_nodes\":[\n";
    bool first = true;
    db_.query("SELECT id,topic,content,COALESCE(keywords,''),importance,frequency,"
              "COALESCE(last_used,0),created_at,COALESCE(expires_at,0),"
              "COALESCE(deleted_at,0) FROM memory_nodes"
              " WHERE deleted_at IS NULL OR deleted_at=0 ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"id\":"          << (r.size()>0 ? r[0] : "0");
                  out << ",\"topic\":"      << jsonStr(r.size()>1 ? r[1] : "");
                  out << ",\"content\":"    << jsonStr(r.size()>2 ? r[2] : "");
                  out << ",\"keywords\":"   << jsonStr(r.size()>3 ? r[3] : "");
                  out << ",\"importance\":" << (r.size()>4 ? r[4] : "1");
                  out << ",\"frequency\":"  << (r.size()>5 ? r[5] : "1");
                  out << ",\"last_used\":"  << (r.size()>6 ? r[6] : "0");
                  out << ",\"created_at\":" << (r.size()>7 ? r[7] : "0");
                  out << ",\"expires_at\":" << (r.size()>8 ? r[8] : "0");
                  out << "}";
              });
    out << "\n]";
}

void Exporter::writeGraphNodes(std::ostream& out) const {
    out << "\"graph_nodes\":[\n";
    bool first = true;
    db_.query("SELECT id,path,lang,context,symbols,size_bytes,file_hash,updated_at,access_count"
              " FROM graph_nodes ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"id\":"           << (r.size()>0 ? r[0] : "0");
                  out << ",\"path\":"        << jsonStr(r.size()>1 ? r[1] : "");
                  out << ",\"lang\":"        << jsonStr(r.size()>2 ? r[2] : "");
                  out << ",\"context\":"     << jsonStr(r.size()>3 ? r[3] : "");
                  out << ",\"symbols\":"     << (r.size()>4 && !r[4].empty() ? r[4] : "{}");
                  out << ",\"size_bytes\":"  << (r.size()>5 ? r[5] : "0");
                  out << ",\"updated_at\":"  << (r.size()>7 ? r[7] : "0");
                  out << ",\"access_count\":" << (r.size()>8 ? r[8] : "0");
                  out << "}";
              });
    out << "\n]";
}

void Exporter::writeGraphEdges(std::ostream& out) const {
    out << "\"graph_edges\":[\n";
    bool first = true;
    // Export with src/dst paths for portability
    db_.query("SELECT e.src,e.dst,e.edge_type,e.weight,"
              "s.path,d.path "
              "FROM graph_edges e "
              "LEFT JOIN graph_nodes s ON s.id=e.src "
              "LEFT JOIN graph_nodes d ON d.id=e.dst "
              "ORDER BY e.src",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"src_id\":"   << (r.size()>0 ? r[0] : "0");
                  out << ",\"dst_id\":"  << (r.size()>1 ? r[1] : "-1");
                  out << ",\"edge_type\":" << jsonStr(r.size()>2 ? r[2] : "imports");
                  out << ",\"weight\":"  << (r.size()>3 ? r[3] : "1.0");
                  out << ",\"src_path\":" << jsonStr(r.size()>4 ? r[4] : "");
                  out << ",\"dst_path\":" << jsonStr(r.size()>5 ? r[5] : "");
                  out << "}";
              });
    out << "\n]";
}

void Exporter::writeAbbreviations(std::ostream& out) const {
    out << "\"abbreviations\":[\n";
    bool first = true;
    db_.query("SELECT id,short_form,full_form,domain,scope_path,frequency,created_at"
              " FROM abbreviations ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"id\":"           << (r.size()>0 ? r[0] : "0");
                  out << ",\"short_form\":"  << jsonStr(r.size()>1 ? r[1] : "");
                  out << ",\"full_form\":"   << jsonStr(r.size()>2 ? r[2] : "");
                  out << ",\"domain\":"      << jsonStr(r.size()>3 ? r[3] : "general");
                  out << ",\"scope_path\":"  << jsonStr(r.size()>4 ? r[4] : "");
                  out << ",\"frequency\":"   << (r.size()>5 ? r[5] : "1");
                  out << ",\"created_at\":"  << (r.size()>6 ? r[6] : "0");
                  out << "}";
              });
    out << "\n]";
}

void Exporter::writeStoredProcs(std::ostream& out) const {
    out << "\"stored_procedures\":[\n";
    bool first = true;
    db_.query("SELECT id,name,db_type,database_name,content,context,"
              "parameters,return_type,tables_used,sp_dependencies,"
              "scope_path,tags,version,created_at,updated_at"
              " FROM stored_procedures ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"id\":"               << (r.size()>0  ? r[0]  : "0");
                  out << ",\"name\":"             << jsonStr(r.size()>1  ? r[1]  : "");
                  out << ",\"db_type\":"          << jsonStr(r.size()>2  ? r[2]  : "");
                  out << ",\"database_name\":"    << jsonStr(r.size()>3  ? r[3]  : "");
                  out << ",\"content\":"          << jsonStr(r.size()>4  ? r[4]  : "");
                  out << ",\"context\":"          << jsonStr(r.size()>5  ? r[5]  : "");
                  out << ",\"parameters\":"       << (r.size()>6  && !r[6].empty()  ? r[6]  : "[]");
                  out << ",\"return_type\":"      << jsonStr(r.size()>7  ? r[7]  : "");
                  out << ",\"tables_used\":"      << (r.size()>8  && !r[8].empty()  ? r[8]  : "[]");
                  out << ",\"sp_dependencies\":"  << (r.size()>9  && !r[9].empty()  ? r[9]  : "[]");
                  out << ",\"scope_path\":"       << jsonStr(r.size()>10 ? r[10] : "");
                  out << ",\"tags\":"             << jsonStr(r.size()>11 ? r[11] : "");
                  out << ",\"version\":"          << (r.size()>12 ? r[12] : "1");
                  out << ",\"created_at\":"       << (r.size()>13 ? r[13] : "0");
                  out << ",\"updated_at\":"       << (r.size()>14 ? r[14] : "0");
                  out << "}";
              });
    out << "\n]";
}

void Exporter::writeRules(std::ostream& out) const {
    out << "\"rules\":[\n";
    bool first = true;
    db_.query("SELECT id,scope_path,rule_type,name,content,priority,active,created_at"
              " FROM rules ORDER BY id",
              {},
              [&](const core::Row& r) {
                  if (!first) out << ",\n";
                  first = false;
                  out << "{";
                  out << "\"id\":"          << (r.size()>0 ? r[0] : "0");
                  out << ",\"scope_path\":" << jsonStr(r.size()>1 ? r[1] : "");
                  out << ",\"rule_type\":"  << jsonStr(r.size()>2 ? r[2] : "custom");
                  out << ",\"name\":"       << jsonStr(r.size()>3 ? r[3] : "");
                  out << ",\"content\":"    << jsonStr(r.size()>4 ? r[4] : "");
                  out << ",\"priority\":"   << (r.size()>5 ? r[5] : "0");
                  out << ",\"active\":"     << (r.size()>6 ? r[6] : "1");
                  out << ",\"created_at\":" << (r.size()>7 ? r[7] : "0");
                  out << "}";
              });
    out << "\n]";
}

// ---- public API ------------------------------------------------------------

void Exporter::exportTo(std::ostream& out, const std::string& type) const {
    out << "{\n";
    out << "\"version\":\"1.0\",\n";

    bool all = type.empty();

    bool wrote = false;
    auto comma = [&]() { if (wrote) out << ",\n"; wrote = true; };

    if (all || type == "memory") {
        comma(); writeMemoryNodes(out);
    }
    if (all || type == "graph") {
        comma(); writeGraphNodes(out);
        comma(); writeGraphEdges(out);
    }
    if (all || type == "abbreviations" || type == "abbr") {
        comma(); writeAbbreviations(out);
    }
    if (all || type == "sp") {
        comma(); writeStoredProcs(out);
    }
    if (all || type == "rules") {
        comma(); writeRules(out);
    }

    out << "\n}\n";
}

std::string Exporter::toJson(const std::string& type) const {
    std::ostringstream ss;
    exportTo(ss, type);
    return ss.str();
}

} // namespace icmg::exporter
