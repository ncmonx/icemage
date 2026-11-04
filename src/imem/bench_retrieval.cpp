// 2026-10-31: see bench_retrieval.hpp.
#include "bench_retrieval.hpp"

#include "../cli/asof_parse.hpp"
#include "../embed/embedder.hpp"

#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>
#include <sstream>
#include <unordered_map>

namespace icmg::imem {

namespace {

constexpr int64_t kDay = 86400;

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::istringstream is(s);
    for (std::string x; is >> x;) w.push_back(x);
    return w;
}

int64_t civilToEpoch(int y, int m, int d) { return cli::daysFromCivil(y, m, d) * kDay; }

void epochToCivil(int64_t t, int& y, int& m, int& d) {
    int64_t z = (t >= 0 ? t : t - (kDay - 1)) / kDay + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (m <= 2));
}

const char* kMonths[] = {"january", "february", "march", "april", "may", "june", "july",
                         "august", "september", "october", "november", "december"};

int numberWord(const std::string& w) {
    static const std::unordered_map<std::string, int> m = {
        {"a", 1}, {"an", 1}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5},
        {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"couple", 2},
        {"few", 3}, {"several", 4}};
    auto it = m.find(w);
    if (it != m.end()) return it->second;
    if (!w.empty() && std::all_of(w.begin(), w.end(), ::isdigit)) return std::atoi(w.c_str());
    return 0;
}

int unitDays(std::string u) {
    if (!u.empty() && u.back() == 's') u.pop_back();
    if (u == "day") return 1;
    if (u == "week") return 7;
    if (u == "month") return 30;
    if (u == "year") return 365;
    return 0;
}

// Strip punctuation so "May?" / "week," tokenise cleanly.
std::vector<std::string> tokens(const std::string& q) {
    std::string s;
    for (char c : lower(q)) s += (std::isalnum((unsigned char)c) || c == '\'') ? c : ' ';
    return words(s);
}

}  // namespace

std::vector<std::pair<std::string, double>>
rrfFuse(const std::vector<std::vector<std::string>>& lists, double k) {
    std::unordered_map<std::string, double> score;
    std::vector<std::string> order;
    for (const auto& l : lists)
        for (size_t r = 0; r < l.size(); ++r) {
            auto [it, fresh] = score.emplace(l[r], 0.0);
            if (fresh) order.push_back(l[r]);
            it->second += 1.0 / (k + (double)(r + 1));
        }
    std::vector<std::pair<std::string, double>> out;
    out.reserve(order.size());
    for (const auto& id : order) out.emplace_back(id, score[id]);
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

std::vector<std::string> chunkWords(const std::string& text, int maxWords, int overlap) {
    std::vector<std::string> out;
    auto w = words(text);
    if (w.empty() || maxWords <= 0) return out;
    int stride = std::max(1, maxWords - std::max(0, overlap));
    for (size_t start = 0;; start += stride) {
        size_t end = std::min(w.size(), start + (size_t)maxWords);
        std::string c;
        for (size_t i = start; i < end; ++i) {
            if (i > start) c += ' ';
            c += w[i];
        }
        out.push_back(std::move(c));
        if (end >= w.size()) break;
    }
    return out;
}

std::string firstPersonView(const std::string& transcript) {
    std::string out;
    std::istringstream is(transcript);
    const std::string tag = "user: ";
    for (std::string line; std::getline(is, line);)
        if (line.compare(0, tag.size(), tag) == 0) out += line.substr(tag.size()) + "\n";
    return out;
}

TemporalWindow parseTemporalHint(const std::string& question, int64_t asOf) {
    TemporalWindow w;
    if (asOf <= 0) return w;
    auto t = tokens(question);
    auto window = [&](int64_t center, int64_t halfWidth) {
        w.found = true;
        w.lo = center - halfWidth;
        w.hi = std::min(asOf, center + halfWidth);
    };
    for (size_t i = 0; i < t.size(); ++i) {
        const std::string& x = t[i];
        if (x == "yesterday") { window(asOf - kDay, kDay); return w; }
        if (x == "today" || x == "tonight") { window(asOf, kDay); return w; }
        if (x == "last" && i + 1 < t.size()) {
            int ud = unitDays(t[i + 1]);
            if (ud > 0) { window(asOf - ud * kDay, ud * kDay); return w; }
            if (t[i + 1] == "weekend") { window(asOf - 7 * kDay, 4 * kDay); return w; }
        }
        // "<n> <unit>s ago"
        if (i + 2 < t.size() && t[i + 2] == "ago") {
            int n = numberWord(x), ud = unitDays(t[i + 1]);
            if (n > 0 && ud > 0) {
                int64_t back = (int64_t)n * ud * kDay;
                window(asOf - back, std::max<int64_t>(kDay, back / 4));
                return w;
            }
        }
    }
    // "in May" / "during March" / "May 2023": latest occurrence at or before asOf.
    for (size_t i = 0; i < t.size(); ++i)
        for (int m = 0; m < 12; ++m) {
            if (t[i] != kMonths[m]) continue;
            // "may" is also a verb: require a time preposition before it.
            bool prep = i > 0 && (t[i - 1] == "in" || t[i - 1] == "during" ||
                                  t[i - 1] == "of" || t[i - 1] == "since");
            bool year = i + 1 < t.size() && t[i + 1].size() == 4 && numberWord(t[i + 1]) > 1900;
            if (m == 4 && !prep && !year) continue;
            int y, mo, d;
            epochToCivil(asOf, y, mo, d);
            int yy = year ? numberWord(t[i + 1]) : (m + 1 <= mo ? y : y - 1);
            int64_t lo = civilToEpoch(yy, m + 1, 1);
            int64_t hi = (m == 11 ? civilToEpoch(yy + 1, 1, 1) : civilToEpoch(yy, m + 2, 1)) - 1;
            w.found = true;
            w.lo = lo;
            w.hi = std::min(hi, asOf);
            return w;
        }
    return w;
}

std::map<std::string, double>
groupPrior(const std::map<std::string, double>& scores,
           const std::map<std::string, std::string>& groupOf, double beta) {
    std::map<std::string, double> best;
    for (const auto& [id, s] : scores) {
        auto g = groupOf.find(id);
        const std::string& key = g == groupOf.end() ? id : g->second;
        auto [it, fresh] = best.emplace(key, s);
        if (!fresh) it->second = std::max(it->second, s);
    }
    std::map<std::string, double> out;
    for (const auto& [id, s] : scores) {
        auto g = groupOf.find(id);
        const std::string& key = g == groupOf.end() ? id : g->second;
        out[id] = s + beta * best[key];
    }
    return out;
}

std::vector<std::string>
mmrSelect(const std::vector<std::pair<std::string, double>>& cand,
          const std::map<std::string, std::vector<float>>& vecs, double lambda, int k) {
    std::vector<std::string> sel;
    if (cand.empty() || k <= 0) return sel;
    double hi = cand.front().second, lo = cand.back().second;
    for (const auto& c : cand) { hi = std::max(hi, c.second); lo = std::min(lo, c.second); }
    const double span = hi - lo > 1e-12 ? hi - lo : 1.0;
    std::vector<bool> used(cand.size(), false);
    auto vecOf = [&](const std::string& id) -> const std::vector<float>* {
        auto it = vecs.find(id);
        return it == vecs.end() ? nullptr : &it->second;
    };
    while ((int)sel.size() < k) {
        int bestI = -1;
        double bestV = -1e300;
        for (size_t i = 0; i < cand.size(); ++i) {
            if (used[i]) continue;
            double rel = (cand[i].second - lo) / span;
            double red = 0.0;
            if (auto* a = vecOf(cand[i].first))
                for (const auto& s : sel)
                    if (auto* b = vecOf(s)) red = std::max(red, (double)embed::cosine(*a, *b));
            double v = lambda * rel - (1.0 - lambda) * red;
            if (v > bestV) { bestV = v; bestI = (int)i; }
        }
        if (bestI < 0) break;
        used[bestI] = true;
        sel.push_back(cand[bestI].first);
    }
    return sel;
}

std::string benchGroupOf(const std::string& id) {
    auto h = id.find('#');
    if (h != std::string::npos) return id.substr(0, h);
    auto c = id.find(':');
    if (c != std::string::npos && id.size() > 1 && id[0] == 'D' &&
        std::all_of(id.begin() + 1, id.begin() + c, ::isdigit))
        return id.substr(0, c);
    return id;
}

std::string benchDateLabel(int64_t epoch) {
    if (epoch <= 0) return "";
    int y, m, d;
    epochToCivil(epoch, y, m, d);
    char buf[64];
    std::string mon = kMonths[m - 1];
    mon[0] = (char)std::toupper((unsigned char)mon[0]);
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d (%s %d)", y, m, d, mon.c_str(), y);
    return buf;
}

std::vector<std::string>
rerankHead(const std::vector<std::string>& ranked, const std::vector<float>& scores, int n) {
    const size_t head = std::min<size_t>(ranked.size(), n > 0 ? (size_t)n : 0);
    if (head == 0 || scores.size() != head) return ranked;
    std::vector<size_t> idx(head);
    for (size_t i = 0; i < head; ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
                     [&](size_t a, size_t b) { return scores[a] > scores[b]; });
    std::vector<std::string> out;
    out.reserve(ranked.size());
    for (size_t i : idx) out.push_back(ranked[i]);
    out.insert(out.end(), ranked.begin() + head, ranked.end());
    return out;
}

std::vector<size_t> topOverlapChunks(const std::string& query,
                                     const std::vector<std::string>& chunks, int m) {
    std::vector<size_t> idx(chunks.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    if (m <= 0 || chunks.size() <= (size_t)m) return idx;
    std::unordered_set<std::string> q;
    for (auto& t : tokens(query)) q.insert(t);
    std::vector<int> hit(chunks.size(), 0);
    for (size_t i = 0; i < chunks.size(); ++i)
        for (auto& t : tokens(chunks[i])) hit[i] += q.count(t) ? 1 : 0;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return hit[a] > hit[b]; });
    idx.resize(m);
    std::sort(idx.begin(), idx.end());
    return idx;
}

std::vector<float> maxPerOwner(const std::vector<float>& passageScores,
                               const std::vector<size_t>& owner, size_t nDocs) {
    if (passageScores.size() != owner.size()) return {};
    std::vector<float> out(nDocs, -1e30f);
    for (size_t p = 0; p < owner.size(); ++p)
        if (owner[p] < nDocs) out[owner[p]] = std::max(out[owner[p]], passageScores[p]);
    return out;
}

}  // namespace icmg::imem
