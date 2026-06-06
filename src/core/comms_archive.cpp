// 2026-06-06: append-only comms archive (JSONL). See comms_archive.hpp.
#include "comms_archive.hpp"
#include <fstream>

namespace icmg::core {

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') { o += "\\n"; continue; }
        o += c;
    }
    return o;
}

void commsAppend(const std::string& path, const std::string& from,
                 const std::string& to, const std::string& body) {
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    f << "{\"from\":\"" << esc(from) << "\",\"to\":\"" << esc(to)
      << "\",\"body\":\"" << esc(body) << "\"}\n";
}

std::vector<CommsRow> commsRead(const std::string& path) {
    std::vector<CommsRow> out;
    std::ifstream f(path);
    std::string line;
    auto field = [](const std::string& l, const std::string& k) -> std::string {
        auto p = l.find("\"" + k + "\":\"");
        if (p == std::string::npos) return "";
        p += k.size() + 4;
        std::string v;
        for (size_t i = p; i < l.size(); ++i) {
            if (l[i] == '\\') { if (i + 1 < l.size()) { v += (l[i+1] == 'n') ? '\n' : l[i+1]; ++i; } continue; }
            if (l[i] == '"') break;
            v += l[i];
        }
        return v;
    };
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        CommsRow r; r.from = field(line, "from"); r.to = field(line, "to"); r.body = field(line, "body");
        out.push_back(r);
    }
    return out;
}

}
