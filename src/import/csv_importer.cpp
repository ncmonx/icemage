#include "base_importer.hpp"
#include "../core/registry.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

// CSV importer: supports abbreviations and memory_nodes.
// Header row required: short,full,domain  OR  topic,content,importance
// --type flag determines which table; if not set, infer from header.

namespace icmg {

class CsvImporter : public BaseImporter {
public:
    std::string name()        const override { return "csv"; }
    std::string description() const override {
        return "Import from CSV (abbreviations or memory_nodes)";
    }

    // Extra option: type hint passed via project_name field (hack to avoid API change)
    // project_name == "abbreviation" | "memory" | "" (auto-detect)

protected:
    ImportStats doImport(const std::string& source,
                          core::Db& target_db,
                          const std::string& project_name) override {
        checkFileSize(source);

        std::ifstream f(source);
        if (!f) throw ImportError("Cannot open: " + source);

        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Read header
        std::string headerLine;
        if (!std::getline(f, headerLine))
            throw ImportError("CSV is empty");

        // Normalize CRLF
        if (!headerLine.empty() && headerLine.back() == '\r')
            headerLine.pop_back();

        std::vector<std::string> headers = splitCsv(headerLine);
        for (auto& h : headers) {
            std::transform(h.begin(), h.end(), h.begin(), ::tolower);
            while (!h.empty() && h.front() == ' ') h.erase(0,1);
            while (!h.empty() && h.back()  == ' ') h.pop_back();
        }

        // Determine type
        std::string type = project_name; // "abbreviation" | "memory" | ""

        if (type.empty()) {
            // Auto-detect
            bool hasShort = std::find(headers.begin(), headers.end(), "short") != headers.end() ||
                            std::find(headers.begin(), headers.end(), "short_form") != headers.end();
            bool hasTopic = std::find(headers.begin(), headers.end(), "topic") != headers.end();
            if (hasShort) type = "abbreviation";
            else if (hasTopic) type = "memory";
            else throw ImportError("Cannot infer CSV type from header: " + headerLine);
        }

        ImportStats stats;

        if (type == "abbreviation" || type == "abbr") {
            importAbbreviations(f, headers, target_db, stats, now);
        } else if (type == "memory") {
            importMemory(f, headers, target_db, stats, now);
        } else {
            throw ImportError("Unknown CSV type: " + type +
                              " (use 'abbreviation' or 'memory')");
        }

        return stats;
    }

private:
    static std::vector<std::string> splitCsv(const std::string& line) {
        std::vector<std::string> fields;
        std::string field;
        bool inQuote = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (inQuote && i + 1 < line.size() && line[i+1] == '"') {
                    field += '"'; ++i;
                } else {
                    inQuote = !inQuote;
                }
            } else if (c == ',' && !inQuote) {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(field);
        return fields;
    }

    static int colIndex(const std::vector<std::string>& headers, const std::string& name) {
        for (int i = 0; i < (int)headers.size(); ++i)
            if (headers[i] == name) return i;
        return -1;
    }

    static std::string getField(const std::vector<std::string>& row,
                                 int idx, const std::string& def = "") {
        if (idx < 0 || idx >= (int)row.size()) return def;
        std::string v = row[idx];
        // Trim
        while (!v.empty() && v.front() == ' ') v.erase(0,1);
        while (!v.empty() && v.back()  == ' ') v.pop_back();
        return v;
    }

    void importAbbreviations(std::ifstream& f,
                               const std::vector<std::string>& headers,
                               core::Db& db, ImportStats& stats, int64_t now) {
        int iShort  = colIndex(headers, "short");
        if (iShort < 0) iShort = colIndex(headers, "short_form");
        int iFull   = colIndex(headers, "full");
        if (iFull  < 0) iFull  = colIndex(headers, "full_form");
        int iDomain = colIndex(headers, "domain");
        int iScope  = colIndex(headers, "scope_path");
        int iFreq   = colIndex(headers, "frequency");

        if (iShort < 0 || iFull < 0)
            throw ImportError("CSV abbreviation requires 'short' and 'full' columns");

        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            auto row    = splitCsv(line);
            std::string sf  = getField(row, iShort);
            std::string ff  = getField(row, iFull);
            std::string dom = getField(row, iDomain, "general");
            std::string sp  = getField(row, iScope);
            std::string frq = getField(row, iFreq, "1");
            int freq = 1;
            try { freq = std::stoi(frq); } catch (...) {}

            if (sf.empty() || ff.empty()) continue;

            try {
                validateString(sf, "short_form");
                validateString(ff, "full_form");
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(e.what());
                continue;
            }

            try {
                db.run("INSERT OR IGNORE INTO abbreviations"
                       "(short_form,full_form,domain,scope_path,frequency,created_at)"
                       " VALUES(?,?,?,?,?,?)",
                       {sf, ff, dom, sp, std::to_string(freq), std::to_string(now)});
                stats.abbreviations++;
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("abbr insert: ") + e.what());
            }
        }
    }

    void importMemory(std::ifstream& f,
                       const std::vector<std::string>& headers,
                       core::Db& db, ImportStats& stats, int64_t now) {
        int iTopic  = colIndex(headers, "topic");
        int iCont   = colIndex(headers, "content");
        int iImp    = colIndex(headers, "importance");
        int iKw     = colIndex(headers, "keywords");
        int iFreq   = colIndex(headers, "frequency");

        if (iTopic < 0 || iCont < 0)
            throw ImportError("CSV memory requires 'topic' and 'content' columns");

        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            auto row       = splitCsv(line);
            std::string topic   = getField(row, iTopic);
            std::string content = getField(row, iCont);
            std::string imp_s   = getField(row, iImp, "1");
            std::string kw      = getField(row, iKw);
            std::string freq_s  = getField(row, iFreq, "1");

            if (topic.empty() || content.empty()) continue;

            int imp = 1;
            if      (imp_s == "low"  || imp_s == "0") imp = 0;
            else if (imp_s == "med"  || imp_s == "1") imp = 1;
            else if (imp_s == "high" || imp_s == "2") imp = 2;
            else if (imp_s == "crit" || imp_s == "3") imp = 3;
            else { try { imp = std::stoi(imp_s); } catch (...) {} }
            imp = std::max(0, std::min(3, imp));

            int freq = 1;
            try { freq = std::stoi(freq_s); } catch (...) {}

            try {
                validateString(topic,   "topic");
                validateString(content, "content");
            } catch (const ValidationError& e) {
                stats.errors++;
                stats.error_messages.push_back(e.what());
                continue;
            }

            try {
                db.run("INSERT OR IGNORE INTO memory_nodes"
                       "(topic,content,keywords,importance,frequency,"
                       " last_used,created_at) VALUES(?,?,?,?,?,?,?)",
                       {topic, content, kw,
                        std::to_string(imp), std::to_string(freq),
                        std::to_string(now), std::to_string(now)});
                stats.memory_nodes++;
            } catch (const std::exception& e) {
                stats.errors++;
                stats.error_messages.push_back(std::string("memory insert: ") + e.what());
            }
        }
    }
};

ICMG_REGISTER_IMPORTER("csv", CsvImporter);

} // namespace icmg
