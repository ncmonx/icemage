#include "../../core/hook_bus.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../abbr_store.hpp"
#include <filesystem>
#include <string>
#include <regex>

namespace icmg::abbreviation {

// ---------------------------------------------------------------------------
// Pattern detection: extract short=full / short:full / (short) full
// from stored content and auto-learn abbreviations.
// ---------------------------------------------------------------------------

struct DetectedAbbr {
    std::string short_form;
    std::string full_form;
};

static std::vector<DetectedAbbr> detectPatterns(const std::string& content) {
    std::vector<DetectedAbbr> out;

    // Pattern 1: word=multi word form   e.g. "bkm=bukti kas masuk"
    // Pattern 2: word:multi word form   e.g. "bkk:bukti kas keluar"
    // Pattern 3: (word) multi word form e.g. "(ju) jurnal umum"
    static const std::regex re1(R"(\b([a-zA-Z][a-zA-Z0-9]{0,9})=([A-Za-z][A-Za-z0-9 ]{2,50})\b)");
    static const std::regex re2(R"(\b([a-zA-Z][a-zA-Z0-9]{0,9}):([A-Za-z][A-Za-z0-9 ]{2,50})\b)");
    static const std::regex re3(R"(\(([a-zA-Z][a-zA-Z0-9]{0,9})\)\s+([A-Za-z][A-Za-z0-9 ]{2,50}))");

    auto process = [&](const std::string& s, const std::string& f) {
        // Filter: full_form must be longer than short_form
        std::string ff = f;
        // trim trailing whitespace
        while (!ff.empty() && ff.back() == ' ') ff.pop_back();
        if (ff.size() > s.size()) out.push_back({s, ff});
    };

    std::sregex_iterator it1(content.begin(), content.end(), re1);
    std::sregex_iterator end;
    for (; it1 != end; ++it1) process((*it1)[1], (*it1)[2]);

    std::sregex_iterator it2(content.begin(), content.end(), re2);
    for (; it2 != end; ++it2) process((*it2)[1], (*it2)[2]);

    std::sregex_iterator it3(content.begin(), content.end(), re3);
    for (; it3 != end; ++it3) process((*it3)[1], (*it3)[2]);

    return out;
}

// ---------------------------------------------------------------------------
// PRE_STORE hook (priority 10): auto-detect and learn abbreviations
// ---------------------------------------------------------------------------

static void autoLearnFromStore(core::HookContext& ctx) {
    try {
        std::string content = ctx.get<std::string>("content", "");
        if (content.empty()) return;

        auto detected = detectPatterns(content);
        if (detected.empty()) return;

        auto& cfg   = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);
        AbbrStore store(db);

        for (auto& d : detected) {
            Abbreviation a;
            a.short_form = d.short_form;
            a.full_form  = d.full_form;
            a.domain     = "";
            try {
                store.learn(a, /*update=*/false);
            } catch (...) {
                // Ignore — duplicate or any error
            }
        }
    } catch (...) {
        // Silent — must not break store operation
    }
}

ICMG_REGISTER_HOOK(core::HookEvent::PRE_STORE, autoLearnFromStore, 10);

// ---------------------------------------------------------------------------
// PRE_RECALL hook (priority 10): expand abbreviations in the recall query
// ---------------------------------------------------------------------------

static void expandRecallQuery(core::HookContext& ctx) {
    try {
        std::string query = ctx.get<std::string>("query", "");
        if (query.empty()) return;

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);
        AbbrStore store(db);

        std::string cwd;
        try { cwd = std::filesystem::current_path().string(); } catch (...) {}
        for (char& c : cwd) if (c == '\\') c = '/';

        std::string expanded = store.expand(query, cwd);
        if (expanded != query) {
            ctx.set<std::string>("query", expanded);
        }
    } catch (...) {
        // Silent
    }
}

ICMG_REGISTER_HOOK(core::HookEvent::PRE_RECALL, expandRecallQuery, 10);

} // namespace icmg::abbreviation
