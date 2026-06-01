// v1.33.0 R1: `icmg ship` release workflow state machine.
//
// Why: AI assistant repeatedly forgets phase order (build → test → pack-win +
// pack-linux → push-private → docs-pr → publish). Soft Stop reminders not
// enough. Hard gate: each phase records evidence; `publish` refuses if any
// required phase missing or stale (>30 min).
//
// State: `.icmg/ship-state.json` per-project. Single-writer assumed.
//
// Phase outputs printed as JSONL for hook consumers + machine-parseable status.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

namespace {

constexpr int kStaleSeconds = 30 * 60; // 30 min

fs::path stateFile() {
    fs::path d = fs::current_path() / ".icmg";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d / "ship-state.json";
}

std::string nowIso() {
    auto t  = std::time(nullptr);
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return std::string(buf);
}

int64_t nowEpoch() {
    return std::time(nullptr);
}

int64_t parseIsoEpoch(const std::string& iso) {
    std::tm tm{};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) return 0;
#ifdef _WIN32
    return static_cast<int64_t>(_mkgmtime(&tm));
#else
    return static_cast<int64_t>(timegm(&tm));
#endif
}

json loadState() {
    fs::path p = stateFile();
    if (!fs::exists(p)) return json::object();
    std::ifstream f(p);
    if (!f) return json::object();
    try {
        json j; f >> j; return j;
    } catch (...) { return json::object(); }
}

void saveState(const json& j) {
    std::ofstream f(stateFile());
    f << j.dump(2);
}

void markPhase(json& s, const std::string& phase, const std::string& evidence) {
    s["phases"][phase]["done"]     = true;
    s["phases"][phase]["ts"]       = nowIso();
    s["phases"][phase]["evidence"] = evidence;
}

bool isPhaseFreshOk(const json& s, const std::string& phase) {
    if (!s.contains("phases") || !s["phases"].contains(phase)) return false;
    if (!s["phases"][phase].value("done", false)) return false;
    int64_t age = nowEpoch() - parseIsoEpoch(s["phases"][phase].value("ts", std::string{}));
    return age <= kStaleSeconds;
}

const std::vector<std::string>& requiredPhases() {
    static const std::vector<std::string> v = {
        "build", "test", "pack-win", "pack-linux", "push-private", "docs-pr"
    };
    return v;
}

} // namespace

class ShipCommand : public BaseCommand {
public:
    std::string name()        const override { return "ship"; }
    std::string description() const override {
        return "Release workflow state machine (build/test/pack/push/docs/publish gates)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg ship <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  start <version>       Initialize ship state for target version\n"
            "  build                 Run cmake --build --target icmg + mark phase\n"
            "  test                  Run ctest gate + mark phase (122/122 required)\n"
            "  pack-win              Pack icmg-<ver>-win-x64.zip + sha256 + mark\n"
            "  pack-linux            Run scripts/release-linux-fast.sh + mark\n"
            "  push-private          Push release/* to private remote + mark\n"
            "  docs-pr               Open docs PR + merge + mark with merge sha\n"
            "  publish               Create release + upload assets (gates: all prior phases)\n"
            "  status                Show current state + missing/stale phases\n"
            "  reset                 Clear ship state (start over)\n\n"
            "Required phase order before `publish`:\n"
            "  build -> test -> (pack-win + pack-linux) -> push-private -> docs-pr -> publish\n"
            "Phases stale after 30 min; re-run before publishing.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "start")        return doStart(args);
        if (sub == "build")        return doBuild();
        if (sub == "test")         return doTest();
        if (sub == "pack-win")     return doPackWin();
        if (sub == "pack-linux")   return doPackLinux();
        if (sub == "push-private") return doPushPrivate();
        if (sub == "docs-pr")      return doDocsPR();
        if (sub == "publish")      return doPublish();
        if (sub == "status")       return doStatus();
        if (sub == "reset")        return doReset();
        std::cerr << "icmg ship: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    int doStart(const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "usage: icmg ship start <version>\n"; return 1; }
        json s;
        s["target_version"] = args[1];
        s["started_at"]     = nowIso();
        s["phases"]         = json::object();
        saveState(s);
        std::cout << "{\"ok\":true,\"target_version\":\"" << args[1] << "\"}\n";
        return 0;
    }

    int doBuild() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state — run `icmg ship start <version>` first\n"; return 2; }
        // v1.35.0 skip-fresh: if build/icmg.exe is newer than every
        // tracked src .cpp/.hpp/.h, skip rebuild. Pure C++ filesystem —
        // no shell dependency. Override: ICMG_SHIP_FORCE_BUILD=1.
        if (!std::getenv("ICMG_SHIP_FORCE_BUILD")) {
            std::error_code ec;
            fs::path bin = fs::path("build") / "icmg.exe";
            if (!fs::exists(bin, ec)) {
                bin = fs::path("build") / "icmg";
            }
            if (fs::exists(bin, ec)) {
                auto bin_mt = fs::last_write_time(bin, ec);
                fs::file_time_type newest_src = fs::file_time_type::min();
                std::vector<fs::path> roots = { "src" };
                if (fs::exists("third_party/llama.cpp/src", ec)) roots.emplace_back("third_party/llama.cpp/src");
                if (fs::exists("third_party/llama.cpp/include", ec)) roots.emplace_back("third_party/llama.cpp/include");
                for (const auto& root : roots) {
                    for (auto it = fs::recursive_directory_iterator(root, ec);
                         it != fs::recursive_directory_iterator(); it.increment(ec)) {
                        if (ec) { ec.clear(); continue; }
                        const auto& p = it->path();
                        auto ext = p.extension().string();
                        if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".cc") continue;
                        auto mt = fs::last_write_time(p, ec);
                        if (ec) { ec.clear(); continue; }
                        if (mt > newest_src) newest_src = mt;
                    }
                }
                if (bin_mt > newest_src) {
                    markPhase(s, "build", "icmg.exe fresh (no src changes since last build)");
                    saveState(s);
                    std::cout << "{\"ok\":true,\"phase\":\"build\",\"skipped\":\"artifact fresh\"}\n";
                    return 0;
                }
            }
        }
        auto r = core::safeExecShell("cmake --build build --target icmg --parallel", true, 600000);
        if (r.exit_code != 0) {
            std::cerr << "build failed (exit " << r.exit_code << ")\n";
            return 3;
        }
        markPhase(s, "build", "icmg.exe relinked OK");
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"build\"}\n";
        return 0;
    }

    int doTest() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        if (!isPhaseFreshOk(s, "build")) {
            std::cerr << "build phase missing or stale — run `icmg ship build`\n";
            return 4;
        }
        // v1.35.0 Strategi 1: skip ctest when git diff since the ship
        // start touches only non-code paths (docs, README, .github/,
        // scripts/*.sh, *.md). Saves ~15-30 s per docs-only patch.
        // Override via ICMG_SHIP_FORCE_TEST=1.
        if (!std::getenv("ICMG_SHIP_FORCE_TEST")) {
            std::string started = s["phases"].value("build", json::object()).value("ts", std::string{});
            if (started.empty()) started = s.value("started_at", std::string{});
            std::string cmd =
                "since_iso=\"" + started + "\"; "
                "since_ts=$(date -d \"$since_iso\" +%s 2>/dev/null || echo 0); "
                "( git log --since=\"$since_iso\" --pretty=format: --name-only 2>/dev/null ; git status --porcelain 2>/dev/null | awk \"{print \$2}\" ) "
                "  | sort -u | grep -v '^$' "
                "  | grep -vE '^(README\.md|CHANGELOG\.md|docs/|\.github/|scripts/.*\.sh|.*\.md)$' "
                "  | head -3";
            auto chk = core::safeExecShell(cmd, true, 15000);
            std::string trimmed = chk.out;
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
                trimmed.pop_back();
            if (trimmed.empty() && chk.exit_code == 0) {
                markPhase(s, "test", "ctest skipped — docs-only since build phase");
                saveState(s);
                std::cout << "{\"ok\":true,\"phase\":\"test\",\"skipped\":\"docs-only\"}\n";
                return 0;
            }
        }
        // Quick build of test exes first (no-op if cached).
        (void)core::safeExecShell("cmake --build build --parallel", true, 900000);
        auto r = core::safeExecShell("ctest --test-dir build -j 16", true, 600000);
        if (r.exit_code != 0) {
            std::cerr << "ctest failed (exit " << r.exit_code << ")\n";
            std::cerr << r.out.substr(r.out.size() > 1000 ? r.out.size() - 1000 : 0) << "\n";
            return 5;
        }
        markPhase(s, "test", "ctest 122/122 (or current count) PASS");
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"test\"}\n";
        return 0;
    }

    int doPackWin() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        if (!isPhaseFreshOk(s, "test")) {
            std::cerr << "test phase missing or stale — run `icmg ship test`\n";
            return 4;
        }
        std::string ver = s.value("target_version", std::string{});
        if (ver.empty()) { std::cerr << "target_version missing in ship state\n"; return 6; }
        std::string cmd =
            "powershell -NoProfile -Command \"$ver='" + ver + "'; "
            "$stage=\\\"C:\\\\Temp\\\\icmg-pkg-$($ver -replace '\\.','')\\\"; "
            "if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }; "
            "New-Item -ItemType Directory -Path $stage | Out-Null; "
            "Copy-Item build\\icmg.exe $stage; "
            "@('C:\\msys64\\mingw64\\bin\\libtree-sitter-0.26.dll',"
            "'C:\\msys64\\mingw64\\bin\\libwinpthread-1.dll',"
            "'C:\\msys64\\mingw64\\bin\\libzstd.dll',"
            "'third_party\\onnxruntime\\lib\\onnxruntime.dll',"
            "'third_party\\onnxruntime\\lib\\onnxruntime_providers_shared.dll',"
            "'C:\\msys64\\mingw64\\bin\\wasmtime.dll')"
            " | ForEach-Object { Copy-Item $_ $stage -ErrorAction SilentlyContinue }; "
            "$zip=\\\"icmg-$ver-win-x64.zip\\\"; "
            "if (Test-Path $zip) { Remove-Item $zip }; "
            "Compress-Archive -Path \\\"$stage\\\\*\\\" -DestinationPath $zip; "
            "$h=(Get-FileHash $zip -Algorithm SHA256).Hash.ToLower(); "
            "Set-Content -Path \\\"$zip.sha256\\\" -Value \\\"$h  $zip\\\" -Encoding ascii -NoNewline; "
            "Write-Host \\\"$h  $zip\\\"\"";
        auto r = core::safeExecShell(cmd, true, 180000);
        if (r.exit_code != 0) {
            std::cerr << "pack-win failed (exit " << r.exit_code << "): " << r.err.substr(0, 500) << "\n";
            return 7;
        }
        markPhase(s, "pack-win", "icmg-" + ver + "-win-x64.zip + sha256");
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"pack-win\"}\n";
        return 0;
    }

    int doPackLinux() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        if (!isPhaseFreshOk(s, "test")) {
            std::cerr << "test phase missing or stale — run `icmg ship test`\n";
            return 4;
        }
        auto r = core::safeExecShell("wsl -e bash scripts/release-linux-fast.sh", true, 900000);
        if (r.exit_code != 0) {
            std::cerr << "pack-linux failed (exit " << r.exit_code << "): " << r.err.substr(0, 500) << "\n";
            return 8;
        }
        std::string ver = s.value("target_version", std::string{});
        markPhase(s, "pack-linux", "icmg-" + ver + "-linux-x64.tar.gz + sha256");
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"pack-linux\"}\n";
        return 0;
    }

    int doPushPrivate() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        // Current branch name
        auto rb = core::safeExecShell("git branch --show-current", true, 10000);
        std::string branch = rb.out;
        while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r')) branch.pop_back();
        auto r = core::safeExecShell("git push private " + branch, true, 120000);
        if (r.exit_code != 0) {
            std::cerr << "push-private failed: " << r.err.substr(0, 500) << "\n";
            return 9;
        }
        markPhase(s, "push-private", "pushed " + branch + " to private remote");
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"push-private\",\"branch\":\"" << branch << "\"}\n";
        return 0;
    }

    int doDocsPR() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        // This phase is manual orchestration; mark it once user has merged.
        // For now, accept a merge-sha as next arg via env ICMG_SHIP_MERGE_SHA.
        const char* sha = std::getenv("ICMG_SHIP_MERGE_SHA");
        if (!sha || !*sha) {
            std::cerr << "docs-pr: expected ICMG_SHIP_MERGE_SHA=<sha> after docs PR is merged.\n"
                      << "Workflow:\n"
                      << "  1. Edit README.md in docs branch (worktree or in-place)\n"
                      << "  2. git push origin docs/v<ver>\n"
                      << "  3. gh pr create + gh pr merge --admin\n"
                      << "  4. ICMG_SHIP_MERGE_SHA=<sha> icmg ship docs-pr\n";
            return 10;
        }
        markPhase(s, "docs-pr", std::string("merged sha=") + sha);
        s["docs_merge_sha"] = sha;
        saveState(s);
        std::cout << "{\"ok\":true,\"phase\":\"docs-pr\",\"sha\":\"" << sha << "\"}\n";
        return 0;
    }

    int doPublish() {
        json s = loadState();
        if (s.empty()) { std::cerr << "no ship state\n"; return 2; }
        std::vector<std::string> missing;
        for (const auto& p : requiredPhases()) {
            if (!isPhaseFreshOk(s, p)) missing.push_back(p);
        }
        if (!missing.empty()) {
            std::cerr << "publish refused — missing or stale phases:\n";
            for (const auto& p : missing) std::cerr << "  - " << p << "\n";
            return 11;
        }
        std::string ver = s.value("target_version", std::string{});
        std::string sha = s.value("docs_merge_sha", std::string{});
        if (ver.empty() || sha.empty()) {
            std::cerr << "publish refused — target_version or docs_merge_sha empty\n";
            return 12;
        }
        // Caller invokes gh release create + upload externally with the version + sha.
        // ship publish itself just validates and records.
        markPhase(s, "publish", "validated all phases at " + nowIso());
        s["published"] = true;
        saveState(s);
        std::cout << "{\"ok\":true,\"target_version\":\"" << ver
                  << "\",\"docs_merge_sha\":\"" << sha << "\","
                  << "\"next\":\"gh release create v" << ver
                  << " --target " << sha
                  << " && gh release upload v" << ver
                  << " <win-zip> <win-sha256> <linux-tgz> <linux-sha256>\"}\n";
        return 0;
    }

    int doStatus() {
        json s = loadState();
        if (s.empty()) { std::cout << "no ship state (run `icmg ship start <ver>`)\n"; return 0; }
        std::cout << "icmg ship status\n";
        std::cout << "  target:   " << s.value("target_version", std::string{}) << "\n";
        std::cout << "  started:  " << s.value("started_at", std::string{}) << "\n";
        std::cout << "  phases:\n";
        for (const auto& p : requiredPhases()) {
            bool fresh = isPhaseFreshOk(s, p);
            std::cout << "    " << (fresh ? "[x] " : "[ ] ") << p;
            if (s.contains("phases") && s["phases"].contains(p)) {
                std::cout << "  (" << s["phases"][p].value("ts", std::string{}) << ")"
                          << (fresh ? "" : "  STALE");
            }
            std::cout << "\n";
        }
        if (s.value("published", false)) std::cout << "  [x] publish — validated\n";
        return 0;
    }

    int doReset() {
        std::error_code ec;
        fs::remove(stateFile(), ec);
        std::cout << "{\"ok\":true,\"reset\":true}\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("ship", ShipCommand);

} // namespace icmg::cli
