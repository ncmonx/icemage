#pragma once
#include <string>
#include <vector>
#include <ostream>

namespace icmg::tkil {

enum class CmdType {
    GitLog,         // git log, git diff, git show, git status
    Build,          // cargo/cmake/make/dotnet/npm run build/msbuild
    Test,           // cargo test, npm test, pytest, dotnet test, go test
    Search,         // grep, rg, ag, find (output-heavy)
    Docker,         // docker build, docker logs, docker-compose
    PackageManager, // npm install, yarn, pip install, gem install (A5)
    Db,             // sqlcmd, mysql, mariadb, psql (Phase 21 Task 5c)
    Vitest,         // vitest test runner
    Playwright,     // playwright test runner
    Tsc,            // typescript compiler
    Lint,           // eslint, clippy, ruff, golangci-lint, etc.
    Logs,           // v1.20.4: docker logs, kubectl logs, journalctl (dedup)
    // v1.21.3 (F3): per-language dedicated filters — sharper than generic Build.
    Rust,           // cargo build/check/run (rustc errors + Compiling noise)
    Go,             // go build/test/run (go: downloading + --- FAIL noise)
    Java,           // mvn/gradle ([INFO] flood + dep download noise)
    Dotnet,         // dotnet build/run/test (MSBuild noise)
    Swift,          // swift build / xcodebuild (xcbeautify-grade noise)
    Kotlin,         // kotlinc / gradle :compileKotlin (gradle noise)
    Default         // unknown
};

class Detector {
public:
    Detector();
    CmdType detect(const std::string& command) const;

private:
    struct Pattern {
        std::string prefix;
        CmdType     type;
    };
    std::vector<Pattern> patterns_;
};

inline std::ostream& operator<<(std::ostream& os, CmdType t) {
    switch (t) {
        case CmdType::GitLog:         return os << "GitLog";
        case CmdType::Build:          return os << "Build";
        case CmdType::Test:           return os << "Test";
        case CmdType::Search:         return os << "Search";
        case CmdType::Docker:         return os << "Docker";
        case CmdType::PackageManager: return os << "PackageManager";
        case CmdType::Db:             return os << "Db";
        case CmdType::Vitest:         return os << "Vitest";
        case CmdType::Playwright:     return os << "Playwright";
        case CmdType::Tsc:            return os << "Tsc";
        case CmdType::Lint:           return os << "Lint";
        case CmdType::Logs:           return os << "Logs";
        case CmdType::Rust:           return os << "Rust";
        case CmdType::Go:             return os << "Go";
        case CmdType::Java:           return os << "Java";
        case CmdType::Dotnet:         return os << "Dotnet";
        case CmdType::Swift:          return os << "Swift";
        case CmdType::Kotlin:         return os << "Kotlin";
        case CmdType::Default:        return os << "Default";
    }
    return os << "Unknown";
}

} // namespace icmg::tkil
