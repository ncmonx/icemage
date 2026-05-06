#include "rtk.hpp"
#include "runner.hpp"
#include "filters/filter_utils.hpp"
#include "../core/registry.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace icmg::rtk {

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

RTK::RTK(core::Db& db) : db_(db) {}

BaseFilter* RTK::getFilter(CmdType type) const {
    static const std::unordered_map<CmdType, std::string> type2key = {
        {CmdType::GitLog,         "git"},
        {CmdType::Build,          "build"},
        {CmdType::Test,           "test"},
        {CmdType::Search,         "search"},
        {CmdType::Docker,         "build"},  // reuse build filter
        {CmdType::PackageManager, "npm"},
        {CmdType::Default,        "default"},
    };
    auto& reg = core::Registry<icmg::BaseFilter>::instance();
    auto it = type2key.find(type);
    std::string key = (it != type2key.end()) ? it->second : "default";
    if (reg.has(key)) {
        // Filters are stateless — safe to return raw pointer from static storage
        static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
        if (cache.find(key) == cache.end())
            cache[key] = reg.create(key);
        return cache[key].get();
    }
    return nullptr;
}

int RTK::runFiltered(const std::string& command, bool raw, bool json,
                     bool dry_run, bool /*stream*/) {
    CmdType type = detector_.detect(command);

    // A7: dry-run mode
    if (dry_run) {
        static const std::string type_names[] = {
            "GitLog","Build","Test","Search","Docker","PackageManager","Default"};
        int idx = (int)type;
        std::string tname = (idx >= 0 && idx < 7) ? type_names[idx] : "Default";
        std::cout << "Command: " << command << "\n"
                  << "Detected type: " << tname << "\n"
                  << "Filter: " << tname << "Filter\n"
                  << "[To execute: icmg run " << command << "]\n";
        return 0;
    }

    // Execute (A1: argv-safe via parseArgv + safeExec)
    auto result = runCommand(command, /*merge_stderr=*/true);

    std::string combined = result.out;
    if (!result.err.empty() && result.out.empty()) combined = result.err;

    FilterResult fr;
    if (raw) {
        fr.output         = combined;
        fr.original_lines = (int)splitLines(combined).size();
        fr.filtered_lines = fr.original_lines;
    } else {
        auto* f = getFilter(type);
        if (f) {
            fr = f->filter(combined, command);
        } else {
            fr.output         = combined;
            fr.original_lines = (int)splitLines(combined).size();
            fr.filtered_lines = fr.original_lines;
        }
    }

    if (json) {
        // Escape output for JSON
        std::string escaped;
        for (char c : fr.output) {
            if      (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else                escaped += c;
        }
        std::cout << "{"
            << "\"command\":\"" << command << "\""
            << ",\"exit_code\":" << result.exit_code
            << ",\"duration_ms\":" << result.duration_ms
            << ",\"original_lines\":" << fr.original_lines
            << ",\"filtered_lines\":" << fr.filtered_lines
            << ",\"was_truncated\":" << (fr.was_truncated ? "true" : "false")
            << ",\"output\":\"" << escaped << "\""
            << "}\n";
    } else {
        std::cout << fr.output;
        if (!raw && fr.was_truncated) {
            std::cout << "[" << fr.filtered_lines << "/" << fr.original_lines
                      << " lines shown]\n";
        }
    }

    recordCommand(command, fr.original_lines, fr.filtered_lines);
    return result.exit_code;
}

void RTK::recordCommand(const std::string& cmd, int orig, int filt) {
    int64_t now = nowEpoch();
    // Upsert: bump frequency + update last_used + accumulate line stats
    db_.run(
        "INSERT INTO commands(command,frequency,last_used,total_original_lines,total_filtered_lines)"
        " VALUES(?,1,?,?,?)"
        " ON CONFLICT(command) DO UPDATE SET"
        " frequency=frequency+1,"
        " last_used=excluded.last_used,"
        " total_original_lines=total_original_lines+excluded.total_original_lines,"
        " total_filtered_lines=total_filtered_lines+excluded.total_filtered_lines",
        {cmd, std::to_string(now), std::to_string(orig), std::to_string(filt)});
}

void RTK::recordManual(const std::string& cmd) {
    recordCommand(cmd, 0, 0);
}

double RTK::computeScore(int freq, int64_t last_used) const {
    int64_t now = nowEpoch();
    double hours = (now - last_used) / 3600.0;
    double recency = std::exp(-0.02 * hours);  // decay faster than memory
    return freq * recency * 100.0;
}

std::vector<CmdRecord> RTK::suggest(const std::string& prefix, int limit) {
    std::vector<CmdRecord> result;
    std::string pat = prefix.empty() ? "%" : prefix + "%";

    db_.query(
        "SELECT id,command,frequency,last_used,"
        "COALESCE(total_original_lines,0),COALESCE(total_filtered_lines,0)"
        " FROM commands WHERE command LIKE ? ORDER BY frequency DESC LIMIT ?",
        {pat, std::to_string(limit * 3)},  // get more, then re-rank by score
        [&](const core::Row& r) {
            CmdRecord rec;
            if (r.size() > 0) try { rec.id = std::stoll(r[0]); } catch (...) {}
            if (r.size() > 1) rec.command = r[1];
            if (r.size() > 2) try { rec.frequency = std::stoi(r[2]); } catch (...) {}
            if (r.size() > 3) try { rec.last_used = std::stoll(r[3]); } catch (...) {}
            if (r.size() > 4) try { rec.total_original_lines = std::stoi(r[4]); } catch (...) {}
            if (r.size() > 5) try { rec.total_filtered_lines = std::stoi(r[5]); } catch (...) {}
            rec.score = computeScore(rec.frequency, rec.last_used);
            result.push_back(rec);
        });

    // Re-sort by score
    std::stable_sort(result.begin(), result.end(),
        [](const CmdRecord& a, const CmdRecord& b) { return a.score > b.score; });
    if ((int)result.size() > limit) result.resize(limit);
    return result;
}

void RTK::printStats() const {
    int total_runs = 0;
    int64_t total_orig = 0, total_filt = 0;

    db_.query("SELECT SUM(frequency), SUM(total_original_lines), SUM(total_filtered_lines) FROM commands",
              {},
              [&](const core::Row& r) {
                  if (r.size() > 0) try { total_runs = std::stoi(r[0]); } catch (...) {}
                  if (r.size() > 1) try { total_orig = std::stoll(r[1]); } catch (...) {}
                  if (r.size() > 2) try { total_filt = std::stoll(r[2]); } catch (...) {}
              });

    double avg_reduction = total_orig > 0
        ? (1.0 - (double)total_filt / total_orig) * 100.0 : 0.0;
    double est_tokens_saved = (total_orig - total_filt) * 4.0 / 1000.0; // ~4 chars/token

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Total runs:     " << total_runs << "\n";
    std::cout << "Avg reduction:  " << avg_reduction << "%\n";
    std::cout << "Est. tokens saved: ~" << (int)(est_tokens_saved) << "K\n";

    // Top savers
    std::cout << "\nTop savers:\n";
    db_.query(
        "SELECT command, frequency,"
        " CASE WHEN total_original_lines>0 THEN"
        "  ROUND((1.0-CAST(total_filtered_lines AS REAL)/total_original_lines)*100)"
        " ELSE 0 END AS reduction"
        " FROM commands WHERE total_original_lines > 0"
        " ORDER BY reduction DESC LIMIT 5",
        {},
        [](const core::Row& r) {
            if (r.size() < 3) return;
            std::cout << "  " << std::left;
            std::cout.width(30); std::cout << r[0];
            std::cout << r[1] << "x  " << r[2] << "% reduction\n";
        });
}

} // namespace icmg::rtk
