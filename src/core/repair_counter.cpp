#include "repair_counter.hpp"
#include "path_utils.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace icmg::core {

RepairCounter::RepairCounter(const std::string& path)
    : path_(path)
{
    if (path_.empty()) path_ = icmgGlobalDir() + "/repair-counter.json";
    fs::create_directories(fs::path(path_).parent_path());
}

void RepairCounter::load(std::vector<Event>& out) {
    out.clear();
    if (!fs::exists(path_)) return;
    std::ifstream f(path_);
    std::string buf((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    // Mini parser — expects {"events":[{"ts":N,"kind":"..."}, ...]}
    // Tolerant: any sequence of {"ts":N, "kind":"X"} pairs accepted.
    size_t i = 0;
    while ((i = buf.find("\"ts\"", i)) != std::string::npos) {
        size_t colon = buf.find(':', i);
        if (colon == std::string::npos) break;
        // Skip whitespace; read number.
        size_t p = colon + 1;
        while (p < buf.size() && std::isspace((unsigned char)buf[p])) ++p;
        size_t num_start = p;
        while (p < buf.size() && (std::isdigit((unsigned char)buf[p]) || buf[p] == '-')) ++p;
        if (p == num_start) { ++i; continue; }
        int64_t ts = 0;
        try { ts = std::stoll(buf.substr(num_start, p - num_start)); }
        catch (...) { ++i; continue; }
        // Find "kind":"..."
        size_t kpos = buf.find("\"kind\"", p);
        if (kpos == std::string::npos) { ++i; continue; }
        size_t qstart = buf.find('"', kpos + 6);  // after "kind"
        if (qstart == std::string::npos) { ++i; continue; }
        qstart = buf.find('"', qstart + 1);  // open of value
        if (qstart == std::string::npos) { ++i; continue; }
        size_t qend = buf.find('"', qstart + 1);
        if (qend == std::string::npos) { ++i; continue; }
        out.push_back({ts, buf.substr(qstart + 1, qend - qstart - 1)});
        i = qend + 1;
    }
}

void RepairCounter::save(const std::vector<Event>& events) {
    std::ofstream f(path_);
    f << "{\"events\":[";
    bool first = true;
    for (auto& e : events) {
        if (!first) f << ",";
        first = false;
        f << "{\"ts\":" << e.ts << ",\"kind\":\"" << e.kind << "\"}";
    }
    f << "]}\n";
}

bool RepairCounter::tryRepair(const std::string& kind, int max_per_hour) {
    std::vector<Event> events;
    load(events);
    int64_t now = std::time(nullptr);
    int64_t cutoff = now - 3600;
    // Trim old.
    events.erase(std::remove_if(events.begin(), events.end(),
                                 [&](const Event& e){ return e.ts < cutoff; }),
                 events.end());
    int kind_count = 0;
    for (auto& e : events) if (e.kind == kind) ++kind_count;
    bool allow = kind_count < max_per_hour;
    // Always record attempt (whether allowed or blocked).
    events.push_back({now, kind});
    save(events);
    return allow;
}

int RepairCounter::countLastHour(const std::string& kind) {
    std::vector<Event> events;
    load(events);
    int64_t cutoff = std::time(nullptr) - 3600;
    int n = 0;
    for (auto& e : events) if (e.ts >= cutoff && e.kind == kind) ++n;
    return n;
}

int RepairCounter::totalLastHour() {
    std::vector<Event> events;
    load(events);
    int64_t cutoff = std::time(nullptr) - 3600;
    int n = 0;
    for (auto& e : events) if (e.ts >= cutoff) ++n;
    return n;
}

void RepairCounter::reset() {
    save({});
}

std::vector<RepairCounter::Event> RepairCounter::recent(int seconds) {
    std::vector<Event> events;
    load(events);
    int64_t cutoff = std::time(nullptr) - seconds;
    events.erase(std::remove_if(events.begin(), events.end(),
                                 [&](const Event& e){ return e.ts < cutoff; }),
                 events.end());
    return events;
}

} // namespace icmg::core
