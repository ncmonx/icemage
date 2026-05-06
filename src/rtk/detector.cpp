#include "detector.hpp"
#include <algorithm>

namespace icmg::rtk {

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

        // Build
        {"cargo build",    CmdType::Build},
        {"cargo check",    CmdType::Build},
        {"cargo clippy",   CmdType::Build},
        {"cmake",          CmdType::Build},
        {"make",           CmdType::Build},
        {"ninja",          CmdType::Build},
        {"dotnet build",   CmdType::Build},
        {"dotnet publish", CmdType::Build},
        {"npm run build",  CmdType::Build},
        {"yarn build",     CmdType::Build},
        {"msbuild",        CmdType::Build},
        {"gradle build",   CmdType::Build},
        {"mvn package",    CmdType::Build},
        {"go build",       CmdType::Build},

        // Test
        {"cargo test",   CmdType::Test},
        {"npm test",     CmdType::Test},
        {"npm run test", CmdType::Test},
        {"yarn test",    CmdType::Test},
        {"pytest",       CmdType::Test},
        {"python -m pytest", CmdType::Test},
        {"dotnet test",  CmdType::Test},
        {"go test",      CmdType::Test},
        {"jest",         CmdType::Test},
        {"vitest",       CmdType::Test},
        {"mocha",        CmdType::Test},

        // Search
        {"grep",   CmdType::Search},
        {"rg ",    CmdType::Search},
        {"ag ",    CmdType::Search},
        {"find ",  CmdType::Search},
        {"fd ",    CmdType::Search},

        // Docker
        {"docker build",   CmdType::Docker},
        {"docker logs",    CmdType::Docker},
        {"docker-compose", CmdType::Docker},
        {"docker compose", CmdType::Docker},
    };
}

CmdType Detector::detect(const std::string& command) const {
    // Trim leading whitespace
    std::string cmd = command;
    while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());

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

} // namespace icmg::rtk
