#include "detector.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace icmg::tkil {

Detector::Detector() {
    // Order matters — first match wins
    patterns_ = {
        // Git
        {"git log",    CmdType::GitLog},
        {"git diff",   CmdType::GitLog},
        {"git show",   CmdType::GitLog},
        {"git status", CmdType::GitLog},

        // Package managers (before build to catch "npm install" before "npm run build")
        {"npm install",  CmdType::PackageManager},
        {"npm ci",       CmdType::PackageManager},
        {"yarn install", CmdType::PackageManager},
        {"yarn add",     CmdType::PackageManager},
        {"pnpm install", CmdType::PackageManager},
        {"pip install",  CmdType::PackageManager},
        {"pip3 install", CmdType::PackageManager},
        {"gem install",  CmdType::PackageManager},
        {"cargo add",    CmdType::PackageManager},
        {"go get",       CmdType::PackageManager},

        // v1.21.3 (F3): per-language dedicated filters — these match BEFORE
        // the generic Build/Test patterns below (first-match wins).
        {"cargo build",      CmdType::Rust},
        {"cargo check",      CmdType::Rust},
        {"cargo run",        CmdType::Rust},
        {"cargo test",       CmdType::Rust},
        {"go build",         CmdType::Go},
        {"go test",          CmdType::Go},
        {"go run",           CmdType::Go},
        {"go vet",           CmdType::Go},
        {"mvn ",             CmdType::Java},
        {"mvnw ",            CmdType::Java},
        {"./mvnw",           CmdType::Java},
        {"gradle ",          CmdType::Java},
        {"gradlew ",         CmdType::Java},
        {"./gradlew",        CmdType::Java},
        {"dotnet build",     CmdType::Dotnet},
        {"dotnet publish",   CmdType::Dotnet},
        {"dotnet test",      CmdType::Dotnet},
        {"dotnet restore",   CmdType::Dotnet},
        {"msbuild",          CmdType::Dotnet},
        {"swift build",      CmdType::Swift},
        {"swift test",       CmdType::Swift},
        {"swift run",        CmdType::Swift},
        {"xcodebuild",       CmdType::Swift},
        {"kotlinc",          CmdType::Kotlin},

        // Build (generic — for cmake/make/ninja/npm/yarn that don't have
        // dedicated filters).
        {"cmake",          CmdType::Build},
        {"make",           CmdType::Build},
        {"ninja",          CmdType::Build},
        {"npm run build",  CmdType::Build},
        {"yarn build",     CmdType::Build},

        // Test (generic JS/Python; lang-specific test commands routed above)
        {"npm test",     CmdType::Test},
        {"npm run test", CmdType::Test},
        {"yarn test",    CmdType::Test},
        {"pytest",       CmdType::Test},
        {"python -m pytest", CmdType::Test},
        {"jest",         CmdType::Test},
        {"mocha",        CmdType::Test},

        // Dedicated test/compile/lint reporters (more specific than generic Test)
        {"vitest",       CmdType::Vitest},
        {"npx vitest",   CmdType::Vitest},
        {"pnpm vitest",  CmdType::Vitest},
        {"playwright test", CmdType::Playwright},
        {"npx playwright",  CmdType::Playwright},
        {"pnpm playwright", CmdType::Playwright},
        {"tsc",          CmdType::Tsc},
        {"npx tsc",      CmdType::Tsc},
        {"pnpm tsc",     CmdType::Tsc},
        {"yarn tsc",     CmdType::Tsc},
        {"eslint",       CmdType::Lint},
        {"npx eslint",   CmdType::Lint},
        {"pnpm eslint",  CmdType::Lint},
        {"ruff check",   CmdType::Lint},
        {"ruff ",        CmdType::Lint},
        {"cargo clippy", CmdType::Rust},
        {"golangci-lint",CmdType::Go},
        {"dotnet format",CmdType::Lint},
        {"prettier",     CmdType::Lint},
        {"black ",       CmdType::Lint},
        {"flake8",       CmdType::Lint},
        {"mypy ",        CmdType::Lint},

        // Search
        {"grep",   CmdType::Search},
        {"rg ",    CmdType::Search},
        {"ag ",    CmdType::Search},
        {"find ",  CmdType::Search},
        {"fd ",    CmdType::Search},

        // v1.20.4 (F6): logs commands route to log-dedup filter — order
        // matters; these match BEFORE generic "docker build"/"docker-compose"
        // because vector lookup walks in declaration order (longest specific).
        {"docker compose logs", CmdType::Logs},
        {"docker-compose logs", CmdType::Logs},
        {"docker logs",         CmdType::Logs},
        {"kubectl logs",        CmdType::Logs},
        {"journalctl",          CmdType::Logs},

        // Docker
        {"docker build",   CmdType::Docker},
        {"docker-compose", CmdType::Docker},
        {"docker compose", CmdType::Docker},

        // DB CLIs (Phase 21 Task 5c). Schema dumps fall through to Default
        // so the full output is preserved (mysqldump etc. produce DDL the
        // user usually wants verbatim).
        {"sqlcmd",   CmdType::Db},
        {"osql",     CmdType::Db},
        {"mysql ",   CmdType::Db},
        {"mariadb ", CmdType::Db},
        {"psql ",    CmdType::Db},

        // Runtime / scripting interpreters — output usually short for trivia
        // (--version, --help) but long stack traces on error. Use Test filter
        // strategy (capture failures, summary on success).
        {"node ",     CmdType::Test},
        {"node.exe",  CmdType::Test},
        {"deno ",     CmdType::Test},
        {"bun ",      CmdType::Test},
        {"ts-node ",  CmdType::Test},
        {"tsx ",      CmdType::Test},
        {"python ",   CmdType::Test},
        {"python3 ",  CmdType::Test},
        {"py ",       CmdType::Test},
        {"ruby ",     CmdType::Test},
        {"php ",      CmdType::Test},
        {"java ",     CmdType::Test},
        {"perl ",     CmdType::Test},
        {"lua ",      CmdType::Test},
        {"dotnet run",CmdType::Test},
        {"go run",    CmdType::Test},

        // Listing / streaming — pass through default (head50 + tail20 cap).
        {"ls ",       CmdType::Default},
        {"dir ",      CmdType::Default},
        {"tree ",     CmdType::Default},
        {"cat ",      CmdType::Default},
        {"head ",     CmdType::Default},
        {"tail ",     CmdType::Default},
        {"wc ",       CmdType::Default},
        {"sort ",     CmdType::Default},
        {"uniq ",     CmdType::Default},
        {"awk ",      CmdType::Default},
        {"sed ",      CmdType::Default},
        {"printf ",   CmdType::Default},
        {"echo ",     CmdType::Default},
        {"curl ",     CmdType::Default},
        {"wget ",     CmdType::Default},
    };
}

CmdType Detector::detect(const std::string& command) const {
    // Trim leading whitespace
    std::string cmd = command;
    while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());

    // Normalize `git <flags> <subcmd>` -> `git <subcmd>` so prefix patterns
    // like "git status" still match `git -C "path" status --short`.
    // Strips git-level flags between binary and subcommand:
    //   -C <path>, -c <key=val>, --git-dir=, --work-tree=, --paginate, --no-pager,
    //   --bare, --literal-pathspecs, -p
    {
        std::string lc = cmd;
        std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
        if (lc.rfind("git ", 0) == 0) {
            std::string rest = cmd.substr(4);
            while (!rest.empty()) {
                while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
                if (rest.empty() || rest[0] != '-') break;
                if (rest.rfind("-C ", 0) == 0 || rest.rfind("-c ", 0) == 0) {
                    rest.erase(0, 3);
                    if (!rest.empty() && rest.front() == '"') {
                        auto end = rest.find('"', 1);
                        rest = (end == std::string::npos) ? "" : rest.substr(end + 1);
                    } else {
                        auto sp = rest.find(' ');
                        rest = (sp == std::string::npos) ? "" : rest.substr(sp);
                    }
                    continue;
                }
                if (rest.rfind("--git-dir=", 0) == 0
                 || rest.rfind("--work-tree=", 0) == 0
                 || rest.rfind("--namespace=", 0) == 0) {
                    auto sp = rest.find(' ');
                    rest = (sp == std::string::npos) ? "" : rest.substr(sp);
                    continue;
                }
                if (rest.rfind("--paginate", 0) == 0
                 || rest.rfind("--no-pager", 0) == 0
                 || rest.rfind("--literal-pathspecs", 0) == 0
                 || rest.rfind("--bare", 0) == 0
                 || rest.rfind("-p ", 0) == 0
                 || rest == "-p") {
                    auto sp = rest.find(' ');
                    rest = (sp == std::string::npos) ? "" : rest.substr(sp);
                    continue;
                }
                break;  // unknown flag — stop normalizing
            }
            while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            if (!rest.empty()) cmd = "git " + rest;
        }
    }

    // 2026-07-07: unwrap shell-wrapper commands (`bash -c "..."`,
    // `sh -c "..."`, incl. full interpreter paths like
    // `"C:\Program Files\Git\bin\bash.exe" -c "..."`) so the REAL inner
    // command gets classified instead of falling through to Default every
    // time. Root cause: prefix-matching below only ever saw the wrapper's
    // own start (the interpreter path), never the command it actually runs.
    // Confirmed in production: 2206 invocations of this exact shape, 8.3MB
    // raw, only 1.2% filtered. Also strips a leading `export VAR=...;`
    // preamble (the exact production shape) before pattern matching.
    {
        std::string lc = cmd;
        std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
        // Find `-c ` after an interpreter name (bash/sh/zsh), regardless of
        // how much path precedes it (quoted or not).
        auto isShellInterpreter = [](const std::string& low) {
            static const char* kNames[] = {"bash", "sh", "zsh", "dash"};
            for (auto* n : kNames) {
                size_t pos = low.rfind(n);
                if (pos == std::string::npos) continue;
                size_t end = pos + std::strlen(n);
                // must be at a path/word boundary: preceded by / \ " or start,
                // followed by .exe", " -c, or end-of-word.
                bool boundary_before = (pos == 0) || low[pos-1] == '/' ||
                                        low[pos-1] == '\\' || low[pos-1] == '"';
                bool boundary_after = (end < low.size()) &&
                    (low.compare(end, 4, ".exe") == 0 || low[end] == ' ' || low[end] == '"');
                if (boundary_before && boundary_after) return true;
            }
            return false;
        };
        if (isShellInterpreter(lc)) {
            size_t cpos = lc.find(" -c ");
            if (cpos != std::string::npos) {
                std::string rest = cmd.substr(cpos + 4);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '"'))
                    rest.erase(rest.begin());
                while (!rest.empty() && (rest.back() == ' ' || rest.back() == '"'))
                    rest.pop_back();
                // Strip a leading `export VAR="...";` preamble (possibly
                // several, semicolon-separated) to reach the real command.
                for (;;) {
                    std::string trimmed = rest;
                    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
                    if (trimmed.rfind("export ", 0) != 0) { rest = trimmed; break; }
                    size_t semi = trimmed.find(';');
                    if (semi == std::string::npos) { rest = ""; break; }
                    rest = trimmed.substr(semi + 1);
                }
                while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
                if (!rest.empty()) cmd = rest;
            }
        }
    }

    // 2026-07-08: unwrap `powershell`/`pwsh -File <script>` wrappers by
    // reading the SCRIPT's content and classifying that instead. Found while
    // investigating why a sibling project's savings-dashboard still sat at
    // ~33-40% even after the bash -c fix above: the icemage-code agent
    // harness (a DIFFERENT codebase from icmg) writes the real command to a
    // temp .ps1 file and runs it as
    // `powershell -NoProfile -ExecutionPolicy Bypass -File <temp>\run.ps1`.
    // Confirmed in production: 3511 invocations of this exact shape, 12.7MB
    // raw (excluding one already-fixed 472MB outlier), only ~38% filtered.
    // Unlike bash -c, the real command lives in a FILE, not the command
    // string -- but detect() runs BEFORE execution (Tkil::runFiltered calls
    // detect() first, executes later), so the script is normally still on
    // disk. Best-effort: if the file is gone/unreadable, degrade silently to
    // whatever the wrapper string itself would classify as (Default) --
    // never throw, never behave worse than before this fix.
    {
        std::string lc = cmd;
        std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
        bool is_ps = (lc.rfind("powershell", 0) == 0) || (lc.rfind("pwsh", 0) == 0);
        if (is_ps) {
            size_t fpos = lc.find(" -file ");
            if (fpos != std::string::npos) {
                std::string rest = cmd.substr(fpos + 7);
                while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
                bool quoted = !rest.empty() && rest.front() == '"';
                std::string path;
                if (quoted) {
                    size_t end = rest.find('"', 1);
                    path = (end == std::string::npos) ? rest.substr(1) : rest.substr(1, end - 1);
                } else {
                    size_t sp = rest.find(' ');
                    path = (sp == std::string::npos) ? rest : rest.substr(0, sp);
                }
                if (!path.empty()) {
                    std::error_code ec;
                    if (std::filesystem::exists(path, ec) && !ec) {
                        std::ifstream f(path, std::ios::binary);
                        if (f) {
                            std::ostringstream ss;
                            ss << f.rdbuf();
                            std::string script = ss.str();
                            while (!script.empty() &&
                                   (script.back() == '\n' || script.back() == '\r' || script.back() == ' '))
                                script.pop_back();
                            size_t start = 0;
                            while (start < script.size() && (script[start] == ' ' || script[start] == '\n' || script[start] == '\r'))
                                ++start;
                            if (start > 0) script.erase(0, start);
                            if (!script.empty()) cmd = script;
                        }
                    }
                }
            }
        }
    }

    for (auto& p : patterns_) {
        // Prefix match (case-insensitive check via lowercase compare)
        std::string low_cmd = cmd;
        std::transform(low_cmd.begin(), low_cmd.end(), low_cmd.begin(), ::tolower);
        std::string low_pat = p.prefix;
        std::transform(low_pat.begin(), low_pat.end(), low_pat.begin(), ::tolower);

        if (low_cmd.rfind(low_pat, 0) == 0) return p.type;
        // Also check if prefix appears after 'sudo ', 'time ', etc.
        if (low_cmd.rfind("sudo " + low_pat, 0) == 0) return p.type;
        if (low_cmd.rfind("time " + low_pat, 0) == 0) return p.type;
    }
    return CmdType::Default;
}

} // namespace icmg::tkil
