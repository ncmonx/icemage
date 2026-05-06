#pragma once
#include <string>
#include <vector>
#include <ostream>

namespace icmg::rtk {

enum class CmdType {
    GitLog,         // git log, git diff, git show, git status
    Build,          // cargo/cmake/make/dotnet/npm run build/msbuild
    Test,           // cargo test, npm test, pytest, dotnet test, go test
    Search,         // grep, rg, ag, find (output-heavy)
    Docker,         // docker build, docker logs, docker-compose
    PackageManager, // npm install, yarn, pip install, gem install (A5)
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
        case CmdType::Default:        return os << "Default";
    }
    return os << "Unknown";
}

} // namespace icmg::rtk
