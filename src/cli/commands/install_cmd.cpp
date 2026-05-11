// `icmg install` — install icmg to a system-wide path so all server users
// share one binary. After user upgrades with `icmg update --apply`, the
// system path binary is also refreshed automatically.
//
// --system          Copy binary (+ DLLs on Windows) to system install dir
// --path <dir>      Override default system dir
// --no-dlls         Skip DLL copy (when DLLs already in place)
// --status          Show current system install info

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;

namespace icmg::cli {

// Sentinel: ~/.icmg/system-path.txt records where system install lives.
static fs::path systemPathSentinel() {
    std::string gdir = core::icmgGlobalDir();
    return fs::path(gdir) / "system-path.txt";
}

static std::string readSystemPath() {
    auto s = systemPathSentinel();
    if (!fs::exists(s)) return "";
    std::ifstream f(s);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

static void writeSystemPath(const std::string& dir) {
    auto s = systemPathSentinel();
    std::ofstream f(s);
    f << dir << "\n";
}

static fs::path selfPath() {
#ifdef _WIN32
    char buf[2048]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
    return buf;
#else
    return fs::canonical("/proc/self/exe");
#endif
}

static std::string defaultSystemDir() {
#ifdef _WIN32
    const char* pd = std::getenv("ProgramData");
    if (pd && *pd) return std::string(pd) + "\\icmg";
    return "C:\\ProgramData\\icmg";
#elif defined(__APPLE__)
    return "/usr/local/bin";
#else
    return "/usr/local/bin";
#endif
}

// Bundled DLLs that live alongside icmg.exe on Windows.
static const std::vector<std::string> BUNDLED_DLLS = {
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "libtree-sitter-0.26.dll",
    "wasmtime.dll",
    "libzstd.dll",
    "libwinpthread-1.dll",
};

class InstallCommand : public BaseCommand {
public:
    std::string name()        const override { return "install"; }
    std::string description() const override { return "Install icmg to a system-wide path for all users"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg install [options]\n\n"
            "Options:\n"
            "  --system          Copy binary (+ DLLs) to system install dir\n"
            "  --path <dir>      Override default system dir\n"
            "  --no-dlls         Skip DLL copy\n"
            "  --status          Show system install info\n\n"
            "Default system dir:\n"
            "  Windows:  C:\\ProgramData\\icmg\\\n"
            "  Linux:    /usr/local/bin\n"
            "  macOS:    /usr/local/bin\n\n"
            "After `icmg install --system`, running `icmg update --apply`\n"
            "from any user also refreshes the system binary automatically.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        bool do_system = hasFlag(args, "--system");
        bool do_status = hasFlag(args, "--status");
        bool no_dlls   = hasFlag(args, "--no-dlls");
        std::string path_override = flagValue(args, "--path");

        if (do_status) return showStatus();
        if (!do_system) { usage(); return 1; }

        return installSystem(path_override, no_dlls);
    }

private:
    int showStatus() {
        std::string sp = readSystemPath();
        if (sp.empty()) {
            std::cout << "No system install recorded.\n"
                      << "Run `icmg install --system` to set one up.\n";
            return 0;
        }
        std::cout << "System install path: " << sp << "\n";
        fs::path bin = fs::path(sp) / binaryName();
        if (fs::exists(bin)) {
            std::cout << "Binary: " << bin.string() << "\n";
            try {
                auto sz = fs::file_size(bin);
                std::cout << "Size:   " << sz << " bytes\n";
            } catch (...) {}
        } else {
            std::cout << "Binary NOT found at recorded path — re-run `icmg install --system`.\n";
        }
        return 0;
    }

    int installSystem(const std::string& path_override, bool no_dlls) {
        std::string dest_dir = path_override.empty() ? defaultSystemDir() : path_override;
        fs::path dest = fs::path(dest_dir);

        // Create dir if missing
        std::error_code ec;
        fs::create_directories(dest, ec);
        if (ec) {
            std::cerr << "icmg install: cannot create " << dest_dir
                      << ": " << ec.message() << "\n";
#ifdef _WIN32
            std::cerr << "  Tip: run as Administrator, or use --path to choose a writable dir.\n";
#else
            std::cerr << "  Tip: run with sudo, or use --path to choose a writable dir.\n";
#endif
            return 1;
        }

        fs::path self = selfPath();
        fs::path self_dir = self.parent_path();

        // Copy binary
        fs::path dest_bin = dest / binaryName();
        fs::copy_file(self, dest_bin, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "icmg install: failed to copy binary: " << ec.message() << "\n";
            return 1;
        }
        std::cout << "  Installed: " << dest_bin.string() << "\n";

#ifdef _WIN32
        // Copy DLLs on Windows
        if (!no_dlls) {
            for (auto& dll : BUNDLED_DLLS) {
                fs::path src_dll = self_dir / dll;
                if (!fs::exists(src_dll)) continue;
                fs::path dst_dll = dest / dll;
                fs::copy_file(src_dll, dst_dll, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::cerr << "  warn: " << dll << ": " << ec.message() << "\n";
                } else {
                    std::cout << "  Installed: " << dst_dll.string() << "\n";
                }
            }
        }
#endif

        // Save sentinel
        writeSystemPath(dest_dir);
        std::cout << "\nSystem install complete: " << dest_dir << "\n";

        // PATH hint
        bool on_path = isOnPath(dest_dir);
        if (!on_path) {
#ifdef _WIN32
            std::cout << "\nTo make icmg available to all users, add to system PATH:\n"
                      << "  " << dest_dir << "\n"
                      << "  (Control Panel > System > Advanced > Environment Variables > Path)\n"
                      << "  Or run as Administrator:\n"
                      << "  setx /M PATH \"%PATH%;" << dest_dir << "\"\n";
#else
            std::cout << "\nAdd to system PATH if not already:\n"
                      << "  echo 'export PATH=\"" << dest_dir << ":$PATH\"' >> /etc/profile.d/icmg.sh\n";
#endif
        } else {
            std::cout << "Already on PATH.\n";
        }

        std::cout << "\nFuture `icmg update --apply` will also refresh this system binary.\n";
        return 0;
    }

    static std::string binaryName() {
#ifdef _WIN32
        return "icmg.exe";
#else
        return "icmg";
#endif
    }

    static bool isOnPath(const std::string& dir) {
        const char* path_env = std::getenv("PATH");
        if (!path_env) return false;
        std::string path = path_env;
#ifdef _WIN32
        char sep = ';';
#else
        char sep = ':';
#endif
        std::string tok;
        std::istringstream ss(path);
        while (std::getline(ss, tok, sep)) {
            try {
                if (fs::equivalent(fs::path(tok), fs::path(dir))) return true;
            } catch (...) {
                if (tok == dir) return true;
            }
        }
        return false;
    }
};

ICMG_REGISTER_COMMAND("install", InstallCommand);

} // namespace icmg::cli
