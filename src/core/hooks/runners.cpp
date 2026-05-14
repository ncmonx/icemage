#include "runners.hpp"
#include "../exec_utils.hpp"
#include "../path_utils.hpp"
#include "../config.hpp"
#include "../db.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
  #include <process.h>
  #define ICMG_GETPID() _getpid()
#else
  #include <unistd.h>
  #define ICMG_GETPID() getpid()
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core::hooks {

namespace {

// Write stdin content to a temp file under icmgGlobalDir()/hook-stdin/.
// Returns path; "" on failure. Caller cleans up.
std::string writeStdinTmp(const std::string& event, const std::string& content) {
    fs::path dir = fs::path(icmgGlobalDir()) / "hook-stdin";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path p = dir / (event + "-" + std::to_string(ICMG_GETPID()) + "-"
                      + std::to_string(std::time(nullptr)) + ".json");
    std::ofstream f(p, std::ios::binary);
    if (!f) return "";
    f << content;
    f.close();
    return p.string();
}

void runWithStdin(const std::string& args_line, const std::string& stdin_path) {
    std::string cmd = "icmg " + args_line + " < \"" + stdin_path + "\" 2>/dev/null || true";
    (void)safeExecShell(cmd, false, 30000);
}

void runNoStdin(const std::string& args_line) {
    std::string cmd = "icmg " + args_line + " 2>/dev/null || true";
    (void)safeExecShell(cmd, false, 15000);
}

} // namespace

// ---- Stop ------------------------------------------------------------------

std::string runStopHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_STOP_HOOK")) return "";
    if (stdin_raw.empty()) return "";

    std::string tmp = writeStdinTmp("stop", stdin_raw);
    if (tmp.empty()) return "";

    runWithStdin("distill auto --min-len 100", tmp);
    runNoStdin("fail sync-denials");
    runWithStdin("compliance check-thinking --max-words 80", tmp);
    runNoStdin("tool-budget reset");

    std::error_code ec;
    fs::remove(tmp, ec);
    return "";
}

// ---- PreCompact ------------------------------------------------------------

std::string runPreCompactHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_PRECOMPACT_HOOK")) return "";

    std::string tmp;
    if (!stdin_raw.empty()) tmp = writeStdinTmp("precompact", stdin_raw);

    // Step 1: snapshot via existing Python script.
    fs::path snap = fs::current_path() / ".claude" / "hooks" / "icmg-precompact-snapshot.py";
    if (fs::exists(snap)) {
        std::string cmd = "python3 \"" + snap.string()
                        + "\" 2>/dev/null || python \"" + snap.string()
                        + "\" 2>/dev/null || true";
        (void)safeExecShell(cmd, false, 30000);
    }

    // Step 2: distill session transcript.
    if (!tmp.empty()) {
        try {
            json j = json::parse(stdin_raw);
            std::string transcript = j.value("transcript", std::string(""));
            if (!transcript.empty()) {
                std::string tpath = writeStdinTmp("precompact-transcript", transcript);
                if (!tpath.empty()) {
                    runWithStdin("distill session", tpath);
                    std::error_code ec;
                    fs::remove(tpath, ec);
                }
            }
        } catch (...) {}
    }

    // Step 3: ABSOLUTE RULE + top-5 pinned anchors.
    std::ostringstream rule;
    rule << "ABSOLUTE RULE - icmg FIRST. Before any native Read/Bash/Grep/Glob/"
            "WebFetch, check icmg equivalent.\n";
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        rule << "Pinned decisions:\n";
        int n = 0;
        db.query(
            "SELECT topic, stance FROM decisions WHERE pinned = 1 "
            "AND superseded_at IS NULL ORDER BY made_at DESC LIMIT 5",
            {}, [&](const Row& r){
                if (r.size() < 2) return;
                rule << "  - " << r[0] << ": " << r[1] << "\n";
                ++n;
            });
        if (n == 0) rule << "  (none pinned)\n";
    } catch (...) {}

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PreCompact";
    out["hookSpecificOutput"]["additionalContext"] = rule.str();

    if (!tmp.empty()) {
        std::error_code ec;
        fs::remove(tmp, ec);
    }
    return out.dump();
}

// ---- PostToolUse:Read ------------------------------------------------------

std::string runPostToolUseReadHook(const std::string& stdin_raw) {
    if (std::getenv("ICMG_NO_COMPRESS_HOOK")) return "";
    if (stdin_raw.empty()) return "";

    std::string content;
    try {
        json j = json::parse(stdin_raw);
        if (j.contains("tool_response")) {
            auto& tr = j["tool_response"];
            if (tr.contains("content") && tr["content"].is_string())
                content = tr["content"].get<std::string>();
            else if (tr.contains("output") && tr["output"].is_string())
                content = tr["output"].get<std::string>();
        }
    } catch (...) { return ""; }

    if (content.size() < 1024) return "";

    std::string tmp = writeStdinTmp("posttooluse-read", content);
    if (tmp.empty()) return "";

    std::string cmd = "icmg compress --threshold 256 < \"" + tmp + "\" 2>/dev/null";
    auto res = safeExecShell(cmd, false, 30000);
    std::error_code ec;
    fs::remove(tmp, ec);

    if (res.exit_code != 0 || res.out.empty()) return "";
    if (res.out.size() >= content.size()) return ""; // compress no-op

    std::ostringstream msg;
    msg << "Read output auto-compressed (" << content.size() << "B -> "
        << res.out.size() << "B). Glossary inline; aliases match original tokens.\n"
        << res.out;

    json out;
    out["hookSpecificOutput"]["hookEventName"] = "PostToolUse";
    out["hookSpecificOutput"]["additionalContext"] = msg.str();
    return out.dump();
}

} // namespace icmg::core::hooks
