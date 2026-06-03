// `icmg prompt-capture` — a Stop hook that auto-records the last (prompt, response)
// exchange of the turn into prompt_history, so a future similar prompt can reuse the
// past answer via `icmg profile qa-suggest` -- no manual qa-add. Reads the Stop hook
// payload (JSON with transcript_path) from stdin, extracts the last real exchange,
// and stores it in a per-day session zone in the persona DB. Opt out with
// ICMG_NO_PROMPT_CAPTURE=1. Always exits 0 (a hook must never block the turn).
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/prompt_history.hpp"
#include "../../core/transcript_extract.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/user_identity.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class PromptCaptureCommand : public BaseCommand {
public:
    std::string name() const override { return "prompt-capture"; }
    std::string description() const override {
        return "Stop hook: auto-record the turn's prompt/response into prompt_history";
    }
    void usage() const override {
        std::cout << "Usage: (Stop hook) echo '<payload>' | icmg prompt-capture [--zone Z]\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (std::getenv("ICMG_NO_PROMPT_CAPTURE")) return 0;

        std::string zoneOverride;
        for (size_t i = 0; i < args.size(); ++i)
            if (args[i] == "--zone" && i + 1 < args.size()) zoneOverride = args[++i];

        std::string payload = core::slurpStdinSafe();
        if (payload.empty()) return 0;

        std::string transcriptPath;
        try {
            auto j = nlohmann::json::parse(payload);
            if (j.contains("transcript_path") && j["transcript_path"].is_string())
                transcriptPath = j["transcript_path"].get<std::string>();
        } catch (...) { return 0; }
        if (transcriptPath.empty()) return 0;

        std::ifstream f(transcriptPath, std::ios::binary);
        if (!f) return 0;
        std::ostringstream ss;
        ss << f.rdbuf();
        auto pair = core::extractLastPair(ss.str());
        if (!pair.ok || pair.prompt.size() < 6) return 0;  // skip trivial/empty

        // Cap stored sizes so the persona DB stays lean.
        if (pair.response.size() > 4000) pair.response.resize(4000);
        if (pair.prompt.size() > 1000) pair.prompt.resize(1000);

        const std::string zone = zoneOverride.empty() ? todaySessionZone() : zoneOverride;
        const std::string user = core::currentUser();
        core::Db& db = core::personaDbAvailable() ? core::personaDb()
                                                  : core::GlobalDb::instance().db();
        core::PromptHistory ph(db);
        ph.record(user, zone, pair.prompt, pair.response);
        return 0;
    }

private:
    static std::string todaySessionZone() {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "session-%Y-%m-%d", &tm);
        return std::string(buf);
    }
};

ICMG_REGISTER_COMMAND("prompt-capture", PromptCaptureCommand);

}  // namespace icmg::cli
