// v1.37.0 Bash replacement Phase 1: 14 thin wrapper cmds.
//
// Each forwards to a system utility via core::safeExecShell with Tkil
// filter applied at the calling layer. Goal: AI never needs raw bash —
// every action goes through icmg surface.
//
// Cmds: build sed awk jq zip sha256 env gh mkdir rmdir cp mv rm slice date wc base64
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/http_stream.hpp"  // sha256OfFile reuse

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

std::string joinArgs(const std::vector<std::string>& args, std::size_t start = 0) {
    std::ostringstream o;
    for (std::size_t i = start; i < args.size(); ++i) {
        if (i > start) o << ' ';
        const auto& a = args[i];
        if (a.find(' ') != std::string::npos) o << '"' << a << '"';
        else o << a;
    }
    return o.str();
}

int passthroughExec(const std::string& cmd, int timeout_ms = 60000) {
    auto r = core::safeExecShell(cmd, true, timeout_ms);
    if (!r.out.empty())  std::cout << r.out;
    if (!r.err.empty())  std::cerr << r.err;
    return r.exit_code;
}

} // namespace

// ---- icmg build ----
class BuildCommand : public BaseCommand {
public:
    std::string name()        const override { return "build"; }
    std::string description() const override { return "Build icmg target via cmake (replaces bare `cmake --build`)"; }
    void usage() const override {
        std::cout << "Usage: icmg build [<target>] [--parallel] [--config C]\n"
                     "  Default target: icmg. Use 'all' for full build.\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) { usage(); return 0; }
        std::string target = "icmg";
        bool parallel = true;
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--parallel") parallel = true;
            else if (args[i] == "--no-parallel") parallel = false;
            else if (!args[i].empty() && args[i][0] != '-') target = args[i];
        }
        std::string cmd = "cmake --build build";
        if (target != "all") cmd += " --target " + target;
        if (parallel) cmd += " --parallel";
        return passthroughExec(cmd, 1200000);
    }
};

// ---- icmg sed ----
class SedCommand : public BaseCommand {
public:
    std::string name()        const override { return "sed"; }
    std::string description() const override { return "Stream editor (forwards to system sed via icmg run)"; }
    void usage() const override { std::cout << "Usage: icmg sed <args...>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        return passthroughExec("sed " + joinArgs(args));
    }
};

// ---- icmg awk ----
class AwkCommand : public BaseCommand {
public:
    std::string name()        const override { return "awk"; }
    std::string description() const override { return "AWK forwarder"; }
    void usage() const override { std::cout << "Usage: icmg awk <args...>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        return passthroughExec("awk " + joinArgs(args));
    }
};

// ---- icmg jq ----
class JqCommand : public BaseCommand {
public:
    std::string name()        const override { return "jq"; }
    std::string description() const override { return "JSON query forwarder"; }
    void usage() const override { std::cout << "Usage: icmg jq <args...>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        return passthroughExec("jq " + joinArgs(args));
    }
};

// ---- icmg zip ----
class ZipCommand : public BaseCommand {
public:
    std::string name()        const override { return "zip"; }
    std::string description() const override { return "Cross-platform zip (PS Compress-Archive Win / zip Linux)"; }
    void usage() const override {
        std::cout << "Usage: icmg zip <dest.zip> <src1> [src2...]\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.size() < 2) { usage(); return 1; }
        std::string dest = args[0];
        std::string srcs;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (!srcs.empty()) srcs += i == 1 ? "" : ",";
            srcs += "'" + args[i] + "'";
        }
#ifdef _WIN32
        std::string cmd = "powershell -NoProfile -Command \"Compress-Archive -Path " + srcs + " -DestinationPath '" + dest + "' -Force\"";
#else
        std::string cmd = "zip -r '" + dest + "' " + joinArgs(args, 1);
#endif
        return passthroughExec(cmd, 300000);
    }
};

// ---- icmg sha256 ----
class Sha256Command : public BaseCommand {
public:
    std::string name()        const override { return "sha256"; }
    std::string description() const override { return "SHA256 of file (cross-platform certutil/sha256sum)"; }
    void usage() const override { std::cout << "Usage: icmg sha256 <file>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 1; }
        std::string h = core::sha256OfFile(args[0]);
        if (h.empty()) { std::cerr << "icmg sha256: failed to hash " << args[0] << "\n"; return 2; }
        std::cout << h << "  " << args[0] << "\n";
        return 0;
    }
};

// ---- icmg env ----
class EnvCommand : public BaseCommand {
public:
    std::string name()        const override { return "env"; }
    std::string description() const override { return "Env var get/set/list/unset"; }
    void usage() const override {
        std::cout << "Usage: icmg env <get|set|list|unset> [name] [value]\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        const std::string& sub = args[0];
        if (sub == "get" && args.size() >= 2) {
            const char* v = std::getenv(args[1].c_str());
            if (v) std::cout << v << "\n";
            return v ? 0 : 1;
        }
        if (sub == "set" && args.size() >= 3) {
#ifdef _WIN32
            _putenv_s(args[1].c_str(), args[2].c_str());
#else
            ::setenv(args[1].c_str(), args[2].c_str(), 1);
#endif
            std::cout << "{\"ok\":true,\"name\":\"" << args[1] << "\"}\n";
            return 0;
        }
        if (sub == "list") {
            // Cross-platform env enumerate via system shell (cheap, ~5ms).
#ifdef _WIN32
            auto r = core::safeExecShell("set", true, 5000);
#else
            auto r = core::safeExecShell("env", true, 5000);
#endif
            std::cout << r.out;
            return r.exit_code;
        }
        if (sub == "unset" && args.size() >= 2) {
#ifdef _WIN32
            _putenv_s(args[1].c_str(), "");
#else
            ::unsetenv(args[1].c_str());
#endif
            return 0;
        }
        usage(); return 1;
    }
};

// ---- icmg gh ----
class GhCommand : public BaseCommand {
public:
    std::string name()        const override { return "gh"; }
    std::string description() const override { return "GitHub CLI wrapper (Tkil-filtered output)"; }
    void usage() const override { std::cout << "Usage: icmg gh <args...>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        return passthroughExec("gh " + joinArgs(args), 120000);
    }
};

// ---- icmg mkdir / rmdir ----
class MkdirCommand : public BaseCommand {
public:
    std::string name()        const override { return "mkdir"; }
    std::string description() const override { return "Create directory (with parents)"; }
    void usage() const override { std::cout << "Usage: icmg mkdir <path> [path2...]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        int rc = 0;
        for (const auto& p : args) {
            if (p[0] == '-') continue;
            std::error_code ec;
            fs::create_directories(p, ec);
            if (ec) { std::cerr << "mkdir " << p << ": " << ec.message() << "\n"; rc = 2; }
        }
        return rc;
    }
};

class RmdirCommand : public BaseCommand {
public:
    std::string name()        const override { return "rmdir"; }
    std::string description() const override { return "Remove directory (empty unless --recursive)"; }
    void usage() const override { std::cout << "Usage: icmg rmdir [--recursive] <path>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        bool recursive = hasFlag(args, "--recursive");
        for (const auto& p : args) {
            if (p[0] == '-') continue;
            std::error_code ec;
            if (recursive) fs::remove_all(p, ec);
            else           fs::remove(p, ec);
            if (ec) { std::cerr << "rmdir " << p << ": " << ec.message() << "\n"; return 2; }
        }
        return 0;
    }
};

// ---- icmg mv / rm ----
class MvCommand : public BaseCommand {
public:
    std::string name()        const override { return "mv"; }
    std::string description() const override { return "Move/rename file or dir"; }
    void usage() const override { std::cout << "Usage: icmg mv <src> <dest>\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.size() < 2) { usage(); return 1; }
        std::error_code ec;
        fs::rename(args[0], args[1], ec);
        if (ec) { std::cerr << "mv: " << ec.message() << "\n"; return 2; }
        return 0;
    }
};

class RmCommand : public BaseCommand {
public:
    std::string name()        const override { return "rm"; }
    std::string description() const override { return "Remove file (safer than bash rm — no -rf without confirm)"; }
    void usage() const override { std::cout << "Usage: icmg rm <file> [--force]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        for (const auto& p : args) {
            if (p[0] == '-') continue;
            std::error_code ec;
            fs::remove(p, ec);
            if (ec) { std::cerr << "rm " << p << ": " << ec.message() << "\n"; return 2; }
        }
        return 0;
    }
};

// ---- icmg slice ----
class SliceCommand : public BaseCommand {
public:
    std::string name()        const override { return "slice"; }
    std::string description() const override { return "Read file slice (replaces head/tail/sed -n)"; }
    void usage() const override {
        std::cout << "Usage: icmg slice <file> [--head N | --tail N | --lines A-B]\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string path = args[0];
        int head = 0, tail = 0, a = 0, b = 0;
        try { head = std::stoi(flagValue(args, "--head", "0")); } catch (...) {}
        try { tail = std::stoi(flagValue(args, "--tail", "0")); } catch (...) {}
        std::string lines = flagValue(args, "--lines", "");
        if (!lines.empty()) {
            auto dash = lines.find('-');
            if (dash != std::string::npos) {
                try { a = std::stoi(lines.substr(0, dash)); } catch (...) {}
                try { b = std::stoi(lines.substr(dash + 1)); } catch (...) {}
            }
        }
        // v1.42.0: stdin support — "-" reads stdin instead of file.
        bool from_stdin = (path == "-");
        std::vector<std::string> all;
        std::string line;
        if (from_stdin) {
            while (std::getline(std::cin, line)) all.push_back(line);
        } else {
            std::ifstream f(path);
            if (!f) { std::cerr << "slice: cannot open " << path << "\n"; return 2; }
            while (std::getline(f, line)) all.push_back(line);
        }
        std::size_t from = 0, to = all.size();
        if (head > 0) to = std::min<std::size_t>(all.size(), (std::size_t)head);
        else if (tail > 0) from = all.size() > (std::size_t)tail ? all.size() - tail : 0;
        else if (a > 0) { from = (std::size_t)(a - 1); to = (std::size_t)std::min<int>(b, (int)all.size()); }
        for (std::size_t i = from; i < to; ++i) std::cout << all[i] << "\n";
        return 0;
    }
};

// ---- icmg date ----
class DateCommand : public BaseCommand {
public:
    std::string name()        const override { return "date"; }
    std::string description() const override { return "Cross-platform date (ISO/epoch/custom fmt)"; }
    void usage() const override {
        std::cout << "Usage: icmg date [--utc] [--epoch | --fmt FMT]\n";
    }
    int run(const std::vector<std::string>& args) override {
        bool epoch = hasFlag(args, "--epoch");
        bool utc   = hasFlag(args, "--utc");
        std::string fmt = flagValue(args, "--fmt", "%Y-%m-%dT%H:%M:%S");
        if (epoch) { std::cout << std::time(nullptr) << "\n"; return 0; }
        std::time_t t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), fmt.c_str(), utc ? std::gmtime(&t) : std::localtime(&t));
        std::cout << buf << (utc ? "Z" : "") << "\n";
        return 0;
    }
};

// ---- icmg wc ----
class WcCommand : public BaseCommand {
public:
    std::string name()        const override { return "wc"; }
    std::string description() const override { return "Word/line/byte count"; }
    void usage() const override { std::cout << "Usage: icmg wc <file> [--lines | --bytes]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        bool only_lines = hasFlag(args, "--lines");
        bool only_bytes = hasFlag(args, "--bytes");
        std::ifstream f(args[0], std::ios::binary);
        if (!f) { std::cerr << "wc: cannot open " << args[0] << "\n"; return 2; }
        std::size_t bytes = 0, lines = 0;
        char c;
        while (f.get(c)) { ++bytes; if (c == '\n') ++lines; }
        if (only_lines) std::cout << lines << "\n";
        else if (only_bytes) std::cout << bytes << "\n";
        else std::cout << lines << " " << bytes << " " << args[0] << "\n";
        return 0;
    }
};

// Registrations

// ---- icmg tar ----
class TarCommand : public BaseCommand {
public:
    std::string name()        const override { return "tar"; }
    std::string description() const override { return "Cross-platform tar (create/extract via system tar)"; }
    void usage() const override {
        std::cout << "Usage: icmg tar <args...>\n"
                     "  Common: icmg tar -czf out.tar.gz dir/\n"
                     "          icmg tar -xzf in.tar.gz\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return args.empty() ? 1 : 0; }
        return passthroughExec("tar " + joinArgs(args), 600000);
    }
};

// ---- icmg ps ----
class PsCommand : public BaseCommand {
public:
    std::string name()        const override { return "ps"; }
    std::string description() const override { return "Process list (cross-platform tasklist/ps)"; }
    void usage() const override { std::cout << "Usage: icmg ps [--name <substr>]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (!args.empty() && args[0] == "--help") { usage(); return 0; }
        std::string name_filter = flagValue(args, "--name", "");
#ifdef _WIN32
        std::string cmd = name_filter.empty()
            ? std::string("tasklist /FO TABLE")
            : "tasklist /FI \"IMAGENAME eq " + name_filter + "*\" /FO TABLE";
#else
        std::string cmd = name_filter.empty()
            ? std::string("ps -ef")
            : "ps -ef | grep -i '" + name_filter + "' | grep -v grep";
#endif
        return passthroughExec(cmd, 30000);
    }
};

// ---- icmg kill ----
class KillCommand : public BaseCommand {
public:
    std::string name()        const override { return "kill"; }
    std::string description() const override { return "Kill process by PID (refuses PID 0/1/4 by default)"; }
    void usage() const override {
        std::cout << "Usage: icmg kill <pid> [--force] [--unsafe]\n"
                     "  Refuses PID < 100 unless --unsafe given (Win SYSTEM/Linux init).\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return args.empty() ? 1 : 0; }
        bool unsafe = hasFlag(args, "--unsafe");
        bool force  = hasFlag(args, "--force");
        int pid = 0;
        try { pid = std::stoi(args[0]); } catch (...) {
            std::cerr << "icmg kill: invalid PID '" << args[0] << "'\n"; return 1;
        }
        if (pid < 100 && !unsafe) {
            std::cerr << "icmg kill: PID " << pid << " < 100 (system process). Use --unsafe to override.\n";
            return 2;
        }
#ifdef _WIN32
        std::string cmd = std::string("taskkill ") + (force ? "/F " : "") + "/PID " + std::to_string(pid);
#else
        std::string cmd = std::string("kill ") + (force ? "-9 " : "") + std::to_string(pid);
#endif
        return passthroughExec(cmd, 15000);
    }
};

// ---- icmg df ----
class DfCommand : public BaseCommand {
public:
    std::string name()        const override { return "df"; }
    std::string description() const override { return "Disk free (cross-platform)"; }
    void usage() const override { std::cout << "Usage: icmg df [path]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (!args.empty() && args[0] == "--help") { usage(); return 0; }
        std::string path = args.empty() ? "." : args[0];
#ifdef _WIN32
        std::string cmd = "wmic logicaldisk get DeviceID,FreeSpace,Size /format:list";
#else
        std::string cmd = "df -h " + path;
#endif
        return passthroughExec(cmd, 30000);
    }
};

// ---- icmg du ----
class DuCommand : public BaseCommand {
public:
    std::string name()        const override { return "du"; }
    std::string description() const override { return "Directory size (cross-platform)"; }
    void usage() const override { std::cout << "Usage: icmg du <path> [--summary]\n"; }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return args.empty() ? 1 : 0; }
        std::string path = args[0];
        bool summary = hasFlag(args, "--summary");
#ifdef _WIN32
        std::string cmd = "powershell -NoProfile -Command \""
                          "$p='" + path + "'; "
                          "$bytes=(Get-ChildItem -Recurse -File -ErrorAction SilentlyContinue \\\"$p\\\" "
                          "| Measure-Object -Property Length -Sum).Sum; "
                          "$mb=[math]::Round($bytes/1MB,1); Write-Host \\\"$mb MB  $p\\\"\"";
#else
        std::string cmd = summary ? ("du -sh " + path) : ("du -h " + path);
#endif
        return passthroughExec(cmd, 120000);
    }
};

// Registrations appended at end of file via separate sed insert.
ICMG_REGISTER_COMMAND("build", BuildCommand);
ICMG_REGISTER_COMMAND("sed",   SedCommand);
ICMG_REGISTER_COMMAND("awk",   AwkCommand);
ICMG_REGISTER_COMMAND("jq",    JqCommand);
ICMG_REGISTER_COMMAND("zip",   ZipCommand);
ICMG_REGISTER_COMMAND("sha256",Sha256Command);
ICMG_REGISTER_COMMAND("env",   EnvCommand);
ICMG_REGISTER_COMMAND("gh",    GhCommand);
ICMG_REGISTER_COMMAND("mkdir", MkdirCommand);
ICMG_REGISTER_COMMAND("rmdir", RmdirCommand);
ICMG_REGISTER_COMMAND("mv",    MvCommand);
ICMG_REGISTER_COMMAND("rm",    RmCommand);
ICMG_REGISTER_COMMAND("slice", SliceCommand);
ICMG_REGISTER_COMMAND("date",  DateCommand);
ICMG_REGISTER_COMMAND("wc",    WcCommand);
ICMG_REGISTER_COMMAND("tar",   TarCommand);
ICMG_REGISTER_COMMAND("ps",    PsCommand);
ICMG_REGISTER_COMMAND("kill",  KillCommand);
ICMG_REGISTER_COMMAND("df",    DfCommand);
ICMG_REGISTER_COMMAND("du",    DuCommand);

} // namespace icmg::cli
