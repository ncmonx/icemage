// 2026-10-29: LongMemEval / LoCoMo loaders -- see bench_dataset.hpp.
#include "bench_dataset.hpp"
#include "../cli/asof_parse.hpp"   // daysFromCivil (shared civil-date helper)

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace icmg::imem {

using nlohmann::json;

namespace {

int64_t toEpoch(int y, int mo, int d, int h, int mi) {
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
        mi < 0 || mi > 59)
        return 0;
    return icmg::cli::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60;
}

int monthFromName(std::string s) {
    static const char* kMon[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                 "jul", "aug", "sep", "oct", "nov", "dec"};
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.size() < 3) return 0;
    for (int i = 0; i < 12; ++i)
        if (s.compare(0, 3, kMon[i]) == 0) return i + 1;
    return 0;
}

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

// "D2:1; D1:2" / "D3:5,D3:6" -> {"D2:1","D1:2"} ...
std::vector<std::string> splitEvidence(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ';' || c == ',' || std::isspace((unsigned char)c)) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void pushUnique(std::vector<std::string>& v, const std::string& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

}  // namespace

int forEachTopLevelObject(const std::string& text,
                          const std::function<void(const std::string&)>& fn) {
    int count = 0, depth = 0;
    bool inStr = false, esc = false;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            if (depth > 0 && --depth == 0) {
                fn(text.substr(start, i - start + 1));
                ++count;
            }
        }
    }
    return count;
}

int64_t parseBenchDate(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    // LongMemEval: "2023/05/20 (Sat) 02:21"
    char dow[16] = {0};
    if (std::sscanf(s.c_str(), "%d/%d/%d (%15[^)]) %d:%d", &y, &mo, &d, dow, &h, &mi) == 6)
        return toEpoch(y, mo, d, h, mi);
    if (std::sscanf(s.c_str(), "%d/%d/%d", &y, &mo, &d) == 3)
        return toEpoch(y, mo, d, 0, 0);
    // LoCoMo: "1:56 pm on 8 May, 2023"
    char ampm[8] = {0}, mon[16] = {0};
    if (std::sscanf(s.c_str(), "%d:%d %7s on %d %15[A-Za-z], %d", &h, &mi, ampm, &d, mon, &y) == 6) {
        std::string ap = ampm;
        for (auto& c : ap) c = (char)std::tolower((unsigned char)c);
        if (h < 1 || h > 12) return 0;
        if (ap == "am") h = (h == 12) ? 0 : h;
        else if (ap == "pm") h = (h == 12) ? 12 : h + 12;
        else return 0;
        return toEpoch(y, monthFromName(mon), d, h, mi);
    }
    return 0;
}

bool parseBenchGranularity(const std::string& name, BenchGranularity& g) {
    if (name == "session") { g = BenchGranularity::Session; return true; }
    if (name == "turn") { g = BenchGranularity::Turn; return true; }
    if (name == "user-session") { g = BenchGranularity::UserSession; return true; }
    return false;
}

BenchCorpus parseLongMemEval(const std::string& instanceJson, BenchGranularity g) {
    const bool perSession = g != BenchGranularity::Turn;
    const bool userOnly = g == BenchGranularity::UserSession;
    BenchCorpus c;
    json j = json::parse(instanceJson);
    const auto& sids = j.value("haystack_session_ids", json::array());
    const auto& dates = j.value("haystack_dates", json::array());
    const auto& sessions = j.value("haystack_sessions", json::array());

    BenchQuestion q;
    q.id = str(j, "question_id");
    q.category = str(j, "question_type");
    q.question = str(j, "question");
    q.abstention = q.id.size() >= 4 && q.id.compare(q.id.size() - 4, 4, "_abs") == 0;

    int64_t maxDate = 0;
    for (size_t s = 0; s < sessions.size(); ++s) {
        std::string sid = s < sids.size() && sids[s].is_string()
                              ? sids[s].get<std::string>()
                              : "session_" + std::to_string(s);
        int64_t date = s < dates.size() && dates[s].is_string()
                           ? parseBenchDate(dates[s].get<std::string>())
                           : 0;
        maxDate = std::max(maxDate, date);
        std::string sessionText;
        const auto& turns = sessions[s];
        for (size_t t = 0; t < turns.size(); ++t) {
            std::string line = str(turns[t], "role") + ": " + str(turns[t], "content");
            if (g == BenchGranularity::Turn) {
                std::string tid = sid + "#" + std::to_string(t);
                c.docs.push_back({tid, line, date});
                if (turns[t].value("has_answer", false)) pushUnique(q.evidence, tid);
            } else if (!userOnly || str(turns[t], "role") == "user") {
                sessionText += line;
                sessionText += '\n';
            }
        }
        if (perSession) c.docs.push_back({sid, sessionText, date});
    }
    if (perSession)
        for (const auto& e : j.value("answer_session_ids", json::array()))
            if (e.is_string()) pushUnique(q.evidence, e.get<std::string>());

    int64_t qd = parseBenchDate(str(j, "question_date"));
    q.as_of = qd > 0 ? qd : maxDate;
    c.questions.push_back(std::move(q));
    return c;
}

BenchCorpus parseLoCoMo(const std::string& sampleJson, BenchGranularity g) {
    BenchCorpus c;
    json j = json::parse(sampleJson);
    const std::string sample = str(j, "sample_id");
    const auto& conv = j.value("conversation", json::object());

    // Sessions are keyed session_<N>; collect N in numeric order.
    std::vector<int> nums;
    for (auto it = conv.begin(); it != conv.end(); ++it) {
        const std::string& k = it.key();
        int n = 0;
        char tail = 0;
        if (std::sscanf(k.c_str(), "session_%d%c", &n, &tail) == 1 && it->is_array())
            nums.push_back(n);
    }
    std::sort(nums.begin(), nums.end());

    int64_t maxDate = 0;
    for (int n : nums) {
        const std::string key = "session_" + std::to_string(n);
        int64_t date = parseBenchDate(str(conv, (key + "_date_time").c_str()));
        maxDate = std::max(maxDate, date);
        std::string sessionText;
        for (const auto& t : conv[key]) {
            std::string line = str(t, "speaker") + ": " + str(t, "text");
            std::string cap = str(t, "blip_caption");
            if (!cap.empty()) line += " [image: " + cap + "]";
            if (g == BenchGranularity::Turn)
                c.docs.push_back({str(t, "dia_id"), line, date});
            else {
                sessionText += line;
                sessionText += '\n';
            }
        }
        if (g == BenchGranularity::Session)
            c.docs.push_back({"S" + std::to_string(n), sessionText, date});
    }

    int qi = 0;
    for (const auto& qa : j.value("qa", json::array())) {
        BenchQuestion q;
        q.id = sample + "#" + std::to_string(qi++);
        int cat = qa.value("category", 0);
        q.category = "locomo-cat" + std::to_string(cat);
        q.question = str(qa, "question");
        q.abstention = (cat == 5);
        for (const auto& e : qa.value("evidence", json::array())) {
            if (!e.is_string()) continue;
            for (const auto& id : splitEvidence(e.get<std::string>())) {
                if (g == BenchGranularity::Turn) { pushUnique(q.evidence, id); continue; }
                int s = 0;
                if (std::sscanf(id.c_str(), "D%d", &s) == 1)
                    pushUnique(q.evidence, "S" + std::to_string(s));
            }
        }
        q.as_of = maxDate;  // LoCoMo QA is asked after the whole conversation
        c.questions.push_back(std::move(q));
    }
    return c;
}

}  // namespace icmg::imem
